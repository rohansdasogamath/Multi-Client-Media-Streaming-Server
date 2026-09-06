#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace streamer {

constexpr uint16_t DEFAULT_PORT = 8080;
constexpr size_t DEFAULT_CHUNK_SIZE = 64 * 1024; // 64 KB
constexpr const char* DEFAULT_MEDIA_DIR = "media";
constexpr const char* DEFAULT_LOG_FILE = "logs/server.log";
constexpr int DEFAULT_BACKLOG = 128;
constexpr int DEFAULT_SOCKET_TIMEOUT_SEC = 30;

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

struct ServerConfig {
    std::string host{"0.0.0.0"};
    uint16_t port{DEFAULT_PORT};
    std::string media_dir{DEFAULT_MEDIA_DIR};
    size_t chunk_size{DEFAULT_CHUNK_SIZE};
    int backlog{DEFAULT_BACKLOG};
    int socket_timeout_sec{DEFAULT_SOCKET_TIMEOUT_SEC};
    std::string log_file{DEFAULT_LOG_FILE};
};

struct MediaFileInfo {
    std::string filename;
    uint64_t size_bytes{0};
};

struct StreamStats {
    uint64_t bytes_sent{0};
    double elapsed_seconds{0.0};
    double throughput_mbps{0.0};
};

} // namespace streamer
