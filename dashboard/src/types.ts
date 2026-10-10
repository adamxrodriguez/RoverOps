export type Position = { x_m: number; y_m: number };
export type Mode =
  | 'idle'
  | 'navigating'
  | 'returning_home'
  | 'paused'
  | 'emergency_stopped'
  | 'arrived'
  | 'depleted';
export type Telemetry = {
  type: 'telemetry';
  schema_version: 1;
  sequence: number;
  timestamp_unix_ms: number;
  simulation_time_s: number;
  timing_overruns: number;
  position: Position;
  home: Position;
  waypoint: Position | null;
  heading_rad: number;
  velocity_mps: number;
  target_velocity_mps: number;
  battery_percent: number;
  mode: Mode;
  sensors: { name: string; health: string; source: string; timestamp_unix_ms: number }[];
  warnings: string[];
  errors: string[];
};
export type Limits = {
  max_velocity_mps: number;
  world_limit_m: number;
  arrival_tolerance_m: number;
  tick_hz: number;
  telemetry_hz: number;
};
export type Command =
  | { command: 'start' | 'pause' | 'resume' | 'emergency_stop' | 'return_home' }
  | { command: 'set_velocity'; velocity_mps: number }
  | { command: 'set_waypoint'; x_m: number; y_m: number };

// Validate the wire boundary; TypeScript assertions alone do not validate incoming JSON.
export function isTelemetry(value: unknown): value is Telemetry {
  if (!value || typeof value !== 'object') return false;
  const v = value as Record<string, unknown>;
  const position = (p: unknown): p is Position =>
    !!p &&
    typeof p === 'object' &&
    Number.isFinite((p as Position).x_m) &&
    Number.isFinite((p as Position).y_m);
  const strings = (p: unknown) => Array.isArray(p) && p.every((x) => typeof x === 'string');
  return (
    v.type === 'telemetry' &&
    v.schema_version === 1 &&
    [
      'sequence',
      'timestamp_unix_ms',
      'simulation_time_s',
      'timing_overruns',
      'heading_rad',
      'velocity_mps',
      'target_velocity_mps',
      'battery_percent',
    ].every((k) => Number.isFinite(v[k])) &&
    position(v.position) &&
    position(v.home) &&
    (v.waypoint === null || position(v.waypoint)) &&
    [
      'idle',
      'navigating',
      'returning_home',
      'paused',
      'emergency_stopped',
      'arrived',
      'depleted',
    ].includes(String(v.mode)) &&
    strings(v.warnings) &&
    strings(v.errors) &&
    Array.isArray(v.sensors) &&
    v.sensors.every(
      (s) =>
        s &&
        typeof s.name === 'string' &&
        typeof s.health === 'string' &&
        typeof s.source === 'string' &&
        Number.isFinite(s.timestamp_unix_ms),
    )
  );
}
