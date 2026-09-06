#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/socket.h>

namespace streamer {

class Socket {
public:
    Socket();
    explicit Socket(int fd);
    ~Socket();

    // RAII Rule of Five: Non-copyable, movable
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    // Factory methods
    static Socket create_tcp();

    // Server socket operations
    bool set_reuse_address(bool reuse = true);
    bool bind(const std::string& host, uint16_t port);
    bool listen(int backlog = 128);
    Socket accept(std::string& client_ip, uint16_t& client_port);

    // Client connection
    bool connect(const std::string& host, uint16_t port);

    // Config
    bool set_timeouts(int rcv_sec, int snd_sec);

    // I/O operations
    // Returns true if all bytes were sent, false on disconnect/error
    bool send_all(const void* data, size_t length, int* out_bytes_sent = nullptr);
    bool send_string(const std::string& str);

    // Receives exactly length bytes into data buffer
    // Returns 1 if all received, 0 on client disconnect (EOF), -1 on error
    int recv_all(void* data, size_t length);

    // Receives a single newline-terminated line (strips \r and \n)
    // Returns true on success, false on EOF or error
    bool recv_line(std::string& line, size_t max_length = 4096);

    // Lifecycle
    void shutdown(int how = SHUT_RDWR);
    void close();

    bool is_valid() const noexcept { return fd_ >= 0; }
    int native_handle() const noexcept { return fd_; }

    // Query peer address
    bool get_peer_info(std::string& ip, uint16_t& port) const;

private:
    int fd_{-1};
};

} // namespace streamer
