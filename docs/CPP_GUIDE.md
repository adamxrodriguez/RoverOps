# Reading RoverOps as a Java or JavaScript engineer

Read the code in this order: `core/include/roverops/rover.hpp`, `core/src/rover.cpp`,
`service/src/simulation.cpp`, `service/src/protocol.cpp`, `service/src/api_server.cpp`,
then `service/src/main.cpp`. Each layer introduces one additional concern.

## Values, references, and ownership

`Simulation` contains its `Rover` **by value**. There is one owner and no reason to
allocate a rover with `new`. C++ member objects are destroyed automatically with
their containing object; no garbage collector decides when that happens.

`const Command&` borrows an existing command for the duration of the call and
prevents modification through that reference. It neither copies the command nor
extends its lifetime. Do not store it for later unless you copy the value.

`Rover::state()` returns `const RoverState&`, a cheap read-only view of the model.
That is safe inside the locked service operation. `Simulation::snapshot()` returns
an **owned copy** instead. A reference to shared state would become unsafe as soon
as the lock guard was destroyed and another thread wrote to the rover.

`ApiServer` owns `std::unique_ptr<httplib::Server>`. This is exclusive ownership,
used here to hide a large library header behind a forward declaration. Its destructor
is defined in the `.cpp`, where the server type is complete. `std::make_unique`
allocates and transfers ownership safely even if construction throws. There is no
`shared_ptr`: no application object needs shared ownership.

The networking library's task-queue factory requires a raw pointer; the library
takes ownership of that pointer. This is a library boundary, not a general pattern
for application ownership. Its returned queue is destroyed by the server.

## RAII and destruction order

RAII ties resource cleanup to object lifetime: stack unwinding invokes destructors
on normal returns and exceptions. `std::lock_guard` unlocks its mutex on leaving
the scope. `std::ifstream` closes its file. `unique_ptr` destroys its owned server.

Members are initialized in **declaration order**, not initializer-list order, and
destroyed in reverse order. `Simulation` declares its worker last and also explicitly
requests stop and joins in its destructor. The worker cannot outlive its mutex or
rover. In `main`, the simulation is constructed before the API, and the shutdown
watcher after the API. Destruction therefore stops the watcher before destroying
the API, then destroys the simulation last.

## Threads and locking

A `std::jthread` is a joining thread with a cooperative stop token. `request_stop()`
sets a flag; it does not forcibly kill code or interrupt `sleep_until`. The worker
checks the flag between ticks, so shutdown waits at most one configured tick for
the simulation loop. A detached thread would make object lifetime much harder to
prove and is intentionally avoided.

The simulation loop and REST handlers share one mutex. Each update, command, or
snapshot is atomic with respect to the others. `mutable` on the mutex permits a
logically read-only `snapshot() const` to lock; it does not make the rover data
freely mutable. JSON construction and socket I/O happen **after** copying and
unlocking, so a slow client cannot hold the rover lock.

WebSocket connection counters and the shutdown flag use `std::atomic`, because
they are independent scalar values. An atomic flag would not protect a multi-field
rover update; that invariant requires the mutex. Signal handlers use a separate
lock-free `std::atomic<bool>` and do nothing except store to it. A compile-time
assertion guarantees that this atomic needs no lock; it is safe for the signal
handler and watcher thread to share. Logging, locks, and network shutdown occur
later in a normal thread.

## Move semantics and return values

`std::move(config)` permits moving the configuration's strings and vector into the
API object. It is a cast enabling move construction, not a magic move operation
itself. The source remains valid but its contents should not be relied on afterward.
The plain dynamics configuration is small enough to pass and copy by value.

Return-value optimization often constructs returned snapshots/results directly in
the caller's storage. Do not add `std::move` to local return expressions: it can
prevent this optimization. Copying a snapshot's small vectors is a deliberate
clarity tradeoff at this update rate, not a zero-copy design.

## Types, errors, and templates

`enum class Mode` and `enum class CommandType` are distinct types; C++ will not
silently mix them with integers or each other. `Position` groups coordinates,
unit suffixes document SI values, `std::chrono` expresses durations, and
`std::optional<Position>` represents an absent waypoint without sentinel values.
The code does not introduce a general physical-units template library.

Expected command rejection is `CommandResult`, not an exception. Invalid startup
configuration and malformed wire inputs use exceptions at boundaries where they
can be caught and converted to a useful CLI or HTTP error. `Rover::step` throws
for programmer misuse (an invalid time step); valid service configuration cannot
produce that case.

STL templates such as `vector<SensorStatus>` and `optional<Position>` generate
type-specific code at compile time. Generic API lambdas simply let the compiler
deduce request/response reference types; there is no custom template framework.
The config loader's `double DynamicsConfig::*` maps a key to a member within the
configuration object. It is not a pointer to independently allocated memory.

## Defend these choices in an interview

1. Why would returning a reference from `snapshot()` be a data race?
2. What could happen if the worker were detached or the API outlived `Simulation`?
3. Why does serializing inside the lock increase command latency?
4. Why does `start` reject an emergency-stopped rover, even when it has a waypoint?
5. Why is a fresh snapshot sufficient for telemetry while commands need acknowledgments?
6. How would a hardware actuator adapter change stop acknowledgment and safety semantics?

Try one extension at a time: first test an intermittent sensor fault, then add its
model and diagnostics. Keep ROS 2 transport out of the deterministic domain model.
