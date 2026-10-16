#include "speaker_refinement.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace {
constexpr size_t max_embeddings = 512;
constexpr size_t max_crops = 8;
constexpr double window_ms = 1500;
constexpr double stride_ms = 500;
struct Crop { double start, end; size_t turn; };
using Vector = std::vector<float>;

std::vector<Crop> clean_fragments(const std::vector<SpeakerTurn>& turns, size_t index) {
    std::vector<Crop> pieces{{turns[index].start_ms, turns[index].end_ms, index}};
    for (size_t i = 0; i < turns.size(); ++i) {
        if (i == index) continue;
        std::vector<Crop> next;
        for (const auto& p : pieces) {
            if (turns[i].end_ms <= p.start || turns[i].start_ms >= p.end) next.push_back(p);
            else {
                if (p.start < turns[i].start_ms) next.push_back({p.start, turns[i].start_ms, index});
                if (turns[i].end_ms < p.end) next.push_back({turns[i].end_ms, p.end, index});
            }
        }
        pieces = std::move(next);
    }
    return pieces;
}

std::vector<Crop> spread(std::vector<Crop> pool) {
    std::sort(pool.begin(), pool.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    if (pool.size() <= max_crops) return pool;
    const auto middle = [](const Crop& c) { return (c.start + c.end) / 2; };
    const double first = middle(pool.front()), last = middle(pool.back());
    std::vector<Crop> selected;
    for (size_t i = 0; i < max_crops; ++i) {
        const double target = first + (last - first) * i / (max_crops - 1);
        const auto nearest = std::min_element(pool.begin(), pool.end(), [&](const auto& a, const auto& b) {
            return std::abs(middle(a) - target) < std::abs(middle(b) - target);
        });
        selected.push_back(*nearest);
        pool.erase(nearest);
    }
    return selected;
}

void normalize(Vector& v) {
    double sum = 0;
    for (float x : v) sum += static_cast<double>(x) * x;
    if (!std::isfinite(sum) || sum < 1e-16) throw std::runtime_error("Invalid speaker embedding");
    for (float& x : v) x = static_cast<float>(x / std::sqrt(sum));
}

double cosine(const Vector& a, const Vector& b) {
    double value = 0;
    for (size_t i = 0; i < a.size(); ++i) value += static_cast<double>(a[i]) * b[i];
    return value;
}

Vector centroid(const std::vector<Vector>& vectors, size_t omit = max_embeddings) {
    Vector mean(vectors.front().size(), 0);
    for (size_t i = 0; i < vectors.size(); ++i)
        if (i != omit)
            for (size_t j = 0; j < mean.size(); ++j) mean[j] += vectors[i][j];
    normalize(mean);
    return mean;
}
}  // namespace

SpeakerRefinementResult refine_speaker_turns(const std::vector<SpeakerTurn>& turns,
                                            const SpeakerEmbeddingCallback& embedding) {
    SpeakerRefinementResult result{turns, 0, 0, 0, {}};
    try {
        for (const auto& t : turns)
            if (!std::isfinite(t.start_ms) || !std::isfinite(t.end_ms) || !std::isfinite(t.confidence) ||
                t.start_ms < 0 || t.end_ms <= t.start_ms)
                throw std::runtime_error("Invalid speaker turn; refinement skipped");
        std::map<int, std::vector<Crop>> seeds;
        for (size_t i = 0; i < turns.size(); ++i) {
            if (turns[i].speaker < 0 || turns[i].confidence < .4) continue;
            const auto pieces = clean_fragments(turns, i);
            const auto longest = std::max_element(pieces.begin(), pieces.end(), [](const auto& a, const auto& b) {
                return a.end - a.start < b.end - b.start;
            });
            if (longest == pieces.end() || longest->end - longest->start < 2000) continue;
            const double width = std::min(3000.0, longest->end - longest->start);
            const double start = (longest->start + longest->end - width) / 2;
            seeds[turns[i].speaker].push_back({start, start + width, i});
        }
        size_t planned = 0;
        for (auto it = seeds.begin(); it != seeds.end();) {
            if (it->second.size() < 3) it = seeds.erase(it);
            else { it->second = spread(std::move(it->second)); planned += it->second.size(); ++it; }
        }
        if (seeds.size() < 2) return result;
        std::vector<std::vector<Crop>> queries(turns.size());
        for (size_t i = 0; i < turns.size() && planned <= max_embeddings; ++i) {
            const auto& t = turns[i];
            if (t.confidence >= .35 || !seeds.count(t.speaker) || t.end_ms - t.start_ms < window_ms) continue;
            const double last = t.end_ms - window_ms;
            const double steps = std::floor((last - t.start_ms) / stride_ms);
            std::vector<double> starts;
            if (last - t.start_ms <= (max_crops - 1) * stride_ms) {
                for (int k = 0; k <= steps; ++k) starts.push_back(t.start_ms + k * stride_ms);
                if (last - starts.back() > 10) starts.push_back(last);
            } else {
                // Spread pairs, not isolated windows: each pair retains double-supported audio.
                constexpr size_t pairs = max_crops / 2;
                const double last_pair = last - stride_ms;
                for (size_t k = 0; k < pairs; ++k) {
                    const double start = k + 1 == pairs ? last_pair : t.start_ms +
                        std::round((last_pair - t.start_ms) * k / (pairs - 1) / stride_ms) * stride_ms;
                    starts.push_back(start);
                    starts.push_back(start + stride_ms);
                }
            }
            std::vector<Crop> crops;
            for (double start : starts) crops.push_back({start, start + window_ms, i});
            for (const auto& c : spread(std::move(crops))) {
                bool overlap = false;
                for (size_t j = 0; j < turns.size(); ++j)
                    overlap |= j != i && turns[j].start_ms < c.end && turns[j].end_ms > c.start;
                if (!overlap) queries[i].push_back(c);
            }
            planned += queries[i].size();
        }
        if (planned > max_embeddings) {
            result.notice = "Speaker refinement exceeds 512 embeddings; original turns retained";
            return result;
        }
        if (std::all_of(queries.begin(), queries.end(), [](const auto& q) { return q.empty(); })) return result;
        size_t dimension = 0;
        const auto extract = [&](const Crop& c) {
            ++result.embeddings_requested;
            Vector v = embedding(c.start, c.end);
            if (!dimension) dimension = v.size();
            if (v.empty() || v.size() != dimension) throw std::runtime_error("Missing/inconsistent speaker embedding");
            normalize(v);
            return v;
        };
        std::map<int, std::vector<Vector>> references;
        std::map<int, Vector> centroids;
        for (const auto& entry : seeds) {
            auto& refs = references[entry.first];
            for (const auto& c : entry.second) refs.push_back(extract(c));
            centroids[entry.first] = centroid(refs);
        }
        std::map<int, bool> reliable;
        for (const auto& entry : references) {
            bool valid = true;
            for (size_t i = 0; i < entry.second.size(); ++i) {
                const double own = cosine(entry.second[i], centroid(entry.second, i));
                double other = -std::numeric_limits<double>::infinity();
                for (const auto& c : centroids)
                    if (c.first != entry.first) other = std::max(other, cosine(entry.second[i], c.second));
                valid &= own >= .5 && own - other >= .15;
            }
            reliable[entry.first] = valid;
            result.reliable_speakers += valid;
        }
        std::vector<SpeakerTurn> output;
        size_t changes = 0;
        for (size_t i = 0; i < turns.size(); ++i) {
            const auto& original = turns[i];
            std::vector<std::pair<Crop, int>> votes;
            if (reliable[original.speaker]) for (const auto& c : queries[i]) {
                const auto v = extract(c);
                double best = -2, second = -2;
                int winner = -1;
                for (const auto& candidate : centroids) {
                    const double score = cosine(v, candidate.second);
                    if (score > best) { second = best; best = score; winner = candidate.first; }
                    else second = std::max(second, score);
                }
                if (best >= .5 && best - second >= .15)
                    votes.push_back({c, winner});
            }
            std::vector<SpeakerTurn> patches;
            for (size_t a = 0; a < votes.size(); ++a) for (size_t b = a + 1; b < votes.size(); ++b) {
                if (votes[a].second == original.speaker || votes[a].second != votes[b].second ||
                    !reliable[votes[a].second]) continue;
                const double start = std::max(votes[a].first.start, votes[b].first.start);
                const double end = std::min(votes[a].first.end, votes[b].first.end);
                // Strong contrary evidence vetoes the entire proposed intersection.
                const bool contrary = std::any_of(votes.begin(), votes.end(), [&](const auto& vote) {
                    return vote.second != votes[a].second && vote.first.start < end && vote.first.end > start;
                });
                if (end - start >= stride_ms && !contrary) {
                    auto patch = original;
                    patch.start_ms = start;
                    patch.end_ms = end;
                    patch.speaker = votes[a].second;
                    patch.confidence = -2;
                    patch.original_speaker = original.speaker;
                    patches.push_back(patch);
                }
            }
            std::sort(patches.begin(), patches.end(), [](const auto& a, const auto& b) { return a.start_ms < b.start_ms; });
            std::vector<SpeakerTurn> merged;
            bool conflict = false;
            for (const auto& p : patches) {
                if (!merged.empty() && p.start_ms <= merged.back().end_ms) {
                    if (p.speaker != merged.back().speaker) { conflict = true; break; }
                    merged.back().end_ms = std::max(merged.back().end_ms, p.end_ms);
                } else merged.push_back(p);
            }
            if (conflict) merged.clear();
            double cursor = original.start_ms;
            for (const auto& p : merged) {
                if (cursor < p.start_ms) {
                    auto unchanged = original;
                    unchanged.start_ms = cursor;
                    unchanged.end_ms = p.start_ms;
                    output.push_back(unchanged);
                }
                output.push_back(p);
                cursor = p.end_ms;
                ++changes;
            }
            if (cursor < original.end_ms) {
                auto unchanged = original;
                unchanged.start_ms = cursor;
                output.push_back(unchanged);
            }
        }
        if (changes) {
            std::stable_sort(output.begin(), output.end(), [](const auto& a, const auto& b) { return a.start_ms < b.start_ms; });
            result.turns = std::move(output);
            result.changed_regions = changes;
        }
    } catch (const std::exception& e) {
        result.notice = e.what();
    } catch (...) {
        result.notice = "Speaker embedding failed; original turns retained";
    }
    return result;
}
