#include "spectral_clustering.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void rejects(const std::vector<float>& data, int rows, int columns) {
    try {
        spectral_cluster(data.data(), rows, columns);
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Invalid embedding matrix was accepted");
}

}  // namespace

int main() {
    try {
        check(spectral_cluster(nullptr, 0, 3).labels.empty(), "empty speech must remain empty");
        rejects({}, -1, 3);
        rejects({}, 4097, 3);
        rejects({}, 1, 0);
        rejects({}, 1, 5000);
        rejects({0, 0, 0}, 1, 3);
        rejects({1, std::numeric_limits<float>::quiet_NaN(), 0}, 1, 3);
        rejects({1, std::numeric_limits<float>::infinity(), 0}, 1, 3);
        const std::vector<float> single{1, 2, 3};
        const auto one = spectral_cluster(single.data(), 1, 3);
        check(one.labels == std::vector<int32_t>{0} && one.silhouettes == std::vector<float>{0},
              "one observation has no measurable confidence");
        const std::vector<float> pair{1, 0, 0, 1};
        const auto two = spectral_cluster(pair.data(), 2, 2);
        check(two.labels == std::vector<int32_t>({0, 0}) && two.silhouettes[0] == -2,
              "two observations cannot establish spectral speaker count");
        const std::vector<float> identical(60, 1);
        const auto same = spectral_cluster(identical.data(), 20, 3);
        check(*std::max_element(same.labels.begin(), same.labels.end()) == 0 && same.silhouettes[0] == -2,
              "identical embeddings must not produce fabricated speakers");

        std::vector<float> features;
        for (int speaker = 0; speaker < 3; ++speaker)
            for (int point = 0; point < 30; ++point)
                for (int coordinate = 0; coordinate < 3; ++coordinate)
                    features.push_back(speaker == coordinate ? 1.0f : 0.0f);
        const auto original = features;
        const auto clustered = spectral_cluster(features.data(), 90, 3);
        check(features == original, "clustering must preserve acoustic embeddings");
        check(clustered.labels.size() == 90 && clustered.silhouettes.size() == 90,
              "each embedding must retain a label and confidence");
        for (int point = 0; point < 90; ++point) {
            check(clustered.labels[point] == point / 30, "well-separated acoustic groups must remain distinct");
            check(clustered.silhouettes[point] > .99f && clustered.silhouettes[point] <= 1,
                  "silhouette must use unmodified cosine distances");
        }
        const auto repeat = spectral_cluster(features.data(), 90, 3);
        check(clustered.labels == repeat.labels && clustered.silhouettes == repeat.silhouettes,
              "identical input must produce deterministic output");
        std::vector<float> permuted;
        for (int point = 0; point < 90; ++point) {
            const int source = (point * 17) % 90;
            permuted.insert(permuted.end(), features.begin() + source * 3, features.begin() + source * 3 + 3);
        }
        const auto reordered = spectral_cluster(permuted.data(), 90, 3);
        for (int a = 0; a < 90; ++a)
            for (int b = 0; b < 90; ++b)
                check((reordered.labels[a] == reordered.labels[b]) ==
                      (((a * 17) % 90) / 30 == ((b * 17) % 90) / 30),
                      "reordering must preserve a separated partition");
        // The pinned Python reference also over-splits this local-neighbor graph.
        // Record the limitation instead of silently forcing the known count.
        std::mt19937 random(51);
        for (float& value : features)
            value += (static_cast<int>(random() % 1000) - 500) * 0.00001f;
        const auto noisy = spectral_cluster(features.data(), 90, 3);
        check(*std::max_element(noisy.labels.begin(), noisy.labels.end()) + 1 == 12,
              "fixed-neighbor eigengap must reproduce the reference's documented limitation");
        auto hybrid_features = features;
        for (float& value : hybrid_features) value -= 1.0f / 3;
        const auto selected = spectral_cluster(hybrid_features.data(), 90, 3, false, true);
        check(selected.selected_complete_linkage && selected.complete_quality > selected.spectral_quality,
              "acoustic quality selection must reject the over-split synthetic partition");
        for (int point = 0; point < 90; ++point)
            check(selected.labels[point] == point / 30, "quality selection must preserve separated voices");
        const auto adaptive = spectral_cluster(features.data(), 90, 3, true);
        check(adaptive.neighbors > 0 && adaptive.neighbors <= 13,
              "automatic neighbors must stay within the fixed search budget");
        for (int point = 0; point < 90; ++point)
            check(adaptive.labels[point] == point / 30, "automatic neighbors must recover the noisy three-group fixture");
        std::vector<float> extended;
        for (int speaker = 0; speaker < 3; ++speaker)
            for (int point = 0; point < 342; ++point)
                for (int coordinate = 0; coordinate < 3; ++coordinate)
                    extended.push_back(speaker == coordinate ? 1.0f + point % 7 : 0.0f);
        const auto subsampled = spectral_cluster(extended.data(), 1026, 3, true);
        check(subsampled.labels.size() == 1026, "neighbor search must not drop unsampled embeddings");
        for (int point = 0; point < 1026; ++point)
            check(subsampled.labels[point] == point / 342,
                  "subsampled search and varying amplitudes must preserve well-separated speakers");
        std::cout << "spectral clustering tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
