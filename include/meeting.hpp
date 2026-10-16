#pragma once

#include "types.hpp"

#include <cstdint>
#include <vector>

struct TimeRange {
    double start_ms = 0.0;
    double end_ms = 0.0;
};

struct SpeechWindow {
    TimeRange audio;
    TimeRange ownership;
};

struct SpeakerAssignment {
    int speaker = -1;
    bool overlap = false;
};

struct TimedText {
    std::string text;
    double start_ms = 0.0;
    double end_ms = 0.0;
    int segment = 0;
    bool approximate_timing = false;
};

struct ScoredToken {
    std::string text;
    double probability = -1.0;
};

std::vector<SpeechWindow> plan_speech_windows(
    const std::vector<TimeRange>& speech, double duration_ms,
    const std::vector<SpeakerTurn>& turns = {});
void normalize_speakers(std::vector<SpeakerTurn>& turns);
SpeakerAssignment assign_speaker(double start_ms, double end_ms,
                                 const std::vector<SpeakerTurn>& turns);
bool owns_timestamp(const SpeechWindow& window, double start_ms, double end_ms);
bool correction_is_conservative(const std::string& original, const std::string& candidate);
std::string correction_rejection_reason(const std::string& original, const std::string& candidate);
bool correction_is_live_review_safe(const std::string& original, const std::string& candidate);
bool has_weak_token_span(const std::vector<ScoredToken>& tokens);
bool local_correction_is_supported(const std::vector<ScoredToken>& original,
                                   const std::vector<ScoredToken>& candidate);
double repetition_ratio(const std::vector<int32_t>& tokens);
int choose_language(const std::vector<float>& probabilities, int previous_language);
bool repair_token_timestamps(std::vector<TimedText>& tokens, double duration_ms);
bool align_token_timestamps(std::vector<TimedText>& tokens,
                            const std::vector<double>& acoustic_times,
                            double segment_end_ms, double duration_ms);
void append_decoded_window(TranscriptionResult& result, const std::vector<TimedText>& tokens,
                           const SpeechWindow& window, const std::vector<SpeakerTurn>& turns,
                           double confidence, const std::vector<std::string>& review_reasons);
