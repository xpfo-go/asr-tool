#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct LiveCaptureConfig {
    int sample_rate = 16000;
    size_t callback_frames = 1024;
    size_t max_buffered_samples = 2 * 16000;
};

// Captures the default microphone and delivers mono 16 kHz PCM on a worker
// thread. The platform callback only converts and enqueues bounded chunks.
class LiveCapture {
public:
    using Sink = std::function<void(const std::vector<float>&)>;

    explicit LiveCapture(Sink sink, LiveCaptureConfig config = {});
    ~LiveCapture();
    LiveCapture(const LiveCapture&) = delete;
    LiveCapture& operator=(const LiveCapture&) = delete;

    bool start();
    void stop();
    bool failed() const;
    std::string error_message() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
