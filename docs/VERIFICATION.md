# Phase 1 local verification

Verified October 9, 2026 (America/New_York) in the standalone RoverOps repository.

## Environment

- macOS, Intel x86_64; Apple Clang 17.0.0 with the Xcode C++ standard library.
- CMake 4.4.4 installed in the ignored project-local `.tools` environment.
- Node.js 25.4.0, npm 11.7.0; frontend versions pinned in `package-lock.json`.
- Playwright 1.64.0 with its Chromium browser.

## Checks performed

| Check | Result |
| --- | --- |
| C++20 Debug build with `-Wall -Wextra -Wpedantic -Werror` | Passed |
| doctest domain/service suite | 17 test cases, 2,170 assertions passed |
| AddressSanitizer + UndefinedBehaviorSanitizer domain/service tests | Passed |
| REST/WebSocket process acceptance test | Passed |
| REST/WebSocket process acceptance with ASan + UBSan executable | Passed; clean shutdown in 173 ms |
| SIGTERM with connected WebSocket clients | Clean exit; 109 ms in the initial smoke run |
| Strict TypeScript check and Vite production build | Passed |
| Clean frontend dependency install with `npm ci` | Passed |
| Browser mission: waypoint, start, speed, pause, resume, E-stop, return home | Passed |
| Mobile layout and disabled controls without telemetry | Passed |
| Forced WebSocket disconnect and dashboard reconnect | Passed |
| Invalid CLI argument, missing config, fractional tick rate, unknown key, non-loopback host | Rejected with nonzero exit and useful error |
| Vendored dependency SHA-256 manifest | All six header/license files matched |
| C++ and frontend formatting checks | Passed |
| Final running application at `http://127.0.0.1:5173` | Browser confirmed live telemetry and idle mode |

The process test also checks malformed JSON, input bounds, content type, unknown
fields, denied browser origin, payload limits, E-stop bypass rejection, telemetry
capacity, spare HTTP command capacity, disconnect/reconnect, and completed arrival
within the configured home tolerance.

`screenshots/dashboard.png` is a real browser capture made during the successful
mission test. Its state and timing values are an observed sample, not performance
benchmarks. Compilation and browser startup can cause visible timing overruns on
this development machine; skipped simulation ticks are counted explicitly.

Vite may log `EPIPE` when the reconnect test deliberately closes a proxied socket
or when test processes shut down. The browser tests passed with no page JavaScript
errors. This is distinct from a failed simulator command or assertion.

One newly rebuilt executable took more than eight seconds to become ready on this
Mac. The initial short process-test readiness limit failed; the live process then
started and served telemetry normally. The test harness now allows 60 seconds for
startup, without relaxing command, movement, or shutdown assertions. The cause of
that initial launch delay was not established.

## Verification limits

The public repository is [adamxrodriguez/RoverOps](https://github.com/adamxrodriguez/RoverOps).
The [initial hosted CI run](https://github.com/adamxrodriguez/RoverOps/actions/runs/38034125810)
passed on October 10, 2026: GCC and Clang C++ tests, ASan/UBSan, API acceptance,
frontend build, and Chromium browser tests on Ubuntu 24.04.
ThreadSanitizer, Windows, load testing, and physical hardware
have not been verified. Sanitizer checks are evidence for these exercised
paths, not proof of the absence of all memory/concurrency errors.

The simulated stop is immediate software state change. No actuator, physical
interlock, braking distance, communication-loss watchdog, or real sensor is tested.
