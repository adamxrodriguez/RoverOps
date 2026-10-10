#pragma once
#include "roverops/simulation.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace roverops {
struct AppConfig {
    DynamicsConfig dynamics;
    std::string host{"127.0.0.1"};
    int port{8080};
    int tick_hz{50};
    int telemetry_hz{10};
    int max_websocket_clients{4};
    bool public_demo{false};
    std::string static_root;
    std::vector<std::string> allowed_origins{"http://127.0.0.1:5173", "http://localhost:5173"};
    void validate() const;
};
AppConfig load_config(const std::filesystem::path& path);
Command parse_command(const nlohmann::json& payload);
nlohmann::json telemetry_json(const Snapshot& snapshot);
} // namespace roverops
