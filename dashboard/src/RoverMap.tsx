import type { Position, Telemetry } from './types';

export function RoverMap({ state, trail }: { state: Telemetry | null; trail: Position[] }) {
  const points = [...trail, state?.position, state?.waypoint, state?.home].filter(
    (p): p is Position => !!p,
  );
  const extent = Math.max(
    10,
    ...points.flatMap((p) => [Math.abs(p.x_m) * 1.25, Math.abs(p.y_m) * 1.25]),
  );
  const scale = 210 / extent;
  const xy = (p: Position) => `${260 + p.x_m * scale},${240 - p.y_m * scale}`;
  return (
    <div className="map-wrap">
      <svg
        viewBox="0 0 520 480"
        role="img"
        aria-label="Rover map showing home, trail, heading, and target waypoint"
      >
        <defs>
          <pattern id="grid" width="42" height="42" x="8" y="-12" patternUnits="userSpaceOnUse">
            <path d="M 42 0 L 0 0 0 42" fill="none" stroke="#293d47" strokeWidth="0.7" />
          </pattern>
        </defs>
        <rect width="520" height="480" fill="url(#grid)" />
        <line x1="30" y1="240" x2="490" y2="240" stroke="#48616a" />
        <line x1="260" y1="20" x2="260" y2="460" stroke="#48616a" />
        <text x="491" y="232" className="map-label">
          +X
        </text>
        <text x="271" y="24" className="map-label">
          +Y
        </text>
        <circle cx="260" cy="240" r="6" fill="#8fa3ae" />
        <text x="273" y="261" className="map-label">
          HOME
        </text>
        <polyline
          points={trail.map(xy).join(' ')}
          fill="none"
          stroke="#53d9ba"
          strokeWidth="2.5"
          strokeOpacity="0.55"
        />
        {state?.waypoint ? (
          <g transform={`translate(${xy(state.waypoint)})`}>
            <circle r="12" stroke="#e8b96e" strokeWidth="2" fill="none" />
            <path d="M -18 0 H 18 M 0 -18 V 18" stroke="#e8b96e" />
            <text x="16" y="-15" className="map-label target-label">
              TARGET
            </text>
          </g>
        ) : null}
        {state ? (
          <g transform={`translate(${xy(state.position)})`}>
            <circle r="20" fill="#53d9ba" fillOpacity="0.1" />
            <path
              d="M 13 0 L -9 -8 L -5 0 L -9 8 Z"
              fill="#74f0d1"
              stroke="#112b28"
              strokeWidth="1.5"
              transform={`rotate(${(-state.heading_rad * 180) / Math.PI})`}
            />
          </g>
        ) : null}
        <text x="20" y="455" className="map-label">
          GRID {(extent / 5).toFixed(1)} m · LOCAL XY
        </text>
      </svg>
      {!state ? <div className="map-empty">Waiting for simulator telemetry…</div> : null}
    </div>
  );
}
