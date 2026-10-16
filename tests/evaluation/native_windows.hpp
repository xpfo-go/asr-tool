#pragma once

#include "meeting.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

inline std::vector<SpeechWindow> plan_native_gap_windows(
    const std::vector<SpeechWindow>& parents, const std::vector<TimeRange>& speech) {
    std::vector<SpeechWindow> windows;
    for (const auto& parent : parents) {
        double begin = parent.audio.start_ms;
        for (size_t i = 1; i < speech.size(); ++i) {
            if (speech[i].start_ms - speech[i - 1].end_ms < 1500.0) continue;
            const double pause = (speech[i - 1].end_ms + speech[i].start_ms) / 2.0;
            if (pause <= begin || pause >= parent.audio.end_ms) continue;
            // Some AED models stop after a long pause despite later speech.
            windows.push_back({{begin, pause}, {begin, pause}});
            begin = pause;
        }
        windows.push_back({{begin, parent.audio.end_ms}, {begin, parent.audio.end_ms}});
    }
    return windows;
}

inline std::vector<SpeechWindow> plan_native_pause_windows(
    const std::vector<SpeechWindow>& parents, const std::vector<TimeRange>& speech,
    double maximum_ms = 12000.0, double pause_after_ms = 6000.0) {
    constexpr double minimum_tail_ms = 2000.0;
    std::vector<SpeechWindow> windows;
    for (const auto& parent : parents) {
        for (double begin = parent.audio.start_ms; begin < parent.audio.end_ms;) {
            const double remaining = parent.audio.end_ms - begin;
            double end = parent.audio.end_ms;
            if (remaining > maximum_ms) {
                // Balance continuous speech instead of leaving a tiny final decode.
                end = begin + remaining / std::ceil(remaining / maximum_ms);
                const double limit = std::min(begin + maximum_ms,
                                              parent.audio.end_ms - minimum_tail_ms);
                for (size_t i = 1; i < speech.size(); ++i) {
                    const double pause = (speech[i - 1].end_ms + speech[i].start_ms) / 2.0;
                    if (speech[i].start_ms > speech[i - 1].end_ms &&
                        pause >= begin + pause_after_ms && pause <= limit) {
                        end = pause;
                    }
                }
            }
            windows.push_back({{begin, end}, {begin, end}});
            begin = end;
        }
    }
    return windows;
}

inline std::vector<SpeechWindow> plan_native_restart_windows(
    const std::vector<SpeechWindow>& parents, const std::vector<TimeRange>& speech) {
    std::vector<SpeechWindow> groups;
    for (const auto& parent : parents) {
        if (!groups.empty() && groups.back().audio.end_ms == parent.audio.start_ms) {
            groups.back().audio.end_ms = parent.audio.end_ms;
            groups.back().ownership.end_ms = parent.audio.end_ms;
        } else {
            groups.push_back(parent);
        }
    }
    // A pause resets the context budget; obsolete parent cuts can split the next sentence.
    const auto paused = plan_native_gap_windows(groups, speech);
    return plan_native_pause_windows(paused, speech, 26000.0, 18000.0);
}
