#pragma once
#include "roverops/protocol.hpp"
#include <atomic>
#include <memory>

namespace httplib {
class Server;
}
namespace roverops {
class ApiServer {
  public:
    ApiServer(Simulation& simulation, AppConfig config);
    ~ApiServer();
    ApiServer(const ApiServer&) = delete;
    ApiServer& operator=(const ApiServer&) = delete;
    bool bind();
    bool listen();
    void stop();

  private:
    Simulation& simulation_; // Borrowed; main constructs it first and destroys it last.
    AppConfig config_;
    std::atomic<int> websocket_clients_{0};
    std::atomic<bool> stopping_{false};
    // Exclusive heap ownership hides the large third-party header from other translation units.
    std::unique_ptr<httplib::Server> server_;
};
} // namespace roverops
