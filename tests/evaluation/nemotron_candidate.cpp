#include "audio.hpp"

#include <nemo_speech/diar.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(nemo_speech_asr_status status) {
    if (status != NEMO_SPEECH_ASR_OK) {
        throw std::runtime_error(nemo_speech_asr_last_error());
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("Expected AUDIO -m MODEL_DIR -f json -o OUTPUT.json");
        std::filesystem::path model_directory, destination;
        for (int i = 2; i < argc; ++i) {
            const std::string option = argv[i];
            if (++i == argc) throw std::runtime_error("Missing value for " + option);
            if (option == "-m") model_directory = argv[i];
            else if (option == "-o") destination = argv[i];
            else if (option != "-f" || std::string(argv[i]) != "json")
                throw std::runtime_error("Unsupported Nemotron option: " + option);
        }
        if (model_directory.empty() || destination.empty()) {
            throw std::runtime_error("Missing Nemotron model/output directory");
        }
        const auto model_path = model_directory / "model.gguf";
        if (!std::filesystem::is_regular_file(model_path)) {
            throw std::runtime_error("Missing Nemotron model.gguf");
        }
        auto reader = open_audio(argv[1]);
        const auto samples = reader.read_all();
        if (samples.empty()) throw std::runtime_error("Empty Nemotron audio");

        const std::string model_name = model_path.string();
        nemo_speech_diar_model_config config{};
        config.size = sizeof(config);
        config.model_path = model_name.c_str();
        config.gpu = 0;
        config.preset = "v3-offline";
        // Zero is a real override in this ABI; -1 retains the preset's context.
        config.left_context_frames = -1;
        nemo_speech_diar_model* raw_model = nullptr;
        check(nemo_speech_diar_create(&config, &raw_model));
        std::unique_ptr<nemo_speech_diar_model, decltype(&nemo_speech_diar_destroy)>
            model(raw_model, nemo_speech_diar_destroy);
        const int speakers = nemo_speech_diar_num_speakers(model.get());
        const double frame_seconds = nemo_speech_diar_seconds_per_frame(model.get());
        if (speakers != 8 || std::abs(frame_seconds - .01) > 1e-9) {
            throw std::runtime_error("Expected Nemotron 3 eight-speaker high-resolution model");
        }

        nemo_speech_diar_stream* raw_stream = nullptr;
        check(nemo_speech_diar_stream_open(model.get(), &raw_stream));
        std::unique_ptr<nemo_speech_diar_stream, decltype(&nemo_speech_diar_stream_close)>
            stream(raw_stream, nemo_speech_diar_stream_close);
        constexpr size_t push_samples = 2560;
        for (size_t offset = 0; offset < samples.size(); offset += push_samples) {
            check(nemo_speech_diar_stream_push_f32(
                stream.get(), samples.data() + offset,
                std::min(push_samples, samples.size() - offset), 16000));
        }
        check(nemo_speech_diar_stream_finish(stream.get()));
        const auto frame_count = nemo_speech_diar_frame_count(stream.get());
        const auto retained_start = nemo_speech_diar_frame_probs_start(stream.get());
        // This model uses centered 10 ms STFT frames, including the final pad.
        // Silent tails still count: an empty turn list cannot prove coverage.
        const auto expected_frames = static_cast<int64_t>(samples.size() / 160 + 1);
        if (frame_count != expected_frames || retained_start < 0 || retained_start > frame_count) {
            throw std::runtime_error("Incomplete Nemotron frame timeline");
        }

        // Use one upstream segmentation profile for the whole recording,
        // including any prefix finalized by the runtime's bounded stream.
        size_t count = 0;
        check(nemo_speech_diar_segments(stream.get(), nullptr, nullptr, 0, &count));
        std::vector<nemo_speech_diar_segment> segments(count);
        if (count) {
            check(nemo_speech_diar_segments(stream.get(), nullptr, segments.data(), count, &count));
        }
        const double duration = reader.duration_seconds();
        std::ofstream output(destination);
        if (!output) throw std::runtime_error("Could not open Nemotron output");
        output << std::setprecision(12) << "{\"duration\":" << duration
               << ",\"model\":\"nvidia/Nemotron-3-Diarization\",\"settings\":{"
                  "\"runtime_version\":\"" << nemo_speech_asr_version()
               << "\",\"gpu_device\":0,\"preset\":\"v3-offline\","
                  "\"quantization\":\"q8_0\",\"max_speakers\":8,"
                  "\"frame_seconds\":" << frame_seconds
               << ",\"frame_count\":" << frame_count
               << ",\"retained_frame_start\":" << retained_start
               << ",\"postprocessing\":\"upstream-callhome\","
                  "\"onset\":0.641,\"offset\":0.561,\"pad_onset\":0.229,"
                  "\"pad_offset\":0.079,\"min_duration_on\":0.511,\"min_duration_off\":0.296},"
                  "\"speaker_turns\":[";
        bool first = true;
        for (const auto& segment : segments) {
            if (!std::isfinite(segment.start_time) || !std::isfinite(segment.end_time) ||
                segment.start_time < 0 || segment.end_time <= segment.start_time ||
                segment.end_time > duration + .1 || segment.speaker < 1 || segment.speaker > speakers) {
                throw std::runtime_error("Invalid Nemotron speaker interval");
            }
            const double end = std::min(duration, segment.end_time);
            if (segment.start_time >= end) continue;
            if (!first) output << ',';
            first = false;
            output << "{\"start\":" << segment.start_time << ",\"end\":" << end
                   << ",\"speaker\":\"speaker_" << segment.speaker << "\"}";
        }
        output << "]}\n";
        if (!output) throw std::runtime_error("Could not write Nemotron output");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[ERROR] %s\n", error.what());
        return 1;
    }
}
