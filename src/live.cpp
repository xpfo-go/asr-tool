#include "live.hpp"

#include "meeting.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

LiveEvent final_event(const LiveReviewTask& task, const std::string& reason) {
    return {task.utterance_id, task.revision + 1, LiveEventKind::Final,
            task.start_sample, task.end_sample, task.text, task.text, {}, reason, false};
}

}  // namespace

LiveReviewQueue::LiveReviewQueue(Reviewer reviewer, Sink sink)
    : reviewer_(std::move(reviewer)), sink_(std::move(sink)) {
    if (!reviewer_ || !sink_) throw std::invalid_argument("Missing live reviewer or event sink");
    worker_ = std::thread(&LiveReviewQueue::work, this);
}

LiveReviewQueue::~LiveReviewQueue() {
    try { finish(); } catch (...) { }
}

void LiveReviewQueue::rethrow_failure() const {
    if (failure_) std::rethrow_exception(failure_);
}

bool LiveReviewQueue::submit(LiveReviewTask task) {
    if (task.utterance_id == 0 || task.start_sample < 0 || task.end_sample <= task.start_sample ||
        static_cast<uint64_t>(task.end_sample - task.start_sample) != task.audio.size() ||
        task.revision > std::numeric_limits<uint32_t>::max() - 2) {
        throw std::invalid_argument("Invalid live review task");
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        rethrow_failure();
        if (finishing_) throw std::logic_error("Live review input is already closed");
        if (task.utterance_id <= last_id_ || task.start_sample < last_end_) {
            throw std::invalid_argument("Live utterances must have increasing IDs and non-overlapping audio");
        }
        last_id_ = task.utterance_id;
        last_end_ = task.end_sample;
        if (task.audio.size() <= LIVE_REVIEW_SAMPLE_LIMIT - pending_samples_) {
            pending_samples_ += task.audio.size();
            peak_pending_samples_ = std::max(peak_pending_samples_, pending_samples_);
            pending_.push_back(std::move(task));
            changed_.notify_one();
            return true;
        }
    }
    emit(final_event(task, "review_queue_full"));
    return false;
}

void LiveReviewQueue::emit(const LiveEvent& event) {
    std::lock_guard<std::mutex> lock(sink_mutex_);
    sink_(event);
}

void LiveReviewQueue::work() {
    try {
        for (;;) {
            LiveReviewTask task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait(lock, [&] { return finishing_ || !pending_.empty(); });
                if (pending_.empty()) return;
                task = std::move(pending_.front());
                pending_.pop_front();
            }
            LiveReviewResult result;
            try {
                result = reviewer_(task.audio);
            } catch (...) {
                result.reason = "review_failed";
            }
            auto event = final_event(task, result.reason.empty() ? "review_uncertain" : result.reason);
            event.review_text = result.text;
            if (result.reliable) {
                const bool safe = correction_is_conservative(task.text, result.text) ||
                                  (result.allow_broad_correction &&
                                   correction_is_live_review_safe(task.text, result.text));
                if (safe) {
                    event.text = result.text;
                    event.reviewed = true;
                    event.reason = "local_audio_review";
                    if (event.text != task.text) {
                        event.kind = LiveEventKind::Revision;
                        emit(event);
                        ++event.revision;
                        event.kind = LiveEventKind::Final;
                    }
                } else {
                    event.reason = "correction_rejected";
                }
            }
            emit(event);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pending_samples_ -= task.audio.size();
            }
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        failure_ = std::current_exception();
        finishing_ = true;
        pending_.clear();
        pending_samples_ = 0;
    }
}

void LiveReviewQueue::finish() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        finishing_ = true;
    }
    changed_.notify_one();
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    rethrow_failure();
}

size_t LiveReviewQueue::pending_samples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_samples_;
}

size_t LiveReviewQueue::peak_pending_samples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return peak_pending_samples_;
}
