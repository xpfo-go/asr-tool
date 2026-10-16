#include "audio.hpp"
#include "live_capture.hpp"
#include "model_manager.hpp"
#include "output.hpp"
#include "platform.hpp"
#include "streaming.hpp"
#include "transcriber.hpp"
#include "types.hpp"

#include <whisper.h>

#include <cstdio>
#include <algorithm>
#include <atomic>
#include <csignal>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifndef ASR_VERSION
#define ASR_VERSION "unknown"
#endif

namespace {

volatile std::sig_atomic_t live_stop_requested = 0;

void request_live_stop(int) {
    live_stop_requested = 1;
}

void print_help(const char* prog) {
    (void)fprintf(stderr,
                  "用法: %s [选项] <输入文件>\n"
                  "       %s --live [选项]\n\n"
                  "将音频/视频文件转写为文字，或从本机麦克风实时转写\n\n"
                  "常用: asr-tool meeting.mp4 -p \"项目名、术语\"\n"
                  "      asr-tool meeting.mp4 -o meeting.json\n\n"
                  "参数:\n"
                  "  <输入文件>                音频/视频文件路径\n\n"
                  "选项:\n"
                  "  -o, --output <路径>      输出路径 (默认: 输入文件旁的同名 .txt)\n"
                  "  -l, --language <语言>     源语言，如 zh/en/ja/auto (默认: auto)\n"
                  "  -p, --prompt <文本>      行业术语、姓名等识别提示\n"
                  "  -m, --model-dir <路径>   指定 .bin 模型目录（默认: ~/.cache/whisper）\n"
                  "  -f, --format <格式>      text/json/srt/vtt (默认按 -o 扩展名选择)\n"
                  "  --live                   使用默认麦克风实时转写，输出 JSONL\n"
                  "  -v, --verbose             显示详细日志\n"
                  "  --no-speakers             仅转写，跳过说话人分析\n"
                  "  --prepare-models          下载本地模型（仅此命令联网）\n"
                  "  -h, --help                显示帮助\n"
                  "  --version                 显示版本\n",
                  prog, prog);
}

bool parse_format(const std::string& s, OutputFormat* out) {
    if (s == "text" || s == "txt") {
        *out = OutputFormat::Text;
        return true;
    }
    if (s == "json") {
        *out = OutputFormat::Json;
        return true;
    }
    if (s == "srt") {
        *out = OutputFormat::Srt;
        return true;
    }
    if (s == "vtt") {
        *out = OutputFormat::Vtt;
        return true;
    }
    return false;
}

int run_live(const std::string& output_path, const std::string& language,
             const std::string& prompt, const std::string& model_dir,
             bool verbose) {
#if !defined(__APPLE__)
    (void)output_path;
    (void)language;
    (void)prompt;
    (void)model_dir;
    (void)verbose;
    (void)fprintf(stderr, "[ERROR] --live 当前仅支持 macOS\n");
    return 1;
#else
    try {
    if (verbose) (void)fprintf(stderr, "[INFO] verbose 日志已启用\n");
    const Backend backend = detect_backend();
    const std::filesystem::path model_path =
        model_dir.empty() ? get_default_model_path() :
                            get_default_model_path(std::filesystem::path(model_dir));
    if (!ensure_main_model(model_path)) return 3;
    if (!ensure_coreml_encoder_model(model_path)) {
        (void)fprintf(stderr, "[INFO] CoreML 编码器不可用，继续使用 Metal\n");
    }

    Transcriber reviewer(model_path.string(), language, prompt, backend, false);
    if (!reviewer.is_ready()) {
        (void)fprintf(stderr, "[ERROR] 复核模型初始化失败: %s\n",
                      reviewer.error_message().c_str());
        return 3;
    }

    std::unique_ptr<std::ofstream> file;
    std::ostream* output = &std::cout;
    if (!output_path.empty()) {
        file = std::make_unique<std::ofstream>(output_path);
        if (!file->is_open()) {
            (void)fprintf(stderr, "[ERROR] 无法写入输出文件: %s\n", output_path.c_str());
            return 4;
        }
        output = file.get();
    }
    std::mutex output_mutex;
    std::atomic<int64_t> received_samples{0};
    const auto epoch = std::chrono::steady_clock::now();
    auto emit = [&](const LiveEvent& original) {
        std::lock_guard<std::mutex> lock(output_mutex);
        LiveEvent event = original;
        event.emitted_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - epoch).count();
        event.received_samples = received_samples.load();
        write_live_event(*output, event);
        output->flush();
        if (!output->good()) throw std::runtime_error("实时 JSONL 输出失败");
    };
    {
        std::lock_guard<std::mutex> lock(output_mutex);
        *output << "{\"type\":\"run_start\",\"mode\":\"microphone\","
                   "\"sample_rate\":16000,\"review_limit_seconds\":20,"
                   "\"stream_model\":\""
                << STREAMING_MODEL_DIRECTORY << "\"}\n";
        output->flush();
    }

    auto review = [&](const std::vector<float>& audio) {
        const TranscriptionResult result = reviewer.transcribe_samples(audio);
        if (!result.success) throw std::runtime_error(reviewer.error_message());
        bool reliable = true;
        bool broad = true;
        for (const auto& segment : result.segments) {
            for (const auto& reason : segment.review_reasons) {
                if (reason == "low_asr_confidence" || reason == "repetition") reliable = false;
            }
            broad = broad && segment.confidence >= 0.65;
        }
        return LiveReviewResult{result.full_text, reliable,
                                reliable ? "" : "low_asr_confidence", reliable && broad};
    };
    LiveReviewQueue review_queue(review, emit);
    StreamingTranscriber streaming(model_path.parent_path() / STREAMING_MODEL_DIRECTORY,
                                   prompt, review_queue, emit);
    LiveCapture capture([&](const std::vector<float>& samples) {
        received_samples.fetch_add(static_cast<int64_t>(samples.size()));
        streaming.push(samples.data(), samples.size());
    });

    live_stop_requested = 0;
    std::signal(SIGINT, request_live_stop);
    if (!capture.start()) {
        (void)fprintf(stderr, "[ERROR] 麦克风启动失败: %s\n",
                      capture.error_message().c_str());
        return 4;
    }
    (void)fprintf(stderr, "[INFO] 正在实时转写，按 Ctrl-C 结束\n");
    while (!live_stop_requested && !capture.failed()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    capture.stop();
    bool failed = capture.failed();
    if (failed) {
        (void)fprintf(stderr, "[ERROR] 实时采集失败: %s\n",
                      capture.error_message().c_str());
    }
    try {
        streaming.finish();
    } catch (const std::exception& error) {
        (void)fprintf(stderr, "[ERROR] 实时转写失败: %s\n", error.what());
        failed = true;
    }
    try {
        review_queue.finish();
    } catch (const std::exception& error) {
        (void)fprintf(stderr, "[ERROR] 实时复核失败: %s\n", error.what());
        failed = true;
    }
    {
        std::lock_guard<std::mutex> lock(output_mutex);
        *output << "{\"type\":\"run_end\",\"received_samples\":"
                << received_samples.load() << ",\"pending_review_samples\":"
                << review_queue.pending_samples() << "}\n";
        output->flush();
        if (!output->good()) failed = true;
    }
    return failed ? 4 : 0;
    } catch (const std::exception& error) {
        (void)fprintf(stderr, "[ERROR] 实时转写初始化失败: %s\n", error.what());
        return 4;
    }
#endif
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string output_path;
    std::string language = "auto";
    std::string prompt;
    std::string model_dir;
    std::string format_str = "text";
    bool verbose = false;
    bool speakers = true;
    bool prepare_models = false;
    bool live = false;
    bool format_explicit = false;
    std::vector<std::string> positional_args;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);

        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        }
        if (arg == "--version") {
            (void)printf("asr-tool %s\n", ASR_VERSION);
            return 0;
        }
        if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "--no-speakers") {
            speakers = false;
        } else if (arg == "--prepare-models") {
            prepare_models = true;
        } else if (arg == "--live") {
            live = true;
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_path = argv[++i];
        } else if ((arg == "-l" || arg == "--language") && i + 1 < argc) {
            language = argv[++i];
        } else if ((arg == "-p" || arg == "--prompt") && i + 1 < argc) {
            prompt = argv[++i];
        } else if ((arg == "-m" || arg == "--model-dir") && i + 1 < argc) {
            model_dir = argv[++i];
        } else if ((arg == "-f" || arg == "--format") && i + 1 < argc) {
            format_str = argv[++i];
            format_explicit = true;
        } else if (!arg.empty() && arg[0] != '-') {
            positional_args.push_back(arg);
        } else {
            (void)fprintf(stderr, "[ERROR] 未知选项: %s\n\n", arg.c_str());
            print_help(argv[0]);
            return 1;
        }
    }

    if (prepare_models && live) {
        (void)fprintf(stderr, "[ERROR] --live 不能与 --prepare-models 同时使用\n");
        return 1;
    }
    if (prepare_models) {
        if (!positional_args.empty()) {
            (void)fprintf(stderr, "[ERROR] --prepare-models 不接受录音文件，请单独运行\n");
            return 1;
        }
        const auto model_path = get_default_model_path(model_dir);
        if (!ensure_main_model(model_path, true) ||
            !prepare_meeting_models(model_path.parent_path()) ||
            !prepare_streaming_model(model_path.parent_path())) {
            return 3;
        }
#if defined(__APPLE__)
        (void)ensure_coreml_encoder_model(model_path, true);
#endif
        (void)printf("[DONE] 本地模型已就绪，转写时不会联网\n");
        return 0;
    }
    if (language != "auto" && whisper_lang_id(language.c_str()) < 0) {
        (void)fprintf(stderr, "[ERROR] 不支持的语言: %s\n", language.c_str());
        return 1;
    }
    if (live) {
        if (!positional_args.empty()) {
            (void)fprintf(stderr, "[ERROR] --live 不接受输入文件\n");
            return 1;
        }
        if (format_str != "text" && format_str != "json") {
            (void)fprintf(stderr, "[ERROR] --live 只输出 JSONL，请不要使用字幕格式\n");
            return 1;
        }
        return run_live(output_path, language, prompt, model_dir, verbose);
    }
    if (positional_args.size() > 1) {
        (void)fprintf(stderr, "[ERROR] 每次只支持一个输入文件\n");
        return 1;
    }
    if (positional_args.empty()) {
        (void)fprintf(stderr, "[ERROR] 缺少输入文件\n\n");
        print_help(argv[0]);
        return 1;
    }

    std::filesystem::path input_path(positional_args[0]);
    if (verbose) {
        (void)printf("[INFO] verbose 日志已启用\n");
    }

    std::error_code path_error;
    if (!std::filesystem::is_regular_file(input_path, path_error)) {
        (void)fprintf(stderr, "[ERROR] 输入必须是本地文件: %s\n",
                      input_path.string().c_str());
        return 1;
    }

    if (!format_explicit && !output_path.empty()) {
        std::string extension = std::filesystem::path(output_path).extension().string();
        if (extension == ".json") format_str = "json";
        else if (extension == ".srt") format_str = "srt";
        else if (extension == ".vtt") format_str = "vtt";
    }
    OutputFormat format = OutputFormat::Text;
    if (!parse_format(format_str, &format)) {
        (void)fprintf(stderr, "[ERROR] 不支持的输出格式: %s\n", format_str.c_str());
        return 1;
    }
    if (output_path.empty()) {
        output_path = (input_path.parent_path() / input_path.stem()).string();
        switch (format) {
            case OutputFormat::Text: output_path += ".txt"; break;
            case OutputFormat::Json: output_path += ".json"; break;
            case OutputFormat::Srt: output_path += ".srt"; break;
            case OutputFormat::Vtt: output_path += ".vtt"; break;
        }
    }

    if (std::filesystem::equivalent(input_path, output_path, path_error)) {
        (void)fprintf(stderr, "[ERROR] 输出不能覆盖原始录音: %s\n", output_path.c_str());
        return 1;
    }
    const auto output_parent = std::filesystem::absolute(output_path, path_error).parent_path();
    if (path_error) {
        (void)fprintf(stderr, "[ERROR] 无法解析输出路径: %s\n", path_error.message().c_str());
        return 4;
    }
    if (!std::filesystem::is_directory(output_parent, path_error) ||
        std::filesystem::is_directory(output_path, path_error)) {
        (void)fprintf(stderr, "[ERROR] 输出目录不存在或输出路径是目录: %s\n", output_path.c_str());
        return 4;
    }

    if (!check_ffmpeg()) {
        (void)fprintf(stderr, "\n");
        (void)fprintf(stderr, "[ERROR] FFmpeg 未安装\n");
        (void)fprintf(stderr, "\n");
#if defined(__APPLE__)
        (void)fprintf(stderr, "请安装 FFmpeg:\n");
        (void)fprintf(stderr, "  brew install ffmpeg\n");
#elif defined(_WIN32)
        (void)fprintf(stderr, "请安装 FFmpeg:\n");
        (void)fprintf(stderr, "  choco install ffmpeg\n");
        (void)fprintf(stderr, "  或: winget install ffmpeg\n");
#else
        (void)fprintf(stderr, "请安装 FFmpeg:\n");
        (void)fprintf(stderr, "  sudo apt install ffmpeg\n");
#endif
        (void)fprintf(stderr, "\n");
        return 2;
    }

    Backend backend = detect_backend();

    std::filesystem::path model_path;
    if (!model_dir.empty()) {
        const std::filesystem::path model_dir_path(model_dir);
        std::error_code ec;
        if (std::filesystem::exists(model_dir_path, ec) && !ec &&
            !std::filesystem::is_directory(model_dir_path, ec)) {
            (void)fprintf(stderr, "[ERROR] --model-dir 不是目录: %s\n",
                          model_dir_path.string().c_str());
            return 1;
        }
        model_path = get_default_model_path(model_dir_path);
    } else {
        model_path = get_default_model_path();
    }

    if (!ensure_main_model(model_path)) {
        return 3;
    }
#if defined(__APPLE__)
    if (!ensure_coreml_encoder_model(model_path)) {
        (void)fprintf(stderr, "[INFO] CoreML 编码器不可用，继续使用 Metal\n");
    }
#endif

    (void)printf("[INFO] 模型路径: %s\n", model_path.string().c_str());
    (void)printf("[INFO] 输出格式: %s\n", format_str.c_str());
    if (language != "auto") {
        (void)printf("[INFO] 强制语言: %s\n", language.c_str());
    }
    if (!prompt.empty()) {
        (void)printf("[INFO] Prompt: %s\n", prompt.c_str());
    }

    Transcriber transcriber(model_path.string(), language, prompt, backend, speakers);
    if (!transcriber.is_ready()) {
        (void)fprintf(stderr, "[ERROR] 转写器初始化失败: %s\n",
                      transcriber.error_message().c_str());
        return 3;
    }

    TranscriptionResult result = transcriber.transcribe(input_path);
    if (!result.success) {
        (void)fprintf(stderr, "[ERROR] 转写失败: %s\n", transcriber.error_message().c_str());
        return 4;
    }

    if (!write_output(result, format, output_path)) {
        return 4;
    }

    (void)printf("[INFO] 转写完成: %zu 个片段, 时长 %.1f 秒\n",
                 result.segments.size(), result.duration_seconds);
    if (!result.language.empty()) {
        (void)printf("[INFO] 检测语言: %s\n", result.language.c_str());
    }
    const auto review_count = std::count_if(result.segments.begin(), result.segments.end(),
        [](const Segment& segment) { return segment.needs_review; });
    if (review_count) {
        (void)fprintf(stderr, "[INFO] %zu 个片段需核对文字、时间或说话人；标记不代表错误率\n",
                      static_cast<size_t>(review_count));
    }

    // 使用 _Exit 避免 ggml-metal 退出时的断言问题
    (void)fflush(nullptr);
    _Exit(0);
}
