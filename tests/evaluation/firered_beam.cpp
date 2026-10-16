#include "firered_beam.hpp"
#include "token_beam.hpp"

#include <sherpa-onnx/csrc/offline-fire-red-asr-model.h>
#include <sherpa-onnx/csrc/offline-stream.h>
#include <sherpa-onnx/csrc/onnx-utils.h>
#include <sherpa-onnx/csrc/symbol-table.h>

#include <array>
#include <limits>
#include <utility>

static_assert(ORT_API_VERSION == 28, "Beam experiments require ONNX Runtime 1.28.2 headers");

namespace {

using Cache = std::pair<Ort::Value, Ort::Value>;

sherpa_onnx::OfflineModelConfig native_config(const SherpaOnnxOfflineRecognizerConfig& config) {
    sherpa_onnx::OfflineModelConfig model;
    model.num_threads = config.model_config.num_threads;
    model.provider = config.model_config.provider;
    model.debug = config.model_config.debug;
    model.fire_red_asr.encoder = config.model_config.fire_red_asr.encoder;
    model.fire_red_asr.decoder = config.model_config.fire_red_asr.decoder;
    return model;
}

}  // namespace

struct FireRedBeamRecognizer::Impl {
    sherpa_onnx::OfflineFireRedAsrModel model;
    sherpa_onnx::SymbolTable symbols;
    int width;

    Impl(const SherpaOnnxOfflineRecognizerConfig& config, int beam_size)
        : model(native_config(config)), symbols(config.model_config.tokens), width(beam_size) {
        if (width != 1 && width != 3) throw std::runtime_error("Unverified beam width");
    }

    std::string decode(const float* samples, int count) {
        sherpa_onnx::FeatureExtractorConfig features;
        features.normalize_samples = false;
        features.high_freq = 0;
        features.snip_edges = true;
        sherpa_onnx::OfflineStream stream(features);
        stream.AcceptWaveform(16000, samples, count);
        auto frames = stream.GetFrames();
        const auto& metadata = model.GetModelMetadata();
        const int dimension = stream.FeatureDim();
        if (metadata.mean.size() != static_cast<size_t>(dimension) ||
            metadata.inv_stddev.size() != metadata.mean.size() || frames.empty() ||
            frames.size() % dimension != 0)
            throw std::runtime_error("Invalid FireRed feature dimensions");
        for (size_t i = 0; i < frames.size(); ++i)
            frames[i] = (frames[i] - metadata.mean[i % dimension]) * metadata.inv_stddev[i % dimension];
        int64_t frame_count = frames.size() / dimension;
        auto memory = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeDefault);
        const std::array<int64_t, 3> shape{1, frame_count, dimension};
        const int64_t one = 1;
        auto input = Ort::Value::CreateTensor(memory, frames.data(), frames.size(), shape.data(), shape.size());
        auto length = Ort::Value::CreateTensor(memory, &frame_count, 1, &one, 1);
        auto cross = model.ForwardEncoder(std::move(input), std::move(length));
        const int budget = std::min(static_cast<int>(frame_count / 100.0 * 8), metadata.max_len / 2);
        if (budget <= 0) throw std::runtime_error("FireRed window has no decoder budget");

        TokenBeam beam(width, metadata.eos_id, width == 1 ? 1.0 : 1.25, width == 1 ? 0.0 : 0.6);
        std::vector<std::shared_ptr<Cache>> caches{
            std::make_shared<Cache>(model.GetInitialSelfKVCache(std::min(metadata.max_len, budget + 4)))};
        const std::array<int64_t, 2> token_shape{1, 1};
        for (int step = 0; step < budget && !beam.finished(); ++step) {
            const auto& hypotheses = beam.hypotheses();
            std::vector<std::vector<float>> logits(hypotheses.size());
            std::vector<std::shared_ptr<Cache>> updated(hypotheses.size());
            for (size_t i = 0; i < hypotheses.size(); ++i) {
                if (hypotheses[i].ended) continue;
                int64_t token = hypotheses[i].tokens.empty() ? metadata.sos_id : hypotheses[i].tokens.back();
                int64_t offset_value = step;
                auto tokens = Ort::Value::CreateTensor(memory, &token, 1, token_shape.data(), token_shape.size());
                auto offset = Ort::Value::CreateTensor(memory, &offset_value, 1, &one, 1);
                // Branches share read-only input caches; the runtime returns each new state.
                auto output = model.ForwardDecoder(std::move(tokens),
                    sherpa_onnx::View(&caches[i]->first), sherpa_onnx::View(&caches[i]->second),
                    sherpa_onnx::View(&cross.first), sherpa_onnx::View(&cross.second), std::move(offset));
                const auto& scores = std::get<0>(output);
                const auto score_shape = scores.GetTensorTypeAndShapeInfo().GetShape();
                if (score_shape.size() != 3 || score_shape[0] != 1 || score_shape[1] != 1 ||
                    score_shape[2] <= metadata.eos_id || score_shape[2] > std::numeric_limits<int>::max())
                    throw std::runtime_error("Invalid FireRed logits shape");
                const float* values = scores.GetTensorData<float>();
                logits[i].assign(values, values + score_shape[2]);
                updated[i] = std::make_shared<Cache>(std::move(std::get<1>(output)),
                                                    std::move(std::get<2>(output)));
            }
            beam.advance(logits);
            std::vector<std::shared_ptr<Cache>> selected;
            for (const auto& hypothesis : beam.hypotheses())
                selected.push_back(hypothesis.ended ? nullptr : updated[hypothesis.parent]);
            caches = std::move(selected);
        }
        sherpa_onnx::OfflineRecognitionResult result;
        for (int id : beam.best().tokens) {
            if (!symbols.Contains(id)) throw std::runtime_error("Unknown FireRed output token");
            const auto token = symbols[id];
            result.text += token;
            result.tokens.push_back(token);
        }
        return result.AsJsonString();
    }
};

FireRedBeamRecognizer::FireRedBeamRecognizer(const SherpaOnnxOfflineRecognizerConfig& config, int beam_size)
    : impl_(std::make_unique<Impl>(config, beam_size)) {}
FireRedBeamRecognizer::~FireRedBeamRecognizer() = default;
std::string FireRedBeamRecognizer::decode(const float* samples, int count) { return impl_->decode(samples, count); }
