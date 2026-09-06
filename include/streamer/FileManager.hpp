#pragma once

#include "streamer/Types.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace streamer {

class FileManager {
public:
    explicit FileManager(const std::string& root_dir);

    bool init();

    // Directory contents
    std::vector<MediaFileInfo> list_files() const;

    // File metadata
    std::optional<uint64_t> get_file_size(const std::string& filename) const;
    bool file_exists(const std::string& filename) const;

    // Path safety validation to prevent directory traversal
    bool resolve_safe_path(const std::string& filename, std::filesystem::path& out_path) const;

    // Open file stream for binary reading
    std::unique_ptr<std::ifstream> open_binary_file(const std::string& filename) const;

    const std::filesystem::path& get_root_path() const noexcept { return canonical_root_; }

private:
    std::filesystem::path root_dir_;
    std::filesystem::path canonical_root_;
};

} // namespace streamer
