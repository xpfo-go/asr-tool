#pragma once

#include "types.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

using SpeakerEmbeddingCallback = std::function<std::vector<float>(double start_ms, double end_ms)>;

struct SpeakerRefinementResult {
    std::vector<SpeakerTurn> turns;
    size_t embeddings_requested = 0;
    size_t reliable_speakers = 0;
    size_t changed_regions = 0;
    std::string notice;
};

// Fixed acoustic gates; at most 512 callback calls. Failures preserve the exact input.
// Changes need >=500 ms of double window support with no strong contrary window.
// They record original_speaker and confidence -2; unchanged metadata is preserved.
SpeakerRefinementResult refine_speaker_turns(const std::vector<SpeakerTurn>& turns,
                                            const SpeakerEmbeddingCallback& embedding);
