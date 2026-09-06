#include "streamer/Logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <thread>
#include <unistd.h>

namespace streamer {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_stream_.is_open()) {
        file_stream_.flush();
        file_stream_.close();
    }
}

void Logger::init(const std::string& log_file_path, LogLevel min_level, bool enable_console) {
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = min_level;
    enable_console_ = enable_console;
    is_tty_ = isatty(fileno(stdout));

    if (!log_file_path.empty()) {
        if (file_stream_.is_open()) {
            file_stream_.close();
        }
        file_stream_.open(log_file_path, std::ios::out | std::ios::app);
        if (!file_stream_.is_open()) {
            std::cerr << "[Logger] Failed to open log file: " << log_file_path << "\n";
        }
    }
}

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    min_level_ = level;
}

LogLevel Logger::get_level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return min_level_;
}

std::string Logger::format_time_now() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto timer = std::chrono::system_clock::to_time_t(now);
    
    std::tm bt{};
    localtime_r(&timer, &bt);

    std::ostringstream oss;
    oss << std::put_time(&bt, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

const char* Logger::level_to_string(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

const char* Logger::level_to_color(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "\033[36m"; // Cyan
        case LogLevel::INFO:  return "\033[32m"; // Green
        case LogLevel::WARN:  return "\033[33m"; // Yellow
        case LogLevel::ERROR: return "\033[31m"; // Red
    }
    return "\033[0m";
}

void Logger::log_string(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(level) < static_cast<int>(min_level_)) {
        return;
    }

    std::string timestamp = format_time_now();
    auto thread_id = std::this_thread::get_id();
    const char* lvl_str = level_to_string(level);

    // Plain line for file
    std::ostringstream plain_line;
    plain_line << "[" << timestamp << "] [" << lvl_str << "] [T:" << thread_id << "] " << message;

    if (file_stream_.is_open()) {
        file_stream_ << plain_line.str() << "\n";
        file_stream_.flush();
    }

    if (enable_console_) {
        if (is_tty_) {
            const char* color = level_to_color(level);
            const char* reset = "\033[0m";
            std::cout << "\033[90m[" << timestamp << "]\033[0m "
                      << color << "[" << lvl_str << "]" << reset << " "
                      << "\033[35m[T:" << thread_id << "]\033[0m "
                      << message << "\n";
        } else {
            std::cout << plain_line.str() << "\n";
        }
        std::cout.flush();
    }
}

} // namespace streamer
