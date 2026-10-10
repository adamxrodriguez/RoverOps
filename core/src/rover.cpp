#include "roverops/rover.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace roverops {
namespace {
bool active(Mode mode) { return mode == Mode::navigating || mode == Mode::returning_home; }
double wrap_angle(double angle) { return std::remainder(angle, 2.0 * std::numbers::pi); }
bool between(double n, double low, double high) {
    return std::isfinite(n) && n >= low && n <= high;
}
} // namespace

std::string_view to_string(Mode mode) {
    switch (mode) {
    case Mode::idle:
        return "idle";
    case Mode::navigating:
        return "navigating";
    case Mode::paused:
        return "paused";
    case Mode::emergency_stopped:
        return "emergency_stopped";
    case Mode::returning_home:
        return "returning_home";
    case Mode::arrived:
        return "arrived";
    case Mode::depleted:
        return "depleted";
    }
    return "unknown";
}

std::string_view to_string(Health health) {
    return health == Health::nominal ? "nominal" : "unavailable";
}

double distance(Position a, Position b) { return std::hypot(a.x_m - b.x_m, a.y_m - b.y_m); }

void DynamicsConfig::validate() const {
    if (!between(max_velocity_mps, 0.1, 20.0) ||
        !between(initial_target_velocity_mps, 0.0, max_velocity_mps) ||
        !between(acceleration_mps2, 0.01, 10.0) || !between(max_turn_rate_radps, 0.05, 10.0) ||
        !between(arrival_tolerance_m, 0.01, 5.0) || !between(world_limit_m, 10.0, 10000.0) ||
        !between(initial_battery_percent, 0.0, 100.0) ||
        !between(low_battery_percent, 0.0, 100.0) ||
        !between(idle_drain_percent_per_s, 0.0, 10.0) ||
        !between(motion_drain_percent_per_m, 0.0, 10.0)) {
        throw std::invalid_argument(
            "Invalid dynamics configuration; see config/rover.json and README bounds");
    }
}

Rover::Rover(DynamicsConfig config) : config_(config) {
    config_.validate();
    state_.target_velocity_mps = config_.initial_target_velocity_mps;
    state_.battery_percent = config_.initial_battery_percent;
    if (state_.battery_percent == 0.0)
        state_.mode = Mode::depleted;
    refresh_diagnostics();
}

CommandResult Rover::apply(const Command& command) {
    const auto reject = [](std::string message, std::string code = "invalid_transition") {
        return CommandResult{false, std::move(code), std::move(message)};
    };
    // An emergency stop is idempotent and must never be rejected by a mode check.
    if (command.type == CommandType::emergency_stop) {
        if (state_.mode != Mode::emergency_stopped)
            before_estop_ = state_.mode;
        state_.mode = Mode::emergency_stopped;
        state_.velocity_mps = 0.0;
        refresh_diagnostics();
        return {true, "ok", "Emergency stop latched; only resume can release it"};
    }
    if (state_.battery_percent <= 0.0)
        return reject("Battery depleted; restart the simulator to recharge");

    switch (command.type) {
    case CommandType::start:
        if (state_.mode == Mode::emergency_stopped || state_.mode == Mode::paused)
            return reject("Use resume to release the stop before starting");
        if (!state_.waypoint)
            return reject("Set a waypoint before starting");
        if (active(state_.mode))
            return reject("Rover is already running");
        state_.mode = Mode::navigating;
        break;
    case CommandType::pause:
        if (!active(state_.mode))
            return reject("Pause requires a running rover");
        resume_mode_ = state_.mode;
        state_.mode = Mode::paused;
        state_.velocity_mps = 0.0;
        break;
    case CommandType::resume:
        if (state_.mode != Mode::paused && state_.mode != Mode::emergency_stopped)
            return reject("Resume requires a paused or emergency-stopped rover");
        // E-stop while paused releases into paused; a second resume explicitly restarts motion.
        state_.mode = state_.mode == Mode::emergency_stopped ? before_estop_ : resume_mode_;
        break;
    case CommandType::set_velocity:
        if (!between(command.velocity_mps, 0.0, config_.max_velocity_mps))
            return reject("velocity_mps must be finite and between 0 and the configured maximum",
                          "invalid_value");
        state_.target_velocity_mps = command.velocity_mps;
        break;
    case CommandType::set_waypoint:
        if (!between(command.waypoint.x_m, -config_.world_limit_m, config_.world_limit_m) ||
            !between(command.waypoint.y_m, -config_.world_limit_m, config_.world_limit_m))
            return reject("Waypoint coordinates must be finite and inside the configured world",
                          "invalid_value");
        if (state_.mode == Mode::emergency_stopped || state_.mode == Mode::paused)
            return reject("Resume before changing the mission waypoint");
        state_.waypoint = command.waypoint;
        state_.mode = active(state_.mode) ? Mode::navigating : Mode::idle;
        break;
    case CommandType::return_home:
        if (state_.mode == Mode::emergency_stopped || state_.mode == Mode::paused)
            return reject("Use resume before return_home; this command cannot release a stop");
        state_.waypoint = state_.home;
        state_.mode = Mode::returning_home;
        break;
    case CommandType::emergency_stop:
        break; // Handled above.
    }
    refresh_diagnostics();
    return {true, "ok", "Command applied"};
}

void Rover::step(std::chrono::duration<double> elapsed) {
    const double dt = elapsed.count();
    if (!between(dt, 0.000001, 0.1))
        throw std::invalid_argument("Simulation step must be in (0, 0.1] seconds");
    state_.simulation_time_s += dt;
    double travelled = 0.0;
    if (active(state_.mode) && state_.waypoint) {
        const auto goal = *state_.waypoint;
        const double remaining = distance(state_.position, goal);
        if (remaining <= config_.arrival_tolerance_m) {
            state_.velocity_mps = 0.0;
            state_.mode = Mode::arrived;
        } else {
            const double desired_heading =
                std::atan2(goal.y_m - state_.position.y_m, goal.x_m - state_.position.x_m);
            const double turn =
                std::clamp(wrap_angle(desired_heading - state_.heading_rad),
                           -config_.max_turn_rate_radps * dt, config_.max_turn_rate_radps * dt);
            state_.heading_rad = wrap_angle(state_.heading_rad + turn);
            const double alignment =
                std::max(0.0, std::cos(wrap_angle(desired_heading - state_.heading_rad)));
            const double braking_speed =
                std::sqrt(2.0 * config_.acceleration_mps2 *
                          std::max(0.0, remaining - config_.arrival_tolerance_m / 2.0));
            const double requested =
                std::min(state_.target_velocity_mps * alignment, braking_speed);
            state_.velocity_mps +=
                std::clamp(requested - state_.velocity_mps, -config_.acceleration_mps2 * dt,
                           config_.acceleration_mps2 * dt);
            travelled = state_.velocity_mps * dt;
            state_.position.x_m += travelled * std::cos(state_.heading_rad);
            state_.position.y_m += travelled * std::sin(state_.heading_rad);
        }
    }
    state_.battery_percent =
        std::max(0.0, state_.battery_percent - config_.idle_drain_percent_per_s * dt -
                          config_.motion_drain_percent_per_m * travelled);
    if (state_.battery_percent == 0.0) {
        state_.velocity_mps = 0.0;
        // Do not silently release a latched E-stop when the battery runs out.
        if (state_.mode != Mode::emergency_stopped)
            state_.mode = Mode::depleted;
    }
    refresh_diagnostics();
}

void Rover::refresh_diagnostics() {
    state_.warnings.clear();
    state_.errors.clear();
    if (state_.battery_percent <= config_.low_battery_percent)
        state_.warnings.emplace_back("Low battery");
    if (state_.battery_percent <= 0.0)
        state_.errors.emplace_back("Battery depleted; motion disabled");
    if (state_.mode == Mode::emergency_stopped)
        state_.warnings.emplace_back("Emergency stop latched");
    if (state_.mode == Mode::paused)
        state_.warnings.emplace_back("Mission paused");
    if (active(state_.mode) && state_.target_velocity_mps == 0.0)
        state_.warnings.emplace_back("Target velocity is zero");
}
} // namespace roverops
