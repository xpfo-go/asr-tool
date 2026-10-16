#include "live.hpp"
#include "output.hpp"

#include <chrono>
#include <future>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

LiveReviewTask task(uint64_t id, int64_t start, size_t samples, const std::string& text) {
    return {id, 3, start, start + static_cast<int64_t>(samples), text, std::vector<float>(samples, .1f)};
}

void bounded_queue_keeps_ingestion_moving() {
    std::promise<void> release, started;
    auto gate = release.get_future().share();
    auto ready = started.get_future();
    std::vector<LiveEvent> events;
    std::mutex events_mutex;
    int calls = 0;
    LiveReviewQueue queue([&](const auto&) {
        if (++calls == 1) {
            started.set_value();
            if (gate.wait_for(std::chrono::seconds(3)) != std::future_status::ready)
                throw std::runtime_error("Test review gate timed out");
        }
        return LiveReviewResult{"same", true, {}};
    }, [&](const auto& event) {
        std::lock_guard<std::mutex> lock(events_mutex);
        events.push_back(event);
    });
    check(queue.submit(task(1, 0, 12 * LIVE_SAMPLE_RATE, "same")), "First review must fit");
    check(ready.wait_for(std::chrono::seconds(1)) == std::future_status::ready, "Review must start");
    const auto begin = std::chrono::steady_clock::now();
    check(queue.submit(task(2, 12 * LIVE_SAMPLE_RATE, 8 * LIVE_SAMPLE_RATE, "same")), "Exact budget must fit");
    check(!queue.submit(task(3, 20 * LIVE_SAMPLE_RATE, LIVE_SAMPLE_RATE, "preserve me")), "Overload must decline");
    check(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(250), "Submission waited for the reviewer");
    check(queue.pending_samples() == LIVE_REVIEW_SAMPLE_LIMIT, "In-flight audio must count toward budget");
    check(events.size() == 1 && events[0].utterance_id == 3 && events[0].kind == LiveEventKind::Final &&
          !events[0].reviewed && events[0].text == "preserve me" && events[0].reason == "review_queue_full",
          "Overload must finalize the identified original, not silently drop it");
    release.set_value();
    queue.finish();
    check(events.size() == 3 && queue.pending_samples() == 0 &&
          queue.peak_pending_samples() == LIVE_REVIEW_SAMPLE_LIMIT, "Finish must drain accepted work");
    queue.finish();
    bool rejected = false;
    try { queue.submit(task(4, 21 * LIVE_SAMPLE_RATE, 1, "late")); }
    catch (const std::logic_error&) { rejected = true; }
    check(rejected, "Closed queue must reject new input");
}

void revisions_preserve_meaning_and_identity() {
    int call = 0;
    std::vector<LiveEvent> events;
    const std::vector<std::string> originals{
        "Compare the cicle distance", "Ship 12 items", "We cannot send this shipment",
        "uncertain original", "failed original"};
    LiveReviewQueue queue([&](const auto&) -> LiveReviewResult {
        switch (++call) {
            case 1: return {"Compare the cycle distance.", true, {}};
            case 2: return {"Ship 13 items", true, {}};
            case 3: return {"We can send this shipment", true, {}};
            case 4: return {"uncertain candidate", false, "low_asr_confidence"};
            default: throw std::runtime_error("Recognizer failed");
        }
    }, [&](const auto& event) { events.push_back(event); });
    for (size_t i = 0; i < originals.size(); ++i) queue.submit(task(i + 1, i * 16000, 16000, originals[i]));
    queue.finish();
    check(events.size() == 6, "One revision plus exactly one final per utterance");
    check(events[0].kind == LiveEventKind::Revision && events[1].kind == LiveEventKind::Final &&
          events[0].utterance_id == 1 && events[1].utterance_id == 1 && events[0].revision == 4 &&
          events[1].revision == 5 && events[1].reviewed && events[1].original_text == originals[0],
          "Correction must replace a stable utterance and retain its original");
    for (size_t i : {size_t(2), size_t(3)}) {
        check(events[i].kind == LiveEventKind::Final && !events[i].reviewed &&
              events[i].reason == "correction_rejected" && events[i].text == originals[i - 1] &&
              !events[i].review_text.empty(), "Protect numbers and negation without discarding the alternative");
    }
    check(!events[4].reviewed && events[4].text == originals[3] && events[4].reason == "low_asr_confidence",
          "Uncertain review cannot replace text");
    check(!events[5].reviewed && events[5].text == originals[4] && events[5].reason == "review_failed",
          "Review exception must retain and finalize the initial text");
}

void output_failure_is_not_success() {
    LiveReviewQueue queue([](const auto&) { return LiveReviewResult{"same", true, {}}; },
                          [](const auto&) { throw std::runtime_error("Output failed"); });
    queue.submit(task(1, 0, 100, "same"));
    bool failed = false;
    try { queue.finish(); } catch (const std::runtime_error&) { failed = true; }
    check(failed, "Output failure must reach the caller");
}

void validate_audio_ownership_and_json() {
    LiveReviewQueue queue([](const auto&) { return LiveReviewResult{"same", true, {}}; }, [](const auto&) {});
    auto bad = task(1, 0, 10, "same");
    bad.end_sample = 11;
    bool rejected = false;
    try { queue.submit(bad); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Inconsistent sample ownership must fail");
    queue.submit(task(1, 0, 10, "same"));
    rejected = false;
    try { queue.submit(task(2, 9, 10, "same")); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Overlapping ownership must fail");
    queue.finish();
    LiveEvent event{7, 2, LiveEventKind::Final, 0, 16000, "quote\"\n\\", "initial", "candidate",
                    "correction_rejected", false};
    std::ostringstream output;
    write_live_event(output, event);
    const auto json = output.str();
    check(json.find("quote\\\"\\u000a\\\\") != std::string::npos &&
          json.find("\"type\":\"final\"") != std::string::npos &&
          json.find("\"reviewed\":false") != std::string::npos && json.back() == '\n',
          "JSONL must escape content and expose final review state");
    for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()}) {
        event.emitted_seconds = invalid;
        std::ostringstream invalid_output;
        rejected = false;
        try { write_live_event(invalid_output, event); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && invalid_output.str().empty(), "invalid JSONL must fail before emitting a partial line");
    }
}

}  // namespace

int main() {
    try {
        bounded_queue_keeps_ingestion_moving();
        revisions_preserve_meaning_and_identity();
        output_failure_is_not_success();
        validate_audio_ownership_and_json();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
