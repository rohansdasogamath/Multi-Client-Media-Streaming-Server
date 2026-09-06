#include "streamer/Server.hpp"
#include "streamer/Logger.hpp"

#include <csignal>
#include <iostream>

namespace streamer {

std::atomic<Server*> Server::running_instance_{nullptr};

Server::Server(const ServerConfig& config)
    : config_(config),
      file_manager_(config.media_dir),
      is_running_(false) {}

Server::~Server() {
    stop();
}

void Server::handle_signal(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        LOG_INFO("Received termination signal (", signum, "). Initiating shutdown...");
        Server* s = running_instance_.load();
        if (s) {
            s->stop();
        }
    }
}

void Server::install_signal_handlers() {
    // Ignore SIGPIPE so unexpected client disconnects don't terminate the server process
    struct sigaction sa_pipe{};
    sa_pipe.sa_handler = SIG_IGN;
    ::sigemptyset(&sa_pipe.sa_mask);
    ::sigaction(SIGPIPE, &sa_pipe, nullptr);

    // Register SIGINT and SIGTERM handlers for graceful shutdown
    struct sigaction sa_term{};
    sa_term.sa_handler = &Server::handle_signal;
    ::sigemptyset(&sa_term.sa_mask);
    ::sigaction(SIGINT, &sa_term, nullptr);
    ::sigaction(SIGTERM, &sa_term, nullptr);
}

bool Server::start() {
    if (!file_manager_.init()) {
        LOG_ERROR("Failed to initialize file manager for root: ", config_.media_dir);
        return false;
    }

    running_instance_.store(this);
    install_signal_handlers();

    server_socket_ = Socket::create_tcp();
    if (!server_socket_.is_valid()) {
        LOG_ERROR("Failed to create server TCP socket");
        return false;
    }

    if (!server_socket_.set_reuse_address(true)) {
        LOG_WARN("Failed to set SO_REUSEADDR on server socket");
    }

    if (!server_socket_.bind(config_.host, config_.port)) {
        LOG_ERROR("Failed to bind socket to ", config_.host, ":", config_.port);
        server_socket_.close();
        return false;
    }

    if (!server_socket_.listen(config_.backlog)) {
        LOG_ERROR("Failed to listen on server socket with backlog ", config_.backlog);
        server_socket_.close();
        return false;
    }

    is_running_ = true;

    LOG_INFO("=================================================");
    LOG_INFO(" Multi-Client Media Streaming Server Started");
    LOG_INFO(" Host Address:  ", config_.host);
    LOG_INFO(" Port:          ", config_.port);
    LOG_INFO(" Media Root:    ", file_manager_.get_root_path().string());
    LOG_INFO(" Chunk Size:    ", config_.chunk_size, " bytes (", config_.chunk_size / 1024, " KB)");
    LOG_INFO(" Socket Timeout:", config_.socket_timeout_sec, "s");
    LOG_INFO("=================================================");

    accept_loop();
    return true;
}

void Server::accept_loop() {
    while (is_running_) {
        std::string client_ip;
        uint16_t client_port = 0;

        Socket client_sock = server_socket_.accept(client_ip, client_port);
        if (!client_sock.is_valid()) {
            if (!is_running_) {
                // Server was stopped intentionally
                break;
            }
            continue;
        }

        // Apply timeout options to the accepted client socket
        client_sock.set_timeouts(config_.socket_timeout_sec, config_.socket_timeout_sec);

        uint64_t sid = next_session_id_++;
        auto session = std::make_shared<ClientSession>(
            sid,
            std::move(client_sock),
            client_ip,
            client_port,
            file_manager_,
            config_.chunk_size
        );

        cleanup_finished_sessions();

        std::lock_guard<std::mutex> lock(workers_mutex_);
        workers_.emplace_back(SessionWorker{
            session,
            std::thread([session]() {
                session->run();
            })
        });

        LOG_INFO("Active client sessions: ", workers_.size());
    }
}

void Server::cleanup_finished_sessions() {
    std::lock_guard<std::mutex> lock(workers_mutex_);
    for (auto it = workers_.begin(); it != workers_.end(); ) {
        if (!it->session->is_active()) {
            if (it->worker_thread.joinable()) {
                it->worker_thread.join();
            }
            it = workers_.erase(it);
        } else {
            ++it;
        }
    }
}

void Server::stop() {
    bool expected = true;
    if (!is_running_.compare_exchange_strong(expected, false)) {
        return;
    }

    LOG_INFO("Stopping server listening socket...");
    server_socket_.shutdown(SHUT_RDWR);
    server_socket_.close();

    std::lock_guard<std::mutex> lock(workers_mutex_);
    LOG_INFO("Stopping ", workers_.size(), " active client sessions...");

    for (auto& worker : workers_) {
        if (worker.session) {
            worker.session->stop();
        }
    }

    for (auto& worker : workers_) {
        if (worker.worker_thread.joinable()) {
            worker.worker_thread.join();
        }
    }
    workers_.clear();

    running_instance_.store(nullptr);
    LOG_INFO("Server shutdown completed cleanly.");
}

} // namespace streamer
