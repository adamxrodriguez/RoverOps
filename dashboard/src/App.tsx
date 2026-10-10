import { useState } from 'react';
import { RoverMap } from './RoverMap';
import { useTelemetry } from './useTelemetry';
import type { Command } from './types';

const number = (value: number | undefined, digits = 2) =>
  value === undefined ? '—' : value.toFixed(digits);
const modeLabel = (mode: string | undefined) => mode?.replaceAll('_', ' ') ?? 'Waiting';

export default function App() {
  const { state, trail, limits, live, connection, ageMs } = useTelemetry();
  const [x, setX] = useState('8');
  const [y, setY] = useState('5');
  const [velocity, setVelocity] = useState('1.0');
  const [pending, setPending] = useState(0);
  const [notice, setNotice] = useState({
    text: 'Set a waypoint, then start your mission.',
    error: false,
  });
  const send = async (command: Command) => {
    setPending((n) => n + 1);
    try {
      const response = await fetch('/api/commands', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(command),
        signal: AbortSignal.timeout(4000),
      });
      const result = await response.json();
      if (!response.ok)
        throw new Error(result.error?.message ?? `Command failed (${response.status})`);
      setNotice({ text: `${modeLabel(command.command)}: ${result.message}`, error: false });
    } catch (error) {
      setNotice({
        text: error instanceof Error ? error.message : 'Command failed. Check the simulator.',
        error: true,
      });
    } finally {
      setPending((n) => n - 1);
    }
  };
  const ready = live && !!limits && !pending;
  const running = state?.mode === 'navigating' || state?.mode === 'returning_home';
  const stopped = state?.mode === 'paused' || state?.mode === 'emergency_stopped';
  const depleted = state?.battery_percent === 0;
  const distance = state?.waypoint
    ? Math.hypot(state.waypoint.x_m - state.position.x_m, state.waypoint.y_m - state.position.y_m)
    : undefined;
  const warnings = [...(state?.errors ?? []), ...(state?.warnings ?? [])];

  return (
    <main className="shell">
      <header className="topbar">
        <div className="brand">
          <span className="brand-icon" aria-hidden="true">
            ↗
          </span>
          <div>
            <h1>RoverOps</h1>
            <p>
              MISSION CONTROL <span>/</span> ROVER 01
            </p>
          </div>
        </div>
        <div className="top-status">
          <span className="simulation-label">SIMULATION</span>
          <span className={`connection ${live ? 'online' : ''}`} role="status">
            <i />
            {connection}
          </span>
        </div>
      </header>

      <div className="page-title">
        <div>
          <p className="eyebrow">LOCAL OPERATIONS</p>
          <h2>One rover. Every signal.</h2>
          <p>Command, observe, and understand the system.</p>
        </div>
        <div
          className={`mode-badge ${state?.mode === 'emergency_stopped' ? 'danger-text' : ''}`}
          data-testid="mode"
        >
          {modeLabel(state?.mode)}
        </div>
      </div>

      <section className="metrics" aria-label="Rover metrics">
        <article>
          <span>VELOCITY</span>
          <strong data-testid="velocity">
            {number(state?.velocity_mps)} <small>m/s</small>
          </strong>
          <p>Target {number(state?.target_velocity_mps)} m/s</p>
        </article>
        <article>
          <span>BATTERY</span>
          <strong>
            {number(state?.battery_percent, 1)} <small>%</small>
          </strong>
          <div className="battery-track">
            <div style={{ width: `${state?.battery_percent ?? 0}%` }} />
          </div>
        </article>
        <article>
          <span>HEADING</span>
          <strong>
            {state ? number((state.heading_rad * 180) / Math.PI, 1) : '—'}
            <small>°</small>
          </strong>
          <p>Counterclockwise from +X</p>
        </article>
        <article>
          <span>TO WAYPOINT</span>
          <strong>
            {number(distance)} <small>m</small>
          </strong>
          <p>
            {state?.waypoint
              ? `X ${number(state.waypoint.x_m)} · Y ${number(state.waypoint.y_m)}`
              : 'No waypoint assigned'}
          </p>
        </article>
      </section>

      <div className="workspace">
        <section className="panel map-panel">
          <div className="panel-heading">
            <h3>Mission view</h3>
            <span className="subtle">METERS · SIMULATED WORLD</span>
          </div>
          <RoverMap state={state} trail={trail} />
          <div className="map-footer">
            <span>
              <i className="legend-dot" />
              Rover & trail
            </span>
            <span className="target-legend">⊕ Waypoint</span>
            <span className="position" data-testid="position">
              X {number(state?.position.x_m)} / Y {number(state?.position.y_m)}
            </span>
          </div>
        </section>

        <section className="panel controls">
          <div className="panel-heading">
            <h3>Command center</h3>
            <span className="subtle">OPERATOR</span>
          </div>
          <div className="control-body">
            <form
              onSubmit={(event) => {
                event.preventDefault();
                void send({ command: 'set_waypoint', x_m: Number(x), y_m: Number(y) });
              }}
            >
              <h4>
                01 <span>Set a waypoint</span>
              </h4>
              <div className="input-row">
                <label>
                  X position <span>m</span>
                  <input
                    aria-label="X position"
                    type="number"
                    step="0.1"
                    required
                    min={-(limits?.world_limit_m ?? 500)}
                    max={limits?.world_limit_m ?? 500}
                    value={x}
                    onChange={(e) => setX(e.target.value)}
                  />
                </label>
                <label>
                  Y position <span>m</span>
                  <input
                    aria-label="Y position"
                    type="number"
                    step="0.1"
                    required
                    min={-(limits?.world_limit_m ?? 500)}
                    max={limits?.world_limit_m ?? 500}
                    value={y}
                    onChange={(e) => setY(e.target.value)}
                  />
                </label>
              </div>
              <button className="secondary full" disabled={!ready || stopped || depleted}>
                Set waypoint
              </button>
            </form>
            <form
              onSubmit={(event) => {
                event.preventDefault();
                void send({ command: 'set_velocity', velocity_mps: Number(velocity) });
              }}
            >
              <h4>
                02 <span>Choose target velocity</span>
              </h4>
              <div className="velocity-input">
                <label className="sr-only" htmlFor="target-speed">
                  Target velocity
                </label>
                <input
                  id="target-speed"
                  type="number"
                  min="0"
                  max={limits?.max_velocity_mps ?? 3}
                  step="0.1"
                  required
                  value={velocity}
                  onChange={(e) => setVelocity(e.target.value)}
                />
                <span>m/s</span>
                <button className="secondary" disabled={!ready || depleted}>
                  Set velocity
                </button>
              </div>
            </form>
            <div>
              <h4>
                03 <span>Control the mission</span>
              </h4>
              <div className="command-grid">
                <button
                  className="primary"
                  onClick={() => void send({ command: 'start' })}
                  disabled={!ready || running || stopped || depleted || !state?.waypoint}
                >
                  Start
                </button>
                <button
                  className="secondary"
                  onClick={() => void send({ command: 'pause' })}
                  disabled={!ready || !running}
                >
                  Pause
                </button>
                <button
                  className="secondary"
                  onClick={() => void send({ command: 'resume' })}
                  disabled={!ready || !stopped || depleted}
                >
                  Resume
                </button>
                <button
                  className="secondary"
                  onClick={() => void send({ command: 'return_home' })}
                  disabled={!ready || stopped || depleted}
                >
                  Return home
                </button>
              </div>
            </div>
            <button
              className="emergency full"
              onClick={() => void send({ command: 'emergency_stop' })}
            >
              ■ Emergency stop
            </button>
            <p className="control-hint">Latches motion off. Resume explicitly releases the stop.</p>
            <p className={`notice ${notice.error ? 'error' : ''}`} role="status">
              {notice.text}
            </p>
            {!live ? (
              <p className="error">
                Live telemetry unavailable. Emergency stop still attempts a REST command.
              </p>
            ) : !limits ? (
              <p className="error">Waiting for simulator configuration.</p>
            ) : null}
          </div>
        </section>
      </div>

      <div className="lower-grid">
        <section className="panel">
          <div className="panel-heading">
            <h3>Sensor health</h3>
            <span className="subtle">IDEAL SIMULATED DEVICES</span>
          </div>
          <div className="sensor-list">
            {state?.sensors.map((sensor) => (
              <div key={sensor.name}>
                <span className="sensor-name">{sensor.name}</span>
                <span className={sensor.health === 'nominal' ? 'healthy' : 'error'}>
                  ● {sensor.health}
                </span>
                <span className="subtle">{sensor.source}</span>
              </div>
            )) ?? <p className="subtle">Waiting for sensor status…</p>}
          </div>
        </section>
        <section className="panel">
          <div className="panel-heading">
            <h3>System events</h3>
            <span className="subtle">CURRENT DIAGNOSTICS</span>
          </div>
          <div className="events">
            {warnings.length ? (
              warnings.map((warning) => (
                <p className="warning" key={warning}>
                  △ {warning}
                </p>
              ))
            ) : (
              <p className="healthy">
                {state ? '● No active warnings or errors' : 'Waiting for simulator…'}
              </p>
            )}
            <p className="subtle">
              {state
                ? `Simulation ${state.simulation_time_s.toFixed(1)} s · Timing overruns ${state.timing_overruns}`
                : 'Start the C++ service to connect.'}
            </p>
          </div>
        </section>
      </div>
      <footer>
        <span>
          ROVEROPS <span className="subtle">/ PHASE 01</span>
        </span>
        <span>
          {state
            ? `Packet ${state.sequence} · ${Number.isFinite(ageMs) ? ageMs.toFixed(0) : '—'} ms ago · ${new Date(state.timestamp_unix_ms).toLocaleTimeString()}`
            : 'Awaiting telemetry'}
          <span className="subtle">
            {' '}
            ·{' '}
            {limits
              ? `${limits.tick_hz} Hz core / ${limits.telemetry_hz} Hz stream`
              : 'Local connection'}
          </span>
        </span>
      </footer>
    </main>
  );
}
