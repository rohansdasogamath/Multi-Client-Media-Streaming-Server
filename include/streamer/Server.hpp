#pragma once

#include "streamer/ClientSession.hpp"
#include "streamer/FileManager.hpp"
#include "streamer/Socket.hpp"
#include "streamer/Types.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace streamer {

class Server {
public:
    explicit Server(const ServerConfig& config);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Start server: creates socket, binds, listens, and runs accept loop
    bool start();

    // Gracefully stop the server, disconnect clients, join worker threads
    void stop();

    bool is_running() const noexcept { return is_running_; }
    const ServerConfig& get_config() const noexcept { return config_; }

    // Static pointer for signal handling
    static void handle_signal(int signum);

private:
    struct SessionWorker {
        std::shared_ptr<ClientSession> session;
        std::thread worker_thread;
    };

    void accept_loop();
    void cleanup_finished_sessions();
    void install_signal_handlers();

    ServerConfig config_;
    FileManager file_manager_;
    Socket server_socket_;
    std::atomic<bool> is_running_{false};
    std::atomic<uint64_t> next_session_id_{1};

    std::mutex workers_mutex_;
    std::vector<SessionWorker> workers_;

    static std::atomic<Server*> running_instance_;
};

} // namespace streamer
