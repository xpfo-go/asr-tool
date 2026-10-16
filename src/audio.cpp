#include "audio.hpp"
#include "temporary_directory.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace {

// 常量
constexpr int SAMPLE_RATE = 16000;
constexpr int CHANNELS = 1;
constexpr double CHUNK_SECONDS = 30.0;
constexpr size_t CHUNK_SAMPLES =
    static_cast<size_t>(SAMPLE_RATE * CHUNK_SECONDS);  // 480000

#ifdef _WIN32
std::wstring quote_windows_argument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') { ++backslashes; continue; }
        quoted.append(backslashes * (character == L'"' ? 2 : 1), L'\\');
        if (character == L'"') quoted += L'\\';
        quoted += character;
        backslashes = 0;
    }
    quoted.append(backslashes * 2, L'\\');
    return quoted + L'"';
}
#endif

// 执行子进程 (无 shell，防止命令注入)
// 所有平台均使用 argv 数组，不经过 shell
int run_ffmpeg(const std::filesystem::path& input_path, const std::filesystem::path& output_path,
               bool normalize_volume) {
    const char* filter = normalize_volume ? "dynaudnorm=f=500:g=31:p=0.95:m=10" : nullptr;
#ifdef ASR_EXPERIMENT_AUDIO_FILTER
    filter = ASR_EXPERIMENT_AUDIO_FILTER;
#endif
#ifdef _WIN32
    std::vector<std::wstring> arguments = {
        L"ffmpeg", L"-y", L"-nostdin", L"-protocol_whitelist", L"file,pipe",
        L"-i", input_path.native(), L"-ar", std::to_wstring(SAMPLE_RATE),
        L"-ac", std::to_wstring(CHANNELS),
    };
    if (filter) arguments.insert(arguments.end(), {L"-af", std::filesystem::u8path(filter).native()});
    arguments.insert(arguments.end(), {L"-f", L"wav", L"-acodec", L"pcm_f32le", output_path.native()});
    std::wstring cmd_line;
    for (const auto& argument : arguments) {
        if (!cmd_line.empty()) cmd_line += L' ';
        cmd_line += quote_windows_argument(argument);
    }

    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(
        nullptr,
        cmd_line.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);
    if (!ok) {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exit_code);
#else
    // POSIX: 使用 posix_spawnp 避免 shell 注入
    // 预构建 argv 数组，保持字符串生命周期
    std::string arg_ar = std::to_string(SAMPLE_RATE);
    std::string arg_ac = std::to_string(CHANNELS);
    std::vector<const char*> ffmpeg_argv = {
        "ffmpeg",
        "-y",
        "-nostdin",
        "-protocol_whitelist",
        "file,pipe",
        "-i",
        input_path.c_str(),
        "-ar",
        arg_ar.c_str(),
        "-ac",
        arg_ac.c_str(),
    };
    if (filter) ffmpeg_argv.insert(ffmpeg_argv.end(), {"-af", filter});
    ffmpeg_argv.insert(ffmpeg_argv.end(), {"-f", "wav", "-acodec", "pcm_f32le",
                                         output_path.c_str(), nullptr});

    pid_t pid;
    int status = posix_spawnp(&pid, "ffmpeg", nullptr, nullptr,
                              const_cast<char* const*>(ffmpeg_argv.data()), environ);
    if (status != 0) {
        return -1;
    }
    pid_t waited;
    do { waited = waitpid(pid, &status, 0); } while (waited == -1 && errno == EINTR);
    if (waited == -1) {
        if (errno != ECHILD) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {}
        }
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

}  // anonymous namespace

// --- AudioReader::Impl (PIMPL) ---

struct AudioReader::Impl {
    TemporaryDirectory temp_directory{std::filesystem::temp_directory_path()};
    std::FILE* file = nullptr;
    size_t total_samples_read = 0;
    size_t data_size = 0;
    bool eof = false;

    ~Impl() {
        if (file) {
            std::fclose(file);
            file = nullptr;
        }
    }
};

AudioReader::AudioReader(const std::filesystem::path& path, bool normalize_volume)
    : impl_(std::make_unique<Impl>()) {
    eof_ = false;
    total_duration_seconds_ = 0.0;
    processed_seconds_ = 0.0;

    // 用 FFmpeg 转换为临时 WAV 文件
    auto temp_path = impl_->temp_directory.path() / "audio.wav";

    (void)printf("[INFO] 转换音频格式: %s\n", path.filename().u8string().c_str());
    int ret = run_ffmpeg(path, temp_path, normalize_volume);
    if (ret != 0) {
        throw std::runtime_error("FFmpeg 转换失败 (退出码: " +
                                 std::to_string(ret) + ")");
    }

#ifdef _WIN32
    impl_->file = _wfopen(temp_path.c_str(), L"rb");
#else
    impl_->file = std::fopen(temp_path.c_str(), "rb");
#endif
    if (!impl_->file) {
        throw std::runtime_error("无法打开临时音频文件");
    }

    // 解析 WAV 头
    char riff[12];
    if (std::fread(riff, 1, 12, impl_->file) != 12) {
        throw std::runtime_error("无效的 WAV 文件");
    }
    if (std::strncmp(riff, "RIFF", 4) != 0 || std::strncmp(riff + 8, "WAVE", 4) != 0) {
        throw std::runtime_error("不是有效的 WAV 文件");
    }

    // 查找 fmt 和 data chunk
    char chunk[8];
    bool found_fmt = false;
    while (std::fread(chunk, 1, 8, impl_->file) == 8) {
        uint32_t chunk_size;
        std::memcpy(&chunk_size, chunk + 4, sizeof(chunk_size));

        if (std::strncmp(chunk, "fmt ", 4) == 0) {
            found_fmt = true;
            if (chunk_size < 16) {
                throw std::runtime_error("无效的 fmt chunk");
            }
        } else if (std::strncmp(chunk, "data", 4) == 0) {
            if (!found_fmt) {
                throw std::runtime_error("WAV 文件缺少 fmt chunk");
            }
            impl_->data_size = chunk_size;
            if (chunk_size % sizeof(float) != 0)
                throw std::runtime_error("WAV data chunk is not aligned to float samples");
            break;
        }
        const uint64_t skip = uint64_t(chunk_size) + (chunk_size & 1);
        if (skip > static_cast<uint64_t>((std::numeric_limits<long>::max)()) ||
            std::fseek(impl_->file, static_cast<long>(skip), SEEK_CUR) != 0)
            throw std::runtime_error("Could not skip WAV chunk");
    }

    if (impl_->data_size == 0) {
        throw std::runtime_error("WAV 文件缺少 data chunk");
    }

    // 计算总时长（单声道 float32）
    impl_->total_samples_read = 0;
    total_duration_seconds_ =
        static_cast<double>(impl_->data_size) / (sizeof(float) * SAMPLE_RATE);

    (void)printf("[INFO] 音频已就绪 (采样率: %d Hz, 频道: %d)\n",
                 SAMPLE_RATE, CHANNELS);
}

AudioReader::~AudioReader() = default;

AudioReader::AudioReader(AudioReader&& other) noexcept = default;

AudioReader& AudioReader::operator=(AudioReader&& other) noexcept = default;

AudioChunk AudioReader::read_chunk() {
    AudioChunk chunk{};
    if (impl_->eof) {
        eof_ = true;
        return chunk;
    }

    const size_t remaining = impl_->data_size / sizeof(float) - impl_->total_samples_read;
    const size_t requested = std::min(CHUNK_SAMPLES, remaining);
    chunk.samples.resize(requested);
    size_t total_read = 0;

    while (total_read < requested) {
        size_t to_read = requested - total_read;
        size_t frames_read =
            std::fread(chunk.samples.data() + total_read, sizeof(float),
                       to_read, impl_->file);
        total_read += frames_read;

        if (std::ferror(impl_->file) || frames_read == 0)
            throw std::runtime_error("读取音频失败或 WAV 数据被截断");
    }

    // 调整到实际读取的大小
    chunk.samples.resize(total_read);
    chunk.offset_ms =
        static_cast<double>(impl_->total_samples_read) / SAMPLE_RATE * 1000.0;
    impl_->total_samples_read += total_read;
    impl_->eof = impl_->total_samples_read == impl_->data_size / sizeof(float);

    // 更新已处理时长
    processed_seconds_ =
        static_cast<double>(impl_->total_samples_read) / SAMPLE_RATE;

    // 同步 EOF 状态（允许返回最后一个非空分块）
    eof_ = impl_->eof;

    return chunk;
}

std::vector<float> AudioReader::read_all() {
    const size_t count = impl_->data_size / sizeof(float);
    if (count > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("音频过长，请拆分为小于 37 小时的文件");
    }
    std::vector<float> samples;
    samples.reserve(count);
    while (!is_eof()) {
        auto chunk = read_chunk();
        samples.insert(samples.end(), chunk.samples.begin(), chunk.samples.end());
    }
    if (samples.size() != count || std::ferror(impl_->file)) {
        throw std::runtime_error("读取音频失败或 WAV 数据被截断");
    }
    return samples;
}

// --- FFmpeg 检测 ---

bool check_ffmpeg() {
#ifdef _WIN32
    FILE* fp = _popen("ffmpeg -version 2>NUL", "r");
#else
    FILE* fp = popen("ffmpeg -version 2>/dev/null", "r");
#endif
    if (!fp) {
        return false;
    }
    char buf[64] = {0};
#ifdef _WIN32
    if (fgets(buf, sizeof(buf), fp) != nullptr) {
        _pclose(fp);
        return true;
    }
    _pclose(fp);
#else
    if (fgets(buf, sizeof(buf), fp) != nullptr) {
        pclose(fp);
        return true;
    }
    pclose(fp);
#endif
    return false;
}

AudioReader open_audio(const std::filesystem::path& path) {
    return AudioReader(path);
}
