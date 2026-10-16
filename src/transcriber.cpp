#include "transcriber.hpp"

#include "audio.hpp"
#include "diarizer.hpp"
#include "meeting.hpp"

#include <whisper.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <thread>

namespace {

constexpr double SAMPLE_RATE = 16000.0;
#ifdef ASR_EXPERIMENT_VAD_THRESHOLD
constexpr float VAD_THRESHOLD = ASR_EXPERIMENT_VAD_THRESHOLD;
#else
constexpr float VAD_THRESHOLD = 0.5f;
#endif

struct DecodeResult {
    std::vector<TimedText> tokens;
    std::string text;
    std::string language;
    double confidence = 0.0;
    double repetition = 0.0;
    bool repetitive = false;
    bool success = false;
    int confident_language = -1;
};

std::vector<whisper_token> tokenize(whisper_context* ctx, const std::string& text) {
    if (text.empty()) {
        return {};
    }
    std::vector<whisper_token> tokens(512);
    int n = whisper_tokenize(ctx, text.c_str(), tokens.data(), static_cast<int>(tokens.size()));
    if (n < 0) {
        tokens.resize(-n);
        n = whisper_tokenize(ctx, text.c_str(), tokens.data(), static_cast<int>(tokens.size()));
    }
    if (n < 0) {
        return {};
    }
    tokens.resize(n);
    return tokens;
}

DecodeResult decode(whisper_context* ctx, const float* samples, int count,
                    const std::string& language, const std::vector<whisper_token>& prompt,
                    const DecodingSettings& settings, int beam_size, int previous_language = -1) {
    DecodeResult result;
    auto params = whisper_full_default_params(beam_size > 0 ? WHISPER_SAMPLING_BEAM_SEARCH
                                                           : WHISPER_SAMPLING_GREEDY);
    params.language = language.c_str();
    params.n_threads = settings.threads;
    std::string selected_language = language;
    if (language == "auto") {
        if (whisper_pcm_to_mel(ctx, samples, count, params.n_threads) != 0) return result;
        std::vector<float> probabilities(whisper_lang_max_id() + 1, 0.0f);
        const int detected = whisper_lang_auto_detect(ctx, 0, params.n_threads, probabilities.data());
        if (detected < 0) return result;
        const int selected = choose_language(probabilities, previous_language);
        selected_language = whisper_lang_str(selected);
        if (probabilities[detected] >= .8f) result.confident_language = detected;
        params.language = selected_language.c_str();
    }
    params.no_context = true;
    params.translate = false;
    params.suppress_nst = true;
    params.print_special = false;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.token_timestamps = true;
    params.prompt_tokens = prompt.data();
    params.prompt_n_tokens = static_cast<int>(prompt.size());
    params.beam_search.beam_size = beam_size > 0 ? beam_size : 5;
    if (whisper_full(ctx, params, samples, count) != 0) {
        return result;
    }
    const char* detected = whisper_lang_str(whisper_full_lang_id(ctx));
    result.language = detected ? detected : language;
    double logprob = 0.0;
    std::vector<whisper_token> ids;
    for (int i = 0; i < whisper_full_n_segments(ctx); ++i) {
        const double start = whisper_full_get_segment_t0(ctx, i) * 10.0;
        const double end = std::min(whisper_full_get_segment_t1(ctx, i) * 10.0,
                                    count / SAMPLE_RATE * 1000.0);
        const int n = whisper_full_n_tokens(ctx, i);
        double previous = start;
        std::vector<TimedText> segment_tokens;
        std::vector<double> acoustic_times;
        for (int j = 0; j < n; ++j) {
            const auto data = whisper_full_get_token_data(ctx, i, j);
            if (data.id >= whisper_token_eot(ctx)) {
                continue;
            }
            const char* raw = whisper_full_get_token_text(ctx, i, j);
            if (!raw || !raw[0]) {
                continue;
            }
            const double t0 = data.t0 >= 0 ? data.t0 * 10.0 : start;
            const double t1 = data.t1 >= 0 ? data.t1 * 10.0 : end;
            const double token_start = std::clamp(t0, previous, std::max(previous, end));
            const double token_end = std::clamp(t1, token_start, std::max(token_start, end));
            previous = token_start;
            segment_tokens.push_back({raw, token_start, token_end, i});
            acoustic_times.push_back(data.t_dtw * 10.0);
            result.text += raw;
            ids.push_back(data.id);
            logprob += std::log(std::max(1e-8, static_cast<double>(data.p)));
        }
        const double duration = count / SAMPLE_RATE * 1000.0;
        align_token_timestamps(segment_tokens, acoustic_times, end, duration);
        result.tokens.insert(result.tokens.end(), segment_tokens.begin(), segment_tokens.end());
    }
    repair_token_timestamps(result.tokens, count / SAMPLE_RATE * 1000.0);
    if (!ids.empty()) {
        result.confidence = std::exp(logprob / ids.size());
    }
    result.repetition = repetition_ratio(ids);
    result.repetitive = ids.size() >= 16 && result.repetition > 0.5;
    result.success = true;
    return result;
}

}  // namespace

struct Transcriber::Impl {
    whisper_context* ctx = nullptr;
    std::string language;
    std::string prompt;
    std::filesystem::path model_path;
    bool speakers = true;
    DecodingSettings decoding_settings;

    ~Impl() {
        if (ctx) {
            whisper_free(ctx);
        }
    }
};

Transcriber::Transcriber(const std::string& model_path, const std::string& language,
                         const std::string& prompt, Backend backend, bool speakers)
    : impl_(std::make_unique<Impl>()) {
    impl_->language = language;
    impl_->prompt = prompt;
    impl_->model_path = model_path;
    impl_->speakers = speakers;
    impl_->decoding_settings.threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 8);
    impl_->decoding_settings.acoustic_timestamps = speakers;
    impl_->decoding_settings.requested_language = language;
    impl_->decoding_settings.vad_threshold = VAD_THRESHOLD;
#ifdef ASR_EXPERIMENT_NO_HISTORY
    impl_->decoding_settings.history_prompt = false;
#endif
    impl_->decoding_settings.backend = backend == Backend::CPU ? "cpu" :
                                      backend == Backend::CUDA ? "cuda" : "coreml+metal";
    auto params = whisper_context_default_params();
    params.use_gpu = backend != Backend::CPU;
    if (speakers) {
        params.flash_attn = false;
        params.dtw_token_timestamps = true;
        params.dtw_aheads_preset = impl_->model_path.filename().string().find("turbo") != std::string::npos
            ? WHISPER_AHEADS_LARGE_V3_TURBO : WHISPER_AHEADS_LARGE_V3;
    }
    impl_->ctx = whisper_init_from_file_with_params(model_path.c_str(), params);
    if (!impl_->ctx) {
        error_message_ = "无法加载模型: " + model_path;
        return;
    }
    ready_ = true;
}

Transcriber::~Transcriber() = default;

TranscriptionResult Transcriber::transcribe(const std::filesystem::path& audio_path) {
    try {
        auto reader = open_audio(audio_path);
        return transcribe_samples(reader.read_all());
    } catch (const std::exception& e) {
        error_message_ = e.what();
        return {};
    }
}

TranscriptionResult Transcriber::transcribe_samples(const std::vector<float>& samples) {
    TranscriptionResult result;
    result.model_name = impl_->model_path.stem().string();
    result.decoding_settings = impl_->decoding_settings;
    result.duration_seconds = samples.size() / SAMPLE_RATE;
    if (!ready_) {
        return result;
    }
    const auto model_dir = impl_->model_path.parent_path();
    auto vad_params = whisper_vad_default_context_params();
    vad_params.use_gpu = false;
    vad_params.n_threads = 2;
    using Vad = std::unique_ptr<whisper_vad_context, decltype(&whisper_vad_free)>;
    Vad vad(whisper_vad_init_from_file_with_params(
                (model_dir / "ggml-silero-v6.2.0.bin").string().c_str(), vad_params),
            whisper_vad_free);
    if (!vad) {
        error_message_ = "本地 VAD 模型缺失或损坏，请运行 asr-tool --prepare-models";
        return result;
    }
    auto speech_params = whisper_vad_default_params();
    speech_params.threshold = impl_->decoding_settings.vad_threshold;
    speech_params.min_speech_duration_ms = 200;
    speech_params.min_silence_duration_ms = 300;
    speech_params.speech_pad_ms = 200;
    using Speech = std::unique_ptr<whisper_vad_segments, decltype(&whisper_vad_free_segments)>;
    Speech speech(whisper_vad_segments_from_samples(vad.get(), speech_params, samples.data(),
                                                   static_cast<int>(samples.size())),
                  whisper_vad_free_segments);
    if (!speech) {
        error_message_ = "语音活动检测失败";
        return result;
    }
    std::vector<TimeRange> spans;
    for (int i = 0; i < whisper_vad_segments_n_segments(speech.get()); ++i) {
        spans.push_back({whisper_vad_segments_get_segment_t0(speech.get(), i) * 10.0,
                         whisper_vad_segments_get_segment_t1(speech.get(), i) * 10.0});
    }
    const auto windows = plan_speech_windows(spans, result.duration_seconds * 1000.0);
    if (windows.empty()) {
        result.success = true;
        result.warnings.push_back("未检测到语音");
        return result;
    }
    std::vector<SpeakerTurn> turns;
    if (impl_->speakers) {
        std::string error;
        turns = diarize(samples, model_dir, error, result.speaker_model);
        result.speaker_turns = turns;
        result.speakers_available = !turns.empty();
        if (!error.empty()) {
            result.warnings.push_back(error);
            (void)fprintf(stderr, "[WARN] %s\n", error.c_str());
        }
    }
    result.language = impl_->language;
    (void)fprintf(stderr, "[INFO] 转写语言: %s（保留原语言）\n", impl_->language.c_str());
    auto user_prompt = tokenize(impl_->ctx, impl_->prompt);
    if (user_prompt.size() > 160) {
        user_prompt.erase(user_prompt.begin(), user_prompt.end() - 160);
        result.warnings.push_back("prompt 超过 160 tokens，仅使用末尾术语；请缩短提示文本");
    }
    std::vector<whisper_token> history;
    double previous_end = -10000.0;
    int previous_language = -1;
    for (size_t i = 0; i < windows.size(); ++i) {
        const auto& window = windows[i];
        if (window.audio.start_ms - previous_end > 5000.0) {
            history.clear();
        }
        auto prompt = history;
        prompt.insert(prompt.end(), user_prompt.begin(), user_prompt.end());
        const size_t begin = static_cast<size_t>(window.audio.start_ms * SAMPLE_RATE / 1000.0);
        const size_t end = std::min(samples.size(), static_cast<size_t>(window.audio.end_ms * SAMPLE_RATE / 1000.0));
        const auto& settings = impl_->decoding_settings;
        auto decoded = decode(impl_->ctx, samples.data() + begin, static_cast<int>(end - begin),
                              impl_->language, prompt, impl_->decoding_settings,
                              0, previous_language);
        const int confident_language = decoded.confident_language;
        bool review = !decoded.success || decoded.confidence < settings.review_threshold || decoded.repetitive;
        bool correction_rejected = false;
        if (review) {
            const auto retry = decode(impl_->ctx, samples.data() + begin, static_cast<int>(end - begin),
                                      decoded.success ? decoded.language : impl_->language, prompt,
                                      settings, settings.retry_beam_size);
            const bool safe = correction_is_conservative(decoded.text, retry.text);
            if (retry.success && (!decoded.success ||
                (safe && !retry.repetitive && retry.repetition <= decoded.repetition &&
                 retry.confidence > decoded.confidence + settings.correction_confidence_gain))) {
                if (retry.text != decoded.text && decoded.success) {
                    result.corrections.push_back({window.audio.start_ms, window.audio.end_ms,
                                                   decoded.text, retry.text, "local_beam_redecode"});
                }
                decoded = retry;
            }
            review = decoded.confidence < settings.review_threshold || decoded.repetitive || !safe;
            correction_rejected = retry.success && !safe;
        }
        if (!decoded.success) {
            error_message_ = "片段转写失败，未生成完整结果: " + std::to_string(window.audio.start_ms / 1000.0) + "s";
            return result;
        }
        if (!review && confident_language >= 0) previous_language = confident_language;
        const size_t accepted_begin = result.full_text.size();
        std::vector<std::string> reasons;
        if (decoded.confidence < settings.review_threshold) reasons.push_back("low_asr_confidence");
        if (decoded.repetitive) reasons.push_back("repetition");
        if (correction_rejected) reasons.push_back("correction_rejected");
        append_decoded_window(result, decoded.tokens, window, turns, decoded.confidence,
                               reasons);
        history = review || !settings.history_prompt ? std::vector<whisper_token>{}
                         : tokenize(impl_->ctx, result.full_text.substr(accepted_begin));
        if (i == 0) result.language = decoded.language;
        else if (result.language != decoded.language) result.language = "mixed";
        if (history.size() > 64) {
            history.erase(history.begin(), history.end() - 64);
        }
        previous_end = window.audio.end_ms;
        (void)fprintf(stderr, "[INFO] 转写: %zu/%zu (%.0f%%)%s\n", i + 1, windows.size(),
                      window.ownership.end_ms / (result.duration_seconds * 1000.0) * 100.0,
                      review ? " [需复核]" : "");
    }
    result.success = true;
    return result;
}
