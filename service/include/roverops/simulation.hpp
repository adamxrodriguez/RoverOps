#pragma once

#include "roverops/rover.hpp"
#include <cstdint>
#include <mutex>
#include <thread>

namespace roverops {
struct Snapshot {
    RoverState state;
    std::uint64_t sequence{};
    std::int64_t timestamp_unix_ms{};
    std::uint64_t timing_overruns{};
};

class Simulation {
  public:
    explicit Simulation(DynamicsConfig config = {}, int tick_hz = 50);
    ~Simulation();
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;
    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] CommandResult command(const Command& command);

  private:
    void run(std::stop_token stop);
    void stamp(); // Caller holds mutex_.
    // Declaration order is lifetime order. worker_ must be destroyed before the data it uses.
    mutable std::mutex mutex_;
    Rover rover_;
    std::chrono::nanoseconds period_;
    std::uint64_t sequence_{};
    std::int64_t timestamp_unix_ms_{};
    std::uint64_t timing_overruns_{};
    std::jthread worker_;
};
} // namespace roverops
