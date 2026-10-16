#pragma once

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

// Evaluation-only minimum expected framewise speaker error. Marginalization
// preserves evidence from all powerset classes; output stays in the model's
// allowed sets (including its maximum number of simultaneous speakers).
template <typename Scores, typename Mapping>
Mapping minimum_risk_powerset_labels(const Scores& scores, const Mapping& mapping) {
    if (scores.cols() != mapping.rows() || mapping.rows() == 0 || mapping.cols() == 0)
        throw std::invalid_argument("Invalid powerset dimensions");
    for (int i = 0; i < mapping.rows(); ++i)
        for (int j = 0; j < mapping.cols(); ++j)
            if (mapping(i, j) != 0 && mapping(i, j) != 1)
                throw std::invalid_argument("Powerset mapping must be binary");

    Mapping result(scores.rows(), mapping.cols());
    std::vector<double> probabilities(mapping.rows()), marginals(mapping.cols());
    for (int frame = 0; frame < scores.rows(); ++frame) {
        const double maximum = scores.row(frame).maxCoeff();
        if (!std::isfinite(maximum)) throw std::invalid_argument("Invalid powerset scores");
        double total = 0;
        for (int c = 0; c < mapping.rows(); ++c) {
            const double value = scores(frame, c);
            if (std::isnan(value) || value == std::numeric_limits<double>::infinity())
                throw std::invalid_argument("Invalid powerset scores");
            probabilities[c] = std::exp(value - maximum);
            total += probabilities[c];
        }
        for (int speaker = 0; speaker < mapping.cols(); ++speaker) {
            double sum = 0;
            for (int c = 0; c < mapping.rows(); ++c)
                sum += probabilities[c] * mapping(c, speaker);
            marginals[speaker] = sum / total;
        }
        double best = -std::numeric_limits<double>::infinity();
        int selected = 0;
        for (int c = 0; c < mapping.rows(); ++c) {
            double utility = 0;
            for (int speaker = 0; speaker < mapping.cols(); ++speaker)
                utility += mapping(c, speaker) * (2 * marginals[speaker] - 1);
            if (utility > best) {
                best = utility;
                selected = c;
            }
        }
        result.row(frame) = mapping.row(selected);
    }
    return result;
}
