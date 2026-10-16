#include "live_capture.hpp"

#include <mutex>

struct LiveCapture::Impl {
    explicit Impl(Sink output, LiveCaptureConfig capture_config)
        : sink(std::move(output)), config(capture_config) {}

    Sink sink;
    LiveCaptureConfig config;
    mutable std::mutex mutex;
    std::string error;
};

LiveCapture::LiveCapture(Sink sink, LiveCaptureConfig config)
    : impl_(std::make_unique<Impl>(std::move(sink), config)) {}

LiveCapture::~LiveCapture() = default;

bool LiveCapture::start() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->error = "实时麦克风输入目前仅支持 macOS";
    return false;
}

void LiveCapture::stop() {}

bool LiveCapture::failed() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return !impl_->error.empty();
}

std::string LiveCapture::error_message() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}
