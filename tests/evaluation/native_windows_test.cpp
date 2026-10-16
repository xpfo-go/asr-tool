#include "native_windows.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void check_coverage(double duration_ms) {
    const auto windows = plan_native_pause_windows(
        {{{0, duration_ms}, {0, duration_ms}}}, {{0, duration_ms}});
    double previous = 0;
    for (const auto& window : windows) {
        check(window.audio.start_ms == previous, "speech must have no gaps or duplicates");
        const double length = window.audio.end_ms - window.audio.start_ms;
        check(length > 0 && length <= 12000.001, "windows must fit the native context");
        check(duration_ms <= 12000 || length >= 2000,
              "splitting must not create a tiny tail");
        check(window.ownership.start_ms == window.audio.start_ms &&
              window.ownership.end_ms == window.audio.end_ms,
              "all decoded text must remain owned without invented alignment");
        previous = window.audio.end_ms;
    }
    check(std::abs(previous - duration_ms) < .001, "the original tail must survive");
}

}  // namespace

int main() {
    try {
        for (double duration : {400.0, 12000.0, 12425.0, 13000.0, 24425.0, 26000.0})
            check_coverage(duration);
        check(plan_native_pause_windows({}, {}).empty(), "silence needs no windows");
        check(plan_native_gap_windows({}, {}).empty(), "silence needs no gap windows");
        check(plan_native_restart_windows({}, {}).empty(), "silence needs no restarted windows");
        const auto restarted = plan_native_restart_windows(
            {{{0, 26000}, {0, 26000}}, {{26000, 28000}, {26000, 28000}}},
            {{0, 12000}, {14000, 28000}});
        check(restarted.size() == 2 && restarted[0].audio.start_ms == 0 &&
              restarted[0].audio.end_ms == 13000 && restarted[1].audio.start_ms == 13000 &&
              restarted[1].audio.end_ms == 28000,
              "a long pause must reset the budget without retaining an obsolete parent cut");
        for (double duration : {400.0, 26000.0, 26425.0, 52000.0, 52425.0, 100000.0}) {
            std::vector<SpeechWindow> parents;
            for (double start = 0; start < duration; start += 26000.0) {
                const double end = std::min(start + 26000.0, duration);
                parents.push_back({{start, end}, {start, end}});
            }
            double previous = 0;
            for (const auto& window : plan_native_restart_windows(parents, {{0, duration}})) {
                const double length = window.audio.end_ms - window.audio.start_ms;
                check(window.audio.start_ms == previous && length > 0 && length <= 26000.001,
                      "restarted windows must preserve coverage and the 26-second limit");
                check(duration <= 26000 || length >= 2000,
                      "continuous speech must not leave an artificial tiny tail");
                check(window.ownership.start_ms == window.audio.start_ms &&
                      window.ownership.end_ms == window.audio.end_ms,
                      "restarted windows must own all retained audio");
                previous = window.audio.end_ms;
            }
            check(previous == duration, "restarted windows must retain the complete tail");
        }
        const auto distinct = plan_native_restart_windows(
            {{{0, 26000}, {0, 26000}}, {{30000, 30400}, {30000, 30400}}},
            {{0, 26000}, {30000, 30400}});
        check(distinct.size() == 2 && distinct[0].audio.end_ms == 26000 &&
              distinct[1].audio.start_ms == 30000 && distinct[1].audio.end_ms == 30400,
              "excluded silence must stay excluded and genuine short speech must survive");
        const auto gap = plan_native_gap_windows(
            {{{1000, 24000}, {1000, 24000}}},
            {{1000, 5000}, {5500, 13000}, {17000, 20000}, {22000, 24000}});
        check(gap.size() == 3 && gap[0].audio.start_ms == 1000 &&
              gap[0].audio.end_ms == 15000 && gap[1].audio.start_ms == 15000 &&
              gap[1].audio.end_ms == 21000 && gap[2].audio.start_ms == 21000 &&
              gap[2].audio.end_ms == 24000,
              "long pauses must reset decoding without gaps or short-pause splits");
        const auto boundary = plan_native_gap_windows(
            {{{0, 10000}, {0, 10000}}, {{10000, 12000}, {10000, 12000}}},
            {{0, 9000}, {11000, 12000}});
        check(boundary.size() == 2 && boundary[0].audio.end_ms == 10000 &&
              boundary[1].audio.start_ms == 10000,
              "a pause on a parent boundary must not create a zero-length decode");
        const auto short_speech = plan_native_gap_windows(
            {{{0, 6000}, {0, 6000}}}, {{0, 1000}, {5700, 6000}});
        check(short_speech.size() == 2 && short_speech.back().audio.end_ms == 6000 &&
              short_speech.back().ownership.end_ms == 6000,
              "short genuine speech after a long pause must survive");
        const auto pauses = plan_native_pause_windows(
            {{{0, 26000}, {0, 26000}}}, {{0, 7500}, {8500, 26000}});
        check(pauses.front().audio.end_ms == 8000, "prefer a VAD pause to a hard cut");
        const auto tiny_tail = plan_native_pause_windows(
            {{{0, 12425}, {0, 12425}}}, {{0, 11900}, {12100, 12425}});
        check(tiny_tail.size() == 2 && tiny_tail.front().audio.end_ms == 6212.5,
              "a late pause must not create an unusable residual window");
        const auto separated = plan_native_pause_windows(
            {{{1000, 14000}, {1000, 14000}}, {{20000, 20400}, {20000, 20400}}},
            {{1000, 14000}, {20000, 20400}});
        check(separated.back().audio.start_ms == 20000 &&
              separated.back().audio.end_ms == 20400,
              "genuine short speech must not be removed or padded into other speech");
        std::cout << "native window tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
