#include "model_manager.hpp"
#include "temporary_directory.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace {

constexpr const char* MODEL_NAME = "ggml-large-v3-turbo-q8_0.bin";
constexpr const char* MODEL_PREFIX = "ggml-large-v3";
constexpr std::uintmax_t MIN_MODEL_FILE_SIZE = 64ULL * 1024ULL * 1024ULL;  // 64MB
constexpr std::uintmax_t DEFAULT_MODEL_FILE_SIZE = 874188075;

std::string get_model_url() {
    return "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/" +
           std::string(MODEL_NAME);
}

std::filesystem::path get_model_cache_dir(
    const std::filesystem::path& model_dir_override) {
    if (!model_dir_override.empty()) {
        return model_dir_override;
    }

    const char* cache_home = std::getenv("XDG_CACHE_HOME");
    if (!cache_home || cache_home[0] == '\0') {
#if defined(_WIN32)
        const char* home = std::getenv("USERPROFILE");
#else
        const char* home = std::getenv("HOME");
#endif
        if (!home || home[0] == '\0') {
            home = ".";
        }
        std::filesystem::path p(home);
        p /= ".cache";
        p /= "whisper";
        return p;
    }

    std::filesystem::path p(cache_home);
    p /= "whisper";
    return p;
}

int run_command(const std::vector<std::filesystem::path>& arguments, bool quiet = false) {
#if defined(_WIN32)
    std::wstring command;
    for (const auto& argument : arguments) {
        if (!command.empty()) command += L' ';
        command += L'"';
        size_t backslashes = 0;
        for (wchar_t character : argument.native()) {
            if (character == L'\\') { ++backslashes; continue; }
            command.append(backslashes * (character == L'"' ? 2 : 1), L'\\');
            if (character == L'"') command += L'\\';
            command += character;
            backslashes = 0;
        }
        command.append(backslashes * 2, L'\\');
        command += L'"';
    }
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process = {};
    SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
    HANDLE null_file = INVALID_HANDLE_VALUE;
    if (quiet) {
        null_file = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
        if (null_file == INVALID_HANDLE_VALUE) return -1;
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = startup.hStdOutput = startup.hStdError = null_file;
    }
    const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr,
        quiet ? TRUE : FALSE, 0, nullptr, nullptr, &startup, &process);
    if (null_file != INVALID_HANDLE_VALUE) CloseHandle(null_file);
    if (!created) return -1;
    const DWORD waited = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    const BOOL have_code = GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return waited == WAIT_OBJECT_0 && have_code ? static_cast<int>(code) : -1;
#else
    std::vector<char*> argv;
    for (const auto& argument : arguments)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) return -1;
    if (quiet && (posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0) != 0 ||
                  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) != 0)) {
        posix_spawn_file_actions_destroy(&actions);
        return -1;
    }
    pid_t pid;
    const int spawned = posix_spawnp(&pid, argv.front(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) return -1;
    int status = 0;
    pid_t waited;
    do { waited = waitpid(pid, &status, 0); } while (waited == -1 && errno == EINTR);
    if (waited == -1) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
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

bool file_size_matches(const std::filesystem::path& path, std::uintmax_t expected_size) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) &&
           std::filesystem::file_size(path, ec) == expected_size && !ec;
}

bool main_model_size_ok(const std::filesystem::path& path) {
    return path.filename() == MODEL_NAME ? file_size_matches(path, DEFAULT_MODEL_FILE_SIZE)
                                        : file_size_ok(path, MIN_MODEL_FILE_SIZE);
}

std::optional<std::filesystem::path> find_cached_large_v3_model(
    const std::filesystem::path& cache_dir) {
    std::error_code ec;
    if (!std::filesystem::exists(cache_dir, ec) || ec) {
        return std::nullopt;
    }

    std::vector<std::filesystem::path> candidates;
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }

        const std::string filename = entry.path().filename().u8string();
        if (!starts_with(filename, MODEL_PREFIX) || !ends_with(filename, ".bin")) {
            continue;
        }
        if (!main_model_size_ok(entry.path())) {
            continue;
        }
        candidates.push_back(entry.path());
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    std::sort(candidates.begin(), candidates.end());
    for (const auto& path : candidates) {
        if (path.filename() == MODEL_NAME) {
            return path;
        }
    }
    return candidates.front();
}

bool replace_file(const std::filesystem::path& src,
                  const std::filesystem::path& dst) {
    std::error_code ec;
#if defined(_WIN32)
    return MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    std::filesystem::rename(src, dst, ec);
    return !ec;
#endif
}

bool try_download_main_model(const std::filesystem::path& model_path) {
    std::error_code ec;
    std::filesystem::create_directories(model_path.parent_path(), ec);
    if (ec) {
        (void)fprintf(stderr, "[WARN] 创建模型目录失败: %s\n",
                      model_path.parent_path().u8string().c_str());
        return false;
    }

    TemporaryDirectory staging(model_path.parent_path());
    const auto part_path = staging.path() / MODEL_NAME;
    (void)fprintf(stderr, "[INFO] 下载中（主模型）...\n");
    const int code = run_command({"curl", "-fL", "--show-error", "--progress-bar",
        "--retry", "3", "--retry-delay", "1", "--connect-timeout", "20",
        "-o", part_path, get_model_url()});
    if (code != 0) {
        (void)fprintf(stderr,
                      "[WARN] 自动下载命令执行失败 (exit=%d)\n",
                      code);
        (void)fprintf(stderr, "[WARN] 详细错误见上方实时输出\n");
        std::filesystem::remove(part_path, ec);
        (void)ec;
        return false;
    }

    if (!file_size_matches(part_path, DEFAULT_MODEL_FILE_SIZE)) {
        (void)fprintf(stderr, "[WARN] 下载的模型文件异常（大小不足）: %s\n",
                      part_path.u8string().c_str());
        std::filesystem::remove(part_path, ec);
        (void)ec;
        return false;
    }

    if (!replace_file(part_path, model_path)) {
        (void)fprintf(stderr, "[WARN] 模型文件落盘失败: %s\n",
                      model_path.u8string().c_str());
        std::filesystem::remove(part_path, ec);
        (void)ec;
        return false;
    }

    return file_size_matches(model_path, DEFAULT_MODEL_FILE_SIZE);
}

#if defined(__APPLE__)
std::string strip_quant_suffix(std::string model_base) {
    auto dash = model_base.rfind('-');
    if (dash != std::string::npos) {
        const std::string sub = model_base.substr(dash);
        if (sub.size() == 5 && sub[1] == 'q' && sub[3] == '_') {
            model_base = model_base.substr(0, dash);
        }
    }
    return model_base;
}

std::filesystem::path derive_coreml_encoder_path(
    const std::filesystem::path& model_path) {
    std::string base = model_path.string();
    auto dot = base.rfind('.');
    if (dot != std::string::npos) {
        base = base.substr(0, dot);
    }
    base = strip_quant_suffix(base);
    return std::filesystem::path(base + "-encoder.mlmodelc");
}

std::optional<std::string> derive_model_name(
    const std::filesystem::path& model_path) {
    std::string filename = model_path.filename().string();
    if (!ends_with(filename, ".bin")) {
        return std::nullopt;
    }
    filename = filename.substr(0, filename.size() - 4);
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
    const std::filesystem::path target_dir = coreml_path.parent_path();
    std::error_code ec;
    std::filesystem::create_directories(target_dir, ec);
    if (ec) {
        return false;
    }

    TemporaryDirectory staging(target_dir);
    const auto zip_path = staging.path() / "encoder.zip";
    const std::string url =
        "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-" +
        model_name + "-encoder.mlmodelc.zip";

    (void)fprintf(stderr, "[INFO] 下载中（CoreML 编码器）...\n");
    const int code = run_command({"curl", "-fL", "--show-error", "--progress-bar",
        "--retry", "3", "--retry-delay", "1", "--connect-timeout", "20", "-o", zip_path, url});
    if (code != 0) {
        (void)fprintf(stderr,
                      "[WARN] CoreML 编码器下载失败 (exit=%d)\n",
                      code);
        (void)fprintf(stderr, "[WARN] 详细错误见上方实时输出\n");
        std::filesystem::remove(zip_path, ec);
        return false;
    }

    if (run_command({"unzip", "-o", zip_path, "-d", staging.path()}, true) != 0) return false;
    const auto extracted = staging.path() / coreml_path.filename();
    for (const auto* name : {"coremldata.bin", "model.mil", "weights/weight.bin"})
        if (!file_size_ok(extracted / name, 1)) return false;

    const auto previous = staging.path() / "previous";
    const bool had_previous = std::filesystem::exists(coreml_path, ec);
    if (ec) return false;
    if (had_previous) {
        std::filesystem::rename(coreml_path, previous, ec);
        if (ec) return false;
    }
    std::filesystem::rename(extracted, coreml_path, ec);
    if (!ec) return true;
    if (had_previous) std::filesystem::rename(previous, coreml_path, ec);
    return false;
}
#endif

}  // namespace

std::filesystem::path get_default_model_path(
    const std::filesystem::path& model_dir) {
    const std::filesystem::path cache_dir = get_model_cache_dir(model_dir);
    const std::filesystem::path preferred = cache_dir / MODEL_NAME;
    if (main_model_size_ok(preferred)) {
        return preferred;
    }

    auto discovered = find_cached_large_v3_model(cache_dir);
    if (discovered.has_value()) {
        return discovered.value();
    }

    return preferred;
}

bool ensure_main_model(const std::filesystem::path& model_path, bool allow_download) {
    if (main_model_size_ok(model_path)) {
        return true;
    }
    if (!allow_download) {
        (void)fprintf(stderr, "[ERROR] 本地主模型缺失: %s\n请先运行 asr-tool --prepare-models\n",
                      model_path.u8string().c_str());
        return false;
    }

    (void)fprintf(stderr, "\n");
    (void)fprintf(stderr, "[WARN] 模型文件不存在或不完整: %s\n",
                  model_path.u8string().c_str());
    (void)fprintf(stderr, "[INFO] 尝试自动下载模型（约 800MB）...\n");
    if (try_download_main_model(model_path)) {
        (void)printf("[INFO] 模型下载成功: %s\n", model_path.u8string().c_str());
        return true;
    }
    (void)fprintf(stderr,
                  "[WARN] 自动下载模型失败，请检查网络连接或代理配置\n");

    (void)fprintf(stderr, "\n");
    (void)fprintf(stderr,
                  "[ERROR] 模型文件不存在: %s\n", model_path.u8string().c_str());
    (void)fprintf(stderr, "\n");
    (void)fprintf(stderr, "首次运行需要下载模型（约 800MB）\n");
    (void)fprintf(stderr, "\n");
    (void)fprintf(stderr, "手动下载命令:\n");
#if defined(_WIN32)
    (void)fprintf(stderr, "  mkdir \"%s\"\n",
                  model_path.parent_path().u8string().c_str());
#else
    (void)fprintf(stderr, "  mkdir -p \"%s\"\n",
                  model_path.parent_path().u8string().c_str());
#endif
    (void)fprintf(stderr, "  curl -fSL -o \"%s\" \"%s\"\n",
                  model_path.u8string().c_str(), get_model_url().c_str());
    (void)fprintf(stderr, "\n");
    (void)fprintf(stderr, "或使用 -m/--model-dir 指定模型目录\n");
    (void)fprintf(stderr, "\n");
    return false;
}

#if defined(__APPLE__)
bool ensure_coreml_encoder_model(const std::filesystem::path& model_path, bool allow_download) {
    const std::filesystem::path coreml_path =
        derive_coreml_encoder_path(model_path);
    if (file_size_ok(coreml_path / "coremldata.bin", 1) &&
        file_size_ok(coreml_path / "model.mil", 1) &&
        file_size_ok(coreml_path / "weights/weight.bin", 1)) {
        (void)fprintf(stderr, "[INFO] 检测到 CoreML 编码器: %s\n",
                      coreml_path.string().c_str());
        return true;
    }
    if (!allow_download) {
        return false;
    }

    const char* disable = std::getenv("ASR_DISABLE_COREML_AUTO_DOWNLOAD");
    if (disable && std::string(disable) == "1") {
        (void)fprintf(stderr, "[INFO] 已禁用 CoreML 自动下载，跳过\n");
        return false;
    }

    auto model_name_opt = derive_model_name(model_path);
    if (!model_name_opt.has_value()) {
        (void)fprintf(stderr,
                      "[WARN] 无法解析模型名，跳过 CoreML 自动准备\n");
        return false;
    }
    const std::string model_name = model_name_opt.value();

    (void)fprintf(stderr, "[INFO] 未检测到 CoreML 编码器，尝试自动准备: %s\n",
                  coreml_path.string().c_str());
    (void)fprintf(stderr, "[INFO] 尝试下载预构建 CoreML 编码器...\n");
    if (try_download_coreml_encoder(model_name, coreml_path)) {
        (void)fprintf(stderr, "[INFO] CoreML 编码器下载成功\n");
        return true;
    }

    (void)fprintf(stderr,
                  "[WARN] 预构建 CoreML 编码器下载失败，将继续使用非 CoreML 路径\n");
    return false;
}
#endif

bool prepare_meeting_models(const std::filesystem::path& model_dir) {
    std::error_code ec;
    std::filesystem::create_directories(model_dir, ec);
    if (ec) {
        (void)fprintf(stderr, "[ERROR] 无法创建本地模型目录: %s\n", model_dir.u8string().c_str());
        return false;
    }
    const auto download = [&](const std::string& name, const std::string& url,
                              std::uintmax_t expected_size) {
        const auto target = model_dir / name;
        if (file_size_matches(target, expected_size)) {
            return true;
        }
        TemporaryDirectory staging(model_dir);
        const auto partial = staging.path() / name;
        (void)fprintf(stderr, "[INFO] 准备本地模型: %s\n", name.c_str());
        const int code = run_command({"curl", "-fL", "--show-error", "--retry", "3",
            "--connect-timeout", "20", "--max-time", "600", "-o", partial, url});
        if (code != 0 || !file_size_matches(partial, expected_size) || !replace_file(partial, target)) {
            std::filesystem::remove(partial, ec);
            (void)fprintf(stderr, "[ERROR] 模型下载失败或不完整: %s\n", name.c_str());
            return false;
        }
        return true;
    };
    if (!download("ggml-silero-v6.2.0.bin",
                  "https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v6.2.0.bin",
                  885098) ||
        !download(SPEAKER_MODEL_FILENAME,
                  "https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-recongition-models/"
                  "3dspeaker_speech_eres2net_base_sv_zh-cn_3dspeaker_16k.onnx", 39593761)) {
        return false;
    }
    if (file_size_matches(model_dir / "segmentation.onnx", 5992913)) {
        return true;
    }
    if (!download("segmentation.tar.bz2",
                  "https://github.com/k2-fsa/sherpa-onnx/releases/download/speaker-segmentation-models/"
                  "sherpa-onnx-pyannote-segmentation-3-0.tar.bz2", 6958444)) {
        return false;
    }
    TemporaryDirectory staging(model_dir);
    if (run_command({"tar", "-xjf", model_dir / "segmentation.tar.bz2", "-C", staging.path()}, true) != 0) {
        (void)fprintf(stderr, "[ERROR] 无法解压说话人分段模型，需要 tar\n");
        return false;
    }
    const auto extracted = staging.path() / "sherpa-onnx-pyannote-segmentation-3-0";
    return file_size_matches(extracted / "model.onnx", 5992913) &&
        file_size_ok(extracted / "LICENSE", 1) &&
        replace_file(extracted / "LICENSE", model_dir / "segmentation-LICENSE") &&
        replace_file(extracted / "model.onnx", model_dir / "segmentation.onnx");
}

bool prepare_streaming_model(const std::filesystem::path& model_dir) {
    const auto target_dir = model_dir / STREAMING_MODEL_DIRECTORY;
    std::error_code ec;
    std::filesystem::create_directories(target_dir, ec);
    if (ec) {
        (void)fprintf(stderr, "[ERROR] 无法创建流式模型目录: %s\n", target_dir.u8string().c_str());
        return false;
    }
    const std::string base =
        "https://huggingface.co/csukuangfj/sherpa-onnx-" + std::string(STREAMING_MODEL_DIRECTORY) +
        "/resolve/98590b7ed6443e77b714204da2757d75e1a642f4/";
    const auto download = [&](const char* name, std::uintmax_t size) {
        const auto target = target_dir / name;
        if (file_size_matches(target, size)) return true;
        TemporaryDirectory staging(target_dir);
        const auto partial = staging.path() / name;
        const int code = run_command({"curl", "-fL", "--show-error", "--retry", "3",
            "--connect-timeout", "20", "--max-time", "900", "-o", partial, base + name});
        if (code != 0 || !file_size_matches(partial, size) || !replace_file(partial, target)) {
            std::filesystem::remove(partial, ec);
            (void)fprintf(stderr, "[ERROR] 流式模型下载失败或不完整: %s\n", name);
            return false;
        }
        return true;
    };
    return download("encoder-epoch-99-avg-1.int8.onnx", 181895032) &&
           download("decoder-epoch-99-avg-1.onnx", 13876452) &&
           download("joiner-epoch-99-avg-1.int8.onnx", 3228404) &&
           download("tokens.txt", 56317) && download("bpe.vocab", 12564);
}
