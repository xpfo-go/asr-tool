#pragma once

// Beam pruning/ranking adapted from FireRedASR2S transformer_decoder.py.
// Copyright 2026 Xiaohongshu (Author: Kaituo Xu).
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

struct TokenHypothesis {
    std::vector<int> tokens;
    double score = 0;
    bool ended = false;
    size_t parent = 0;
};

// FireRedASR2's beam pruning and final GNMT length penalty, without tensor state.
class TokenBeam {
public:
    TokenBeam(int width, int eos, double smoothing, double length_penalty)
        : width_(width), eos_(eos), smoothing_(smoothing), penalty_(length_penalty) {
        if (width < 1 || eos < 0 || !std::isfinite(smoothing) || smoothing <= 0 ||
            !std::isfinite(length_penalty) || length_penalty < 0)
            throw std::runtime_error("Invalid beam search settings");
    }

    const std::vector<TokenHypothesis>& hypotheses() const { return hypotheses_; }

    bool finished() const {
        return std::all_of(hypotheses_.begin(), hypotheses_.end(),
                           [](const auto& h) { return h.ended; });
    }

    void advance(const std::vector<std::vector<float>>& logits) {
        if (logits.size() != hypotheses_.size())
            throw std::runtime_error("Beam logits and hypotheses differ");
        std::vector<TokenHypothesis> candidates;
        for (size_t parent = 0; parent < hypotheses_.size(); ++parent) {
            const auto& hypothesis = hypotheses_[parent];
            if (hypothesis.ended) {
                auto retained = hypothesis;
                retained.parent = parent;
                candidates.push_back(std::move(retained));
                continue;
            }
            const auto& values = logits[parent];
            if (values.size() <= static_cast<size_t>(eos_) ||
                std::any_of(values.begin(), values.end(), [](float x) {
                    return std::isnan(x) || x == INFINITY;
                })) throw std::runtime_error("Invalid beam logits");
            const double maximum = *std::max_element(values.begin(), values.end());
            if (!std::isfinite(maximum)) throw std::runtime_error("No finite beam logits");
            double sum = 0;
            for (float value : values) sum += std::exp((value - maximum) / smoothing_);
            const double normalizer = std::log(sum);
            std::vector<int> order(values.size());
            std::iota(order.begin(), order.end(), 0);
            const size_t count = std::min(order.size(), static_cast<size_t>(width_));
            std::partial_sort(order.begin(), order.begin() + count, order.end(),
                [&](int a, int b) { return values[a] != values[b] ? values[a] > values[b] : a < b; });
            for (size_t i = 0; i < count; ++i) {
                const int token = order[i];
                if (!std::isfinite(values[token])) continue;
                auto candidate = hypothesis;
                candidate.parent = parent;
                candidate.score += (values[token] - maximum) / smoothing_ - normalizer;
                candidate.ended = token == eos_;
                if (!candidate.ended) candidate.tokens.push_back(token);
                candidates.push_back(std::move(candidate));
            }
        }
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const auto& a, const auto& b) { return a.score > b.score; });
        if (candidates.size() > static_cast<size_t>(width_)) candidates.resize(width_);
        if (candidates.empty()) throw std::runtime_error("Beam search has no surviving path");
        hypotheses_ = std::move(candidates);
    }

    const TokenHypothesis& best() const {
        const auto rank = [&](const auto& h) {
            // The upstream length includes SOS and excludes EOS.
            return h.score / std::pow((6.0 + h.tokens.size()) / 6.0, penalty_);
        };
        const auto best = std::max_element(hypotheses_.begin(), hypotheses_.end(),
            [&](const auto& a, const auto& b) { return rank(a) < rank(b); });
        if (!best->ended) throw std::runtime_error("Beam output did not terminate before the decoder limit");
        return *best;
    }

private:
    int width_;
    int eos_;
    double smoothing_;
    double penalty_;
    std::vector<TokenHypothesis> hypotheses_{1};
};
