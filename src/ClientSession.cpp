#include "streamer/ClientSession.hpp"
#include "streamer/Logger.hpp"
#include "streamer/Protocol.hpp"

#include <chrono>
#include <iomanip>
#include <vector>

namespace streamer {

ClientSession::ClientSession(uint64_t session_id,
                             Socket socket,
                             std::string client_ip,
                             uint16_t client_port,
                             const FileManager& file_manager,
                             size_t chunk_size)
    : session_id_(session_id),
      socket_(std::move(socket)),
      client_ip_(std::move(client_ip)),
      client_port_(client_port),
      file_manager_(file_manager),
      chunk_size_(chunk_size > 0 ? chunk_size : DEFAULT_CHUNK_SIZE),
      is_active_(true) {}

ClientSession::~ClientSession() {
    stop();
}

void ClientSession::stop() {
    bool expected = true;
    if (is_active_.compare_exchange_strong(expected, false)) {
        LOG_DEBUG("[Client #", session_id_, "] Stopping session and shutting down socket");
        socket_.shutdown(SHUT_RDWR);
        socket_.close();
    }
}

void ClientSession::run() {
    is_active_ = true;
    LOG_INFO("[Client #", session_id_, "] Client connected from ", client_ip_, ":", client_port_);

    while (is_active_) {
        std::string line;
        if (!socket_.recv_line(line)) {
            // Client closed connection or socket timed out / errored
            break;
        }

        Request request = Protocol::parse_request(line);
        switch (request.type) {
            case CommandType::LIST:
                handle_list();
                break;

            case CommandType::GET:
                handle_get(request.argument);
                break;

            case CommandType::QUIT:
                LOG_INFO("[Client #", session_id_, "] Client sent QUIT command");
                socket_.send_string(Protocol::build_ok_response("BYE"));
                is_active_ = false;
                break;

            case CommandType::UNKNOWN:
            default:
                LOG_WARN("[Client #", session_id_, "] Invalid request or unknown command: '", request.argument, "'");
                socket_.send_string(Protocol::build_error_response("INVALID_REQUEST"));
                break;
        }
    }

    LOG_INFO("[Client #", session_id_, "] Client disconnected (", client_ip_, ":", client_port_, ")");
    is_active_ = false;
    socket_.close();
}

void ClientSession::handle_list() {
    auto files = file_manager_.list_files();
    std::string response = Protocol::build_list_response(files);
    if (!socket_.send_string(response)) {
        LOG_WARN("[Client #", session_id_, "] Failed to send LIST response to client");
        is_active_ = false;
    } else {
        LOG_INFO("[Client #", session_id_, "] Sent media catalog (", files.size(), " files)");
    }
}

void ClientSession::handle_get(const std::string& filename) {
    LOG_INFO("[Client #", session_id_, "] Requested filename: '", filename, "'");

    if (filename.empty()) {
        LOG_WARN("[Client #", session_id_, "] Invalid request: empty filename");
        socket_.send_string(Protocol::build_error_response("EMPTY_FILENAME"));
        return;
    }

    auto opt_size = file_manager_.get_file_size(filename);
    if (!opt_size.has_value()) {
        LOG_WARN("[Client #", session_id_, "] Missing file or forbidden path: '", filename, "'");
        socket_.send_string(Protocol::build_error_response("FILE_NOT_FOUND"));
        return;
    }

    uint64_t file_size = opt_size.value();
    auto file_stream = file_manager_.open_binary_file(filename);
    if (!file_stream) {
        LOG_ERROR("[Client #", session_id_, "] File-open failure: could not open '", filename, "' for binary reading");
        socket_.send_string(Protocol::build_error_response("FILE_OPEN_FAILED"));
        return;
    }

    std::string header = Protocol::build_get_header(file_size);
    if (!socket_.send_string(header)) {
        LOG_WARN("[Client #", session_id_, "] Client disconnected while sending GET response header");
        is_active_ = false;
        return;
    }

    LOG_INFO("[Client #", session_id_, "] Streaming '", filename, "' (", file_size,
             " bytes, chunk: ", chunk_size_, " bytes)");

    std::vector<char> buffer(chunk_size_);
    uint64_t total_sent = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (is_active_ && total_sent < file_size && file_stream->good()) {
        size_t bytes_to_read = std::min(static_cast<uint64_t>(chunk_size_), file_size - total_sent);
        file_stream->read(buffer.data(), static_cast<std::streamsize>(bytes_to_read));
        std::streamsize bytes_read = file_stream->gcount();
        if (bytes_read <= 0) {
            break;
        }

        if (!socket_.send_all(buffer.data(), static_cast<size_t>(bytes_read))) {
            LOG_ERROR("[Client #", session_id_, "] Transfer failure for '", filename,
                      "': client disconnected mid-transfer after sending ", total_sent, " / ", file_size, " bytes");
            is_active_ = false;
            return;
        }

        total_sent += static_cast<uint64_t>(bytes_read);
    }

    auto end_time = std::chrono::steady_clock::now();
    double duration_sec = std::chrono::duration<double>(end_time - start_time).count();
    double mbps = (duration_sec > 0.0) ? (static_cast<double>(total_sent) / (1024.0 * 1024.0)) / duration_sec : 0.0;

    if (total_sent == file_size) {
        LOG_INFO("[Client #", session_id_, "] Successful transfer of '", filename, "' (", total_sent,
                 " bytes in ", std::fixed, std::setprecision(2), duration_sec, "s, ",
                 mbps, " MB/s)");
    } else if (is_active_) {
        LOG_ERROR("[Client #", session_id_, "] Transfer failure for '", filename,
                  "': streaming ended prematurely (sent ", total_sent, " of ", file_size, " bytes)");
    }
}

} // namespace streamer
