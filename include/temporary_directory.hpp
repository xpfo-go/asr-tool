#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <random>
#else
#include <cerrno>
#include <unistd.h>
#endif

// Atomic directory creation keeps intermediate files separate from existing paths.
class TemporaryDirectory {
public:
    explicit TemporaryDirectory(const std::filesystem::path& parent) {
#ifdef _WIN32
        std::random_device random;
        for (int attempt = 0; attempt < 64; ++attempt) {
            auto candidate = parent / (".asr-tool-" + std::to_string(random()) +
                                       "-" + std::to_string(random()));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = std::move(candidate);
                return;
            }
            if (error && error != std::errc::file_exists)
                throw std::filesystem::filesystem_error("Create temporary directory", candidate, error);
        }
        throw std::runtime_error("Could not create a unique temporary directory");
#else
        const auto pattern = (parent / ".asr-tool-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        if (!mkdtemp(buffer.data()))
            throw std::system_error(errno, std::generic_category(), "Create temporary directory");
        path_ = buffer.data();
#endif
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};
