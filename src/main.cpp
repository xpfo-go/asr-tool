#include "audio.hpp"
#include "output.hpp"
#include "platform.hpp"
#include "transcriber.hpp"
#include "types.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr const char* MODEL_NAME = "ggml-large-v3-turbo-q8_0.bin";
constexpr const char* VERSION = "1.0.0";
constexpr std::uintmax_t MIN_MODEL_FILE_SIZE = 64ULL * 1024ULL * 1024ULL;  // 64MB

// 获取默认模型路径
std::filesystem::path get_default_model_path() {
    const char* cache_home =
        std::getenv("XDG_CACHE_HOME");  // Linux/macOS
    if (!cache_home || cache_home[0] == '\0') {
        const char* home = std::getenv("HOME");
        if (!home || home[0] == '\0') {
            home = ".";  // fallback
        }
        cache_home = home;
        std::filesystem::path p(home);
        p /= ".cache";
        p /= "whisper";
        p /= MODEL_NAME;
        return p;
    }
    std::filesystem::path p(cache_home);
    p /= "whisper";
    p /= MODEL_NAME;
    return p;
}

// 获取模型下载 URL
std::string get_model_url() {
    return "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/" +
           std::string(MODEL_NAME);
}

std::string shell_quote(const std::string& s) {
#if defined(_WIN32)
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') {
            out += "'\"'\"'";
        } else {
            out.push_back(c);
        }
    }
    out += "'";
    return out;
#endif
}

bool run_command_quiet(const std::string& cmd) {
#if defined(_WIN32)
    const std::string full_cmd = cmd + " >NUL 2>&1";
#else
    const std::string full_cmd = cmd + " >/dev/null 2>&1";
#endif
    return std::system(full_cmd.c_str()) == 0;
}

bool has_command(const char* cmd) {
#if defined(_WIN32)
    const std::string check = "where " + std::string(cmd);
#else
    const std::string check = "command -v " + std::string(cmd);
#endif
    return run_command_quiet(check);
}

bool file_size_ok(const std::filesystem::path& path, std::uintmax_t min_size) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return false;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        return false;
    }
    return size >= min_size;
}

bool try_download_main_model(const std::filesystem::path& model_path) {
    if (!has_command("curl")) {
        (void)fprintf(stderr, "[WARN] 未找到 curl，无法自动下载模型\n");
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(model_path.parent_path(), ec);
    if (ec) {
        (void)fprintf(stderr, "[WARN] 创建模型目录失败: %s\n",
                      model_path.parent_path().string().c_str());
        return false;
    }

    const std::filesystem::path part_path =
        model_path.parent_path() / (model_path.filename().string() + ".part");
    std::filesystem::remove(part_path, ec);
    (void)ec;

    const std::string cmd =
        "curl -fL --retry 3 --retry-delay 1 --connect-timeout 20 -o " +
        shell_quote(part_path.string()) + " " + shell_quote(get_model_url());
    if (!run_command_quiet(cmd)) {
        std::filesystem::remove(part_path, ec);
        (void)ec;
        return false;
    }

    if (!file_size_ok(part_path, MIN_MODEL_FILE_SIZE)) {
        (void)fprintf(stderr, "[WARN] 下载的模型文件异常（大小不足）: %s\n",
                      part_path.string().c_str());
        std::filesystem::remove(part_path, ec);
        (void)ec;
        return false;
    }

    std::filesystem::rename(part_path, model_path, ec);
    if (ec) {
        (void)fprintf(stderr, "[WARN] 模型文件落盘失败: %s\n", ec.message().c_str());
        std::filesystem::remove(part_path, ec);
        (void)ec;
        return false;
    }

    return file_size_ok(model_path, MIN_MODEL_FILE_SIZE);
}

// 检查模型是否存在，不存在时自动下载，失败则提示手动下载
bool check_model(const std::filesystem::path& model_path) {
    if (file_size_ok(model_path, MIN_MODEL_FILE_SIZE)) {
        return true;
    }

    (void)fprintf(stderr, "\n");
    (void)fprintf(stderr, "[WARN] 模型文件不存在或不完整: %s\n",
                  model_path.string().c_str());
    (void)fprintf(stderr, "[INFO] 尝试自动下载模型（约 800MB）...\n");
    if (try_download_main_model(model_path)) {
        (void)printf("[INFO] 模型下载成功: %s\n", model_path.string().c_str());
        return true;
    }
    (void)fprintf(stderr,
                  "[WARN] 自动下载模型失败，请检查网络连接或代理配置\n");

    (void)fprintf(stderr, "\n");
    (void)fprintf(stderr,
                  "[ERROR] 模型文件不存在: %s\n", model_path.string().c_str());
    (void)fprintf(stderr, "\n");
    (void)fprintf(stderr, "首次运行需要下载模型（约 800MB）\n");
    (void)fprintf(stderr, "\n");
    (void)fprintf(stderr, "手动下载命令:\n");
    (void)fprintf(stderr, "  mkdir -p \"%s\"\n",
                  model_path.parent_path().string().c_str());
    (void)fprintf(stderr, "  curl -fSL -o \"%s\" \"%s\"\n",
                  model_path.string().c_str(), get_model_url().c_str());
    (void)fprintf(stderr, "\n");
    (void)fprintf(stderr, "或设置环境变量 XDG_CACHE_HOME 指定缓存目录\n");
#if defined(__APPLE__)
    (void)fprintf(stderr,
                  "提示: 程序会尝试自动下载预构建 CoreML 编码器，失败时会回退到非 CoreML 路径\n");
#endif
    (void)fprintf(stderr, "\n");
    return false;
}

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

#if defined(__APPLE__)
bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string strip_quant_suffix(std::string model_base) {
    auto dash = model_base.rfind('-');
    if (dash != std::string::npos) {
        const std::string sub = model_base.substr(dash);
        // 匹配 whisper.cpp 里的 -q8_0 / -q5_1 等量化后缀
        if (sub.size() == 5 && sub[1] == 'q' && sub[3] == '_') {
            model_base = model_base.substr(0, dash);
        }
    }
    return model_base;
}

std::filesystem::path derive_coreml_encoder_path(const std::filesystem::path& model_path) {
    std::string base = model_path.string();
    auto dot = base.rfind('.');
    if (dot != std::string::npos) {
        base = base.substr(0, dot);
    }
    base = strip_quant_suffix(base);
    return std::filesystem::path(base + "-encoder.mlmodelc");
}

std::optional<std::string> derive_model_name(const std::filesystem::path& model_path) {
    std::string filename = model_path.filename().string();
    if (!ends_with(filename, ".bin")) {
        return std::nullopt;
    }
    filename = filename.substr(0, filename.size() - 4);  // 去掉 .bin
    if (starts_with(filename, "ggml-")) {
        filename = filename.substr(5);
    }
    filename = strip_quant_suffix(filename);
    if (filename.empty()) {
        return std::nullopt;
    }
    return filename;
}

bool try_download_coreml_encoder(const std::string& model_name,
                                 const std::filesystem::path& coreml_path) {
    if (!has_command("curl") || !has_command("unzip")) {
        return false;
    }

    const std::filesystem::path target_dir = coreml_path.parent_path();
    std::error_code ec;
    std::filesystem::create_directories(target_dir, ec);
    if (ec) {
        return false;
    }

    const std::filesystem::path zip_path =
        target_dir / ("ggml-" + model_name + "-encoder.mlmodelc.zip");
    const std::string url =
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-" +
        model_name + "-encoder.mlmodelc.zip";

    const std::string dl_cmd =
        "curl -fL -o " + shell_quote(zip_path.string()) + " " +
        shell_quote(url);
    if (!run_command_quiet(dl_cmd)) {
        return false;
    }

    const std::string unzip_cmd =
        "unzip -o " + shell_quote(zip_path.string()) + " -d " +
        shell_quote(target_dir.string());
    const bool unzip_ok = run_command_quiet(unzip_cmd);
    std::filesystem::remove(zip_path, ec);
    (void)ec;
    return unzip_ok && std::filesystem::exists(coreml_path);
}

void ensure_coreml_encoder_model(const std::filesystem::path& model_path) {
    const std::filesystem::path coreml_path =
        derive_coreml_encoder_path(model_path);
    if (std::filesystem::exists(coreml_path)) {
        (void)printf("[INFO] 检测到 CoreML 编码器: %s\n",
                     coreml_path.string().c_str());
        return;
    }

    const char* disable = std::getenv("ASR_DISABLE_COREML_AUTO_DOWNLOAD");
    if (disable && std::string(disable) == "1") {
        (void)printf("[INFO] 已禁用 CoreML 自动下载，跳过\n");
        return;
    }

    auto model_name_opt = derive_model_name(model_path);
    if (!model_name_opt.has_value()) {
        (void)fprintf(stderr,
                      "[WARN] 无法解析模型名，跳过 CoreML 自动准备\n");
        return;
    }
    const std::string model_name = model_name_opt.value();

    (void)printf("[INFO] 未检测到 CoreML 编码器，尝试自动准备: %s\n",
                 coreml_path.string().c_str());

    (void)printf("[INFO] 尝试下载预构建 CoreML 编码器...\n");
    if (try_download_coreml_encoder(model_name, coreml_path)) {
        (void)printf("[INFO] CoreML 编码器下载成功\n");
        return;
    }

    (void)fprintf(stderr,
                  "[WARN] 预构建 CoreML 编码器下载失败，将继续使用非 CoreML 路径\n");
}
#endif

}  // anonymous namespace

int main(int argc, char* argv[]) {
    // 参数解析
    std::string output_path;
    std::string language = "auto";
    std::string prompt;
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

    // 检查输入文件
    if (!std::filesystem::exists(input_path)) {
        (void)fprintf(stderr, "[ERROR] 文件不存在: %s\n",
                       input_path.string().c_str());
        return 1;
    }

    // 默认输出路径
    if (output_path.empty()) {
        output_path = input_path.stem().string();
        OutputFormat fmt = OutputFormat::Text;
        if (!parse_format(format_str, &fmt)) {
            (void)fprintf(stderr, "[ERROR] 不支持的输出格式: %s\n", format_str.c_str());
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
        (void)fprintf(stderr, "[ERROR] 不支持的输出格式: %s\n", format_str.c_str());
        return 1;
    }

    // 检查 FFmpeg
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

    // 检测加速后端
    Backend backend = detect_backend();

    // 检查模型
    auto model_path = get_default_model_path();
    if (!check_model(model_path)) {
        return 3;
    }

#if defined(__APPLE__)
    ensure_coreml_encoder_model(model_path);
#endif

    (void)printf("[INFO] 模型路径: %s\n", model_path.string().c_str());
    (void)printf("[INFO] 输出格式: %s\n", format_str.c_str());
    if (language != "auto") {
        (void)printf("[INFO] 强制语言: %s\n", language.c_str());
    }
    if (!prompt.empty()) {
        (void)printf("[INFO] Prompt: %s\n", prompt.c_str());
    }

    // 创建转写器
    Transcriber transcriber(model_path.string(), language, prompt, backend);

    if (!transcriber.is_ready()) {
        (void)fprintf(stderr, "[ERROR] 转写器初始化失败: %s\n",
                       transcriber.error_message().c_str());
        return 3;
    }

    // 执行转写
    TranscriptionResult result = transcriber.transcribe(input_path);

    if (result.segments.empty() && result.full_text.empty()) {
        (void)fprintf(stderr, "[ERROR] 转写失败，未产生任何输出\n");
        return 4;
    }

    // 输出结果
    write_output(result, format, output_path);

    (void)printf("[INFO] 转写完成: %zu 个片段, 时长 %.1f 秒\n",
                 result.segments.size(), result.duration_seconds);
    if (!result.language.empty()) {
        (void)printf("[INFO] 检测语言: %s\n", result.language.c_str());
    }

    // 使用 _Exit 避免 ggml-metal 退出时的断言问题
    _Exit(0);
}
