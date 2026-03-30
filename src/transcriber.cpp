#include "transcriber.hpp"

#include "audio.hpp"
#include "platform.hpp"

#include <whisper.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

struct Transcriber::Impl {
    whisper_context* ctx = nullptr;
    std::string language;
    std::string prompt;
    Backend backend;

    ~Impl() {
        if (ctx) {
            whisper_free(ctx);
            ctx = nullptr;
        }
    }
};

Transcriber::Transcriber(const std::string& model_path,
                         const std::string& language,
                         const std::string& prompt,
                         Backend backend)
    : impl_(new Impl), ready_(false) {
    impl_->language = language;
    impl_->prompt = prompt;
    impl_->backend = backend;

    // 加载模型
    auto params = whisper_context_default_params();

#if defined(__APPLE__)
    params.use_gpu = (backend == Backend::CoreML);
#elif defined(__linux__) || defined(_WIN32)
    params.use_gpu = (backend == Backend::CUDA);
#endif

    (void)printf("[INFO] 加载模型: %s\n", model_path.c_str());

    impl_->ctx = whisper_init_from_file_with_params(model_path.c_str(), params);
    if (!impl_->ctx) {
        error_message_ = "无法加载模型: " + model_path;
        (void)fprintf(stderr, "[ERROR] %s\n", error_message_.c_str());
        return;
    }

    const char* type_str = whisper_model_type_readable(impl_->ctx);
    (void)printf("[INFO] 模型类型: %s\n", type_str ? type_str : "unknown");

    ready_ = true;
}

Transcriber::~Transcriber() = default;

TranscriptionResult Transcriber::transcribe(const std::filesystem::path& audio_path) {
    TranscriptionResult result;
    result.model_name = "ggml-large-v3-turbo";

    if (!ready_) {
        (void)fprintf(stderr, "[ERROR] 转写器未就绪\n");
        return result;
    }

    // 打开音频文件（流式分块读取）
    std::unique_ptr<AudioReader> reader_ptr;
    try {
        reader_ptr = std::make_unique<AudioReader>(open_audio(audio_path));
    } catch (const std::exception& e) {
        (void)fprintf(stderr, "[ERROR] 打开音频文件失败: %s\n", e.what());
        return result;
    }
    AudioReader& reader = *reader_ptr;

    (void)printf("[INFO] 开始转写: %s\n", audio_path.filename().string().c_str());

    const double total_duration = reader.duration_seconds();
    int chunk_index = 0;

    // 逐块处理
    while (!reader.is_eof()) {
        AudioChunk chunk = reader.read_chunk();
        if (chunk.samples.empty()) {
            break;
        }

        if (!chunk.samples.empty()) {
            // 构建 whisper 参数
            auto params =
                whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

            params.language = impl_->language.c_str();
            params.n_threads =
                static_cast<int32_t>(std::thread::hardware_concurrency());
            if (params.n_threads <= 0) {
                params.n_threads = 4;
            }

            // Prompt
            if (!impl_->prompt.empty()) {
                params.initial_prompt = impl_->prompt.c_str();
            }

            // 禁用不需要的输出
            params.print_special = false;
            params.print_progress = false;
            params.print_realtime = false;
            params.print_timestamps = false;

            // 执行推理
            int ret = whisper_full(
                impl_->ctx, params, chunk.samples.data(),
                static_cast<int>(chunk.samples.size()));

            if (ret != 0) {
                (void)fprintf(stderr, "[WARN] 片段 %d 转写失败，跳过\n",
                               chunk_index);
                ++chunk_index;
                continue;
            }

            // 提取结果，时间戳加上偏移量
            int n_segments = whisper_full_n_segments(impl_->ctx);
            for (int i = 0; i < n_segments; ++i) {
                const char* text = whisper_full_get_segment_text(impl_->ctx, i);
                int64_t t0 = whisper_full_get_segment_t0(impl_->ctx, i);
                int64_t t1 = whisper_full_get_segment_t1(impl_->ctx, i);

                // whisper 内部时间单位是 10ms
                double start_ms = chunk.offset_ms + t0 * 10.0;
                double end_ms = chunk.offset_ms + t1 * 10.0;

                std::string segment_text = text ? text : "";
                result.full_text += segment_text;
                result.segments.push_back({segment_text, start_ms, end_ms});
            }

            const double progress =
                (total_duration > 0.0)
                    ? std::min(100.0, reader.processed_seconds() / total_duration * 100.0)
                    : 0.0;
            (void)printf("[INFO] 片段 %d 完成 (%.0f%%)\n", chunk_index, progress);
        }

        ++chunk_index;
    }

    // 检测语言
    int lang_id = whisper_full_lang_id(impl_->ctx);
    const char* lang_str = whisper_lang_str(lang_id);
    if (lang_str) {
        result.language = lang_str;
    }

    result.duration_seconds =
        (total_duration > 0.0) ? total_duration : reader.processed_seconds();

    return result;
}
