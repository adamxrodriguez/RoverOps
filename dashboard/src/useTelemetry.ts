import { useEffect, useState } from 'react';
import { isTelemetry } from './types';
import type { Limits, Position, Telemetry } from './types';

export function useTelemetry() {
  const [state, setState] = useState<Telemetry | null>(null);
  const [trail, setTrail] = useState<Position[]>([]);
  const [status, setStatus] = useState('Connecting');
  const [receivedAt, setReceivedAt] = useState(0);
  const [now, setNow] = useState(Date.now());
  const [limits, setLimits] = useState<Limits | null>(null);

  useEffect(() => {
    let disposed = false;
    let socket: WebSocket | undefined;
    let retry: ReturnType<typeof setTimeout>;
    let attempts = 0;
    let configRequest: AbortController | undefined;
    const connect = () => {
      const current = new WebSocket(
        `${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}/ws/telemetry`,
      );
      socket = current;
      current.onopen = () => {
        if (disposed) return;
        attempts = 0;
        setStatus('Connected');
        setState(null);
        setTrail([]);
        setReceivedAt(0);
        setLimits(null);
        configRequest?.abort();
        configRequest = new AbortController();
        fetch('/api/config', { signal: configRequest.signal })
          .then((response) => {
            if (!response.ok) throw new Error('Configuration unavailable');
            return response.json();
          })
          .then((config: Limits) => {
            if (
              !disposed &&
              Number.isFinite(config.max_velocity_mps) &&
              Number.isFinite(config.world_limit_m)
            )
              setLimits(config);
          })
          .catch(() => {
            /* Commands remain disabled until limits can be loaded on reconnect. */
          });
      };
      current.onmessage = (event) => {
        if (disposed) return;
        try {
          const value: unknown = JSON.parse(event.data);
          if (!isTelemetry(value)) {
            setStatus('Invalid telemetry');
            return;
          }
          setState(value);
          setStatus('Connected');
          setReceivedAt(Date.now());
          setTrail((previous) => {
            const last = previous.at(-1);
            if (
              last &&
              Math.hypot(last.x_m - value.position.x_m, last.y_m - value.position.y_m) < 0.04
            )
              return previous;
            return [...previous.slice(-399), value.position];
          });
        } catch {
          setStatus('Invalid telemetry');
        }
      };
      current.onerror = () => current.close();
      current.onclose = () => {
        if (disposed) return;
        setStatus('Disconnected · reconnecting');
        retry = setTimeout(connect, Math.min(500 * 2 ** attempts++, 5000));
      };
    };
    // StrictMode's setup/cleanup probe can cancel before opening an unnecessary socket.
    retry = setTimeout(connect, 0);
    const clock = setInterval(() => setNow(Date.now()), 250);
    return () => {
      disposed = true;
      clearTimeout(retry);
      clearInterval(clock);
      configRequest?.abort();
      socket?.close();
    };
  }, []);

  const ageMs = receivedAt ? Math.max(0, now - receivedAt) : Infinity;
  const live = status === 'Connected' && ageMs < 2000;
  const connection =
    status === 'Connected' ? (live ? 'Live telemetry' : 'Telemetry stale') : status;
  return { state, trail, limits, live, connection, ageMs };
}
