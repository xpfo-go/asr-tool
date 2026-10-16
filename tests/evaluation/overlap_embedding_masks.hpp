#pragma once

// The pinned sherpa extractor skips a local speaker with fewer than ten clean
// frames. Preserve its original mask when overlap alone caused that exclusion.
template <typename Matrix>
int restore_overlap_masks(const Matrix& original, Matrix& clean) {
    int restored = 0;
    for (int speaker = 0; speaker < original.cols(); ++speaker) {
        if (clean.col(speaker).sum() < 10 && original.col(speaker).sum() >= 10) {
            clean.col(speaker) = original.col(speaker);
            ++restored;
        }
    }
    return restored;
}
