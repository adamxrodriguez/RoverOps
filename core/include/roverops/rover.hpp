#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace roverops {

struct Position {
    double x_m{};
    double y_m{};
};

enum class Mode { idle, navigating, paused, emergency_stopped, returning_home, arrived, depleted };
enum class CommandType {
    start,
    pause,
    emergency_stop,
    resume,
    set_velocity,
    set_waypoint,
    return_home
};
enum class Health { nominal, unavailable };

std::string_view to_string(Mode mode);
std::string_view to_string(Health health);
double distance(Position a, Position b);

struct DynamicsConfig {
    double max_velocity_mps{3.0};
    double initial_target_velocity_mps{1.0};
    double acceleration_mps2{0.8};
    double max_turn_rate_radps{1.2};
    double arrival_tolerance_m{0.15};
    double world_limit_m{500.0};
    double initial_battery_percent{100.0};
    double low_battery_percent{20.0};
    double idle_drain_percent_per_s{0.002};
    double motion_drain_percent_per_m{0.006};
    void validate() const;
};

struct SensorStatus {
    std::string name;
    Health health{Health::nominal};
    // Phase 1 reports ideal simulated devices; it does not pretend to read hardware.
    std::string source{"simulated"};
};

struct RoverState {
    Position position;
    Position home;
    double heading_rad{}; // Radians, counterclockwise from +X, in [-pi, pi].
    double velocity_mps{};
    double target_velocity_mps{1.0};
    double battery_percent{100.0};
    double simulation_time_s{};
    Mode mode{Mode::idle};
    std::optional<Position> waypoint;
    std::vector<SensorStatus> sensors{{"odometry"}, {"imu"}, {"battery"}};
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

struct Command {
    CommandType type;
    double velocity_mps{};
    Position waypoint{};
};

struct CommandResult {
    bool ok;
    std::string code;
    std::string message;
};

// A deterministic value object. Synchronization belongs to Simulation, not this model.
class Rover {
  public:
    explicit Rover(DynamicsConfig config = {});
    [[nodiscard]] const RoverState& state() const noexcept { return state_; }
    [[nodiscard]] CommandResult apply(const Command& command);
    void step(std::chrono::duration<double> elapsed);

  private:
    void refresh_diagnostics();
    DynamicsConfig config_;
    RoverState state_;
    Mode resume_mode_{Mode::idle};
    Mode before_estop_{Mode::idle};
};

} // namespace roverops
