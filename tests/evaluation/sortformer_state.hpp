#pragma once

#include <vector>

namespace sortformer {

constexpr int kNumSpeakers = 4;
constexpr int kEmbeddingDim = 512;
constexpr int kSpkcacheCapacity = 188;
// Model-card profiles, in 80 ms output frames. These are developer comparison
// binaries, not user-facing settings.
#ifdef ASR_SORTFORMER_FILE_PROFILE
constexpr int kChunkFrames = 340;
constexpr int kRightFrames = 40;
constexpr int kFifoCapacity = 40;
constexpr int kSpkcacheUpdatePeriod = 300;
#else
constexpr int kChunkFrames = 6;
constexpr int kRightFrames = 7;
constexpr int kFifoCapacity = 188;
constexpr int kSpkcacheUpdatePeriod = 144;
#endif

class StreamingState {
public:
    void update(const std::vector<float>& chunk_embeddings, int embedding_frames,
                const std::vector<float>& predictions, int left_context,
                int right_context, std::vector<float>& confirmed,
                std::vector<float>& tentative);

    int spkcache_length() const {
        return static_cast<int>(spkcache_.size() / kEmbeddingDim);
    }
    int fifo_length() const { return fifo_length_; }
    const std::vector<float>& spkcache() const { return spkcache_; }
    const std::vector<float>& fifo() const { return fifo_; }

private:
    void update_silence_profile(const std::vector<float>& embeddings,
                                const std::vector<float>& predictions,
                                int frames);
    void compress_spkcache();

    std::vector<float> spkcache_;
    std::vector<float> spkcache_predictions_;
    std::vector<float> fifo_;
    std::vector<float> fifo_predictions_;
    int fifo_length_ = 0;
    bool spkcache_compressed_ = false;
    std::vector<float> mean_silence_embedding_ =
        std::vector<float>(kEmbeddingDim, 0.0f);
    int silence_frame_count_ = 0;
};

}  // namespace sortformer
