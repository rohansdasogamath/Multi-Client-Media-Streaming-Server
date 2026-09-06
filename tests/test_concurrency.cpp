#include "streamer/FileManager.hpp"
#include "streamer/Logger.hpp"
#include "streamer/Protocol.hpp"
#include "streamer/Server.hpp"
#include "streamer/Socket.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_passed = 0;
int g_failed = 0;

#define CHECK(cond, desc) \
    do { \
        if (cond) { \
            std::cout << "  [PASS] " << desc << "\n"; \
            g_passed++; \
        } else { \
            std::cerr << "  [FAIL] " << desc << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            g_failed++; \
        } \
    } while (0)

void generate_test_pattern_file(const std::filesystem::path& path, size_t size_bytes, char pattern_start) {
    std::ofstream ofs(path, std::ios::binary | std::ios::out);
    std::vector<char> buffer(64 * 1024);
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = static_cast<char>(static_cast<int>(pattern_start) + static_cast<int>(i % 26));
    }
    size_t remaining = size_bytes;
    while (remaining > 0) {
        size_t chunk = std::min(remaining, buffer.size());
        ofs.write(buffer.data(), static_cast<std::streamsize>(chunk));
        remaining -= chunk;
    }
}

bool compare_files(const std::filesystem::path& p1, const std::filesystem::path& p2) {
    std::error_code ec;
    if (std::filesystem::file_size(p1, ec) != std::filesystem::file_size(p2, ec)) {
        return false;
    }
    std::ifstream f1(p1, std::ios::binary);
    std::ifstream f2(p2, std::ios::binary);
    constexpr size_t BUF_SZ = 64 * 1024;
    std::vector<char> b1(BUF_SZ), b2(BUF_SZ);
    while (f1 && f2) {
        f1.read(b1.data(), BUF_SZ);
        f2.read(b2.data(), BUF_SZ);
        if (f1.gcount() != f2.gcount()) return false;
        if (std::memcmp(b1.data(), b2.data(), static_cast<size_t>(f1.gcount())) != 0) return false;
    }
    return true;
}

struct ClientTaskResult {
    int client_id{0};
    std::string filename;
    uint64_t bytes_received{0};
    double elapsed_sec{0.0};
    double mbps{0.0};
    bool success{false};
    bool is_slow_client{false};
};

} // namespace

int main() {
    std::cout << "==========================================================\n";
    std::cout << " Multi-Client Media Streaming Server: Concurrency Test\n";
    std::cout << "==========================================================\n";

    streamer::Logger::instance().init("", streamer::LogLevel::WARN, true);

    const std::string media_dir = "concurrency_test_media";
    const std::string output_dir = "concurrency_test_downloads";
    const uint16_t port = 8995;

    std::filesystem::create_directories(media_dir);
    std::filesystem::create_directories(output_dir);

    // 1. Generate test files
    std::cout << "1. Generating test media files...\n";
    const std::filesystem::path f_large = std::filesystem::path(media_dir) / "large_stream.bin";
    const std::filesystem::path f_medium = std::filesystem::path(media_dir) / "medium_video.mp4";
    const std::filesystem::path f_small = std::filesystem::path(media_dir) / "small_audio.bin";

    constexpr size_t SZ_LARGE  = 10 * 1024 * 1024; // 10 MB
    constexpr size_t SZ_MEDIUM = 4 * 1024 * 1024;  // 4 MB
    constexpr size_t SZ_SMALL  = 256 * 1024;       // 256 KB

    generate_test_pattern_file(f_large, SZ_LARGE, 'A');
    generate_test_pattern_file(f_medium, SZ_MEDIUM, 'M');
    generate_test_pattern_file(f_small, SZ_SMALL, 'S');

    std::cout << "   - large_stream.bin:  " << SZ_LARGE / (1024 * 1024) << " MB\n";
    std::cout << "   - medium_video.mp4:  " << SZ_MEDIUM / (1024 * 1024) << " MB\n";
    std::cout << "   - small_audio.bin:   " << SZ_SMALL / 1024 << " KB\n";

    // 2. Start server in dedicated thread
    std::cout << "2. Starting server on port " << port << "...\n";
    streamer::ServerConfig config;
    config.host = "127.0.0.1";
    config.port = port;
    config.media_dir = media_dir;
    config.chunk_size = 64 * 1024; // 64 KB chunks
    config.log_file = "";

    auto server = std::make_unique<streamer::Server>(config);
    std::thread server_thread([&server]() {
        server->start();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(server->is_running(), "Server is running and listening");

    // 3. Prepare 8 concurrent client tasks
    // Client assignments:
    // Clients 0, 1: large_stream.bin (10 MB each, testing same file concurrency)
    // Clients 2, 3: medium_video.mp4 (4 MB each, testing concurrent video)
    // Clients 4, 5: small_audio.bin  (256 KB each, normal speed)
    // Client 6:     small_audio.bin  (SLOW CLIENT: sleeps 15ms every 16KB)
    // Client 7:     PROBE CLIENT:    issues LIST command while other 7 clients are transferring
    constexpr int NUM_DOWNLOAD_CLIENTS = 7;
    std::vector<ClientTaskResult> results(NUM_DOWNLOAD_CLIENTS);

    std::mutex cv_mutex;
    std::condition_variable cv;
    bool ready_to_launch = false;
    std::atomic<int> clients_ready{0};

    std::vector<std::thread> client_threads;
    client_threads.reserve(NUM_DOWNLOAD_CLIENTS);

    std::cout << "3. Launching 7 concurrent download clients (including 1 slow client)...\n";

    for (int i = 0; i < NUM_DOWNLOAD_CLIENTS; ++i) {
        client_threads.emplace_back([i, &output_dir, &results,
                                     &cv_mutex, &cv, &ready_to_launch, &clients_ready]() {
            ClientTaskResult& res = results[static_cast<size_t>(i)];
            res.client_id = i + 1;

            if (i == 0 || i == 1) {
                res.filename = "large_stream.bin";
            } else if (i == 2 || i == 3) {
                res.filename = "medium_video.mp4";
            } else {
                res.filename = "small_audio.bin";
            }

            if (i == 6) {
                res.is_slow_client = true;
            }

            // Signal readiness and wait on barrier
            {
                std::unique_lock<std::mutex> lock(cv_mutex);
                clients_ready++;
                cv.wait(lock, [&ready_to_launch]() { return ready_to_launch; });
            }

            // Connect
            streamer::Socket sock = streamer::Socket::create_tcp();
            if (!sock.connect("127.0.0.1", port)) return;

            // Send request
            std::string req = "GET " + res.filename + "\n";
            if (!sock.send_string(req)) return;

            // Read response header
            std::string header;
            if (!sock.recv_line(header)) return;
            if (header.rfind("OK", 0) != 0) return;

            uint64_t file_size = std::stoull(header.substr(3));

            std::filesystem::path dl_path = std::filesystem::path(output_dir) /
                ("client_" + std::to_string(res.client_id) + "_" + res.filename);
            std::ofstream out(dl_path, std::ios::binary);

            auto start = std::chrono::steady_clock::now();
            constexpr size_t CHUNK = 32 * 1024;
            std::vector<char> buf(CHUNK);
            uint64_t received = 0;

            while (received < file_size) {
                size_t to_recv = std::min(static_cast<uint64_t>(CHUNK), file_size - received);
                int r = sock.recv_all(buf.data(), to_recv);
                if (r <= 0) break;

                out.write(buf.data(), static_cast<std::streamsize>(to_recv));
                received += to_recv;

                // If this is the slow client, simulate slow network consumer
                if (res.is_slow_client) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(15));
                }
            }

            auto end = std::chrono::steady_clock::now();
            res.bytes_received = received;
            res.elapsed_sec = std::chrono::duration<double>(end - start).count();
            res.mbps = (res.elapsed_sec > 0.0) ? (static_cast<double>(received) / (1024.0 * 1024.0)) / res.elapsed_sec : 0.0;
            res.success = (received == file_size);

            sock.send_string("QUIT\n");
            sock.close();
        });
    }

    // Wait until all 7 client threads are spawned and waiting at the barrier
    while (clients_ready.load() < NUM_DOWNLOAD_CLIENTS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Release barrier to start all downloads at the exact same instant
    auto wall_start = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(cv_mutex);
        ready_to_launch = true;
    }
    cv.notify_all();

    // 4. Test Server Responsiveness: Probe client sends LIST while downloads are active
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::cout << "4. Testing server responsiveness during heavy concurrent transfers...\n";

    double probe_latency_ms = 0.0;
    bool probe_success = false;
    {
        auto p_start = std::chrono::steady_clock::now();
        streamer::Socket probe_sock = streamer::Socket::create_tcp();
        if (probe_sock.connect("127.0.0.1", port)) {
            probe_sock.send_string("LIST\n");
            std::string line;
            if (probe_sock.recv_line(line) && line.rfind("OK", 0) == 0) {
                probe_success = true;
            }
            probe_sock.send_string("QUIT\n");
            probe_sock.close();
        }
        auto p_end = std::chrono::steady_clock::now();
        probe_latency_ms = std::chrono::duration<double, std::milli>(p_end - p_start).count();
    }

    CHECK(probe_success, "Server answered probe request while streaming concurrently");
    std::cout << "   - Probe round-trip latency during active concurrency: "
              << std::fixed << std::setprecision(2) << probe_latency_ms << " ms\n";
    CHECK(probe_latency_ms < 100.0, "Probe latency remained responsive (< 100 ms)");

    // 5. Wait for all client threads to finish
    for (auto& t : client_threads) {
        if (t.joinable()) t.join();
    }
    auto wall_end = std::chrono::steady_clock::now();
    double total_wall_time = std::chrono::duration<double>(wall_end - wall_start).count();

    std::cout << "\n5. Verifying per-client download results:\n";
    std::cout << std::left << std::setw(10) << "Client"
              << std::setw(20) << "File"
              << std::setw(15) << "Type"
              << std::right << std::setw(15) << "Bytes"
              << std::setw(15) << "Time (s)"
              << std::setw(15) << "Speed (MB/s)"
              << std::setw(12) << "Integrity" << "\n";
    std::cout << std::string(102, '-') << "\n";

    uint64_t total_bytes_all = 0;
    bool all_clients_succeeded = true;
    bool all_files_match = true;

    double fast_small_time = 0.0;
    double slow_small_time = 0.0;

    for (const auto& r : results) {
        total_bytes_all += r.bytes_received;
        if (!r.success) all_clients_succeeded = false;

        std::filesystem::path orig = std::filesystem::path(media_dir) / r.filename;
        std::filesystem::path dl = std::filesystem::path(output_dir) /
            ("client_" + std::to_string(r.client_id) + "_" + r.filename);

        bool identical = compare_files(orig, dl);
        if (!identical) all_files_match = false;

        if (r.client_id == 5) fast_small_time = r.elapsed_sec;
        if (r.is_slow_client) slow_small_time = r.elapsed_sec;

        std::cout << std::left << std::setw(10) << ("#" + std::to_string(r.client_id))
                  << std::setw(20) << r.filename
                  << std::setw(15) << (r.is_slow_client ? "SLOW CONSUMER" : "NORMAL")
                  << std::right << std::setw(15) << r.bytes_received
                  << std::setw(15) << std::fixed << std::setprecision(3) << r.elapsed_sec
                  << std::setw(15) << std::fixed << std::setprecision(2) << r.mbps
                  << std::setw(12) << (identical ? "MATCH [OK]" : "MISMATCH") << "\n";
    }
    std::cout << std::string(102, '-') << "\n";

    CHECK(all_clients_succeeded, "All 7 clients completed their downloads successfully");
    CHECK(all_files_match, "Every downloaded file matches original source bit-for-bit");

    // 6. Verify non-blocking concurrency:
    // Fast client downloading small_audio.bin finished much faster than slow client
    std::cout << "6. Verifying non-blocking concurrency:\n";
    std::cout << "   - Normal client duration: " << fast_small_time << " s\n";
    std::cout << "   - Slow client duration:   " << slow_small_time << " s\n";
    CHECK(fast_small_time < slow_small_time, "Fast client was not blocked by concurrent slow client");

    // 7. Aggregate measured performance
    double aggregate_mbps = (total_wall_time > 0.0)
        ? (static_cast<double>(total_bytes_all) / (1024.0 * 1024.0)) / total_wall_time
        : 0.0;

    std::cout << "\n7. Measured Overall Metrics:\n";
    std::cout << "   - Total data transferred: " << total_bytes_all << " bytes ("
              << std::fixed << std::setprecision(2) << (static_cast<double>(total_bytes_all) / (1024.0 * 1024.0)) << " MB)\n";
    std::cout << "   - Total wall clock time:  " << std::fixed << std::setprecision(3) << total_wall_time << " s\n";
    std::cout << "   - Aggregate throughput:   " << std::fixed << std::setprecision(2) << aggregate_mbps << " MB/s\n";

    // 8. Graceful server shutdown and thread termination
    std::cout << "8. Shutting down server and verifying clean thread termination...\n";
    server->stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
    CHECK(!server->is_running(), "Server shutdown completed cleanly");

    // Cleanup test artifacts
    std::error_code ec;
    std::filesystem::remove_all(media_dir, ec);
    std::filesystem::remove_all(output_dir, ec);

    std::cout << "\n==========================================================\n";
    std::cout << " Concurrency Test Results: " << g_passed << " Passed, " << g_failed << " Failed\n";
    std::cout << "==========================================================\n";

    return (g_failed == 0) ? 0 : 1;
}
