import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtemp, readFile, writeFile, rm } from 'node:fs/promises';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../../', import.meta.url));
const binary = path.resolve(root, process.env.ROVEROPS_BINARY ?? 'build/roverops');
const tmp = await mkdtemp(path.join(os.tmpdir(), 'roverops-smoke-'));
const listener = net.createServer();
listener.listen(0, '127.0.0.1');
await once(listener, 'listening');
const port = listener.address().port;
await new Promise((resolve) => listener.close(resolve));
const config = JSON.parse(await readFile(path.join(root, 'config/rover.json'), 'utf8'));
config.server.port = port;
await writeFile(path.join(tmp, 'config.json'), JSON.stringify(config));
const child = spawn(binary, ['--config', path.join(tmp, 'config.json')], {
  cwd: root,
  stdio: ['ignore', 'pipe', 'pipe'],
});
let output = '';
child.stdout.on('data', (data) => {
  output += data;
});
child.stderr.on('data', (data) => {
  output += data;
});
const exited = once(child, 'exit');
const base = `http://127.0.0.1:${port}`;
const sockets = [];
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
async function until(predicate, label, timeout = 8000) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    if (await predicate()) return;
    await sleep(30);
  }
  throw new Error(`Timed out: ${label}`);
}
async function request(body, expected = 200, headers = {}) {
  const response = await fetch(`${base}/api/commands`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', ...headers },
    body: typeof body === 'string' ? body : JSON.stringify(body),
    signal: AbortSignal.timeout(2000),
  });
  const json = await response.json();
  assert.equal(response.status, expected, JSON.stringify(json));
  return json;
}
async function connect() {
  const ws = new WebSocket(`ws://127.0.0.1:${port}/ws/telemetry`);
  sockets.push(ws);
  await Promise.race([
    once(ws, 'open'),
    sleep(3000).then(() => {
      throw new Error('WebSocket did not open');
    }),
  ]);
  return ws;
}

try {
  await until(
    async () => {
      if (child.exitCode !== null) throw new Error(output);
      try {
        return (await fetch(`${base}/api/health`)).ok;
      } catch {
        return false;
      }
    },
    'server ready',
    60_000,
  );
  let latest;
  const ws = await connect();
  ws.addEventListener('message', (event) => {
    latest = JSON.parse(event.data);
  });
  await until(() => latest?.type === 'telemetry', 'first telemetry');
  assert.equal(latest.mode, 'idle');
  assert.equal(latest.sensors[0].source, 'simulated');
  const initialSequence = latest.sequence;
  await until(() => latest.sequence > initialSequence, 'independent simulation updates');

  await request('{broken', 400);
  await request({ command: 'set_velocity', velocity_mps: 'fast' }, 400);
  await request({ command: 'set_velocity', velocity_mps: -1 }, 422);
  await request({ command: 'set_waypoint', x_m: 9999, y_m: 0 }, 422);
  await request({ command: 'start', extra: true }, 400);
  await request({ command: 'start' }, 409);
  await request({ command: 'start' }, 403, { Origin: 'https://untrusted.example' });
  await request({ command: 'start' }, 415, { 'Content-Type': 'text/plain' });
  await request(JSON.stringify({ command: 'start', padding: 'x'.repeat(5000) }), 413);

  await request({ command: 'set_waypoint', x_m: 2, y_m: 0 });
  await request({ command: 'set_velocity', velocity_mps: 1.5 });
  await request({ command: 'start' });
  await until(() => latest.position.x_m > 0.05, 'real motion over WebSocket');
  await request({ command: 'pause' });
  await until(() => latest.mode === 'paused', 'paused telemetry');
  const pausedX = latest.position.x_m;
  await sleep(250);
  assert.equal(latest.position.x_m, pausedX);
  await request({ command: 'resume' });
  await until(() => latest.position.x_m > pausedX + 0.03, 'resumed motion');
  await request({ command: 'emergency_stop' });
  await until(() => latest.mode === 'emergency_stopped', 'latched emergency stop');
  const stoppedX = latest.position.x_m;
  await request({ command: 'start' }, 409);
  await request({ command: 'return_home' }, 409);
  await sleep(200);
  assert.equal(latest.position.x_m, stoppedX);
  assert.equal(latest.velocity_mps, 0);
  await request({ command: 'resume' });
  await request({ command: 'return_home' });
  await until(() => latest.mode === 'arrived', 'return home completion', 12000);
  assert.ok(Math.hypot(latest.position.x_m, latest.position.y_m) <= 0.15);

  // Fill the telemetry allowance; HTTP commands must still have worker capacity.
  for (let i = 1; i < config.server.max_websocket_clients; i++) await connect();
  const extra = new WebSocket(`ws://127.0.0.1:${port}/ws/telemetry`);
  sockets.push(extra);
  const [close] = await once(extra, 'close');
  assert.equal(close.code, 1008);
  await request({ command: 'emergency_stop' });
  const drop = sockets[1];
  const dropped = once(drop, 'close');
  drop.close();
  await dropped;
  await sleep(100);
  const reconnected = await connect();
  const [packet] = await once(reconnected, 'message');
  assert.equal(JSON.parse(packet.data).mode, 'emergency_stopped');

  // Keep live clients attached while terminating: process must close and join all workers.
  const stopStarted = Date.now();
  child.kill('SIGTERM');
  const [code, signal] = await Promise.race([
    exited,
    sleep(5000).then(() => {
      throw new Error('Shutdown exceeded 5 seconds');
    }),
  ]);
  assert.equal(code, 0, output);
  assert.equal(signal, null);
  console.log(
    `PASS: HTTP validation, independent WebSocket telemetry, movement, pause/resume, E-stop latch, return home, capacity, reconnect, shutdown (${Date.now() - stopStarted} ms).`,
  );
} catch (error) {
  console.error(output);
  throw error;
} finally {
  for (const socket of sockets) socket.close();
  if (child.exitCode === null && child.signalCode === null) {
    child.kill('SIGKILL');
    await exited;
  }
  await rm(tmp, { recursive: true, force: true });
}
