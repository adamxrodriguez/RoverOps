#include "roverops/simulation.hpp"
#include <stdexcept>

namespace roverops {
Simulation::Simulation(DynamicsConfig config, int tick_hz) : rover_(config) {
    if (tick_hz < 10 || tick_hz > 200)
        throw std::invalid_argument("tick_hz must be between 10 and 200");
    period_ = std::chrono::nanoseconds(1'000'000'000 / tick_hz);
    stamp();
    // Start only after every member has been initialized. Capturing this is borrowing, not
    // ownership.
    worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
}

Simulation::~Simulation() {
    // jthread also joins automatically. Explicit shutdown makes the lifetime contract visible.
    worker_.request_stop();
    if (worker_.joinable())
        worker_.join();
}

Snapshot Simulation::snapshot() const {
    const std::lock_guard lock(mutex_);
    // Copy while locked. Returning a reference here would expose mutable data after unlocking.
    return {rover_.state(), sequence_, timestamp_unix_ms_, timing_overruns_};
}

CommandResult Simulation::command(const Command& command) {
    const std::lock_guard lock(mutex_);
    auto result = rover_.apply(command);
    if (result.ok)
        stamp();
    return result;
}

void Simulation::stamp() {
    ++sequence_;
    timestamp_unix_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
}

void Simulation::run(std::stop_token stop) {
    auto next = std::chrono::steady_clock::now() + period_;
    while (!stop.stop_requested()) {
        std::this_thread::sleep_until(next);
        if (stop.stop_requested())
            break;
        const auto now = std::chrono::steady_clock::now();
        {
            const std::lock_guard lock(mutex_);
            rover_.step(period_);
            if (now > next + period_)
                ++timing_overruns_;
            stamp();
        } // RAII unlocks here, including on exceptions; no manual unlock pairs.
        next += period_;
        if (next < now)
            next = now + period_; // Do not spin through an unbounded catch-up backlog.
    }
}
} // namespace roverops
