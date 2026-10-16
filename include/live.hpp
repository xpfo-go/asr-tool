#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

inline constexpr int LIVE_SAMPLE_RATE = 16000;
inline constexpr size_t LIVE_REVIEW_SAMPLE_LIMIT = 20 * LIVE_SAMPLE_RATE;

enum class LiveEventKind { Provisional, Revision, Final };

struct LiveEvent {
    uint64_t utterance_id = 0;
    uint32_t revision = 0;
    LiveEventKind kind = LiveEventKind::Provisional;
    int64_t start_sample = 0;
    int64_t end_sample = 0;
    std::string text;
    std::string original_text;
    std::string review_text;
    std::string reason;
    bool reviewed = false;
    double emitted_seconds = -1;
    int64_t received_samples = -1;
};

struct LiveReviewTask {
    uint64_t utterance_id = 0;
    uint32_t revision = 0;
    int64_t start_sample = 0;
    int64_t end_sample = 0;
    std::string text;
    std::vector<float> audio;
};

struct LiveReviewResult {
    std::string text;
    bool reliable = false;
    std::string reason;
    bool allow_broad_correction = false;
};

// Endpoint audio is owned by this bounded queue. The reviewer runs on its own
// thread; accepted audio includes the in-flight job in the 20-second budget.
// The owner must stop submitters before calling finish(), and serialize finish()
// calls. Reviewer/sink callbacks must not call submit() or finish() on this queue.
class LiveReviewQueue {
public:
    using Reviewer = std::function<LiveReviewResult(const std::vector<float>&)>;
    using Sink = std::function<void(const LiveEvent&)>;

    LiveReviewQueue(Reviewer reviewer, Sink sink);
    ~LiveReviewQueue();
    LiveReviewQueue(const LiveReviewQueue&) = delete;
    LiveReviewQueue& operator=(const LiveReviewQueue&) = delete;

    bool submit(LiveReviewTask task);
    void finish();
    size_t pending_samples() const;
    size_t peak_pending_samples() const;

private:
    void work();
    void emit(const LiveEvent& event);
    void rethrow_failure() const;

    Reviewer reviewer_;
    Sink sink_;
    mutable std::mutex mutex_;
    std::mutex sink_mutex_;
    std::condition_variable changed_;
    std::deque<LiveReviewTask> pending_;
    size_t pending_samples_ = 0;
    size_t peak_pending_samples_ = 0;
    uint64_t last_id_ = 0;
    int64_t last_end_ = 0;
    bool finishing_ = false;
    std::exception_ptr failure_;
    std::thread worker_;
};
