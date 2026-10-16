// Streaming cache algorithm adapted from NVIDIA NeMo SortformerModules.
// Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. Licensed under Apache-2.0.
// See docs/THIRD_PARTY.md for source identity and implementation differences.
#include "sortformer_state.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sortformer {
namespace {

constexpr float kPredScoreThreshold = 0.25f;
constexpr float kSilenceThreshold = 0.2f;
constexpr int kSilenceFramesPerSpeaker = 3;
constexpr float kLatestScoreBoost = 0.05f;
constexpr float kStrongBoostRate = 0.75f;
constexpr float kWeakBoostRate = 1.5f;
constexpr float kMinimumPositiveRate = 0.5f;

struct ScoredFrame {
    float score;
    int speaker;
    int frame;
    int linear_index;
    bool silence;
};

}  // namespace

void StreamingState::update(const std::vector<float>& chunk_embeddings,
                            int embedding_frames,
                            const std::vector<float>& predictions,
                            int left_context, int right_context,
                            std::vector<float>& confirmed,
                            std::vector<float>& tentative) {
    if (embedding_frames <= 0 || left_context < 0 || right_context < 0 ||
        left_context + right_context >= embedding_frames ||
        chunk_embeddings.size() !=
            static_cast<size_t>(embedding_frames) * kEmbeddingDim) {
        throw std::runtime_error("Invalid Sortformer chunk state");
    }

    const int cache_frames = spkcache_length();
    const int current_fifo_frames = fifo_length_;
    const int prediction_frames = cache_frames + current_fifo_frames + embedding_frames;
    if (predictions.size() !=
        static_cast<size_t>(prediction_frames) * kNumSpeakers) {
        throw std::runtime_error("Sortformer predictions do not match the unpadded state");
    }

    if (current_fifo_frames > 0) {
        const size_t begin = static_cast<size_t>(cache_frames) * kNumSpeakers;
        const size_t end = begin + static_cast<size_t>(current_fifo_frames) * kNumSpeakers;
        fifo_predictions_.assign(predictions.begin() + begin, predictions.begin() + end);
    }

    const int core_frames = embedding_frames - left_context - right_context;
    const int chunk_start = cache_frames + current_fifo_frames + left_context;
    const int chunk_end = chunk_start + core_frames;
    const size_t confirmed_begin = static_cast<size_t>(chunk_start) * kNumSpeakers;
    const size_t confirmed_end = static_cast<size_t>(chunk_end) * kNumSpeakers;
    confirmed.assign(predictions.begin() + confirmed_begin,
                     predictions.begin() + confirmed_end);

    const size_t tentative_begin = confirmed_end;
    const size_t tentative_end = tentative_begin +
        static_cast<size_t>(right_context) * kNumSpeakers;
    if (tentative_end > predictions.size()) {
        throw std::runtime_error("Sortformer tentative predictions are missing");
    }
    tentative.assign(predictions.begin() + tentative_begin,
                     predictions.begin() + tentative_end);

    const size_t embedding_begin = static_cast<size_t>(left_context) * kEmbeddingDim;
    const size_t embedding_end = embedding_begin +
        static_cast<size_t>(core_frames) * kEmbeddingDim;
    fifo_.insert(fifo_.end(), chunk_embeddings.begin() + embedding_begin,
                 chunk_embeddings.begin() + embedding_end);
    fifo_predictions_.insert(fifo_predictions_.end(), confirmed.begin(), confirmed.end());
    fifo_length_ += core_frames;

    const int context_length = current_fifo_frames + core_frames;
    if (context_length <= kFifoCapacity) return;

    const int pop_frames = std::min(
        context_length,
        std::max(kSpkcacheUpdatePeriod, context_length - kFifoCapacity));
    if (fifo_.size() < static_cast<size_t>(pop_frames) * kEmbeddingDim ||
        fifo_predictions_.size() < static_cast<size_t>(pop_frames) * kNumSpeakers) {
        throw std::runtime_error("Sortformer FIFO state is inconsistent");
    }

    std::vector<float> popped_embeddings(
        fifo_.begin(), fifo_.begin() + static_cast<size_t>(pop_frames) * kEmbeddingDim);
    std::vector<float> popped_predictions(
        fifo_predictions_.begin(),
        fifo_predictions_.begin() + static_cast<size_t>(pop_frames) * kNumSpeakers);
    update_silence_profile(popped_embeddings, popped_predictions, pop_frames);

    fifo_.erase(fifo_.begin(),
                fifo_.begin() + static_cast<size_t>(pop_frames) * kEmbeddingDim);
    fifo_predictions_.erase(
        fifo_predictions_.begin(),
        fifo_predictions_.begin() + static_cast<size_t>(pop_frames) * kNumSpeakers);
    fifo_length_ -= pop_frames;

    spkcache_.insert(spkcache_.end(), popped_embeddings.begin(), popped_embeddings.end());
    if (!spkcache_compressed_) {
        spkcache_predictions_.assign(predictions.begin(),
                                    predictions.begin() + static_cast<size_t>(cache_frames) * kNumSpeakers);
    }
    spkcache_predictions_.insert(spkcache_predictions_.end(),
                                 popped_predictions.begin(), popped_predictions.end());
    if (spkcache_length() > kSpkcacheCapacity) {
        compress_spkcache();
        spkcache_compressed_ = true;
    }
}

void StreamingState::update_silence_profile(
    const std::vector<float>& embeddings, const std::vector<float>& predictions,
    int frames) {
    for (int frame = 0; frame < frames; ++frame) {
        float probability_sum = 0.0f;
        for (int speaker = 0; speaker < kNumSpeakers; ++speaker) {
            probability_sum += predictions[static_cast<size_t>(frame) * kNumSpeakers + speaker];
        }
        if (probability_sum >= kSilenceThreshold) continue;

        const float old_count = static_cast<float>(silence_frame_count_);
        const float new_count = old_count + 1.0f;
        for (int dimension = 0; dimension < kEmbeddingDim; ++dimension) {
            const size_t index = static_cast<size_t>(frame) * kEmbeddingDim + dimension;
            mean_silence_embedding_[dimension] =
                (mean_silence_embedding_[dimension] * old_count + embeddings[index]) /
                new_count;
        }
        ++silence_frame_count_;
    }
}

void StreamingState::compress_spkcache() {
    const int frames = spkcache_length();
    if (frames <= kSpkcacheCapacity) return;

    // Match SortformerModules._compress_spkcache from NeMo. The log score
    // favors non-overlapping speech and the two top-k boosts prevent a long
    // dominant speaker from evicting every other speaker's cache vectors.
    const int per_speaker_capacity = kSpkcacheCapacity / kNumSpeakers -
        kSilenceFramesPerSpeaker;
    const int strong_boost = static_cast<int>(per_speaker_capacity * kStrongBoostRate);
    const int weak_boost = static_cast<int>(per_speaker_capacity * kWeakBoostRate);
    const int minimum_positive = static_cast<int>(per_speaker_capacity * kMinimumPositiveRate);
    std::vector<float> scores(static_cast<size_t>(frames) * kNumSpeakers);
    std::vector<int> positive_counts(kNumSpeakers, 0);

    for (int frame = 0; frame < frames; ++frame) {
        float log_one_probability_sum = 0.0f;
        for (int speaker = 0; speaker < kNumSpeakers; ++speaker) {
            const float probability = spkcache_predictions_[
                static_cast<size_t>(frame) * kNumSpeakers + speaker];
            log_one_probability_sum += std::log(std::max(1.0f - probability,
                                                         kPredScoreThreshold));
        }
        for (int speaker = 0; speaker < kNumSpeakers; ++speaker) {
            const float probability = spkcache_predictions_[
                static_cast<size_t>(frame) * kNumSpeakers + speaker];
            float score = std::log(std::max(probability, kPredScoreThreshold)) -
                std::log(std::max(1.0f - probability, kPredScoreThreshold)) +
                log_one_probability_sum - std::log(0.5f);
            if (probability <= 0.5f) score = -std::numeric_limits<float>::infinity();
            scores[static_cast<size_t>(frame) * kNumSpeakers + speaker] = score;
            if (score > 0.0f) ++positive_counts[speaker];
        }
    }

    for (int frame = 0; frame < frames; ++frame) {
        for (int speaker = 0; speaker < kNumSpeakers; ++speaker) {
            float& score = scores[static_cast<size_t>(frame) * kNumSpeakers + speaker];
            const float probability = spkcache_predictions_[
                static_cast<size_t>(frame) * kNumSpeakers + speaker];
            if (probability > 0.5f && score <= 0.0f && positive_counts[speaker] >= minimum_positive) {
                score = -std::numeric_limits<float>::infinity();
            }
            if (frame >= kSpkcacheCapacity && std::isfinite(score)) {
                score += kLatestScoreBoost;
            }
        }
    }

    auto boost_top_scores = [&](int count, float scale_factor) {
        for (int speaker = 0; speaker < kNumSpeakers; ++speaker) {
            std::vector<int> indices(frames);
            for (int frame = 0; frame < frames; ++frame) indices[frame] = frame;
            std::stable_sort(indices.begin(), indices.end(), [&](int left, int right) {
                const float left_score = scores[static_cast<size_t>(left) * kNumSpeakers + speaker];
                const float right_score = scores[static_cast<size_t>(right) * kNumSpeakers + speaker];
                return left_score > right_score;
            });
            const int boosted = std::min(count, frames);
            for (int index = 0; index < boosted; ++index) {
                float& score = scores[static_cast<size_t>(indices[index]) * kNumSpeakers + speaker];
                if (std::isfinite(score)) score -= scale_factor * std::log(0.5f);
            }
        }
    };
    boost_top_scores(strong_boost, 2.0f);
    boost_top_scores(weak_boost, 1.0f);

    std::vector<ScoredFrame> candidates;
    candidates.reserve(static_cast<size_t>(frames + kSilenceFramesPerSpeaker) * kNumSpeakers);
    const int candidate_frame_count = frames + kSilenceFramesPerSpeaker;
    for (int frame = 0; frame < frames; ++frame) {
        for (int speaker = 0; speaker < kNumSpeakers; ++speaker) {
            candidates.push_back({scores[static_cast<size_t>(frame) * kNumSpeakers + speaker],
                                  speaker, frame, speaker * candidate_frame_count + frame, false});
        }
    }
    for (int speaker = 0; speaker < kNumSpeakers; ++speaker) {
        for (int silence = 0; silence < kSilenceFramesPerSpeaker; ++silence) {
            candidates.push_back({std::numeric_limits<float>::infinity(), speaker,
                                  frames + silence, speaker * candidate_frame_count + frames + silence,
                                  true});
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        if (left.score != right.score) return left.score > right.score;
        return left.linear_index < right.linear_index;
    });
    candidates.resize(std::min(static_cast<size_t>(kSpkcacheCapacity), candidates.size()));
    for (auto& candidate : candidates) {
        if (candidate.score == -std::numeric_limits<float>::infinity()) {
            candidate.linear_index = std::numeric_limits<int>::max();
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return left.linear_index < right.linear_index;
    });

    std::vector<float> new_cache(static_cast<size_t>(kSpkcacheCapacity) * kEmbeddingDim, 0.0f);
    std::vector<float> new_predictions(
        static_cast<size_t>(kSpkcacheCapacity) * kNumSpeakers, 0.0f);
    for (size_t slot = 0; slot < candidates.size(); ++slot) {
        const auto& selected = candidates[slot];
        if (selected.silence || !std::isfinite(selected.score)) {
            std::copy(mean_silence_embedding_.begin(), mean_silence_embedding_.end(),
                      new_cache.begin() + slot * kEmbeddingDim);
            continue;
        }
        const size_t source_embedding = static_cast<size_t>(selected.frame) * kEmbeddingDim;
        const size_t destination_embedding = slot * kEmbeddingDim;
        std::copy(spkcache_.begin() + source_embedding,
                  spkcache_.begin() + source_embedding + kEmbeddingDim,
                  new_cache.begin() + destination_embedding);
        const size_t source_prediction = static_cast<size_t>(selected.frame) * kNumSpeakers;
        const size_t destination_prediction = slot * kNumSpeakers;
        std::copy(spkcache_predictions_.begin() + source_prediction,
                  spkcache_predictions_.begin() + source_prediction + kNumSpeakers,
                  new_predictions.begin() + destination_prediction);
    }
    spkcache_ = std::move(new_cache);
    spkcache_predictions_ = std::move(new_predictions);
}

}  // namespace sortformer
