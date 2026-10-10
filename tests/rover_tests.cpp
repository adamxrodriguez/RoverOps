#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "roverops/rover.hpp"
#include <cmath>
#include <doctest.h>
#include <limits>
#include <numbers>

using namespace roverops;
using namespace std::chrono_literals;
namespace {
void mission(Rover& rover, Position target = {8, 4}) {
    REQUIRE(rover.apply({CommandType::set_waypoint, 0, target}).ok);
    REQUIRE(rover.apply({CommandType::start}).ok);
}
void advance(Rover& rover, int ticks) {
    for (int i = 0; i < ticks; ++i)
        rover.step(20ms);
}
} // namespace

TEST_CASE("initial state is stationary with simulated nominal sensors") {
    Rover rover;
    CHECK(rover.state().mode == Mode::idle);
    CHECK(rover.state().velocity_mps == 0);
    CHECK_FALSE(rover.state().waypoint);
    CHECK(rover.state().sensors.size() == 3);
    CHECK_FALSE(rover.apply({CommandType::start}).ok);
    CHECK_FALSE(rover.apply({CommandType::resume}).ok);
    CHECK_FALSE(rover.apply({CommandType::pause}).ok);
}

TEST_CASE("motion obeys acceleration and turn bounds without teleporting") {
    Rover rover;
    mission(rover, {-8, 8});
    for (int i = 0; i < 700; ++i) {
        const auto before = rover.state();
        rover.step(20ms);
        const auto& after = rover.state();
        CHECK(distance(before.position, after.position) <= 3.0 * 0.02 + 1e-9);
        CHECK(std::abs(std::remainder(after.heading_rad - before.heading_rad,
                                      2 * std::numbers::pi)) <= 1.2 * 0.02 + 1e-9);
        if (after.mode != Mode::arrived)
            CHECK(std::abs(after.velocity_mps - before.velocity_mps) <= 0.8 * 0.02 + 1e-9);
    }
}

TEST_CASE("waypoints in every quadrant converge without snapping position") {
    for (const auto target : {Position{5, 3}, Position{-5, 3}, Position{-5, -3}, Position{5, -3},
                              Position{0.01, 0.01}}) {
        Rover rover;
        mission(rover, target);
        advance(rover, 2000);
        CHECK(rover.state().mode == Mode::arrived);
        CHECK(distance(rover.state().position, target) <= 0.15);
        CHECK(rover.state().velocity_mps == 0);
    }
}

TEST_CASE("pause and resume preserve mission") {
    Rover rover;
    mission(rover);
    advance(rover, 80);
    REQUIRE(rover.apply({CommandType::pause}).ok);
    const auto position = rover.state().position;
    advance(rover, 50);
    CHECK(distance(position, rover.state().position) == 0);
    CHECK_FALSE(rover.apply({CommandType::start}).ok);
    CHECK_FALSE(rover.apply({CommandType::return_home}).ok);
    REQUIRE(rover.apply({CommandType::resume}).ok);
    advance(rover, 50);
    CHECK(distance(position, rover.state().position) > 0);
}

TEST_CASE("emergency stop latches and cannot be bypassed by another mission command") {
    Rover rover;
    mission(rover);
    advance(rover, 100);
    REQUIRE(rover.apply({CommandType::emergency_stop}).ok);
    REQUIRE(rover.apply({CommandType::emergency_stop}).ok);
    const auto position = rover.state().position;
    CHECK(rover.state().velocity_mps == 0);
    CHECK_FALSE(rover.apply({CommandType::start}).ok);
    CHECK_FALSE(rover.apply({CommandType::return_home}).ok);
    CHECK_FALSE(rover.apply({CommandType::set_waypoint, 0, {2, 2}}).ok);
    CHECK(rover.apply({CommandType::set_velocity, 2.0}).ok);
    advance(rover, 100);
    CHECK(distance(position, rover.state().position) == 0);
    REQUIRE(rover.apply({CommandType::resume}).ok);
    CHECK(rover.state().mode == Mode::navigating);
}

TEST_CASE("emergency stop on a paused return-home mission preserves both states") {
    Rover rover;
    mission(rover);
    advance(rover, 200);
    REQUIRE(rover.apply({CommandType::return_home}).ok);
    REQUIRE(rover.apply({CommandType::pause}).ok);
    REQUIRE(rover.apply({CommandType::emergency_stop}).ok);
    REQUIRE(rover.apply({CommandType::resume}).ok);
    CHECK(rover.state().mode == Mode::paused);
    REQUIRE(rover.apply({CommandType::resume}).ok);
    CHECK(rover.state().mode == Mode::returning_home);
    advance(rover, 2000);
    CHECK(distance(rover.state().home, rover.state().position) <= 0.15);
    CHECK(rover.state().mode == Mode::arrived);
}

TEST_CASE("emergency stop while idle releases to idle without inventing a mission") {
    Rover rover;
    REQUIRE(rover.apply({CommandType::emergency_stop}).ok);
    REQUIRE(rover.apply({CommandType::resume}).ok);
    CHECK(rover.state().mode == Mode::idle);
}

TEST_CASE("invalid numbers are rejected without mutating state") {
    Rover rover;
    for (double value : {-1.0, 3.1, std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()}) {
        CHECK_FALSE(rover.apply({CommandType::set_velocity, value}).ok);
        CHECK(rover.state().target_velocity_mps == 1);
    }
    CHECK_FALSE(rover.apply({CommandType::set_waypoint, 0, {501, 0}}).ok);
    CHECK_FALSE(
        rover.apply({CommandType::set_waypoint, 0, {0, std::numeric_limits<double>::quiet_NaN()}})
            .ok);
    CHECK_FALSE(rover.state().waypoint);
    CHECK_THROWS_AS(rover.step(0ms), std::invalid_argument);
    CHECK_THROWS_AS(rover.step(101ms), std::invalid_argument);
}

TEST_CASE("zero target velocity brakes and holds without pausing") {
    Rover rover;
    mission(rover);
    advance(rover, 100);
    REQUIRE(rover.apply({CommandType::set_velocity, 0}).ok);
    advance(rover, 150);
    CHECK(rover.state().velocity_mps == 0);
    CHECK(rover.state().mode == Mode::navigating);
    CHECK_FALSE(rover.state().warnings.empty());
}

TEST_CASE("battery drain produces warnings and prevents motion when depleted") {
    DynamicsConfig config;
    config.initial_battery_percent = 0.01;
    config.idle_drain_percent_per_s = 1;
    Rover rover(config);
    mission(rover);
    advance(rover, 20);
    CHECK(rover.state().battery_percent == 0);
    CHECK(rover.state().velocity_mps == 0);
    CHECK(rover.state().mode == Mode::depleted);
    CHECK_FALSE(rover.state().errors.empty());
    CHECK_FALSE(rover.apply({CommandType::start}).ok);
    REQUIRE(rover.apply({CommandType::emergency_stop}).ok);
    advance(rover, 20);
    CHECK(rover.state().mode == Mode::emergency_stopped);
    CHECK_FALSE(rover.apply({CommandType::resume}).ok);
}

TEST_CASE("configuration rejects invalid physical parameters") {
    DynamicsConfig config;
    config.acceleration_mps2 = 0;
    CHECK_THROWS_AS(Rover{config}, std::invalid_argument);
    config.acceleration_mps2 = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(Rover{config}, std::invalid_argument);
}
