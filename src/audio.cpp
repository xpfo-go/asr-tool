#include "audio.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
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

int current_pid() {
#ifdef _WIN32
    return _getpid();
#else
    return getpid();
#endif
}

// 获取临时目录文件路径
std::filesystem::path make_temp_wav_path() {
    auto temp_dir = std::filesystem::temp_directory_path();
    std::ostringstream oss;
    oss << "asr_tool_" << current_pid() << "_" << rand() << ".wav";
    return temp_dir / oss.str();
}

// 执行子进程 (无 shell，防止命令注入)
// 所有平台均使用 argv 数组，不经过 shell
int run_ffmpeg(const std::string& input_path, const std::string& output_path) {
#ifdef _WIN32
    // Windows: 使用 CreateProcessA 传递 argv，不经过 shell
    // 预构建 argv 数组，保持生命周期
    std::string arg_ar = std::to_string(SAMPLE_RATE);
    std::string arg_ac = std::to_string(CHANNELS);
    const char* ffmpeg_argv[] = {
        "ffmpeg",
        "-y",
        "-i",
        input_path.c_str(),
        "-ar",
        arg_ar.c_str(),
        "-ac",
        arg_ac.c_str(),
        "-f",
        "wav",
        "-acodec",
        "pcm_f32le",
        output_path.c_str(),
        nullptr,
    };

    std::string cmd_line;
    for (int i = 0; ffmpeg_argv[i] != nullptr; ++i) {
        if (i > 0) cmd_line += ' ';
        if (strchr(ffmpeg_argv[i], ' ') != nullptr) {
            cmd_line += '"';
            cmd_line += ffmpeg_argv[i];
            cmd_line += '"';
        } else {
            cmd_line += ffmpeg_argv[i];
        }
    }

    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessA(
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
    const char* ffmpeg_argv[] = {
        "ffmpeg",
        "-y",
        "-i",
        input_path.c_str(),
        "-ar",
        arg_ar.c_str(),
        "-ac",
        arg_ac.c_str(),
        "-f",
        "wav",
        "-acodec",
        "pcm_f32le",
        output_path.c_str(),
        nullptr,
    };

    pid_t pid;
    int status = posix_spawnp(&pid, "ffmpeg", nullptr, nullptr,
                              const_cast<char* const*>(ffmpeg_argv), environ);
    if (status != 0) {
        return -1;
    }
    if (waitpid(pid, &status, 0) == -1) {
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

}  // anonymous namespace

// --- AudioReader::Impl (PIMPL) ---

struct AudioReader::Impl {
    std::FILE* file = nullptr;
    size_t total_samples_read = 0;
    size_t data_size = 0;
    size_t data_offset = 0;
    bool eof = false;

    ~Impl() {
        if (file) {
            std::fclose(file);
            file = nullptr;
        }
    }
};

AudioReader::AudioReader(const std::filesystem::path& path) : impl_(new Impl) {
    eof_ = false;
    total_duration_seconds_ = 0.0;
    processed_seconds_ = 0.0;

    // 用 FFmpeg 转换为临时 WAV 文件
    auto temp_path = make_temp_wav_path();

    (void)printf("[INFO] 转换音频格式: %s\n", path.filename().string().c_str());
    int ret = run_ffmpeg(path.string(), temp_path.string());
    if (ret != 0) {
        throw std::runtime_error("FFmpeg 转换失败 (退出码: " +
                                 std::to_string(ret) + ")");
    }

    impl_->file = std::fopen(temp_path.string().c_str(), "rb");
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
            std::fseek(impl_->file, chunk_size, SEEK_CUR);
        } else if (std::strncmp(chunk, "data", 4) == 0) {
            if (!found_fmt) {
                throw std::runtime_error("WAV 文件缺少 fmt chunk");
            }
            impl_->data_size = chunk_size;
            impl_->data_offset = static_cast<size_t>(std::ftell(impl_->file));
            break;
        } else {
            std::fseek(impl_->file, chunk_size, SEEK_CUR);
        }
    }

    if (impl_->data_size == 0) {
        throw std::runtime_error("WAV 文件缺少 data chunk");
    }

    // 计算总时长（单声道 float32）
    impl_->total_samples_read = 0;
    total_duration_seconds_ =
        static_cast<double>(impl_->data_size) / (sizeof(float) * SAMPLE_RATE);

    // 清理临时文件
    std::filesystem::remove(temp_path);

    (void)printf("[INFO] 音频已就绪 (采样率: %d Hz, 频道: %d)\n",
                 SAMPLE_RATE, CHANNELS);
}

AudioReader::~AudioReader() = default;

AudioReader::AudioReader(AudioReader&& other) noexcept
    : impl_(other.impl_),
      eof_(other.eof_),
      total_duration_seconds_(other.total_duration_seconds_),
      processed_seconds_(other.processed_seconds_) {
    other.impl_ = nullptr;
    other.eof_ = true;
    other.total_duration_seconds_ = 0.0;
    other.processed_seconds_ = 0.0;
}

AudioReader& AudioReader::operator=(AudioReader&& other) noexcept {
    if (this != &other) {
        delete impl_;
        impl_ = other.impl_;
        eof_ = other.eof_;
        total_duration_seconds_ = other.total_duration_seconds_;
        processed_seconds_ = other.processed_seconds_;
        other.impl_ = nullptr;
        other.eof_ = true;
        other.total_duration_seconds_ = 0.0;
        other.processed_seconds_ = 0.0;
    }
    return *this;
}

AudioChunk AudioReader::read_chunk() {
    AudioChunk chunk;
    if (impl_->eof) {
        eof_ = true;
        return chunk;
    }

    chunk.samples.resize(CHUNK_SAMPLES);
    size_t total_read = 0;

    while (total_read < CHUNK_SAMPLES) {
        size_t to_read = CHUNK_SAMPLES - total_read;
        size_t frames_read =
            std::fread(chunk.samples.data() + total_read, sizeof(float),
                       to_read, impl_->file);
        total_read += frames_read;

        if (frames_read == 0) {
            impl_->eof = true;
            break;
        }
    }

    // 调整到实际读取的大小
    chunk.samples.resize(total_read);
    chunk.offset_ms =
        static_cast<double>(impl_->total_samples_read) / SAMPLE_RATE * 1000.0;
    impl_->total_samples_read += total_read;

    // 更新已处理时长
    processed_seconds_ =
        static_cast<double>(impl_->total_samples_read) / SAMPLE_RATE;

    // 同步 EOF 状态（允许返回最后一个非空分块）
    eof_ = impl_->eof;

    return chunk;
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
