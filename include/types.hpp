#pragma once

#include <string>
#include <vector>

struct Segment {
    std::string text;
    double start_ms;  // 毫秒
    double end_ms;    // 毫秒
};

struct TranscriptionResult {
    std::string full_text;
    std::vector<Segment> segments;
    std::string language;
    std::string model_name;
    double duration_seconds;
};

enum class OutputFormat {
    Text,
    Json,
    Srt,
    Vtt
};

enum class Backend {
    CoreML,
    CUDA,
    CPU
};
