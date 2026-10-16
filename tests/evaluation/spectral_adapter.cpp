#include "spectral_clustering.hpp"

#include <sherpa-onnx/csrc/fast-clustering.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

// Evaluation-only replacement for the pinned macOS static archive's clusterer.
// Production continues to link the original implementation.
namespace sherpa_onnx {

class FastClustering::Impl {};

FastClustering::FastClustering(const FastClusteringConfig& config)
    : impl_(std::make_unique<Impl>()) {
    if (config.num_clusters != -1) throw std::invalid_argument("Spectral evaluation estimates speaker count");
}

FastClustering::~FastClustering() = default;

std::vector<int32_t> FastClustering::Cluster(float* features, int32_t rows, int32_t columns,
                                           std::vector<float>* confidence) const {
#ifdef ASR_CANDIDATE_HYBRID
    auto result = spectral_cluster(features, rows, columns, false, true);
    (void)fprintf(stderr, "[SELECTION] method=%s spectral_quality=%.8f complete_quality=%.8f\n",
                  result.selected_complete_linkage ? "complete" : "spectral",
                  result.spectral_quality, result.complete_quality);
#elif defined(ASR_CANDIDATE_NME)
    auto result = spectral_cluster(features, rows, columns, true);
#else
    auto result = spectral_cluster(features, rows, columns);
#endif
    const int count = result.labels.empty() ? 0 : *std::max_element(result.labels.begin(), result.labels.end()) + 1;
    (void)fprintf(stderr, "[SPECTRAL] embeddings=%d speakers=%d neighbors=%d eigenvalues=", rows, count, result.neighbors);
    for (double value : result.eigenvalues) (void)fprintf(stderr, " %.8g", value);
    (void)fprintf(stderr, "\n");
    if (confidence) *confidence = std::move(result.silhouettes);
    return result.labels;
}

}  // namespace sherpa_onnx
