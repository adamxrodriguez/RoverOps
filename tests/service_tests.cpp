#include "roverops/protocol.hpp"
#include <atomic>
#include <doctest.h>
#include <thread>

using namespace roverops;
using namespace std::chrono_literals;
using nlohmann::json;

TEST_CASE("command parser rejects malformed shapes and unknown fields") {
    for (const auto& input :
         {json(nullptr), json::array(), json{{"command", 42}}, json{{"command", "fly"}},
          json{{"command", "start"}, {"velocity_mps", 1}},
          json{{"command", "set_waypoint"}, {"x_m", 1}},
          json{{"command", "set_velocity"}, {"velocity_mps", "fast"}},
          json{{"command", "set_velocity"}, {"velocity_mps", true}}}) {
        CHECK_THROWS_AS(parse_command(input), std::invalid_argument);
    }
    CHECK(parse_command({{"command", "set_velocity"}, {"velocity_mps", 1.5}}).velocity_mps == 1.5);
    CHECK(parse_command({{"command", "set_waypoint"}, {"x_m", 2}, {"y_m", 3}}).waypoint.y_m == 3);
}

TEST_CASE("configuration file loads and server bounds are checked") {
    const auto config = load_config("config/rover.json");
    CHECK(config.tick_hz == 50);
    CHECK(config.dynamics.max_velocity_mps == 3);
    AppConfig invalid;
    invalid.telemetry_hz = 99;
    CHECK_THROWS_AS(invalid.validate(), std::invalid_argument);
    CHECK_THROWS_AS(load_config("missing-roverops-config.json"), std::runtime_error);
}

TEST_CASE("telemetry has versioned SI-unit fields and explicit sensor provenance") {
    Simulation simulation;
    const auto state = telemetry_json(simulation.snapshot());
    CHECK(state.at("schema_version") == 1);
    CHECK(state.at("waypoint").is_null());
    CHECK(state.at("mode") == "idle");
    CHECK(state.at("timestamp_unix_ms").get<std::int64_t>() > 0);
    CHECK(state.at("sensors").at(0).at("source") == "simulated");
}

TEST_CASE("public deployment requires explicit mode and exact origins") {
    AppConfig config;
    config.host = "0.0.0.0";
    CHECK_THROWS_AS(config.validate(), std::invalid_argument);
    config.public_demo = true;
    config.allowed_origins = {"https://rover.example.com"};
    CHECK_NOTHROW(config.validate());
    config.allowed_origins = {"*"};
    CHECK_THROWS_AS(config.validate(), std::invalid_argument);
    config.allowed_origins = {"https://rover.example.com/path"};
    CHECK_THROWS_AS(config.validate(), std::invalid_argument);
    config.allowed_origins.clear();
    CHECK_THROWS_AS(config.validate(), std::invalid_argument);
}

TEST_CASE("simulation advances independently and snapshots remain owned copies") {
    Simulation simulation;
    const auto first = simulation.snapshot();
    REQUIRE(simulation.command({CommandType::set_waypoint, 0, {5, 0}}).ok);
    REQUIRE(simulation.command({CommandType::start}).ok);
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (simulation.snapshot().state.position.x_m == 0 &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(10ms);
    const auto second = simulation.snapshot();
    CHECK(second.sequence > first.sequence);
    CHECK(second.state.position.x_m > 0);
    CHECK(first.state.position.x_m == 0);
    CHECK(first.state.mode == Mode::idle);
}

TEST_CASE("concurrent commands and snapshots preserve invariants") {
    Simulation simulation;
    REQUIRE(simulation.command({CommandType::set_waypoint, 0, {10, 2}}).ok);
    REQUIRE(simulation.command({CommandType::start}).ok);
    std::atomic<bool> valid{true};
    std::vector<std::jthread> clients;
    for (int client = 0; client < 4; ++client) {
        clients.emplace_back([&] {
            std::uint64_t previous = 0;
            for (int i = 0; i < 500; ++i) {
                if (!simulation.command({CommandType::set_velocity, (i % 3) * 1.0}).ok)
                    valid = false;
                const auto snapshot = simulation.snapshot();
                if (snapshot.sequence < previous || snapshot.state.velocity_mps < 0 ||
                    snapshot.state.velocity_mps > 3 || snapshot.state.battery_percent < 0)
                    valid = false;
                previous = snapshot.sequence;
            }
        });
    }
    clients.clear(); // Each jthread joins before the captured references leave scope.
    CHECK(valid.load());
}

TEST_CASE("RAII shutdown joins promptly and invalid tick rates fail before starting") {
    CHECK_THROWS_AS(Simulation({}, 0), std::invalid_argument);
    const auto start = std::chrono::steady_clock::now();
    {
        Simulation simulation({}, 100);
    }
    CHECK(std::chrono::steady_clock::now() - start < 1s);
}
