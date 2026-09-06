#include "streamer/FileManager.hpp"
#include "streamer/Logger.hpp"

#include <algorithm>
#include <system_error>

namespace streamer {

FileManager::FileManager(const std::string& root_dir)
    : root_dir_(root_dir) {}

bool FileManager::init() {
    std::error_code ec;
    if (!std::filesystem::exists(root_dir_, ec)) {
        if (!std::filesystem::create_directories(root_dir_, ec)) {
            LOG_ERROR("Failed to create media directory: ", root_dir_.string(), " (", ec.message(), ")");
            return false;
        }
    }

    canonical_root_ = std::filesystem::canonical(root_dir_, ec);
    if (ec) {
        LOG_ERROR("Failed to canonicalize media directory: ", root_dir_.string(), " (", ec.message(), ")");
        return false;
    }

    LOG_INFO("Media repository root: ", canonical_root_.string());
    return true;
}

bool FileManager::resolve_safe_path(const std::string& filename, std::filesystem::path& out_path) const {
    if (filename.empty()) {
        return false;
    }

    // Basic sanity checks: reject null bytes
    if (filename.find('\0') != std::string::npos) {
        return false;
    }

    std::filesystem::path candidate = filename;
    // Disallow absolute paths directly
    if (candidate.is_absolute()) {
        LOG_WARN("Absolute path rejected: '", filename, "'");
        return false;
    }

    std::error_code ec;
    std::filesystem::path full_candidate = canonical_root_ / candidate;
    std::filesystem::path weakly_canon = std::filesystem::weakly_canonical(full_candidate, ec);
    if (ec) {
        return false;
    }

    // Ensure the canonicalized path resides inside canonical_root_
    auto rel = weakly_canon.lexically_relative(canonical_root_);
    if (rel.empty() || rel.string().rfind("..", 0) == 0 || rel.is_absolute()) {
        LOG_WARN("Path traversal attempt detected: '", filename, "' resolves outside root");
        return false;
    }

    out_path = weakly_canon;
    return true;
}

std::vector<MediaFileInfo> FileManager::list_files() const {
    std::vector<MediaFileInfo> files;
    std::error_code ec;

    if (!std::filesystem::exists(canonical_root_, ec) || !std::filesystem::is_directory(canonical_root_, ec)) {
        return files;
    }

    for (const auto& entry : std::filesystem::directory_iterator(canonical_root_, ec)) {
        if (ec) {
            LOG_WARN("Error during directory iteration: ", ec.message());
            break;
        }

        if (entry.is_regular_file(ec)) {
            std::string name = entry.path().filename().string();
            // Ignore hidden files and dotfiles (like .gitkeep)
            if (!name.empty() && name[0] == '.') {
                continue;
            }

            uint64_t size = entry.file_size(ec);
            if (!ec) {
                files.push_back({name, size});
            }
        }
    }

    std::sort(files.begin(), files.end(), [](const MediaFileInfo& a, const MediaFileInfo& b) {
        return a.filename < b.filename;
    });

    return files;
}

bool FileManager::file_exists(const std::string& filename) const {
    std::filesystem::path safe_path;
    if (!resolve_safe_path(filename, safe_path)) {
        return false;
    }

    std::error_code ec;
    return std::filesystem::is_regular_file(safe_path, ec);
}

std::optional<uint64_t> FileManager::get_file_size(const std::string& filename) const {
    std::filesystem::path safe_path;
    if (!resolve_safe_path(filename, safe_path)) {
        return std::nullopt;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(safe_path, ec)) {
        return std::nullopt;
    }

    uint64_t sz = std::filesystem::file_size(safe_path, ec);
    if (ec) {
        return std::nullopt;
    }

    return sz;
}

std::unique_ptr<std::ifstream> FileManager::open_binary_file(const std::string& filename) const {
    std::filesystem::path safe_path;
    if (!resolve_safe_path(filename, safe_path)) {
        LOG_WARN("Cannot open file outside sandbox: ", filename);
        return nullptr;
    }

    auto file = std::make_unique<std::ifstream>(safe_path, std::ios::binary | std::ios::in);
    if (!file || !file->is_open()) {
        LOG_ERROR("Failed to open binary file for reading: ", safe_path.string());
        return nullptr;
    }

    return file;
}

} // namespace streamer
