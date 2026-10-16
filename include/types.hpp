#pragma once

#include <string>
#include <vector>

struct Segment {
    std::string text;
    double start_ms = 0.0;  // 毫秒
    double end_ms = 0.0;    // 毫秒
    int speaker = -1;
    bool overlap = false;
    bool needs_review = false;
    double confidence = 0.0;
    std::vector<std::string> review_reasons;
};

struct SpeakerTurn {
    double start_ms = 0.0;
    double end_ms = 0.0;
    int speaker = -1;
    double confidence = -2.0;
};

struct Correction {
    double start_ms = 0.0;
    double end_ms = 0.0;
    std::string original_text;
    std::string corrected_text;
    std::string reason;
};

struct DecodingSettings {
    int threads = 1;
    int retry_beam_size = 5;
    double review_threshold = 0.65;
    double correction_confidence_gain = 0.08;
    float vad_threshold = 0.5f;
    bool acoustic_timestamps = false;
    bool history_prompt = true;
    std::string backend;
    std::string requested_language;
};

struct TranscriptionResult {
    std::string full_text;
    std::vector<Segment> segments;
    std::string language;
    std::string model_name;
    double duration_seconds = 0.0;
    bool success = false;
    bool speakers_available = false;
    std::vector<Correction> corrections;
    std::vector<std::string> warnings;
    std::vector<SpeakerTurn> speaker_turns;
    std::string speaker_model;
    DecodingSettings decoding_settings;
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
