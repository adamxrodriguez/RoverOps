#include "roverops/api_server.hpp"
#include <algorithm>
#include <httplib.h>
#include <iostream>
#include <mutex>

namespace roverops {
namespace {
void respond(httplib::Response& response, int status, const nlohmann::json& body) {
    response.status = status;
    response.set_header("Cache-Control", "no-store");
    response.set_content(body.dump(), "application/json");
}
void failure(httplib::Response& response, int status, const std::string& code,
             const std::string& message) {
    respond(response, status, {{"ok", false}, {"error", {{"code", code}, {"message", message}}}});
}
} // namespace

ApiServer::ApiServer(Simulation& simulation, AppConfig config)
    : simulation_(simulation), config_(std::move(config)),
      server_(std::make_unique<httplib::Server>()) {
    config_.validate();
    server_->set_payload_max_length(4096);
    server_->set_read_timeout(2);
    server_->set_write_timeout(1);
    server_->set_websocket_ping_interval(5);
    server_->set_websocket_max_missed_pongs(2);
    server_->new_task_queue = [] { return new httplib::ThreadPool(12, 16, 32); };
    // cpp-httplib takes ownership of the TaskQueue returned by this factory.
    server_->set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
        if (stopping_) {
            failure(res, 503, "shutting_down", "Simulator is shutting down");
            return httplib::Server::HandlerResponse::Handled;
        }
        const auto origin = req.get_header_value("Origin");
        if (!origin.empty() &&
            std::find(config_.allowed_origins.begin(), config_.allowed_origins.end(), origin) ==
                config_.allowed_origins.end()) {
            failure(res, 403, "origin_denied", "Browser origin is not in server.allowed_origins");
            return httplib::Server::HandlerResponse::Handled;
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });
    server_->Get("/api/health", [](const auto&, auto& res) {
        respond(res, 200, {{"status", "ok"}, {"service", "roverops"}, {"schema_version", 1}});
    });
    server_->Get("/api/config", [this](const auto&, auto& res) {
        respond(res, 200,
                {{"max_velocity_mps", config_.dynamics.max_velocity_mps},
                 {"world_limit_m", config_.dynamics.world_limit_m},
                 {"arrival_tolerance_m", config_.dynamics.arrival_tolerance_m},
                 {"tick_hz", config_.tick_hz},
                 {"telemetry_hz", config_.telemetry_hz}});
    });
    server_->Get("/api/state", [this](const auto&, auto& res) {
        respond(res, 200, telemetry_json(simulation_.snapshot()));
    });
    server_->Post("/api/commands", [this](const auto& req, auto& res) {
        const auto content_type = req.get_header_value("Content-Type");
        if (content_type.substr(0, content_type.find(';')) != "application/json") {
            failure(res, 415, "unsupported_media_type", "Use Content-Type: application/json");
            return;
        }
        try {
            const auto command = parse_command(nlohmann::json::parse(req.body));
            const auto result = simulation_.command(command);
            if (!result.ok) {
                failure(res, result.code == "invalid_value" ? 422 : 409, result.code,
                        result.message);
                return;
            }
            respond(res, 200,
                    {{"ok", true},
                     {"message", result.message},
                     {"state", telemetry_json(simulation_.snapshot())}});
        } catch (const nlohmann::json::exception& error) {
            failure(res, 400, "invalid_json", error.what());
        } catch (const std::invalid_argument& error) {
            failure(res, 400, "invalid_command", error.what());
        }
    });
    server_->WebSocket("/ws/telemetry", [this](const auto&, httplib::ws::WebSocket& socket) {
        // Counter guard restores capacity on every exit, including an exception or dropped peer.
        struct ClientLease {
            std::atomic<int>& count;
            ~ClientLease() { --count; }
        } lease{websocket_clients_};
        const int clients = ++websocket_clients_;
        if (clients > config_.max_websocket_clients) {
            socket.close(httplib::ws::CloseStatus::PolicyViolation,
                         "Telemetry client limit reached");
            return;
        }
        const auto interval = std::chrono::milliseconds(1000 / config_.telemetry_hz);
        socket.set_read_timeout(interval);
        auto next = std::chrono::steady_clock::now();
        std::string incoming;
        while (!stopping_ && socket.is_open()) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= next) {
                // Snapshot and serialization finish before a potentially slow socket write.
                if (!socket.send(telemetry_json(simulation_.snapshot()).dump()))
                    break;
                next = now + interval; // Latest state only; no growing telemetry queue.
            }
            const auto result = socket.read(incoming); // Also processes ping/pong/close frames.
            if (result == httplib::ws::Fail)
                break;
            if (result == httplib::ws::Text || result == httplib::ws::Binary) {
                socket.close(httplib::ws::CloseStatus::PolicyViolation, "Use REST for commands");
                break;
            }
        }
    });
    server_->set_error_handler([](const auto&, auto& res) {
        if (res.body.empty())
            failure(res, res.status, "http_error",
                    "Request rejected; check route, method, and payload size");
    });
    server_->set_exception_handler([](const auto&, auto& res, std::exception_ptr) {
        failure(res, 500, "internal_error", "Unexpected server error; inspect simulator logs");
    });
    server_->set_logger([](const auto& req, const auto& res) {
        static std::mutex log_mutex;
        const std::lock_guard lock(log_mutex);
        std::clog << "[http] " << req.method << ' ' << req.path << " status=" << res.status << '\n';
    });
}

// Defined where httplib::Server is complete, so unique_ptr can safely invoke its destructor.
ApiServer::~ApiServer() { stop(); }
bool ApiServer::bind() { return server_->bind_to_port(config_.host, config_.port); }
bool ApiServer::listen() { return server_->listen_after_bind(); }
void ApiServer::stop() {
    stopping_ = true;
    server_->stop();
}
} // namespace roverops
