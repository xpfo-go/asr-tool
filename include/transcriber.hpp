#pragma once

#include "types.hpp"
#include <filesystem>
#include <memory>
#include <string>

class Transcriber {
public:
    explicit Transcriber(const std::string& model_path,
                         const std::string& language,
                         const std::string& prompt,
                         Backend backend,
                         bool speakers = true);
    ~Transcriber();

    TranscriptionResult transcribe(const std::filesystem::path& audio_path);
    TranscriptionResult transcribe_samples(const std::vector<float>& samples);

    bool is_ready() const { return ready_; }
    std::string error_message() const { return error_message_; }

private:
    TranscriptionResult transcribe_samples_impl(const std::vector<float>& samples,
                                                const std::vector<float>& original_samples,
                                                bool offline);
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool ready_ = false;
    std::string error_message_;
};
