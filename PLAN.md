# RoverOps Phase 1

Build a runnable single-rover C++20 system with a React/TypeScript operator console.

## Architecture

- `core/`: deterministic rover model, bounded motion and battery dynamics, explicit command state machine; no HTTP or wall-clock dependencies.
- `service/`: one simulation worker, mutex-protected state, JSON/config boundary, REST + WebSocket adapter using cpp-httplib.
- `dashboard/`: React + Vite; live telemetry, map, connection health, and controls. Vite proxies `/api` and `/ws` locally.
- `tests/`: doctest domain/service tests and a real-process HTTP/WebSocket acceptance test.
- `docs/`: C++ learning notes, verification evidence, and a dashboard screenshot.
- `third_party/`: pinned, licensed single-header dependencies; C++ builds need no dependency download.

## Decisions

- SI units, bounded acceleration/turn rate, braking near a waypoint, and explicit simulated sensor status.
- Emergency stop latches until `resume`; `start` and `return_home` cannot bypass it. Stops are idealized, not hardware safety guarantees.
- Copy snapshots under a short lock, then serialize/send outside it. Networking never owns the rover model.
- Loopback binding; local operator tool, no cloud deployment or hardware control in Phase 1.
- Own the simulation worker through RAII and stop/join before destroying shared state.

## Delivery sequence

1. Implement and test model/state transitions.
2. Add config, simulation loop, JSON, REST/WebSocket, and shutdown.
3. Build the React dashboard and connect live controls.
4. Verify domain tests, invalid commands, concurrent snapshots, movement, stop/resume, return home, reconnect, and shutdown.
5. Add CI, README, learning guide, and exact run commands; leave both apps running locally.

## Delivery record

Phase 1 implementation is complete. The Debug build, 17 C++ test cases, real-process
REST/WebSocket tests, three browser tests, and sanitizer unit tests passed. The README
and C++ guide explain the implementation and extension boundaries; verification
details and a real screenshot are under `docs/`. GitHub CI awaits the first push.
