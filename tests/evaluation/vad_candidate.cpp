#include "audio.hpp"
#include "meeting.hpp"
#include "vad.hpp"

#include <whisper.h>

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc != 5 && argc != 6) throw std::runtime_error("Expected AUDIO MODEL whisper|onnx OUTPUT.json [THRESHOLD]");
        const float threshold = argc == 6 ? std::stof(argv[5]) : .5f;
        if (!std::isfinite(threshold) || threshold <= 0 || threshold >= 1)
            throw std::runtime_error("VAD threshold must be between zero and one");
        auto reader = open_audio(argv[1]);
        const auto samples = reader.read_all();
        const std::string backend = argv[3];
        std::vector<TimeRange> spans;
        if (backend == "onnx") {
            spans = detect_speech(samples, argv[2], threshold);
        } else if (backend == "whisper") {
            auto config = whisper_vad_default_context_params();
            config.use_gpu = false;
            config.n_threads = 2;
            using Vad = std::unique_ptr<whisper_vad_context, decltype(&whisper_vad_free)>;
            Vad vad(whisper_vad_init_from_file_with_params(argv[2], config), whisper_vad_free);
            if (!vad) throw std::runtime_error("Could not initialize Whisper VAD");
            auto parameters = whisper_vad_default_params();
            parameters.threshold = threshold;
            parameters.min_speech_duration_ms = 200;
            parameters.min_silence_duration_ms = 300;
            parameters.speech_pad_ms = 200;
            using Speech = std::unique_ptr<whisper_vad_segments, decltype(&whisper_vad_free_segments)>;
            Speech speech(whisper_vad_segments_from_samples(vad.get(), parameters, samples.data(),
                                                           static_cast<int>(samples.size())), whisper_vad_free_segments);
            if (!speech) throw std::runtime_error("Whisper VAD failed");
            const double duration_ms = reader.duration_seconds() * 1000.0;
            for (int i = 0; i < whisper_vad_segments_n_segments(speech.get()); ++i) {
                const double start = std::clamp(whisper_vad_segments_get_segment_t0(speech.get(), i) * 10.0,
                                                0.0, duration_ms);
                const double end = std::clamp(whisper_vad_segments_get_segment_t1(speech.get(), i) * 10.0,
                                              0.0, duration_ms);
                if (end > start) spans.push_back({start, end});
            }
        } else {
            throw std::runtime_error("Unknown VAD backend");
        }
        const auto windows = plan_speech_windows(spans, reader.duration_seconds() * 1000.0);
        std::ofstream output(argv[4]);
        if (!output) throw std::runtime_error("Could not open VAD output");
        auto ranges = [&](const std::vector<TimeRange>& items) {
            output << '[';
            for (size_t i = 0; i < items.size(); ++i) {
                if (i) output << ',';
                output << "{\"start\":" << items[i].start_ms / 1000.0
                       << ",\"end\":" << items[i].end_ms / 1000.0 << '}';
            }
            output << ']';
        };
        output << std::setprecision(12) << "{\"duration\":" << reader.duration_seconds()
               << ",\"backend\":\"" << backend << "\",\"settings\":{\"threshold\":" << threshold
#ifdef ASR_EXPERIMENT_AUDIO_FILTER
               << ",\"audio_filter\":\"" << ASR_EXPERIMENT_AUDIO_FILTER << "\""
#endif
               << ',' <<
                  "\"minimum_speech_seconds\":0.2,\"minimum_silence_seconds\":0.3,\"padding_seconds\":0.2},\"speech\":";
        ranges(spans);
        std::vector<TimeRange> audio;
        for (const auto& window : windows) audio.push_back(window.audio);
        output << ",\"windows\":";
        ranges(audio);
        output << "}\n";
        if (!output) throw std::runtime_error("Could not write VAD output");
        return 0;
    } catch (const std::exception& error) {
        (void)fprintf(stderr, "[ERROR] %s\n", error.what());
        return 1;
    }
}
