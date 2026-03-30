#pragma once

#include "types.hpp"
#include <filesystem>
#include <string>

class Transcriber {
public:
    explicit Transcriber(const std::string& model_path,
                         const std::string& language,
                         const std::string& prompt,
                         Backend backend);
    ~Transcriber();

    TranscriptionResult transcribe(const std::filesystem::path& audio_path);

    bool is_ready() const { return ready_; }
    std::string error_message() const { return error_message_; }

private:
    struct Impl;
    Impl* impl_;
    bool ready_ = false;
    std::string error_message_;
};
