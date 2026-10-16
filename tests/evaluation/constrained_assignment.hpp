#pragma once

#include <Eigen/Core>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

// Each segmentation window has at most three local speakers. Enumerating their
// best N candidates is exact: at most N-1 better candidates can be occupied.
inline std::vector<int32_t> distinct_speaker_assignment(const Eigen::MatrixXd& scores) {
    const int rows = static_cast<int>(scores.rows());
    const int columns = static_cast<int>(scores.cols());
    if (rows > 3 || columns == 0 || !scores.allFinite())
        throw std::invalid_argument("Invalid local speaker assignment matrix");
    const int required = std::min(rows, columns);
    std::vector<std::vector<int>> candidates(rows, std::vector<int>(columns));
    for (int row = 0; row < rows; ++row) {
        auto& order = candidates[row];
        std::iota(order.begin(), order.end(), 0);
        std::partial_sort(order.begin(), order.begin() + required, order.end(), [&](int a, int b) {
            return scores(row, a) == scores(row, b) ? a < b : scores(row, a) > scores(row, b);
        });
        order.resize(required);
    }
    std::vector<int32_t> current(rows, -1), best(rows, -1);
    double best_score = -std::numeric_limits<double>::infinity();
    std::function<void(int, int, double)> visit = [&](int row, int assigned, double score) {
        if (row == rows) {
            if (assigned == required && score > best_score) { best = current; best_score = score; }
            return;
        }
        for (int cluster : candidates[row]) {
            if (std::find(current.begin(), current.begin() + row, cluster) != current.begin() + row) continue;
            current[row] = cluster;
            visit(row + 1, assigned + 1, score + scores(row, cluster));
        }
        current[row] = -1;
        if (rows - row - 1 + assigned >= required) visit(row + 1, assigned, score);
    };
    visit(0, 0, 0);
    return best;
}

// Native evaluation of pyannote's centroid/cosine constrained assignment recipe.
// No transcript, reference identity, or reference speaker count is consumed.
template <typename Embeddings>
std::vector<int32_t> constrained_centroid_assignment(
    const Embeddings& embeddings, const std::vector<std::pair<int32_t, int32_t>>& chunk_speakers,
    const std::vector<int32_t>& labels) {
    if (embeddings.rows() != static_cast<int>(labels.size()) || labels.size() != chunk_speakers.size())
        throw std::invalid_argument("Speaker assignment dimensions differ");
    if (labels.empty()) return {};
    if (!embeddings.allFinite() || *std::min_element(labels.begin(), labels.end()) < 0)
        throw std::invalid_argument("Invalid embedding or cluster label");
    const int clusters = *std::max_element(labels.begin(), labels.end()) + 1;
    if (clusters > embeddings.rows()) throw std::invalid_argument("Invalid speaker count");
    Eigen::MatrixXd normalized = embeddings.template cast<double>();
    Eigen::MatrixXd centers = Eigen::MatrixXd::Zero(clusters, embeddings.cols());
    std::map<int32_t, std::vector<int>> groups;
    for (int row = 0; row < embeddings.rows(); ++row) {
        centers.row(labels[row]) += normalized.row(row);
        const double norm = normalized.row(row).norm();
        if (!(norm > 0)) throw std::invalid_argument("Zero speaker embedding");
        normalized.row(row) /= norm;
        groups[chunk_speakers[row].first].push_back(row);
    }
    for (int cluster = 0; cluster < clusters; ++cluster) {
        const double norm = centers.row(cluster).norm();
        if (!(norm > 0)) throw std::invalid_argument("Empty or degenerate speaker centroid");
        centers.row(cluster) /= norm;
    }
    // Like the reference recipe, an estimated single speaker stays one speaker.
    if (clusters == 1) return labels;
    const Eigen::MatrixXd similarity = normalized * centers.transpose();
    std::vector<int32_t> result(labels.size(), -1);
    for (const auto& group : groups) {
        Eigen::MatrixXd scores(group.second.size(), clusters);
        for (size_t i = 0; i < group.second.size(); ++i) scores.row(i) = similarity.row(group.second[i]);
        const auto assignment = distinct_speaker_assignment(scores);
        for (size_t i = 0; i < group.second.size(); ++i) result[group.second[i]] = assignment[i];
    }
    return result;
}
