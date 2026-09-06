#include "streamer/Socket.hpp"
#include "streamer/Logger.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace streamer {

Socket::Socket() : fd_(-1) {}

Socket::Socket(int fd) : fd_(fd) {}

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

Socket Socket::create_tcp() {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("socket() creation failed: ", std::strerror(errno));
        return Socket(-1);
    }
    return Socket(fd);
}

bool Socket::set_reuse_address(bool reuse) {
    if (!is_valid()) return false;
    int opt = reuse ? 1 : 0;
    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_WARN("setsockopt(SO_REUSEADDR) failed: ", std::strerror(errno));
        return false;
    }
    return true;
}

bool Socket::bind(const std::string& host, uint16_t port) {
    if (!is_valid()) return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            LOG_ERROR("Invalid IPv4 address: ", host);
            return false;
        }
    }

    if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR("bind() on ", host, ":", port, " failed: ", std::strerror(errno));
        return false;
    }

    return true;
}

bool Socket::listen(int backlog) {
    if (!is_valid()) return false;

    if (::listen(fd_, backlog) < 0) {
        LOG_ERROR("listen() failed: ", std::strerror(errno));
        return false;
    }
    return true;
}

Socket Socket::accept(std::string& client_ip, uint16_t& client_port) {
    if (!is_valid()) return Socket(-1);

    struct sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);

    int client_fd = ::accept(fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
    if (client_fd < 0) {
        // Only log error if not interrupted or shut down
        if (errno != EINTR && errno != EBADF && errno != EINVAL) {
            LOG_ERROR("accept() failed: ", std::strerror(errno));
        }
        return Socket(-1);
    }

    char ip_buffer[INET_ADDRSTRLEN];
    if (::inet_ntop(AF_INET, &client_addr.sin_addr, ip_buffer, sizeof(ip_buffer)) != nullptr) {
        client_ip = ip_buffer;
    } else {
        client_ip = "unknown";
    }
    client_port = ntohs(client_addr.sin_port);

    return Socket(client_fd);
}

bool Socket::connect(const std::string& host, uint16_t port) {
    if (!is_valid()) return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        LOG_ERROR("inet_pton failed for host: ", host);
        return false;
    }

    if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR("connect() to ", host, ":", port, " failed: ", std::strerror(errno));
        return false;
    }

    return true;
}

bool Socket::set_timeouts(int rcv_sec, int snd_sec) {
    if (!is_valid()) return false;

    struct timeval rcv_tv{};
    rcv_tv.tv_sec = rcv_sec;
    rcv_tv.tv_usec = 0;
    if (::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &rcv_tv, sizeof(rcv_tv)) < 0) {
        LOG_WARN("setsockopt(SO_RCVTIMEO) failed: ", std::strerror(errno));
        return false;
    }

    struct timeval snd_tv{};
    snd_tv.tv_sec = snd_sec;
    snd_tv.tv_usec = 0;
    if (::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &snd_tv, sizeof(snd_tv)) < 0) {
        LOG_WARN("setsockopt(SO_SNDTIMEO) failed: ", std::strerror(errno));
        return false;
    }

    return true;
}

bool Socket::send_all(const void* data, size_t length, int* out_bytes_sent) {
    if (!is_valid()) return false;

    const char* ptr = static_cast<const char*>(data);
    size_t total_sent = 0;

    while (total_sent < length) {
        // Use MSG_NOSIGNAL to prevent SIGPIPE signal if peer has closed the connection
        ssize_t bytes = ::send(fd_, ptr + total_sent, length - total_sent, MSG_NOSIGNAL);
        if (bytes < 0) {
            if (errno == EINTR) {
                continue; // interrupted by signal, retry
            }
            if (errno == EPIPE || errno == ECONNRESET) {
                LOG_DEBUG("send_all: Client closed connection abruptly (", std::strerror(errno), ")");
            } else {
                LOG_WARN("send_all failed: ", std::strerror(errno));
            }
            if (out_bytes_sent) *out_bytes_sent = static_cast<int>(total_sent);
            return false;
        }
        if (bytes == 0) {
            // Connection closed
            if (out_bytes_sent) *out_bytes_sent = static_cast<int>(total_sent);
            return false;
        }
        total_sent += static_cast<size_t>(bytes);
    }

    if (out_bytes_sent) *out_bytes_sent = static_cast<int>(total_sent);
    return true;
}

bool Socket::send_string(const std::string& str) {
    return send_all(str.data(), str.size());
}

int Socket::recv_all(void* data, size_t length) {
    if (!is_valid()) return -1;

    char* ptr = static_cast<char*>(data);
    size_t total_recv = 0;

    while (total_recv < length) {
        ssize_t bytes = ::recv(fd_, ptr + total_recv, length - total_recv, 0);
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_WARN("recv_all: Socket timed out");
            } else if (errno != ECONNRESET) {
                LOG_WARN("recv_all failed: ", std::strerror(errno));
            }
            return -1;
        }
        if (bytes == 0) {
            // Peer closed socket cleanly (EOF)
            return 0;
        }
        total_recv += static_cast<size_t>(bytes);
    }

    return 1;
}

bool Socket::recv_line(std::string& line, size_t max_length) {
    line.clear();
    if (!is_valid()) return false;

    char ch;
    while (line.size() < max_length) {
        ssize_t bytes = ::recv(fd_, &ch, 1, 0);
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (bytes == 0) {
            // End of stream
            return !line.empty();
        }

        if (ch == '\n') {
            // Strip trailing \r if present
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return true;
        }
        line.push_back(ch);
    }

    // Line exceeded maximum length
    LOG_WARN("recv_line exceeded maximum allowed length (", max_length, " bytes)");
    return false;
}

void Socket::shutdown(int how) {
    if (fd_ >= 0) {
        ::shutdown(fd_, how);
    }
}

void Socket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool Socket::get_peer_info(std::string& ip, uint16_t& port) const {
    if (!is_valid()) return false;

    struct sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (::getpeername(fd_, reinterpret_cast<struct sockaddr*>(&addr), &len) < 0) {
        return false;
    }

    char buffer[INET_ADDRSTRLEN];
    if (::inet_ntop(AF_INET, &addr.sin_addr, buffer, sizeof(buffer)) != nullptr) {
        ip = buffer;
    } else {
        ip = "unknown";
    }
    port = ntohs(addr.sin_port);
    return true;
}

} // namespace streamer
