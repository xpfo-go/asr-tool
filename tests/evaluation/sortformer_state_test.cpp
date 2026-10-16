#include "sortformer_state.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
using namespace sortformer;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<float> numbered_embeddings(int start, int count) {
    std::vector<float> result(static_cast<size_t>(count) * kEmbeddingDim);
    for (int frame = 0; frame < count; ++frame) {
        std::fill_n(result.begin() + static_cast<size_t>(frame) * kEmbeddingDim,
                    kEmbeddingDim, static_cast<float>(start + frame));
    }
    return result;
}

void context_and_tail() {
    StreamingState state;
    auto embeddings = numbered_embeddings(1, 13);
    std::vector<float> predictions(13 * kNumSpeakers, .1f), confirmed, tentative;
    predictions[0] = .9f;
    predictions[6 * kNumSpeakers] = .8f;
    state.update(embeddings, 13, predictions, 0, 7, confirmed, tentative);
    require(confirmed.size() == 6 * kNumSpeakers && confirmed[0] == .9f,
            "First chunk must confirm six core frames");
    require(tentative.size() == 7 * kNumSpeakers && tentative[0] == .8f,
            "Right context must stay tentative");
    require(state.fifo().back() == 6, "Right context leaked into FIFO");

    // One left-context frame and two final core frames, with no right context.
    predictions.assign(9 * kNumSpeakers, .1f);
    predictions[7 * kNumSpeakers + 1] = .95f;
    state.update(numbered_embeddings(6, 3), 3, predictions, 1, 0, confirmed, tentative);
    require(confirmed.size() == 2 * kNumSpeakers && confirmed[1] == .95f,
            "Final chunk must discard left context only");
    require(tentative.empty() && state.fifo_length() == 8 && state.fifo().back() == 8,
            "Final chunk was lost or duplicated");

    predictions.assign((state.fifo_length() + 2) * kNumSpeakers, .1f);
    bool rejected = false;
    try {
        state.update(numbered_embeddings(9, 1), 1, predictions, 0, 0, confirmed, tentative);
    } catch (const std::runtime_error&) { rejected = true; }
    require(rejected && state.fifo_length() == 8, "Padded predictions must be rejected before mutation");
}

void compression_refresh_and_disabled_order() {
    StreamingState state;
    std::vector<float> confirmed, tentative;
    std::vector<float> predictions(192 * kNumSpeakers, 0);
    for (int i = 0; i < 192; ++i) predictions[i * kNumSpeakers] = .95f;
    state.update(numbered_embeddings(1, 192), 192, predictions, 0, 0, confirmed, tentative);
    require(state.spkcache_length() == 144 && state.fifo_length() == 48,
            "First cache update must retain uncompressed history");

    // Fresh model output revises the first cached speaker and marks all other
    // frames silent. Stale probabilities would retain 143 wrong frames.
    predictions.assign((144 + 48 + 144) * kNumSpeakers, 0);
    predictions[1] = .95f;
    state.update(numbered_embeddings(193, 144), 144, predictions, 0, 0, confirmed, tentative);
    require(state.spkcache_length() == kSpkcacheCapacity && state.fifo_length() == 48,
            "Compression capacities are incorrect");
    const auto& cache = state.spkcache();
    const float silence = 216.5f;  // Mean of popped embeddings 145 through 288.
    for (int i = 0; i < kSpkcacheCapacity; ++i) {
        const float expected = i == 3 ? 1.0f : silence;
        require(std::abs(cache[static_cast<size_t>(i) * kEmbeddingDim] - expected) < .001f,
                "Fresh cache labels, speaker order, or disabled-frame silence replacement failed");
    }
}

void repeated_compression_retains_speakers() {
    StreamingState state;
    std::vector<float> confirmed, tentative;
    int next = 1;
    for (int step = 0; step < 300; ++step) {
        auto embeddings = numbered_embeddings(next, 6);
        std::vector<float> all = state.spkcache();
        all.insert(all.end(), state.fifo().begin(), state.fifo().end());
        all.insert(all.end(), embeddings.begin(), embeddings.end());
        std::vector<float> predictions(all.size() / kEmbeddingDim * kNumSpeakers, 0);
        for (size_t i = 0; i < all.size() / kEmbeddingDim; ++i) {
            const int id = static_cast<int>(all[i * kEmbeddingDim]);
            if (id > 0) predictions[i * kNumSpeakers + (id / 60) % kNumSpeakers] = .95f;
        }
        state.update(embeddings, 6, predictions, 0, 0, confirmed, tentative);
        require(state.fifo_length() <= kFifoCapacity && state.spkcache_length() <= kSpkcacheCapacity,
                "Streaming state exceeded bounded capacity");
        next += 6;
    }
    bool retained[kNumSpeakers] = {};
    for (size_t i = 0; i < state.spkcache().size(); i += kEmbeddingDim) {
        const int id = static_cast<int>(state.spkcache()[i]);
        if (id > 0) retained[(id / 60) % kNumSpeakers] = true;
    }
    for (bool present : retained) require(present, "Compression evicted a known speaker");
}
}  // namespace

int main() {
    try {
        context_and_tail();
        compression_refresh_and_disabled_order();
        repeated_compression_retains_speakers();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
