#include "streamer/Protocol.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace streamer {

static std::string trim(const std::string& str) {
    size_t start = 0;
    while (start < str.size() && std::isspace(static_cast<unsigned char>(str[start]))) {
        ++start;
    }
    size_t end = str.size();
    while (end > start && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
        --end;
    }
    return str.substr(start, end - start);
}

Request Protocol::parse_request(const std::string& raw_line) {
    Request req;
    std::string line = trim(raw_line);
    if (line.empty()) {
        return req;
    }

    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    // Convert command to uppercase
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) {
        return std::toupper(c);
    });

    if (cmd == "LIST") {
        req.type = CommandType::LIST;
    } else if (cmd == "GET") {
        req.type = CommandType::GET;
        // Remaining part of the line is the filename
        std::string filename;
        std::getline(iss >> std::ws, filename);
        req.argument = trim(filename);
    } else if (cmd == "QUIT" || cmd == "EXIT" || cmd == "CLOSE") {
        req.type = CommandType::QUIT;
    } else {
        req.type = CommandType::UNKNOWN;
        req.argument = cmd;
    }

    return req;
}

std::string Protocol::build_list_response(const std::vector<MediaFileInfo>& files) {
    std::ostringstream oss;
    oss << "OK " << files.size() << "\n";
    for (const auto& file : files) {
        oss << file.filename << " " << file.size_bytes << "\n";
    }
    return oss.str();
}

std::string Protocol::build_get_header(uint64_t file_size) {
    return "OK " + std::to_string(file_size) + "\n";
}

std::string Protocol::build_error_response(const std::string& error_message) {
    return "ERROR " + error_message + "\n";
}

std::string Protocol::build_ok_response(const std::string& message) {
    if (message.empty()) {
        return "OK\n";
    }
    return "OK " + message + "\n";
}

} // namespace streamer
