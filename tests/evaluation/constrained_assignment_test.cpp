#include "constrained_assignment.hpp"

#include <iostream>
#include <random>

int main() {
    try {
        Eigen::MatrixXd collision(2, 2);
        collision << .9, .8, .85, .1;
        if (distinct_speaker_assignment(collision) != std::vector<int32_t>{1, 0})
            throw std::runtime_error("Joint assignment must outperform greedy collision repair");
        Eigen::MatrixXd shortage(3, 2);
        shortage << .9, .1, .1, .8, .3, .2;
        if (distinct_speaker_assignment(shortage) != std::vector<int32_t>{0, 1, -1})
            throw std::runtime_error("Insufficient clusters must retain an explicit abstention");
        // Compare the pruned solver to exhaustive assignments over all clusters.
        std::mt19937 random(42);
        std::uniform_real_distribution<double> uniform(-1, 1);
        for (int trial = 0; trial < 100; ++trial) {
            Eigen::MatrixXd scores(3, 7);
            for (int i = 0; i < scores.size(); ++i) scores.data()[i] = uniform(random);
            const auto result = distinct_speaker_assignment(scores);
            const double selected = scores(0, result[0]) + scores(1, result[1]) + scores(2, result[2]);
            double expected = -INFINITY;
            for (int a = 0; a < 7; ++a)
                for (int b = 0; b < 7; ++b)
                    for (int c = 0; c < 7; ++c)
                        if (a != b && a != c && b != c)
                            expected = std::max(expected, scores(0, a) + scores(1, b) + scores(2, c));
            if (std::abs(selected - expected) > 1e-12)
                throw std::runtime_error("Pruning lost the globally best assignment");
        }
        Eigen::MatrixXd embeddings(4, 2);
        embeddings << 1, 0, .8, .6, 1, 0, 0, 1;
        const std::vector<std::pair<int32_t, int32_t>> windows{{0, 0}, {0, 1}, {1, 0}, {2, 0}};
        const auto reassigned = constrained_centroid_assignment(embeddings, windows, {0, 0, 0, 1});
        if (reassigned != std::vector<int32_t>{0, 1, 0, 1})
            throw std::runtime_error("Same-window voices must use distinct acoustic centroids");
        if (constrained_centroid_assignment(embeddings, windows, {0, 0, 0, 0}) != std::vector<int32_t>(4, 0))
            throw std::runtime_error("Do not invent another speaker in a single-speaker partition");
        embeddings(1, 0) = NAN;
        bool rejected = false;
        try { (void)constrained_centroid_assignment(embeddings, windows, {0, 0, 0, 1}); }
        catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Nonfinite embeddings must fail");
        std::cout << "constrained assignment tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
