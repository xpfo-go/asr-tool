#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct AudioChunk {
    std::vector<float> samples;
    double offset_ms;  // 此块在音频中的起始时间（毫秒）
};

class AudioReader {
public:
    explicit AudioReader(const std::filesystem::path& path);
    ~AudioReader();

    // 禁用拷贝，允许移动
    AudioReader(const AudioReader&) = delete;
    AudioReader& operator=(const AudioReader&) = delete;
    AudioReader(AudioReader&&) noexcept;
    AudioReader& operator=(AudioReader&&) noexcept;

    // 读取下一块音频，返回空表示结束
    AudioChunk read_chunk();

    bool is_eof() const { return eof_; }
    // 输入音频总时长（秒）
    double duration_seconds() const { return total_duration_seconds_; }
    // 已处理时长（秒）
    double processed_seconds() const { return processed_seconds_; }

private:
    struct Impl;
    Impl* impl_;
    bool eof_ = false;
    double total_duration_seconds_ = 0.0;
    double processed_seconds_ = 0.0;
};

// 检测 FFmpeg 是否可用
bool check_ffmpeg();

// 转换输入文件为 WAV PCM 流式读取器
AudioReader open_audio(const std::filesystem::path& path);
