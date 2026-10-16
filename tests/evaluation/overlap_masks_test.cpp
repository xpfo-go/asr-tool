#include "overlap_embedding_masks.hpp"

#include <Eigen/Core>

#include <iostream>
#include <stdexcept>

int main() {
    try {
        Eigen::MatrixXi original = Eigen::MatrixXi::Zero(30, 4);
        Eigen::MatrixXi clean = original;
        original.col(0).head(20).setOnes();
        clean.col(0).head(10).setOnes();
        original.col(1).head(15).setOnes();
        clean.col(1).head(2).setOnes();
        original.col(2).head(9).setOnes();
        clean.col(2).head(1).setOnes();
        original.col(3).head(10).setOnes();
        const auto before = clean;
        if (restore_overlap_masks(original, clean) != 2)
            throw std::runtime_error("Only speakers with sufficient original speech may be restored");
        if (clean.col(0) != before.col(0) || clean.col(2) != before.col(2))
            throw std::runtime_error("Keep clean speech and insufficient speech unchanged");
        if (clean.col(1) != original.col(1) || clean.col(3) != original.col(3))
            throw std::runtime_error("Preserve the original overlap mask without inventing frames");
        if (restore_overlap_masks(original, clean) != 0)
            throw std::runtime_error("Mask restoration must be idempotent");
        Eigen::MatrixXi empty(0, 4);
        if (restore_overlap_masks(empty, empty) != 0)
            throw std::runtime_error("Empty speech must remain empty");
        std::cout << "overlap mask tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
