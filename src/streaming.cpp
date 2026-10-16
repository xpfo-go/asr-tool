#include "streaming.hpp"

#include <sherpa-onnx/c-api/c-api.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace {

std::string hotwords(std::string prompt) {
    if (prompt.size() > 4096) throw std::invalid_argument("Terminology prompt exceeds 4096 bytes");
    for (const std::string delimiter : {",", ";", "\xe3\x80\x81", "\xef\xbc\x8c", "\xef\xbc\x9b"}) {
        size_t offset = 0;
        while ((offset = prompt.find(delimiter, offset)) != std::string::npos) {
            prompt.replace(offset, delimiter.size(), "\n");
            ++offset;
        }
    }
    return prompt;
}

}  // namespace

struct StreamingTranscriber::Impl {
    using Recognizer = std::unique_ptr<const SherpaOnnxOnlineRecognizer,
                                      decltype(&SherpaOnnxDestroyOnlineRecognizer)>;
    using Stream = std::unique_ptr<const SherpaOnnxOnlineStream, decltype(&SherpaOnnxDestroyOnlineStream)>;
    Recognizer recognizer{nullptr, SherpaOnnxDestroyOnlineRecognizer};
    Stream stream{nullptr, SherpaOnnxDestroyOnlineStream};
    LiveReviewQueue& review;
    LiveReviewQueue::Sink sink;
    std::vector<float> audio;
    std::string text;
    uint64_t id = 1;
    uint32_t revision = 0;
    int64_t start = 0;
    bool finished = false;
    bool active = false;
    std::exception_ptr failure;

    Impl(LiveReviewQueue& queue, LiveReviewQueue::Sink output)
        : review(queue), sink(std::move(output)) {}

    void open() {
        stream.reset(SherpaOnnxCreateOnlineStream(recognizer.get()));
        if (!stream) throw std::runtime_error("Could not create local streaming recognizer state");
    }

    void decode() {
        while (SherpaOnnxIsOnlineStreamReady(recognizer.get(), stream.get())) {
            SherpaOnnxDecodeOnlineStream(recognizer.get(), stream.get());
        }
    }

    void publish(bool endpoint = false) {
        using Result = std::unique_ptr<const SherpaOnnxOnlineRecognizerResult,
                                       decltype(&SherpaOnnxDestroyOnlineRecognizerResult)>;
        Result result(SherpaOnnxGetOnlineStreamResult(recognizer.get(), stream.get()),
                      SherpaOnnxDestroyOnlineRecognizerResult);
        if (!result || !result->text) throw std::runtime_error("Missing streaming ASR result");
        if (text != result->text || endpoint) {
            text = result->text;
            sink({id, ++revision, LiveEventKind::Provisional, start,
                  start + static_cast<int64_t>(audio.size()), text, {}, {},
                  endpoint ? "endpoint" : "streaming", false});
        }
    }

    void complete() {
        if (audio.empty()) return;
        // Flush lookahead before closing this utterance. Padding is excluded
        // from the review audio and the public clock, so real input has one owner.
        const std::array<float, LIVE_SAMPLE_RATE> padding{};
        SherpaOnnxOnlineStreamAcceptWaveform(stream.get(), LIVE_SAMPLE_RATE, padding.data(), padding.size());
        SherpaOnnxOnlineStreamInputFinished(stream.get());
        decode();
        publish(true);
        const int64_t end = start + static_cast<int64_t>(audio.size());
        review.submit({id, revision, start, end, text, std::move(audio)});
        stream.reset();
        audio.clear();
        text.clear();
        start = end;
        ++id;
        revision = 0;
    }
};

StreamingTranscriber::StreamingTranscriber(const std::filesystem::path& model_directory,
                                         const std::string& prompt, LiveReviewQueue& review,
                                         LiveReviewQueue::Sink sink)
    : impl_(std::make_unique<Impl>(review, std::move(sink))) {
    if (!impl_->sink) throw std::invalid_argument("Missing streaming event sink");
    const auto encoder = (model_directory / "encoder-epoch-99-avg-1.int8.onnx").string();
    const auto decoder = (model_directory / "decoder-epoch-99-avg-1.onnx").string();
    const auto joiner = (model_directory / "joiner-epoch-99-avg-1.int8.onnx").string();
    const auto tokens = (model_directory / "tokens.txt").string();
    const auto vocabulary = (model_directory / "bpe.vocab").string();
    for (const auto& path : {encoder, decoder, joiner, tokens, vocabulary}) {
        if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("Missing local model file: " + path);
    }
    const auto terminology = hotwords(prompt);
    SherpaOnnxOnlineRecognizerConfig config{};
    config.feat_config.sample_rate = LIVE_SAMPLE_RATE;
    config.feat_config.feature_dim = 80;
    config.model_config.transducer.encoder = encoder.c_str();
    config.model_config.transducer.decoder = decoder.c_str();
    config.model_config.transducer.joiner = joiner.c_str();
    config.model_config.tokens = tokens.c_str();
    config.model_config.modeling_unit = "cjkchar+bpe";
    config.model_config.bpe_vocab = vocabulary.c_str();
    config.model_config.provider = "cpu";
    config.model_config.num_threads = 2;
    config.decoding_method = "modified_beam_search";
    config.max_active_paths = 4;
    config.hotwords_buf = terminology.c_str();
    config.hotwords_buf_size = static_cast<int32_t>(terminology.size());
    config.hotwords_score = 1.5f;
    config.enable_endpoint = 1;
    config.rule1_min_trailing_silence = 2.4f;
    config.rule2_min_trailing_silence = 1.6f;
    config.rule3_min_utterance_length = 10.0f;
    impl_->recognizer.reset(SherpaOnnxCreateOnlineRecognizer(&config));
    if (!impl_->recognizer) throw std::runtime_error("Could not initialize local streaming recognizer");
}

StreamingTranscriber::~StreamingTranscriber() = default;

void StreamingTranscriber::push(const float* samples, size_t count) {
    if (impl_->failure) std::rethrow_exception(impl_->failure);
    if (impl_->active) throw std::logic_error("Streaming callbacks must not reenter the transcriber");
    if (impl_->finished) throw std::logic_error("Streaming audio is already closed");
    if (!samples && count) throw std::invalid_argument("Missing streaming PCM");
    impl_->active = true;
    try {
    constexpr size_t chunk = 2560;
    constexpr size_t maximum = 20 * LIVE_SAMPLE_RATE;
    for (size_t offset = 0; offset < count;) {
        if (!impl_->stream) impl_->open();
        const size_t size = std::min({chunk, count - offset, maximum - impl_->audio.size()});
        impl_->audio.insert(impl_->audio.end(), samples + offset, samples + offset + size);
        SherpaOnnxOnlineStreamAcceptWaveform(impl_->stream.get(), LIVE_SAMPLE_RATE, samples + offset,
                                             static_cast<int32_t>(size));
        impl_->decode();
        impl_->publish();
        if (impl_->audio.size() == maximum ||
            SherpaOnnxOnlineStreamIsEndpoint(impl_->recognizer.get(), impl_->stream.get())) {
            impl_->complete();
        }
        offset += size;
    }
    impl_->active = false;
    } catch (...) {
        impl_->active = false;
        impl_->failure = std::current_exception();
        throw;
    }
}

void StreamingTranscriber::finish() {
    if (impl_->failure) std::rethrow_exception(impl_->failure);
    if (impl_->active) throw std::logic_error("Streaming callbacks must not reenter the transcriber");
    if (impl_->finished) return;
    impl_->active = true;
    try {
        impl_->complete();
        impl_->finished = true;
        impl_->active = false;
    } catch (...) {
        impl_->active = false;
        impl_->failure = std::current_exception();
        throw;
    }
}
