#include "live_capture.hpp"

#import <AVFoundation/AVFoundation.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {

std::string ns_error(NSError* error, const char* fallback) {
    if (error && error.localizedDescription.UTF8String) {
        return error.localizedDescription.UTF8String;
    }
    return fallback;
}

}  // namespace

struct LiveCapture::Impl {
    Impl(Sink output, LiveCaptureConfig capture_config)
        : sink(std::move(output)), config(capture_config) {}

    Sink sink;
    LiveCaptureConfig config;
    AVAudioEngine* engine = nil;
    AVAudioConverter* converter = nil;
    AVAudioFormat* output_format = nil;
    std::deque<std::vector<float>> pending;
    size_t pending_samples = 0;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::thread worker;
    std::string error;
    bool started = false;
    bool stopping = false;

    void fail_locked(const std::string& message) {
        if (error.empty()) error = message;
        stopping = true;
        pending.clear();
        pending_samples = 0;
    }

    void enqueue(AVAudioPCMBuffer* input) {
        if (!input || input.frameLength == 0) return;

        const double source_rate = input.format.sampleRate;
        if (!(source_rate > 0.0)) {
            std::lock_guard<std::mutex> lock(mutex);
            fail_locked("麦克风返回了无效采样率");
            changed.notify_one();
            return;
        }
        const AVAudioFrameCount capacity = static_cast<AVAudioFrameCount>(
            std::ceil(static_cast<double>(input.frameLength) * config.sample_rate / source_rate) + 8);
        AVAudioPCMBuffer* converted = [[AVAudioPCMBuffer alloc]
            initWithPCMFormat:output_format frameCapacity:capacity];
        if (!converted) {
            std::lock_guard<std::mutex> lock(mutex);
            fail_locked("无法分配麦克风重采样缓冲区");
            changed.notify_one();
            return;
        }

        __block bool consumed = false;
        NSError* conversion_error = nil;
        const AVAudioConverterOutputStatus status = [converter
            convertToBuffer:converted
            error:&conversion_error
            withInputFromBlock:^AVAudioBuffer*(AVAudioPacketCount, AVAudioConverterInputStatus* out_status) {
                if (consumed) {
                    *out_status = AVAudioConverterInputStatus_NoDataNow;
                    return nil;
                }
                consumed = true;
                *out_status = AVAudioConverterInputStatus_HaveData;
                return input;
            }];
        if (status == AVAudioConverterOutputStatus_Error || converted.frameLength == 0) {
            std::lock_guard<std::mutex> lock(mutex);
            fail_locked(ns_error(conversion_error, "麦克风音频重采样失败"));
            changed.notify_one();
            return;
        }

        const float* channel = converted.floatChannelData ? converted.floatChannelData[0] : nullptr;
        if (!channel) {
            std::lock_guard<std::mutex> lock(mutex);
            fail_locked("麦克风重采样没有返回浮点音频");
            changed.notify_one();
            return;
        }
        std::vector<float> samples(channel, channel + converted.frameLength);
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping) return;
            if (samples.size() > config.max_buffered_samples - pending_samples) {
                fail_locked("麦克风处理速度落后，采集队列已满");
                changed.notify_one();
                return;
            }
            pending_samples += samples.size();
            pending.push_back(std::move(samples));
        }
        changed.notify_one();
    }

    void consume() {
        for (;;) {
            std::vector<float> samples;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return stopping || !pending.empty(); });
                if (pending.empty()) return;
                samples = std::move(pending.front());
                pending.pop_front();
                pending_samples -= samples.size();
            }
            try {
                sink(samples);
            } catch (const std::exception& exception) {
                std::lock_guard<std::mutex> lock(mutex);
                fail_locked(exception.what());
                changed.notify_one();
                return;
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex);
                fail_locked("实时识别线程失败");
                changed.notify_one();
                return;
            }
        }
    }
};

LiveCapture::LiveCapture(Sink sink, LiveCaptureConfig config)
    : impl_(std::make_unique<Impl>(std::move(sink), config)) {
    if (!impl_->sink) throw std::invalid_argument("Missing live capture sink");
    if (impl_->config.sample_rate != 16000 || impl_->config.max_buffered_samples == 0) {
        throw std::invalid_argument("Invalid live capture configuration");
    }
}

LiveCapture::~LiveCapture() {
    stop();
}

bool LiveCapture::start() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->started) return false;

    impl_->engine = [[AVAudioEngine alloc] init];
    AVAudioInputNode* input = impl_->engine.inputNode;
    AVAudioFormat* input_format = [input outputFormatForBus:0];
    if (!input_format || input_format.sampleRate <= 0.0 || input_format.channelCount == 0) {
        impl_->error = "无法读取默认麦克风格式，请检查麦克风权限";
        return false;
    }
    impl_->output_format = [[AVAudioFormat alloc]
        initStandardFormatWithSampleRate:impl_->config.sample_rate channels:1];
    impl_->converter = [[AVAudioConverter alloc]
        initFromFormat:input_format toFormat:impl_->output_format];
    if (!impl_->converter) {
        impl_->error = "无法初始化麦克风重采样器";
        return false;
    }

    __block Impl* state = impl_.get();
    [input installTapOnBus:0
                bufferSize:static_cast<AVAudioFrameCount>(impl_->config.callback_frames)
                    format:input_format
                     block:^(AVAudioPCMBuffer* buffer, AVAudioTime*) {
                         state->enqueue(buffer);
                     }];
    [impl_->engine prepare];
    NSError* start_error = nil;
    if (![impl_->engine startAndReturnError:&start_error]) {
        [input removeTapOnBus:0];
        impl_->error = ns_error(start_error, "无法启动默认麦克风，请检查系统权限和输入设备");
        return false;
    }
    impl_->stopping = false;
    impl_->started = true;
    impl_->worker = std::thread(&Impl::consume, impl_.get());
    return true;
}

void LiveCapture::stop() {
    AVAudioEngine* engine = nil;
    AVAudioInputNode* input = nil;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->started && !impl_->worker.joinable()) return;
        impl_->stopping = true;
        engine = impl_->engine;
        input = engine ? engine.inputNode : nil;
    }
    if (engine) [engine stop];
    if (input) [input removeTapOnBus:0];
    impl_->changed.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->started = false;
}

bool LiveCapture::failed() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return !impl_->error.empty();
}

std::string LiveCapture::error_message() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}
