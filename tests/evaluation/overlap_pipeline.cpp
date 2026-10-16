#include "overlap_embedding_masks.hpp"
#include "powerset_decode.hpp"
#include "constrained_assignment.hpp"

#include <cstdio>

#include <sherpa-onnx/csrc/offline-speaker-diarization-pyannote-impl.h>

// The generated header changes one pipeline stage, with a checked source hash.
// This factory overrides the pinned static archive solely in the test target.
namespace sherpa_onnx {

std::unique_ptr<OfflineSpeakerDiarizationImpl> OfflineSpeakerDiarizationImpl::Create(
    const OfflineSpeakerDiarizationConfig& config) {
    return std::make_unique<OfflineSpeakerDiarizationPyannoteImpl>(config);
}

}  // namespace sherpa_onnx
