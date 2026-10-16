#pragma once

#include <cstdint>
#include <vector>

struct SpectralClusteringResult {
    std::vector<int32_t> labels;
    std::vector<float> silhouettes;
    std::vector<double> eigenvalues;
    int neighbors = 0;
    bool selected_complete_linkage = false;
    double spectral_quality = -2;
    double complete_quality = -2;
};

// Developer candidate: row-major embeddings, no reference speaker count.
// Dense clustering is bounded to 4096 embeddings; larger inputs fail explicitly.
SpectralClusteringResult spectral_cluster(const float* features, int rows, int columns,
                                          bool auto_neighbors = false, bool compare_linkage = false);
