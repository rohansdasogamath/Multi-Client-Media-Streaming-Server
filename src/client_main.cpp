#include "streamer/Socket.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void print_usage(const char* prog) {
    std::cout << "Multi-Client Media Streaming Client (C++17)\n\n"
              << "Usage:\n"
              << "  " << prog << " <server_ip> <port> <filename> [output_dir]\n"
              << "  " << prog << " [options]\n\n"
              << "Arguments:\n"
              << "  server_ip                 Server IPv4 address (e.g., 127.0.0.1)\n"
              << "  port                      Server port (e.g., 8080)\n"
              << "  filename                  Target media file to stream (e.g., sample.mp4)\n"
              << "  output_dir                Optional local destination directory (default: downloads)\n\n"
              << "Options:\n"
              << "  -s, --server <host>       Server host IPv4 address\n"
              << "  -p, --port <port>         Server port\n"
              << "  -g, --get <filename>      File to download\n"
              << "  -o, --output-dir <dir>    Output directory (default: downloads)\n"
              << "  -l, --list                List media files available on server\n"
              << "  -h, --help                Show this help message and exit\n\n"
              << "Example:\n"
              << "  " << prog << " 127.0.0.1 8080 sample_video_720p.mp4\n";
}

bool do_list(streamer::Socket& sock) {
    if (!sock.send_string("LIST\n")) {
        std::cerr << "Failed to send LIST command.\n";
        return false;
    }

    std::string header;
    if (!sock.recv_line(header)) {
        std::cerr << "Failed to receive response from server.\n";
        return false;
    }

    if (header.rfind("OK", 0) != 0) {
        std::cerr << "Server returned error: " << header << "\n";
        return false;
    }

    std::istringstream iss(header.substr(3));
    size_t count = 0;
    iss >> count;

    std::cout << "\n--- Available Media Files (" << count << ") ---\n";
    std::cout << std::left << std::setw(35) << "Filename"
              << std::right << std::setw(15) << "Size (Bytes)"
              << std::setw(15) << "Size (MB)" << "\n";
    std::cout << std::string(65, '-') << "\n";

    for (size_t i = 0; i < count; ++i) {
        std::string line;
        if (!sock.recv_line(line)) {
            std::cerr << "Premature end of file list.\n";
            return false;
        }

        std::istringstream line_iss(line);
        std::string name;
        uint64_t size = 0;
        line_iss >> name >> size;

        double mb = static_cast<double>(size) / (1024.0 * 1024.0);
        std::cout << std::left << std::setw(35) << name
                  << std::right << std::setw(15) << size
                  << std::right << std::setw(15) << std::fixed << std::setprecision(2) << mb << " MB\n";
    }
    std::cout << std::string(65, '-') << "\n\n";

    sock.send_string("QUIT\n");
    return true;
}

bool do_get(streamer::Socket& sock, const std::string& filename, const std::string& output_dir) {
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
        std::cerr << "Error: Failed to create output directory: " << ec.message() << "\n";
        return false;
    }

    std::filesystem::path out_path = std::filesystem::path(output_dir) / filename;

    // Send GET <filename>\n command to server
    std::string request = "GET " + filename + "\n";
    if (!sock.send_string(request)) {
        std::cerr << "Error: Failed to send GET command to server.\n";
        return false;
    }

    // Read and validate server response line
    std::string header;
    if (!sock.recv_line(header)) {
        std::cerr << "Error: Failed to receive response from server (server disconnected or timed out).\n";
        return false;
    }

    if (header.rfind("OK", 0) != 0) {
        std::cerr << "Server returned error: " << header << "\n";
        return false;
    }

    std::istringstream iss(header.substr(3));
    uint64_t file_size = 0;
    if (!(iss >> file_size)) {
        std::cerr << "Error: Malformed header received from server: " << header << "\n";
        return false;
    }

    std::cout << "Streaming '" << filename << "' (" << file_size << " bytes)...\n";

    std::ofstream outfile(out_path, std::ios::binary | std::ios::out);
    if (!outfile.is_open()) {
        std::cerr << "Error: Failed to create local destination file: " << out_path.string() << "\n";
        return false;
    }

    constexpr size_t CHUNK_SIZE = 64 * 1024; // 64 KB chunk size
    std::vector<char> buffer(CHUNK_SIZE);
    uint64_t total_received = 0;
    auto start_time = std::chrono::steady_clock::now();

    // Loop receiving file chunks and handling partial receives
    while (total_received < file_size) {
        size_t to_recv = std::min(static_cast<uint64_t>(CHUNK_SIZE), file_size - total_received);
        int res = sock.recv_all(buffer.data(), to_recv);
        if (res == 0) {
            std::cerr << "\nError: Server disconnected prematurely after receiving "
                      << total_received << " / " << file_size << " bytes.\n";
            return false;
        } else if (res < 0) {
            std::cerr << "\nError: Network receive failure during file streaming.\n";
            return false;
        }

        outfile.write(buffer.data(), static_cast<std::streamsize>(to_recv));
        total_received += to_recv;

        // Progress reporting
        int percent = (file_size > 0) ? static_cast<int>((total_received * 100) / file_size) : 100;
        std::cout << "\rProgress: [" << std::setw(3) << percent << "%] "
                  << total_received << " / " << file_size << " bytes" << std::flush;
    }

    auto end_time = std::chrono::steady_clock::now();
    double duration = std::chrono::duration<double>(end_time - start_time).count();
    double mbps = (duration > 0.0) ? (static_cast<double>(total_received) / (1024.0 * 1024.0)) / duration : 0.0;

    std::cout << "\n\n================ Transfer Statistics ================\n";
    std::cout << " Status:        SUCCESS\n";
    std::cout << " File:          " << filename << "\n";
    std::cout << " Bytes Saved:   " << total_received << " bytes\n";
    std::cout << " Output Path:   " << std::filesystem::canonical(out_path).string() << "\n";
    std::cout << " Elapsed Time:  " << std::fixed << std::setprecision(2) << duration << " seconds\n";
    std::cout << " Speed:         " << std::fixed << std::setprecision(2) << mbps << " MB/s\n";
    std::cout << "=====================================================\n";

    sock.send_string("QUIT\n");
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    uint16_t port = 8080;
    std::string get_file;
    std::string output_dir = "downloads";
    bool list_mode = false;

    if (argc >= 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        print_usage(argv[0]);
        return 0;
    }

    // Check for --list or -l anywhere in arguments
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-l" || std::string(argv[i]) == "--list") {
            list_mode = true;
            break;
        }
    }

    if (list_mode) {
        // Support: ./media_client 127.0.0.1 8080 --list or --server / --port flags
        bool host_set = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-l" || arg == "--list") continue;
            if (arg == "-s" || arg == "--server") {
                if (i + 1 < argc) host = argv[++i];
            } else if (arg == "-p" || arg == "--port") {
                if (i + 1 < argc) port = static_cast<uint16_t>(std::atoi(argv[++i]));
            } else if (arg[0] != '-') {
                if (!host_set) {
                    host = arg;
                    host_set = true;
                } else {
                    port = static_cast<uint16_t>(std::atoi(arg.c_str()));
                }
            }
        }
    } else if (argc >= 4 && argv[1][0] != '-') {
        // Positional syntax: ./media_client <server_ip> <port> <filename> [output_dir]
        host = argv[1];
        port = static_cast<uint16_t>(std::atoi(argv[2]));
        get_file = argv[3];
        if (argc >= 5) {
            output_dir = argv[4];
        }
    } else {
        // Flag-based syntax: -s, -p, -g, -o
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-s" || arg == "--server") {
                if (i + 1 < argc) host = argv[++i];
            } else if (arg == "-p" || arg == "--port") {
                if (i + 1 < argc) port = static_cast<uint16_t>(std::atoi(argv[++i]));
            } else if (arg == "-g" || arg == "--get") {
                if (i + 1 < argc) get_file = argv[++i];
            } else if (arg == "-o" || arg == "--output-dir") {
                if (i + 1 < argc) output_dir = argv[++i];
            } else {
                std::cerr << "Error: Unknown option '" << arg << "'\n\n";
                print_usage(argv[0]);
                return 1;
            }
        }
    }

    if (!list_mode && get_file.empty()) {
        std::cerr << "Error: Please specify target filename or use --list.\n\n";
        print_usage(argv[0]);
        return 1;
    }

    if (port == 0) {
        std::cerr << "Error: Invalid port number.\n";
        return 1;
    }

    streamer::Socket sock = streamer::Socket::create_tcp();
    if (!sock.is_valid()) {
        std::cerr << "Error: Failed to create socket.\n";
        return 1;
    }

    std::cout << "Connecting to " << host << ":" << port << "...\n";
    if (!sock.connect(host, port)) {
        std::cerr << "Error: Could not connect to " << host << ":" << port << "\n";
        return 1;
    }
    std::cout << "Connected to server successfully!\n";

    bool ok = true;
    if (list_mode) {
        ok = do_list(sock);
    }
    if (ok && !get_file.empty()) {
        ok = do_get(sock, get_file, output_dir);
    }

    sock.close();
    return ok ? 0 : 1;
}
