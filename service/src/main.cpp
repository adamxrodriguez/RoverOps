#include "roverops/api_server.hpp"
#include <atomic>
#include <charconv>
#include <csignal>
#include <iostream>
#include <stdexcept>

namespace {
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool> shutdown_requested{false};
void signal_handler(int) {
    shutdown_requested.store(true, std::memory_order_relaxed);
} // No locks, allocations, or logging in a signal handler.
} // namespace

int main(int argc, char** argv) {
    try {
        std::filesystem::path config_path{"config/rover.json"};
        int port_override = 0;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg{argv[i]};
            if (arg == "--help") {
                std::cout << "Usage: roverops [--config config/rover.json] [--port 8080]\n";
                return 0;
            }
            if (arg == "--config" && i + 1 < argc)
                config_path = argv[++i];
            else if (arg == "--port" && i + 1 < argc) {
                const std::string_view value{argv[++i]};
                const auto [end, error] =
                    std::from_chars(value.data(), value.data() + value.size(), port_override);
                if (error != std::errc{} || end != value.data() + value.size() ||
                    port_override < 1 || port_override > 65535)
                    throw std::invalid_argument("--port must be an integer between 1 and 65535");
            } else
                throw std::invalid_argument("Unknown or incomplete argument: " + std::string(arg));
        }
        auto config = roverops::load_config(config_path);
        if (port_override)
            config.port = port_override;
        config.validate();
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        std::signal(SIGPIPE, SIG_IGN);

        roverops::Simulation simulation(config.dynamics, config.tick_hz);
        roverops::ApiServer api(simulation, config);
        if (!api.bind())
            throw std::runtime_error("Cannot bind " + config.host + ":" +
                                     std::to_string(config.port) + "; port may be in use");
        std::jthread shutdown_watcher([&api](std::stop_token stop) {
            while (!stop.stop_requested()) {
                if (shutdown_requested) {
                    api.stop();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });
        std::cout << "RoverOps listening at http://" << config.host << ':' << config.port
                  << " | simulation=" << config.tick_hz << "Hz telemetry=" << config.telemetry_hz
                  << "Hz\n"
                  << std::flush;
        const bool success =
            api.listen(); // Returns after HTTP workers and WebSocket handlers stop.
        shutdown_watcher.request_stop();
        std::cout << "RoverOps shutdown complete\n";
        return success || shutdown_requested ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "[fatal] " << error.what() << '\n';
        return 1;
    }
}
