#include "streamer/FileManager.hpp"
#include "streamer/Logger.hpp"
#include "streamer/Protocol.hpp"
#include "streamer/Server.hpp"
#include "streamer/Socket.hpp"

#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_tests_passed = 0;
int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (cond) { \
            std::cout << "  [PASS] " << msg << "\n"; \
            g_tests_passed++; \
        } else { \
            std::cerr << "  [FAIL] " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
            g_tests_failed++; \
        } \
    } while (0)

void create_dummy_file(const std::filesystem::path& path, size_t size_bytes, char fill_byte = 'A') {
    std::ofstream ofs(path, std::ios::binary | std::ios::out);
    std::vector<char> buffer(64 * 1024, fill_byte);
    size_t remaining = size_bytes;
    while (remaining > 0) {
        size_t to_write = std::min(remaining, buffer.size());
        ofs.write(buffer.data(), static_cast<std::streamsize>(to_write));
        remaining -= to_write;
    }
}

bool files_are_identical(const std::filesystem::path& p1, const std::filesystem::path& p2) {
    if (std::filesystem::file_size(p1) != std::filesystem::file_size(p2)) {
        return false;
    }
    std::ifstream f1(p1, std::ios::binary);
    std::ifstream f2(p2, std::ios::binary);
    constexpr size_t buf_size = 4096;
    std::vector<char> b1(buf_size), b2(buf_size);
    while (f1 && f2) {
        f1.read(b1.data(), buf_size);
        f2.read(b2.data(), buf_size);
        if (f1.gcount() != f2.gcount()) return false;
        if (std::memcmp(b1.data(), b2.data(), static_cast<size_t>(f1.gcount())) != 0) return false;
    }
    return true;
}

void test_protocol_parsing() {
    std::cout << "\n--- Running Protocol Parsing Tests ---\n";

    auto req1 = streamer::Protocol::parse_request("LIST\n");
    TEST_ASSERT(req1.type == streamer::CommandType::LIST, "Parse LIST command");

    auto req2 = streamer::Protocol::parse_request("get sample_video.mp4\r\n");
    TEST_ASSERT(req2.type == streamer::CommandType::GET && req2.argument == "sample_video.mp4", "Parse GET command with argument");

    auto req3 = streamer::Protocol::parse_request("QUIT\n");
    TEST_ASSERT(req3.type == streamer::CommandType::QUIT, "Parse QUIT command");

    auto req4 = streamer::Protocol::parse_request("UNKNOWN_CMD arg\n");
    TEST_ASSERT(req4.type == streamer::CommandType::UNKNOWN, "Parse UNKNOWN command");

    std::string err_resp = streamer::Protocol::build_error_response("FILE_NOT_FOUND");
    TEST_ASSERT(err_resp == "ERROR FILE_NOT_FOUND\n", "Build error response");

    std::string get_hdr = streamer::Protocol::build_get_header(1048576);
    TEST_ASSERT(get_hdr == "OK 1048576\n", "Build GET header");
}

void test_file_manager_and_security(const std::string& test_dir) {
    std::cout << "\n--- Running FileManager & Path Traversal Security Tests ---\n";

    streamer::FileManager fm(test_dir);
    TEST_ASSERT(fm.init(), "FileManager initialized test media directory");

    std::filesystem::path f1 = std::filesystem::path(test_dir) / "test_media_1.bin";
    create_dummy_file(f1, 128 * 1024, 'X');

    TEST_ASSERT(fm.file_exists("test_media_1.bin"), "Detects existing test file");
    auto sz = fm.get_file_size("test_media_1.bin");
    TEST_ASSERT(sz.has_value() && sz.value() == 128 * 1024, "Correct file size reported");

    // Path traversal attacks
    std::filesystem::path resolved;
    TEST_ASSERT(!fm.resolve_safe_path("../../../etc/passwd", resolved), "Blocked ../ traversal");
    TEST_ASSERT(!fm.resolve_safe_path("/etc/shadow", resolved), "Blocked absolute path traversal");
    TEST_ASSERT(!fm.file_exists("non_existent_file.xyz"), "Non-existent file returns false");

    auto list = fm.list_files();
    TEST_ASSERT(!list.empty(), "list_files returned non-empty catalog");
}

void test_concurrent_streaming_integration(const std::string& test_dir, uint16_t port) {
    std::cout << "\n--- Running End-to-End Concurrent Streaming Integration Tests ---\n";

    // Create test files
    std::filesystem::path test_file_1 = std::filesystem::path(test_dir) / "stream_small.bin";
    std::filesystem::path test_file_2 = std::filesystem::path(test_dir) / "stream_large.bin";
    create_dummy_file(test_file_1, 256 * 1024, 'A');   // 256 KB
    create_dummy_file(test_file_2, 2 * 1024 * 1024, 'B'); // 2 MB

    streamer::ServerConfig config;
    config.host = "127.0.0.1";
    config.port = port;
    config.media_dir = test_dir;
    config.chunk_size = 32 * 1024; // 32 KB chunk
    config.log_file = ""; // console only

    auto server = std::make_unique<streamer::Server>(config);
    std::thread server_thread([&server]() {
        server->start();
    });

    // Wait for server to bind & listen
    for (int attempts = 0; attempts < 50 && !server->is_running(); ++attempts) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // 1. Test LIST command
    {
        streamer::Socket client = streamer::Socket::create_tcp();
        TEST_ASSERT(client.connect("127.0.0.1", port), "Client connected for LIST test");
        TEST_ASSERT(client.send_string("LIST\n"), "Sent LIST command");
        std::string line;
        TEST_ASSERT(client.recv_line(line), "Received LIST header response");
        TEST_ASSERT(line.rfind("OK", 0) == 0, "LIST response begins with OK");
        client.send_string("QUIT\n");
        client.close();
    }

    // 2. Test Single Client Stream & Integrity Check
    std::filesystem::path download_small = std::filesystem::path(test_dir) / "downloaded_small.bin";
    {
        streamer::Socket client = streamer::Socket::create_tcp();
        TEST_ASSERT(client.connect("127.0.0.1", port), "Client connected for single stream");
        TEST_ASSERT(client.send_string("GET stream_small.bin\n"), "Sent GET stream_small.bin");
        std::string header;
        TEST_ASSERT(client.recv_line(header), "Received GET header");
        TEST_ASSERT(header == "OK 262144", "GET header matches exact size");

        std::ofstream out(download_small, std::ios::binary);
        std::vector<char> buf(32768);
        size_t total = 0;
        while (total < 262144) {
            size_t to_recv = std::min(size_t(32768), 262144 - total);
            int res = client.recv_all(buf.data(), to_recv);
            TEST_ASSERT(res > 0, "recv_all chunk succeeded");
            out.write(buf.data(), static_cast<std::streamsize>(to_recv));
            total += to_recv;
        }
        out.close();
        client.send_string("QUIT\n");
        client.close();

        TEST_ASSERT(files_are_identical(test_file_1, download_small), "Downloaded file is byte-for-byte identical to original");
    }

    // 3. Test Multi-Client Concurrent Streaming (5 clients simultaneously)
    {
        constexpr int NUM_CLIENTS = 5;
        std::vector<std::thread> client_threads;
        std::atomic<int> successful_downloads{0};

        std::cout << "  Spawning " << NUM_CLIENTS << " concurrent clients downloading 2MB files...\n";
        for (int i = 0; i < NUM_CLIENTS; ++i) {
            client_threads.emplace_back([i, port, test_dir, &successful_downloads, &test_file_2]() {
                streamer::Socket sock = streamer::Socket::create_tcp();
                if (!sock.connect("127.0.0.1", port)) return;
                if (!sock.send_string("GET stream_large.bin\n")) return;

                std::string header;
                if (!sock.recv_line(header)) return;
                if (header.rfind("OK", 0) != 0) return;

                std::filesystem::path dl_path = std::filesystem::path(test_dir) / ("concurrent_" + std::to_string(i) + ".bin");
                std::ofstream out(dl_path, std::ios::binary);
                size_t expected_size = 2 * 1024 * 1024;
                std::vector<char> buf(32768);
                size_t received = 0;

                while (received < expected_size) {
                    size_t chunk = std::min(size_t(32768), expected_size - received);
                    if (sock.recv_all(buf.data(), chunk) <= 0) break;
                    out.write(buf.data(), static_cast<std::streamsize>(chunk));
                    received += chunk;
                }
                out.close();
                sock.send_string("QUIT\n");
                sock.close();

                if (received == expected_size && files_are_identical(test_file_2, dl_path)) {
                    successful_downloads++;
                }
            });
        }

        for (auto& t : client_threads) {
            if (t.joinable()) t.join();
        }

        TEST_ASSERT(successful_downloads.load() == NUM_CLIENTS, "All 5 concurrent client streams verified identical");
    }

    // 4. Test Abrupt Client Disconnect handling (server must survive)
    {
        streamer::Socket abruptly_killed_client = streamer::Socket::create_tcp();
        if (abruptly_killed_client.connect("127.0.0.1", port)) {
            abruptly_killed_client.send_string("GET stream_large.bin\n");
            std::string header;
            abruptly_killed_client.recv_line(header);
            std::vector<char> buf(1024);
            abruptly_killed_client.recv_all(buf.data(), 1024);
            // Abruptly close socket mid-transfer
            abruptly_killed_client.close();
            std::cout << "  Simulated client abrupt disconnect mid-stream.\n";
        }

        // Verify server is still completely responsive to a new client
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        streamer::Socket probe_client = streamer::Socket::create_tcp();
        TEST_ASSERT(probe_client.connect("127.0.0.1", port), "Server is alive and accepting connections after abrupt peer disconnect");
        TEST_ASSERT(probe_client.send_string("LIST\n"), "Server responded to probe request after peer disconnect");
        probe_client.send_string("QUIT\n");
        probe_client.close();
    }

    // 5. Test Path Traversal over network
    {
        streamer::Socket bad_client = streamer::Socket::create_tcp();
        if (bad_client.connect("127.0.0.1", port)) {
            bad_client.send_string("GET ../../../etc/passwd\n");
            std::string line;
            bad_client.recv_line(line);
            TEST_ASSERT(line.rfind("ERROR", 0) == 0 || line.rfind("ERR", 0) == 0, "Server refused path traversal request with ERROR");
            bad_client.send_string("QUIT\n");
            bad_client.close();
        }
    }

    // 6. Graceful Server Shutdown
    server->stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
    TEST_ASSERT(!server->is_running(), "Server shutdown completed and thread joined cleanly");
}

void test_repository_fixture_streaming(uint16_t port) {
    std::cout << "\n--- Running Repository Test Fixture Streaming Verification ---\n";

    std::filesystem::path fixture_path = "tests/data/test_media_fixture.bin";
    if (!std::filesystem::exists(fixture_path)) {
        fixture_path = "../tests/data/test_media_fixture.bin";
    }

    TEST_ASSERT(std::filesystem::exists(fixture_path), "Repository test fixture exists under tests/data/");
    uint64_t expected_size = std::filesystem::file_size(fixture_path);
    TEST_ASSERT(expected_size == 131072, "Fixture size is exactly 131072 bytes (128 KB)");

    std::filesystem::path fixture_dir = fixture_path.parent_path();
    std::string fixture_filename = fixture_path.filename().string();

    streamer::ServerConfig config;
    config.host = "127.0.0.1";
    config.port = port;
    config.media_dir = fixture_dir.string();
    config.chunk_size = 32 * 1024; // 32 KB chunk (tests 4 chunks for 128 KB)
    config.log_file = "";

    auto server = std::make_unique<streamer::Server>(config);
    std::thread server_thread([&server]() {
        server->start();
    });

    // Wait for server to bind & listen
    for (int attempts = 0; attempts < 50 && !server->is_running(); ++attempts) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    TEST_ASSERT(server->is_running(), "Server started with tests/data as media repository");

    // Client connects and downloads fixture
    std::filesystem::path dl_fixture = "downloaded_fixture_test.bin";
    {
        streamer::Socket client = streamer::Socket::create_tcp();
        TEST_ASSERT(client.connect("127.0.0.1", port), "Client connected to fixture server");

        std::string req = "GET " + fixture_filename + "\n";
        TEST_ASSERT(client.send_string(req), "Client sent GET request for test fixture");

        std::string header;
        TEST_ASSERT(client.recv_line(header), "Server sent response header");
        TEST_ASSERT(header == "OK 131072", "Server validated request, opened file, and returned OK 131072");

        std::ofstream out(dl_fixture, std::ios::binary);
        std::vector<char> buf(32768);
        size_t total = 0;
        bool stream_ok = true;
        while (total < 131072) {
            size_t to_recv = std::min(size_t(32768), 131072 - total);
            int res = client.recv_all(buf.data(), to_recv);
            if (res <= 0) {
                stream_ok = false;
                break;
            }
            out.write(buf.data(), static_cast<std::streamsize>(to_recv));
            total += to_recv;
        }
        out.close();
        client.send_string("QUIT\n");
        client.close();

        TEST_ASSERT(stream_ok && total == 131072, "Server streamed all 131072 bytes across 4 chunks");
        TEST_ASSERT(files_are_identical(fixture_path, dl_fixture), "Received fixture matches original repository fixture byte-for-byte");
    }

    // Server cleanup
    server->stop();
    if (server_thread.joinable()) {
        server_thread.join();
    }
    TEST_ASSERT(!server->is_running(), "Server shut down cleanly after fixture test");

    std::error_code ec;
    std::filesystem::remove(dl_fixture, ec);
}

} // namespace

int main() {
    std::cout << "========================================\n";
    std::cout << " Running Multi-Client Streamer Tests\n";
    std::cout << "========================================\n";

    streamer::Logger::instance().init("", streamer::LogLevel::WARN, true);

    test_protocol_parsing();

    std::string test_dir = "test_sandbox_media";
    std::filesystem::create_directories(test_dir);

    test_file_manager_and_security(test_dir);
    test_concurrent_streaming_integration(test_dir, 9988);
    test_repository_fixture_streaming(9989);

    // Clean up temporary test folder
    std::error_code ec;
    std::filesystem::remove_all(test_dir, ec);

    std::cout << "\n========================================\n";
    std::cout << " Test Results: " << g_tests_passed << " Passed, " << g_tests_failed << " Failed\n";
    std::cout << "========================================\n";

    return (g_tests_failed == 0) ? 0 : 1;
}
