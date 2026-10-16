#pragma once

#include "live.hpp"

#include <filesystem>
#include <memory>

// Single producer: accepts mono 16 kHz PCM. No capture or network operations.
// Callbacks cannot reenter push/finish. Processing failures are terminal and are
// rethrown on subsequent calls; closing a failed stream never feeds it again.
class StreamingTranscriber {
public:
    StreamingTranscriber(const std::filesystem::path& model_directory,
                         const std::string& prompt, LiveReviewQueue& review,
                         LiveReviewQueue::Sink sink);
    ~StreamingTranscriber();
    void push(const float* samples, size_t count);
    void finish();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
