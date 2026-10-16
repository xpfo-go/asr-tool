// Spectral graph recipe: WeSpeaker, Copyright (c) 2022 Xu Xiang, Apache-2.0.
// NME search: NeMo, Copyright (c) 2021 NVIDIA CORPORATION & AFFILIATES, Apache-2.0.
// See docs/THIRD_PARTY.md for the pinned reference and implementation differences.
#include "spectral_clustering.hpp"

#include <Eigen/Eigenvalues>
#include <fastcluster-all-in-one.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>

namespace {

using Matrix = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

Eigen::MatrixXi neighbor_order(const Matrix& similarity) {
    const int rows = static_cast<int>(similarity.rows());
    Eigen::MatrixXi sorted(rows, rows);
    std::vector<int> order(rows);
    for (int row = 0; row < rows; ++row) {
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return similarity(row, a) != similarity(row, b)
                ? similarity(row, a) < similarity(row, b) : a < b;
        });
        for (int index = 0; index < rows; ++index) sorted(row, index) = order[index];
    }
    return sorted;
}

Matrix laplacian(const Eigen::MatrixXi& order, int neighbors) {
    const int rows = static_cast<int>(order.rows());
    Matrix graph = Matrix::Zero(rows, rows);
    for (int row = 0; row < rows; ++row)
        for (int index = rows - neighbors; index < rows; ++index) graph(row, order(row, index)) = 1;
    graph = (0.5 * (graph + graph.transpose())).eval();
    graph.diagonal().setZero();
    const Eigen::VectorXd degrees = graph.rowwise().sum();
    graph = -graph;
    graph.diagonal() = degrees;
    return graph;
}

int speaker_count(const Eigen::VectorXd& values) {
    const int limit = std::min(20, static_cast<int>(values.size()) - 1);
    int speakers = 1;
    double gap = -1;
    for (int index = 0; index < limit; ++index) {
        const double candidate = values[index + 1] - values[index];
        if (candidate > gap) { gap = candidate; speakers = index + 1; }
    }
    return speakers;
}

bool connected(const Matrix& graph) {
    std::vector<bool> visited(graph.rows(), false);
    std::vector<int> pending{0};
    visited[0] = true;
    for (size_t index = 0; index < pending.size(); ++index)
        for (int other = 0; other < graph.rows(); ++other)
            if (!visited[other] && graph(pending[index], other) < 0) {
                visited[other] = true;
                pending.push_back(other);
            }
    return pending.size() == static_cast<size_t>(graph.rows());
}

int auto_neighbor_count(const Matrix& similarity) {
    // Normalized maximum eigengap (Park et al., 2019 / NeMo NMESC).
    // Subsample only the search; final clustering still uses every embedding.
    const int stride = std::max(1, static_cast<int>(similarity.rows()) / 512);
    const int rows = (static_cast<int>(similarity.rows()) + stride - 1) / stride;
    Matrix sample(rows, rows);
    for (int a = 0; a < rows; ++a)
        for (int b = 0; b < rows; ++b) sample(a, b) = similarity(a * stride, b * stride);
    const auto order = neighbor_order(sample);
    const int maximum = std::max(2, static_cast<int>(rows * .15));
    const int trials = std::min(30, maximum);
    int best = maximum;
    double best_ratio = std::numeric_limits<double>::infinity();
    std::vector<int> candidates;
    for (int trial = 0; trial < trials; ++trial) {
        const int neighbors = 1 + trial * (maximum - 1) / (trials - 1);
        candidates.push_back(neighbors);
        Eigen::SelfAdjointEigenSolver<Matrix> solver(laplacian(order, neighbors), Eigen::EigenvaluesOnly);
        if (solver.info() != Eigen::Success) throw std::runtime_error("Neighbor-search eigensolver failed");
        const auto& values = solver.eigenvalues();
        const int speakers = speaker_count(values);
        const double gap = (values[speakers] - values[speakers - 1]) / (values.maxCoeff() + 1e-10);
        const double ratio = (static_cast<double>(neighbors) / rows) / (gap + 1e-10);
        if (ratio < best_ratio) { best_ratio = ratio; best = neighbors; }
    }
    if (!connected(laplacian(order, best))) {
        best = maximum;
        for (int candidate : candidates)
            if (connected(laplacian(order, candidate))) { best = candidate; break; }
    }
    return std::min(static_cast<int>(similarity.rows()), best * stride);
}

std::vector<int32_t> kmeans(const Matrix& points, int count) {
    const int rows = static_cast<int>(points.rows());
    std::mt19937 random(42);
    double best_cost = std::numeric_limits<double>::infinity();
    std::vector<int32_t> best;
    for (int attempt = 0; attempt < 10; ++attempt) {
        Matrix centers(count, points.cols());
        centers.row(0) = points.row(random() % rows);
        Eigen::VectorXd distances = Eigen::VectorXd::Constant(rows, std::numeric_limits<double>::infinity());
        // K-means++ seeding; generate the uniform value explicitly for portability.
        for (int cluster = 1; cluster < count; ++cluster) {
            for (int row = 0; row < rows; ++row)
                distances[row] = std::min(distances[row],
                    (points.row(row) - centers.row(cluster - 1)).squaredNorm());
            const double total = distances.sum();
            double target = (static_cast<double>(random()) / 4294967296.0) * total;
            int selected = rows - 1;
            for (int row = 0; row < rows; ++row) {
                target -= distances[row];
                if (target < 0) { selected = row; break; }
            }
            centers.row(cluster) = points.row(selected);
        }
        std::vector<int32_t> labels(rows, -1);
        bool converged = false;
        for (int iteration = 0; iteration < 300; ++iteration) {
            bool changed = false;
            for (int row = 0; row < rows; ++row) {
                int closest = 0;
                double distance = std::numeric_limits<double>::infinity();
                for (int cluster = 0; cluster < count; ++cluster) {
                    const double candidate = (points.row(row) - centers.row(cluster)).squaredNorm();
                    if (candidate < distance) { distance = candidate; closest = cluster; }
                }
                changed |= labels[row] != closest;
                labels[row] = closest;
                distances[row] = distance;
            }
            Matrix sums = Matrix::Zero(count, points.cols());
            std::vector<int> sizes(count, 0);
            for (int row = 0; row < rows; ++row) {
                sums.row(labels[row]) += points.row(row);
                ++sizes[labels[row]];
            }
            // Empty clusters take the farthest point from a non-singleton donor.
            for (int cluster = 0; cluster < count; ++cluster) {
                if (sizes[cluster]) continue;
                int farthest = -1;
                for (int row = 0; row < rows; ++row)
                    if (sizes[labels[row]] > 1 &&
                        (farthest < 0 || distances[row] > distances[farthest])) farthest = row;
                if (farthest < 0) throw std::runtime_error("Cannot repair an empty speaker cluster");
                const int donor = labels[farthest];
                sums.row(donor) -= points.row(farthest);
                --sizes[donor];
                labels[farthest] = cluster;
                sums.row(cluster) = points.row(farthest);
                sizes[cluster] = 1;
                changed = true;
            }
            for (int cluster = 0; cluster < count; ++cluster)
                centers.row(cluster) = sums.row(cluster) / sizes[cluster];
            if (!changed) { converged = true; break; }
        }
        if (!converged) continue;
        double cost = 0;
        for (int row = 0; row < rows; ++row)
            cost += (points.row(row) - centers.row(labels[row])).squaredNorm();
        if (cost < best_cost) { best_cost = cost; best = std::move(labels); }
    }
    if (best.empty()) throw std::runtime_error("Spectral k-means did not converge");
    return best;
}

std::vector<float> silhouettes(const Matrix& similarity, const std::vector<int32_t>& labels) {
    const int rows = static_cast<int>(labels.size());
    const int count = *std::max_element(labels.begin(), labels.end()) + 1;
    if (count == 1) return std::vector<float>(rows, rows == 1 ? 0.0f : -2.0f);
    std::vector<int> sizes(count, 0);
    for (int label : labels) ++sizes[label];
    std::vector<float> result(rows, 0);
    for (int row = 0; row < rows; ++row) {
        if (sizes[labels[row]] == 1) continue;
        std::vector<double> sums(count, 0);
        for (int other = 0; other < rows; ++other)
            if (other != row) sums[labels[other]] += std::max(0.0, 1.0 - similarity(row, other));
        const double within = sums[labels[row]] / (sizes[labels[row]] - 1);
        double nearest = std::numeric_limits<double>::infinity();
        for (int cluster = 0; cluster < count; ++cluster)
            if (cluster != labels[row] && sizes[cluster])
                nearest = std::min(nearest, sums[cluster] / sizes[cluster]);
        const double denominator = std::max(within, nearest);
        result[row] = denominator > 0 ? static_cast<float>((nearest - within) / denominator) : 0;
    }
    return result;
}

std::vector<int32_t> complete_linkage(const Matrix& similarity) {
    const int rows = static_cast<int>(similarity.rows());
    std::vector<double> distances;
    distances.reserve(static_cast<size_t>(rows) * (rows - 1) / 2);
    for (int a = 0; a < rows; ++a)
        for (int b = a + 1; b < rows; ++b) distances.push_back(std::max(0.0, 1.0 - similarity(a, b)));
    std::vector<int32_t> merge(2 * (rows - 1));
    std::vector<double> heights(rows - 1);
    fastclustercpp::hclust_fast(rows, distances.data(), fastclustercpp::HCLUST_METHOD_COMPLETE,
                              merge.data(), heights.data());
    std::vector<int32_t> labels(rows);
    fastclustercpp::cutree_cdist(rows, merge.data(), heights.data(), 1.05f, labels.data());
    return labels;
}

}  // namespace

SpectralClusteringResult spectral_cluster(const float* features, int rows, int columns,
                                          bool auto_neighbors, bool compare_linkage) {
    if (rows < 0 || rows > 4096 || columns <= 0 || columns > 4096 || (rows && !features))
        throw std::invalid_argument("Invalid or oversized spectral embedding matrix");
    if (!rows) return {};
    Eigen::Map<const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
        input(features, rows, columns);
    if (!input.allFinite()) throw std::invalid_argument("Non-finite speaker embedding");
    Matrix normalized = input.cast<double>();
    for (int row = 0; row < rows; ++row) {
        const double norm = normalized.row(row).norm();
        if (norm <= 0) throw std::invalid_argument("Zero speaker embedding");
        normalized.row(row) /= norm;
    }
    const Matrix similarity = normalized * normalized.transpose();
    SpectralClusteringResult result;
    if (rows <= 2 || (similarity.array() - 1.0).abs().maxCoeff() < 1e-12) {
        result.labels.assign(rows, 0);
    } else {
        const int pruned = rows < 1000 ? std::max(rows - 10, 2) : static_cast<int>(0.99 * rows);
        result.neighbors = auto_neighbors ? auto_neighbor_count(similarity) : rows - pruned;
        Eigen::SelfAdjointEigenSolver<Matrix> solver(laplacian(neighbor_order(similarity), result.neighbors));
        if (solver.info() != Eigen::Success) throw std::runtime_error("Spectral eigensolver failed");
        const int limit = std::min(20, rows - 1);
        const auto& values = solver.eigenvalues();
        result.eigenvalues.assign(values.data(), values.data() + limit + 1);
        const int speakers = speaker_count(values);
        result.labels = kmeans(solver.eigenvectors().leftCols(speakers), speakers);
    }
    result.silhouettes = silhouettes(similarity, result.labels);
    if (compare_linkage && rows > 2) {
        auto alternative = complete_linkage(similarity);
        auto alternative_confidence = silhouettes(similarity, alternative);
        const auto quality = [](const std::vector<float>& confidence) {
            return std::accumulate(confidence.begin(), confidence.end(), 0.0) / confidence.size();
        };
        result.spectral_quality = quality(result.silhouettes);
        result.complete_quality = quality(alternative_confidence);
        if (result.complete_quality >= result.spectral_quality) {
            result.labels = std::move(alternative);
            result.silhouettes = std::move(alternative_confidence);
            result.selected_complete_linkage = true;
        }
    }
    // Stable IDs follow first occurrence, independently of k-means initialization.
    std::vector<int32_t> mapping(rows, -1);
    int32_t next = 0;
    for (int32_t& label : result.labels) {
        if (mapping[label] < 0) mapping[label] = next++;
        label = mapping[label];
    }
    return result;
}
