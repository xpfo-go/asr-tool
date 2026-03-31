#include "audio.hpp"
#include "model_manager.hpp"
#include "output.hpp"
#include "platform.hpp"
#include "transcriber.hpp"
#include "types.hpp"

#include <ggml-backend.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr const char* VERSION = "1.0.0";

void print_help(const char* prog) {
    (void)fprintf(stderr,
                  "用法: %s [选项] <输入文件>\n\n"
                  "将音频/视频文件转写为文字\n\n"
                  "参数:\n"
                  "  <输入文件>                音频/视频文件路径\n\n"
                  "选项:\n"
                  "  -o, --output <路径>      输出文件路径 (默认: 输入文件.txt)\n"
                  "  -l, --language <语言>     源语言，如 zh/en/ja/auto (默认: auto)\n"
                  "  -p, --prompt <文本>      提示文本，引导模型输出\n"
                  "  -m, --model-dir <路径>   指定 .bin 模型目录（默认: ~/.cache/whisper）\n"
                  "  -f, --format <格式>      输出格式: text/json/srt/vtt (默认: text)\n"
                  "  -v, --verbose             显示详细日志\n"
                  "  -h, --help                显示帮助\n"
                  "  --version                 显示版本\n",
                  prog);
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

void load_runtime_backends() {
#if defined(__linux__) || defined(_WIN32)
    const size_t before = ggml_backend_dev_count();
    ggml_backend_load_all();
    const size_t after = ggml_backend_dev_count();
    (void)fprintf(stderr, "[INFO] 后端加载: %zu -> %zu 个设备\n", before, after);
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
    std::vector<std::string> positional_args;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);

        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        }
        if (arg == "--version") {
            (void)printf("asr-tool %s\n", VERSION);
            return 0;
        }
        if (arg == "-v" || arg == "--verbose") {
            verbose = true;
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
        } else if (!arg.empty() && arg[0] != '-') {
            positional_args.push_back(arg);
        } else {
            (void)fprintf(stderr, "[ERROR] 未知选项: %s\n\n", arg.c_str());
            print_help(argv[0]);
            return 1;
        }
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

    if (!std::filesystem::exists(input_path)) {
        (void)fprintf(stderr, "[ERROR] 文件不存在: %s\n",
                      input_path.string().c_str());
        return 1;
    }

    if (output_path.empty()) {
        output_path = input_path.stem().string();
        OutputFormat fmt = OutputFormat::Text;
        if (!parse_format(format_str, &fmt)) {
            (void)fprintf(stderr, "[ERROR] 不支持的输出格式: %s\n",
                          format_str.c_str());
            return 1;
        }
        switch (fmt) {
            case OutputFormat::Text: output_path += ".txt"; break;
            case OutputFormat::Json: output_path += ".json"; break;
            case OutputFormat::Srt: output_path += ".srt"; break;
            case OutputFormat::Vtt: output_path += ".vtt"; break;
        }
    }

    OutputFormat format = OutputFormat::Text;
    if (!parse_format(format_str, &format)) {
        (void)fprintf(stderr, "[ERROR] 不支持的输出格式: %s\n",
                      format_str.c_str());
        return 1;
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

    load_runtime_backends();
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
        backend = Backend::CPU;
        (void)fprintf(stderr, "[INFO] CoreML 编码器不可用，回退 CPU 推理\n");
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

    Transcriber transcriber(model_path.string(), language, prompt, backend);
    if (!transcriber.is_ready()) {
        (void)fprintf(stderr, "[ERROR] 转写器初始化失败: %s\n",
                      transcriber.error_message().c_str());
        return 3;
    }

    TranscriptionResult result = transcriber.transcribe(input_path);
    if (result.segments.empty() && result.full_text.empty()) {
        (void)fprintf(stderr, "[ERROR] 转写失败，未产生任何输出\n");
        return 4;
    }

    write_output(result, format, output_path);

    (void)printf("[INFO] 转写完成: %zu 个片段, 时长 %.1f 秒\n",
                 result.segments.size(), result.duration_seconds);
    if (!result.language.empty()) {
        (void)printf("[INFO] 检测语言: %s\n", result.language.c_str());
    }

    // 使用 _Exit 避免 ggml-metal 退出时的断言问题
    _Exit(0);
}
