#include "roverops/protocol.hpp"
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>

namespace roverops {
using nlohmann::json;
namespace {
void check_keys(const json& object, const std::set<std::string>& allowed) {
    if (!object.is_object())
        throw std::invalid_argument("Expected a JSON object");
    for (const auto& [key, value] : object.items()) {
        (void)value;
        if (!allowed.contains(key))
            throw std::invalid_argument("Unknown field: " + key);
    }
}
void read_int(const json& object, const char* key, int& value) {
    if (!object.contains(key))
        return;
    const auto& input = object.at(key);
    if (!input.is_number_integer() || input.get<double>() < 0 || input.get<double>() > 65535)
        throw std::invalid_argument(std::string(key) + " must be a nonnegative integer <= 65535");
    value = input.get<int>();
}
} // namespace

void AppConfig::validate() const {
    dynamics.validate();
    if (host != "127.0.0.1" && host != "0.0.0.0")
        throw std::invalid_argument("host must be 127.0.0.1 or 0.0.0.0");
    if (host == "0.0.0.0" && !public_demo)
        throw std::invalid_argument("External binding requires explicit public_demo mode");
    if (public_demo && allowed_origins.empty())
        throw std::invalid_argument("Public demo requires an explicit allowed browser origin");
    for (const auto& origin : allowed_origins) {
        if (!std::regex_match(origin, std::regex(R"(^https?://[A-Za-z0-9.-]+(:[0-9]{1,5})?$)")))
            throw std::invalid_argument(
                "Allowed origins must be exact HTTP(S) origins without paths or wildcards");
    }
    if (port < 1 || port > 65535)
        throw std::invalid_argument("port must be between 1 and 65535");
    if (tick_hz < 10 || tick_hz > 200)
        throw std::invalid_argument("tick_hz must be between 10 and 200");
    if (telemetry_hz < 1 || telemetry_hz > 30 || telemetry_hz > tick_hz)
        throw std::invalid_argument("telemetry_hz must be 1..30 and <= tick_hz");
    if (max_websocket_clients < 1 || max_websocket_clients > 8)
        throw std::invalid_argument("max_websocket_clients must be between 1 and 8");
}

AppConfig load_config(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot open configuration: " + path.string());
    const auto input = json::parse(file);
    check_keys(input, {"server", "dynamics"});
    AppConfig config;
    if (input.contains("server")) {
        const auto& server = input.at("server");
        check_keys(server, {"host", "port", "tick_hz", "telemetry_hz", "max_websocket_clients",
                            "allowed_origins", "public_demo", "static_root"});
        config.host = server.value("host", config.host);
        read_int(server, "port", config.port);
        read_int(server, "tick_hz", config.tick_hz);
        read_int(server, "telemetry_hz", config.telemetry_hz);
        read_int(server, "max_websocket_clients", config.max_websocket_clients);
        config.allowed_origins = server.value("allowed_origins", config.allowed_origins);
        config.public_demo = server.value("public_demo", config.public_demo);
        config.static_root = server.value("static_root", config.static_root);
    }
    if (input.contains("dynamics")) {
        const auto& d = input.at("dynamics");
        // Pointer-to-member is a compact mapping of config keys to plain data members.
        const std::map<std::string, double DynamicsConfig::*> fields{
            {"max_velocity_mps", &DynamicsConfig::max_velocity_mps},
            {"initial_target_velocity_mps", &DynamicsConfig::initial_target_velocity_mps},
            {"acceleration_mps2", &DynamicsConfig::acceleration_mps2},
            {"max_turn_rate_radps", &DynamicsConfig::max_turn_rate_radps},
            {"arrival_tolerance_m", &DynamicsConfig::arrival_tolerance_m},
            {"world_limit_m", &DynamicsConfig::world_limit_m},
            {"initial_battery_percent", &DynamicsConfig::initial_battery_percent},
            {"low_battery_percent", &DynamicsConfig::low_battery_percent},
            {"idle_drain_percent_per_s", &DynamicsConfig::idle_drain_percent_per_s},
            {"motion_drain_percent_per_m", &DynamicsConfig::motion_drain_percent_per_m}};
        if (!d.is_object())
            throw std::invalid_argument("dynamics must be a JSON object");
        for (const auto& [key, value] : d.items()) {
            const auto found = fields.find(key);
            if (found == fields.end())
                throw std::invalid_argument("Unknown dynamics field: " + key);
            if (!value.is_number())
                throw std::invalid_argument(key + " must be a number");
            config.dynamics.*(found->second) = value.get<double>();
        }
    }
    config.validate();
    return config;
}

Command parse_command(const json& payload) {
    if (!payload.is_object() || !payload.contains("command") || !payload.at("command").is_string())
        throw std::invalid_argument("Body must be an object with a string command field");
    const auto name = payload.at("command").get<std::string>();
    static const std::map<std::string, CommandType> types{
        {"start", CommandType::start},
        {"pause", CommandType::pause},
        {"emergency_stop", CommandType::emergency_stop},
        {"resume", CommandType::resume},
        {"set_velocity", CommandType::set_velocity},
        {"set_waypoint", CommandType::set_waypoint},
        {"return_home", CommandType::return_home}};
    const auto found = types.find(name);
    if (found == types.end())
        throw std::invalid_argument("Unknown command: " + name);
    Command command{found->second};
    if (command.type == CommandType::set_velocity) {
        check_keys(payload, {"command", "velocity_mps"});
        if (!payload.contains("velocity_mps") || !payload.at("velocity_mps").is_number())
            throw std::invalid_argument("set_velocity requires numeric velocity_mps");
        command.velocity_mps = payload.at("velocity_mps").get<double>();
    } else if (command.type == CommandType::set_waypoint) {
        check_keys(payload, {"command", "x_m", "y_m"});
        if (!payload.contains("x_m") || !payload.contains("y_m") ||
            !payload.at("x_m").is_number() || !payload.at("y_m").is_number())
            throw std::invalid_argument("set_waypoint requires numeric x_m and y_m");
        command.waypoint = {payload.at("x_m").get<double>(), payload.at("y_m").get<double>()};
    } else
        check_keys(payload, {"command"});
    return command;
}

json telemetry_json(const Snapshot& snapshot) {
    const auto& s = snapshot.state; // A read-only borrow of this local snapshot's owned state.
    json sensors = json::array();
    for (const auto& sensor : s.sensors)
        sensors.push_back({{"name", sensor.name},
                           {"health", to_string(sensor.health)},
                           {"source", sensor.source},
                           {"timestamp_unix_ms", snapshot.timestamp_unix_ms}});
    json waypoint = nullptr;
    if (s.waypoint)
        waypoint = {{"x_m", s.waypoint->x_m}, {"y_m", s.waypoint->y_m}};
    return {{"type", "telemetry"},
            {"schema_version", 1},
            {"sequence", snapshot.sequence},
            {"timestamp_unix_ms", snapshot.timestamp_unix_ms},
            {"timing_overruns", snapshot.timing_overruns},
            {"simulation_time_s", s.simulation_time_s},
            {"position", {{"x_m", s.position.x_m}, {"y_m", s.position.y_m}}},
            {"home", {{"x_m", s.home.x_m}, {"y_m", s.home.y_m}}},
            {"heading_rad", s.heading_rad},
            {"velocity_mps", s.velocity_mps},
            {"target_velocity_mps", s.target_velocity_mps},
            {"battery_percent", s.battery_percent},
            {"mode", to_string(s.mode)},
            {"waypoint", waypoint},
            {"sensors", sensors},
            {"warnings", s.warnings},
            {"errors", s.errors}};
}
} // namespace roverops
