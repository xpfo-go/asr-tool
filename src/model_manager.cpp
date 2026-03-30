#include "model_manager.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace {

constexpr const char* MODEL_NAME = "ggml-large-v3-turbo-q8_0.bin";
constexpr std::uintmax_t MIN_MODEL_FILE_SIZE = 64ULL * 1024ULL * 1024ULL;  // 64MB

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

bool replace_file(const std::filesystem::path& src,
                  const std::filesystem::path& dst) {
    std::error_code ec;
#if defined(_WIN32)
    // Windows 上 rename 到已存在目标通常会失败，先尝试删除旧文件。
    std::filesystem::remove(dst, ec);
    ec.clear();
#endif
    std::filesystem::rename(src, dst, ec);
    if (!ec) {
        return true;
    }

    ec.clear();
    std::filesystem::copy_file(src, dst,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec) {
        return false;
    }

    ec.clear();
    std::filesystem::remove(src, ec);
    return true;
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

    if (!replace_file(part_path, model_path)) {
        (void)fprintf(stderr, "[WARN] 模型文件落盘失败: %s\n",
                      model_path.string().c_str());
        std::filesystem::remove(part_path, ec);
        (void)ec;
        return false;
    }

    return file_size_ok(model_path, MIN_MODEL_FILE_SIZE);
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
        "curl -fL -o " + shell_quote(zip_path.string()) + " " + shell_quote(url);
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
#endif

}  // namespace

std::filesystem::path get_default_model_path() {
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
        p /= MODEL_NAME;
        return p;
    }

    std::filesystem::path p(cache_home);
    p /= "whisper";
    p /= MODEL_NAME;
    return p;
}

bool ensure_main_model(const std::filesystem::path& model_path) {
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
#if defined(_WIN32)
    (void)fprintf(stderr, "  mkdir \"%s\"\n",
                  model_path.parent_path().string().c_str());
#else
    (void)fprintf(stderr, "  mkdir -p \"%s\"\n",
                  model_path.parent_path().string().c_str());
#endif
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

#if defined(__APPLE__)
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

