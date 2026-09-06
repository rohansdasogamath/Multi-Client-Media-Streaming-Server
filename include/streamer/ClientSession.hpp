#pragma once

#include "streamer/FileManager.hpp"
#include "streamer/Socket.hpp"
#include "streamer/Types.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace streamer {

class ClientSession {
public:
    ClientSession(uint64_t session_id,
                  Socket socket,
                  std::string client_ip,
                  uint16_t client_port,
                  const FileManager& file_manager,
                  size_t chunk_size);
    ~ClientSession();

    // Run the session communication loop (executed inside worker thread)
    void run();

    // Signal the session to shut down and unblock socket calls
    void stop();

    bool is_active() const noexcept { return is_active_; }
    uint64_t session_id() const noexcept { return session_id_; }
    const std::string& client_ip() const noexcept { return client_ip_; }
    uint16_t client_port() const noexcept { return client_port_; }

private:
    void handle_list();
    void handle_get(const std::string& filename);

    uint64_t session_id_{0};
    Socket socket_;
    std::string client_ip_;
    uint16_t client_port_{0};
    const FileManager& file_manager_;
    size_t chunk_size_{DEFAULT_CHUNK_SIZE};
    std::atomic<bool> is_active_{false};
};

} // namespace streamer
