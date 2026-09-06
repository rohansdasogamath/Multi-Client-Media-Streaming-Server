#pragma once

#include "streamer/Types.hpp"

#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace streamer {

class Logger {
public:
    static Logger& instance();

    void init(const std::string& log_file_path = "",
              LogLevel min_level = LogLevel::INFO,
              bool enable_console = true);

    void set_level(LogLevel level);
    LogLevel get_level() const;

    void log_string(LogLevel level, const std::string& message);

    void log(LogLevel level, const std::string& message) {
        log_string(level, message);
    }

    // Variadic helper to log formatted or concatenated messages
    template <typename T1, typename T2, typename... Args>
    void log(LogLevel level, T1&& first, T2&& second, Args&&... rest) {
        std::ostringstream oss;
        oss << std::forward<T1>(first) << std::forward<T2>(second);
        ((oss << std::forward<Args>(rest)), ...);
        log_string(level, oss.str());
    }

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string format_time_now();
    const char* level_to_string(LogLevel level) const;
    const char* level_to_color(LogLevel level) const;

    mutable std::mutex mutex_;
    LogLevel min_level_{LogLevel::INFO};
    bool enable_console_{true};
    std::ofstream file_stream_;
    bool is_tty_{false};
};

#define LOG_DEBUG(...) streamer::Logger::instance().log(streamer::LogLevel::DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  streamer::Logger::instance().log(streamer::LogLevel::INFO,  __VA_ARGS__)
#define LOG_WARN(...)  streamer::Logger::instance().log(streamer::LogLevel::WARN,  __VA_ARGS__)
#define LOG_ERROR(...) streamer::Logger::instance().log(streamer::LogLevel::ERROR, __VA_ARGS__)

} // namespace streamer
