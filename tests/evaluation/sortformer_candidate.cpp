#include "audio.hpp"
#include "meeting.hpp"
#include "sortformer_state.hpp"

#include <onnxruntime_cxx_api.h>
#include <Accelerate/Accelerate.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kMelFeatures = 128;
constexpr int kChunkCoreMelFrames = sortformer::kChunkFrames * 8;
constexpr int kChunkLeftMelFrames = 8;
constexpr int kChunkRightMelFrames = sortformer::kRightFrames * 8;
constexpr int kChunkMelFrames = kChunkCoreMelFrames + kChunkLeftMelFrames + kChunkRightMelFrames;
constexpr double kOutputFrameSeconds = 0.08;
constexpr float kOnsetThreshold = 0.5f;

struct ModelOutput {
    std::vector<float> predictions;
    std::vector<float> embeddings;
    int embedding_frames = 0;
};

class SortformerRuntime {
public:
    explicit SortformerRuntime(const std::filesystem::path& model)
        : env_(ORT_LOGGING_LEVEL_WARNING, "asr-tool-sortformer") {
        options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        options_.SetIntraOpNumThreads(2);
        options_.SetInterOpNumThreads(1);
        session_ = std::make_unique<Ort::Session>(env_, model.string().c_str(), options_);
        if (session_->GetInputCount() != 6 || session_->GetOutputCount() != 3) {
            throw std::runtime_error("Unexpected Sortformer model interface");
        }
    }

    ModelOutput run(const std::vector<float>& chunk, int chunk_length,
                    const sortformer::StreamingState& state) {
        if (chunk.size() != static_cast<size_t>(chunk_length) * kMelFeatures ||
            chunk_length <= 0 || chunk_length > kChunkMelFrames) {
            throw std::runtime_error("Invalid Sortformer feature chunk");
        }

        // This export concatenates physical tensors before applying its length
        // mask. Padding either cache would mask real chunk frames as padding.
        const int64_t chunk_length_value = chunk_length;
        const int64_t spkcache_length_value = state.spkcache_length();
        const int64_t fifo_length_value = state.fifo_length();
        const std::array<int64_t, 3> chunk_shape{1, chunk_length, kMelFeatures};
        const std::array<int64_t, 3> cache_shape{
            1, spkcache_length_value, sortformer::kEmbeddingDim};
        const std::array<int64_t, 3> fifo_shape{
            1, fifo_length_value, sortformer::kEmbeddingDim};
        const std::array<int64_t, 1> length_shape{1};

        std::vector<Ort::Value> inputs;
        inputs.reserve(6);
        inputs.push_back(Ort::Value::CreateTensor<float>(
            memory_info_, const_cast<float*>(chunk.data()), chunk.size(),
            chunk_shape.data(), chunk_shape.size()));
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(
            memory_info_, const_cast<int64_t*>(&chunk_length_value), 1,
            length_shape.data(), length_shape.size()));
        inputs.push_back(Ort::Value::CreateTensor<float>(
            memory_info_, const_cast<float*>(state.spkcache().data()), state.spkcache().size(),
            cache_shape.data(), cache_shape.size()));
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(
            memory_info_, const_cast<int64_t*>(&spkcache_length_value), 1,
            length_shape.data(), length_shape.size()));
        inputs.push_back(Ort::Value::CreateTensor<float>(
            memory_info_, const_cast<float*>(state.fifo().data()), state.fifo().size(),
            fifo_shape.data(), fifo_shape.size()));
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(
            memory_info_, const_cast<int64_t*>(&fifo_length_value), 1,
            length_shape.data(), length_shape.size()));

        static const char* input_names[] = {
            "chunk", "chunk_lengths", "spkcache", "spkcache_lengths", "fifo", "fifo_lengths"};
        static const char* output_names[] = {
            "spkcache_fifo_chunk_preds", "chunk_pre_encode_embs", "chunk_pre_encode_lengths"};
        auto outputs = session_->Run(Ort::RunOptions{nullptr}, input_names, inputs.data(),
                                     inputs.size(), output_names, 3);

        const auto pred_info = outputs[0].GetTensorTypeAndShapeInfo();
        const auto pred_shape = pred_info.GetShape();
        if (pred_shape.size() != 3 || pred_shape[0] != 1 || pred_shape[2] != sortformer::kNumSpeakers) {
            throw std::runtime_error("Unexpected Sortformer prediction shape");
        }
        const size_t pred_count = pred_info.GetElementCount();
        const float* pred_data = outputs[0].GetTensorData<float>();
        ModelOutput result;
        result.predictions.assign(pred_data, pred_data + pred_count);
        if (std::any_of(result.predictions.begin(), result.predictions.end(),
                        [](float value) { return !std::isfinite(value) || value < -0.01f || value > 1.01f; })) {
            throw std::runtime_error("Sortformer predictions are not probabilities");
        }

        const auto embedding_info = outputs[1].GetTensorTypeAndShapeInfo();
        const auto embedding_shape = embedding_info.GetShape();
        if (embedding_shape.size() != 3 || embedding_shape[0] != 1 ||
            embedding_shape[2] != sortformer::kEmbeddingDim) {
            throw std::runtime_error("Unexpected Sortformer embedding shape");
        }
        const int64_t* embedding_length = outputs[2].GetTensorData<int64_t>();
        if (*embedding_length <= 0 || *embedding_length > embedding_shape[1]) {
            throw std::runtime_error("Invalid Sortformer embedding length");
        }
        result.embedding_frames = static_cast<int>(*embedding_length);
        const float* embedding_data = outputs[1].GetTensorData<float>();
        result.embeddings.assign(
            embedding_data,
            embedding_data + static_cast<size_t>(result.embedding_frames) * sortformer::kEmbeddingDim);
        return result;
    }

private:
    Ort::Env env_;
    Ort::SessionOptions options_;
    std::unique_ptr<Ort::Session> session_;
    Ort::MemoryInfo memory_info_ = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
};

class SortformerMel {
public:
    explicit SortformerMel(size_t sample_count) : setup_(nullptr) {
        setup_ = vDSP_DFT_zop_CreateSetup(nullptr, kFftSize, vDSP_DFT_FORWARD);
        if (!setup_) throw std::runtime_error("Could not initialize Sortformer FFT");
        const size_t padded_count = sample_count + 2 * kFftSize / 2;
        padded_.assign(padded_count, 0.0f);
        real_in_.resize(kFftSize);
        imag_in_.assign(kFftSize, 0.0f);
        real_out_.resize(kFftSize);
        imag_out_.resize(kFftSize);
        power_.resize(kFftSize / 2 + 1);
        filterbank_.resize(kMelFeatures * (kFftSize / 2 + 1));
        build_filterbank();
    }

    ~SortformerMel() {
        if (setup_) vDSP_DFT_DestroySetup(setup_);
    }

    std::vector<float> compute(const std::vector<float>& samples) {
        if (samples.empty()) return {};
        const size_t pad = kFftSize / 2;
        padded_.assign(samples.size() + 2 * pad, 0.0f);
        padded_[pad] = samples.front();
        for (size_t i = 1; i < samples.size(); ++i) {
            padded_[pad + i] = samples[i] - 0.97f * samples[i - 1];
        }
        // NeMo masks the extra centered-STFT frame; it is not valid audio.
        const size_t frames = samples.size() / kHopSize;
        std::vector<float> output(frames * kMelFeatures, 0.0f);
        for (size_t frame = 0; frame < frames; ++frame) {
            std::fill(real_in_.begin(), real_in_.end(), 0.0f);
            std::fill(imag_in_.begin(), imag_in_.end(), 0.0f);
            const size_t begin = frame * kHopSize + (kFftSize - kWindowSize) / 2;
            for (size_t i = 0; i < kWindowSize && begin + i < padded_.size(); ++i) {
                const float phase = static_cast<float>(i) /
                    static_cast<float>(kWindowSize - 1);
                const float window = 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * phase);
                real_in_[i] = padded_[begin + i] * window;
            }
            vDSP_DFT_Execute(setup_, real_in_.data(), imag_in_.data(),
                             real_out_.data(), imag_out_.data());
            for (size_t bin = 0; bin <= kFftSize / 2; ++bin) {
                power_[bin] = real_out_[bin] * real_out_[bin] +
                    imag_out_[bin] * imag_out_[bin];
            }
            for (int mel = 0; mel < kMelFeatures; ++mel) {
                float energy = 0.0f;
                for (size_t bin = 0; bin <= kFftSize / 2; ++bin) {
                    energy += filterbank_[static_cast<size_t>(mel) * (kFftSize / 2 + 1) + bin] *
                        power_[bin];
                }
                output[frame * kMelFeatures + mel] = std::log(energy + std::pow(2.0f, -24.0f));
            }
        }
        return output;
    }

private:
    static constexpr size_t kFftSize = 512;
    static constexpr size_t kWindowSize = 400;
    static constexpr size_t kHopSize = 160;

    static double hz_to_mel(double hz) {
        // This is librosa's default Slaney scale, used by NeMo's
        // AudioToMelSpectrogramPreprocessor when htk=false.
        constexpr double linear_step = 200.0 / 3.0;
        constexpr double min_log_hz = 1000.0;
        constexpr double min_log_mel = min_log_hz / linear_step;
        const double logstep = std::log(6.4) / 27.0;
        return hz < min_log_hz ? hz / linear_step
                               : min_log_mel + std::log(hz / min_log_hz) / logstep;
    }

    static double mel_to_hz(double mel) {
        constexpr double linear_step = 200.0 / 3.0;
        constexpr double min_log_hz = 1000.0;
        constexpr double min_log_mel = min_log_hz / linear_step;
        const double logstep = std::log(6.4) / 27.0;
        return mel < min_log_mel ? mel * linear_step
                                 : min_log_hz * std::exp(logstep * (mel - min_log_mel));
    }

    void build_filterbank() {
        const double low_mel = hz_to_mel(0.0);
        const double high_mel = hz_to_mel(8000.0);
        std::array<double, kMelFeatures + 2> mel_frequencies{};
        for (size_t i = 0; i < mel_frequencies.size(); ++i) {
            mel_frequencies[i] = mel_to_hz(low_mel + (high_mel - low_mel) *
                static_cast<double>(i) / static_cast<double>(kMelFeatures + 1));
        }
        for (int mel = 0; mel < kMelFeatures; ++mel) {
            const double lower_width = mel_frequencies[mel + 1] - mel_frequencies[mel];
            const double upper_width = mel_frequencies[mel + 2] - mel_frequencies[mel + 1];
            const double area_scale = 2.0 /
                (mel_frequencies[mel + 2] - mel_frequencies[mel]);
            for (size_t bin = 0; bin <= kFftSize / 2; ++bin) {
                const double frequency = static_cast<double>(bin) * 16000.0 / kFftSize;
                const double lower = (frequency - mel_frequencies[mel]) / lower_width;
                const double upper = (mel_frequencies[mel + 2] - frequency) / upper_width;
                const double weight = std::max(0.0, std::min(lower, upper));
                filterbank_[static_cast<size_t>(mel) * (kFftSize / 2 + 1) + bin] =
                    static_cast<float>(weight * area_scale);
            }
        }
    }

    vDSP_DFT_Setup setup_;
    std::vector<float> padded_;
    std::vector<float> real_in_, imag_in_, real_out_, imag_out_, power_, filterbank_;
};

std::vector<SpeakerTurn> probability_turns(const std::vector<float>& probabilities,
                                           int frame_count, double duration_seconds) {
    std::vector<SpeakerTurn> turns;
    for (int speaker = 0; speaker < sortformer::kNumSpeakers; ++speaker) {
        std::vector<bool> active(frame_count, false);
        bool previous = false;
        for (int frame = 0; frame < frame_count; ++frame) {
            const float probability = probabilities[
                static_cast<size_t>(frame) * sortformer::kNumSpeakers + speaker];
            // NeMo's default onset and offset are both 0.5. Equal values keep
            // the preceding state, which is the hysteresis behavior used by
            // its binarization helper.
            if (probability > kOnsetThreshold) previous = true;
            else if (probability < kOnsetThreshold) previous = false;
            active[frame] = previous;
        }
        for (int frame = 0; frame < frame_count;) {
            if (!active[frame]) {
                ++frame;
                continue;
            }
            const int begin = frame;
            float confidence = 0.0f;
            while (frame < frame_count && active[frame]) {
                confidence += probabilities[static_cast<size_t>(frame) * sortformer::kNumSpeakers + speaker];
                ++frame;
            }
            const int end = frame;
            const double start_seconds = begin * kOutputFrameSeconds;
            const double end_seconds = std::min(duration_seconds, end * kOutputFrameSeconds);
            if (end_seconds > start_seconds) {
                turns.push_back({start_seconds * 1000.0, end_seconds * 1000.0, speaker,
                                 confidence / static_cast<float>(end - begin)});
            }
        }
    }
    std::sort(turns.begin(), turns.end(), [](const auto& left, const auto& right) {
        if (left.start_ms != right.start_ms) return left.start_ms < right.start_ms;
        return left.speaker < right.speaker;
    });
    return turns;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("Expected AUDIO -m MODEL_DIR -f json -o OUTPUT.json");
        std::filesystem::path model_directory, destination, mel_dump;
        for (int i = 2; i < argc; ++i) {
            const std::string option = argv[i];
            if (++i == argc) throw std::runtime_error("Missing value for " + option);
            if (option == "-m") model_directory = argv[i];
            else if (option == "-o") destination = argv[i];
            else if (option == "-d") mel_dump = argv[i];
            else if (option != "-f" || std::string(argv[i]) != "json")
                throw std::runtime_error("Unsupported Sortformer option: " + option);
        }
        if (model_directory.empty() || destination.empty()) {
            throw std::runtime_error("Missing Sortformer model/output directory");
        }
        const auto model = model_directory / "model.onnx";
        if (!std::filesystem::is_regular_file(model)) {
            throw std::runtime_error("Missing Sortformer model.onnx");
        }

        auto reader = open_audio(argv[1]);
        const auto samples = reader.read_all();
        if (samples.empty()) throw std::runtime_error("Empty Sortformer audio");

        const auto mel = SortformerMel(samples.size()).compute(samples);
        if (mel.empty() || mel.size() % kMelFeatures != 0) {
            throw std::runtime_error("Sortformer feature extraction failed");
        }
        if (!mel_dump.empty()) {
            std::ofstream dump(mel_dump, std::ios::binary);
            if (!dump) throw std::runtime_error("Could not open Sortformer Mel dump");
            dump.write(reinterpret_cast<const char*>(mel.data()),
                       static_cast<std::streamsize>(mel.size() * sizeof(float)));
            if (!dump) throw std::runtime_error("Could not write Sortformer Mel dump");
        }
        const int mel_frames = static_cast<int>(mel.size() / kMelFeatures);

        SortformerRuntime runtime(model);
        sortformer::StreamingState state;
        std::vector<float> probabilities;
        int start_frame = 0;
        while (start_frame < mel_frames) {
            const int left = std::min(start_frame, kChunkLeftMelFrames);
            const int end_frame = std::min(start_frame + kChunkCoreMelFrames, mel_frames);
            const int right = std::min(kChunkRightMelFrames, mel_frames - end_frame);
            const int chunk_start = start_frame - left;
            const int chunk_end = end_frame + right;
            const int actual_length = chunk_end - chunk_start;
            std::vector<float> chunk(
                mel.begin() + static_cast<size_t>(chunk_start) * kMelFeatures,
                mel.begin() + static_cast<size_t>(chunk_end) * kMelFeatures);

            const auto output = runtime.run(chunk, actual_length, state);
            if (output.embedding_frames != (actual_length + 7) / 8) {
                throw std::runtime_error("Unexpected Sortformer subsampling length");
            }
            std::vector<float> confirmed, tentative;
            state.update(output.embeddings, output.embedding_frames, output.predictions,
                         left / 8, (right + 7) / 8, confirmed, tentative);
            probabilities.insert(probabilities.end(), confirmed.begin(), confirmed.end());
            start_frame = end_frame;
        }
        const int frame_count = static_cast<int>(probabilities.size() / sortformer::kNumSpeakers);
        if (frame_count != (mel_frames + 7) / 8) {
            throw std::runtime_error("Sortformer predictions do not cover the complete feature sequence");
        }
        auto turns = probability_turns(probabilities, frame_count, reader.duration_seconds());

        std::ofstream output(destination);
        if (!output) throw std::runtime_error("Could not open Sortformer output");
        output << std::setprecision(12) << "{\"duration\":" << reader.duration_seconds()
               << ",\"settings\":{\"backend\":\"onnxruntime-cpu\",\"threads\":2,"
               << "\"runtime_version\":\"" << OrtGetApiBase()->GetVersionString() << "\","
                  "\"model\":\"nvidia/diar_streaming_sortformer_4spk-v2.1\","
                  "\"mel_features\":128,\"frame_seconds\":0.08,\"threshold\":0.5,"
               << "\"chunk_mel_frames\":" << kChunkMelFrames
               << ",\"core_output_frames\":" << sortformer::kChunkFrames
               << ",\"right_output_frames\":" << sortformer::kRightFrames
               << ",\"spkcache\":" << sortformer::kSpkcacheCapacity
               << ",\"fifo\":" << sortformer::kFifoCapacity
               << ",\"fifo_update_period\":" << sortformer::kSpkcacheUpdatePeriod
               << ",\"valid_mel_frames\":" << mel_frames
               << ",\"output_frames\":" << frame_count
               << ",\"mel_norm\":\"slaney\"},"
                  "\"speaker_turns\":[";
        for (size_t i = 0; i < turns.size(); ++i) {
            if (i) output << ',';
            const auto& turn = turns[i];
            output << "{\"start\":" << turn.start_ms / 1000.0
                   << ",\"end\":" << turn.end_ms / 1000.0
                   << ",\"confidence\":" << turn.confidence << ",\"speaker\":\"speaker_"
                   << turn.speaker + 1 << "\"}";
        }
        output << "]}\n";
        if (!output) throw std::runtime_error("Could not write Sortformer output");
        return 0;
    } catch (const std::exception& error) {
        (void)std::fprintf(stderr, "[ERROR] %s\n", error.what());
        return 1;
    }
}
