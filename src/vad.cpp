#include "vad.hpp"

#include <sherpa-onnx/c-api/c-api.h>

#include <algorithm>
#include <memory>
#include <stdexcept>

std::vector<TimeRange> detect_speech(const std::vector<float>& samples,
                                    const std::filesystem::path& model_path,
                                    float threshold) {
    if (!std::filesystem::is_regular_file(model_path)) {
        throw std::runtime_error("Missing local VAD model: " + model_path.string());
    }
    const auto model = model_path.string();
    SherpaOnnxVadModelConfig config{};
    config.silero_vad.model = model.c_str();
    config.silero_vad.threshold = threshold;
    config.silero_vad.min_speech_duration = .2f;
    config.silero_vad.min_silence_duration = .3f;
    config.silero_vad.max_speech_duration = 30;
    config.silero_vad.window_size = 512;
    config.sample_rate = 16000;
    config.num_threads = 2;
    config.provider = "cpu";
    using Detector = std::unique_ptr<const SherpaOnnxVoiceActivityDetector,
                                     decltype(&SherpaOnnxDestroyVoiceActivityDetector)>;
    Detector detector(SherpaOnnxCreateVoiceActivityDetector(&config, 60),
                      SherpaOnnxDestroyVoiceActivityDetector);
    if (!detector) throw std::runtime_error("Could not initialize local VAD");
    std::vector<TimeRange> speech;
    const double duration_ms = samples.size() / 16.0;
    auto drain = [&]() {
        while (!SherpaOnnxVoiceActivityDetectorEmpty(detector.get())) {
            using Segment = std::unique_ptr<const SherpaOnnxSpeechSegment,
                                            decltype(&SherpaOnnxDestroySpeechSegment)>;
            Segment segment(SherpaOnnxVoiceActivityDetectorFront(detector.get()),
                            SherpaOnnxDestroySpeechSegment);
            if (!segment) throw std::runtime_error("Missing VAD speech segment");
            const double start = std::max(0.0, segment->start / 16.0 - 200.0);
            const double end = std::min(duration_ms, (segment->start + segment->n) / 16.0 + 200.0);
            if (end > start) speech.push_back({start, end});
            SherpaOnnxVoiceActivityDetectorPop(detector.get());
        }
    };
    for (size_t i = 0; i < samples.size(); i += 512) {
        SherpaOnnxVoiceActivityDetectorAcceptWaveform(detector.get(), samples.data() + i,
                                                     static_cast<int>(std::min<size_t>(512, samples.size() - i)));
        drain();
    }
    SherpaOnnxVoiceActivityDetectorFlush(detector.get());
    drain();
    return speech;
}
