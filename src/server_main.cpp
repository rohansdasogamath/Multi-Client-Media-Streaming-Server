#include "streamer/Logger.hpp"
#include "streamer/Server.hpp"
#include "streamer/Types.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n\n"
              << "Options:\n"
              << "  -p, --port <port>         Port number to listen on (default: 8080)\n"
              << "  -d, --media-dir <dir>     Directory containing media files (default: media)\n"
              << "  -c, --chunk-size <bytes>  Streaming chunk size in bytes (default: 65536)\n"
              << "  -b, --backlog <n>         TCP listen backlog size (default: 128)\n"
              << "  -l, --log-file <path>     Path to output log file (default: logs/server.log)\n"
              << "  --log-level <level>       Minimum log level: debug, info, warn, error (default: info)\n"
              << "  -h, --help                Show this help message and exit\n";
}

} // namespace

int main(int argc, char* argv[]) {
    streamer::ServerConfig config;
    std::string log_level_str = "info";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                config.port = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
                if (config.port == 0) {
                    std::cerr << "Error: Invalid port number: " << argv[i] << "\n";
                    return 1;
                }
            } else {
                std::cerr << "Error: --port requires an argument\n";
                return 1;
            }
        } else if (arg == "-d" || arg == "--media-dir") {
            if (i + 1 < argc) {
                config.media_dir = argv[++i];
            } else {
                std::cerr << "Error: --media-dir requires an argument\n";
                return 1;
            }
        } else if (arg == "-c" || arg == "--chunk-size") {
            if (i + 1 < argc) {
                config.chunk_size = std::strtoull(argv[++i], nullptr, 10);
                if (config.chunk_size == 0) {
                    std::cerr << "Error: Invalid chunk size: " << argv[i] << "\n";
                    return 1;
                }
            } else {
                std::cerr << "Error: --chunk-size requires an argument\n";
                return 1;
            }
        } else if (arg == "-b" || arg == "--backlog") {
            if (i + 1 < argc) {
                config.backlog = std::atoi(argv[++i]);
            } else {
                std::cerr << "Error: --backlog requires an argument\n";
                return 1;
            }
        } else if (arg == "-l" || arg == "--log-file") {
            if (i + 1 < argc) {
                config.log_file = argv[++i];
            } else {
                std::cerr << "Error: --log-file requires an argument\n";
                return 1;
            }
        } else if (arg == "--log-level") {
            if (i + 1 < argc) {
                log_level_str = argv[++i];
            } else {
                std::cerr << "Error: --log-level requires an argument\n";
                return 1;
            }
        } else {
            std::cerr << "Error: Unknown option '" << arg << "'\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    streamer::LogLevel level = streamer::LogLevel::INFO;
    if (log_level_str == "debug") level = streamer::LogLevel::DEBUG;
    else if (log_level_str == "info") level = streamer::LogLevel::INFO;
    else if (log_level_str == "warn") level = streamer::LogLevel::WARN;
    else if (log_level_str == "error") level = streamer::LogLevel::ERROR;

    streamer::Logger::instance().init(config.log_file, level, true);

    streamer::Server server(config);
    if (!server.start()) {
        std::cerr << "Failed to start server.\n";
        return 1;
    }

    return 0;
}
