#pragma once

#include "streamer/Types.hpp"

#include <string>
#include <vector>

namespace streamer {

enum class CommandType {
    LIST,
    GET,
    QUIT,
    UNKNOWN
};

struct Request {
    CommandType type{CommandType::UNKNOWN};
    std::string argument;
};

class Protocol {
public:
    // Parse raw request line from client
    static Request parse_request(const std::string& line);

    // Build server responses
    static std::string build_list_response(const std::vector<MediaFileInfo>& files);
    static std::string build_get_header(uint64_t file_size);
    static std::string build_error_response(const std::string& error_message);
    static std::string build_ok_response(const std::string& message = "");
};

} // namespace streamer
