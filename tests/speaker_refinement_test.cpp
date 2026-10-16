#include "speaker_refinement.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Vector = std::vector<float>;
const Vector a{1, 0, 0}, b{0, 1, 0};

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool same(const std::vector<SpeakerTurn>& left, const std::vector<SpeakerTurn>& right) {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i)
        if (left[i].start_ms != right[i].start_ms || left[i].end_ms != right[i].end_ms ||
            left[i].speaker != right[i].speaker || left[i].confidence != right[i].confidence ||
            left[i].original_speaker != right[i].original_speaker) return false;
    return true;
}

std::vector<SpeakerTurn> fixture() {
    return {{0, 3000, 10, .6}, {4000, 7000, 10, .6}, {8000, 11000, 10, .6},
            {12000, 15000, 20, .6}, {16000, 19000, 20, .6}, {20000, 23000, 20, .6},
            {24000, 29000, 10, .2}, {29000, 32000, 20, .7}};
}

Vector acoustic(double start, double end) {
    if (end <= 11000) return a;
    if (end <= 23000 || start >= 29000) return b;
    return start >= 26000 ? b : a;
}

void test_split_and_coverage() {
    const auto original = fixture();
    std::vector<std::pair<double, double>> evidence;
    const auto result = refine_speaker_turns(original, [&](double start, double end) {
        check(end - start >= 1500 && end - start <= 3000, "bounded crop length");
        if (start >= 24000 && end <= 29000 && start >= 26000) evidence.emplace_back(start, end);
        return acoustic(start, end);
    });
    check(result.notice.empty() && result.changed_regions == 1, "one supported split expected");
    check(result.reliable_speakers == 2 && result.embeddings_requested == 15, "fixed seed/query workload");
    check(result.turns.size() == original.size() + 2, "split must preserve both unchanged portions");
    check(result.turns[7].start_ms == 27000 && result.turns[7].end_ms == 28500 &&
          result.turns[7].speaker == 20 && result.turns[7].confidence == -2 &&
          result.turns[7].original_speaker == 10, "only double-supported interval changes with audit metadata");
    check(result.turns[6].confidence == .2 && result.turns[8].confidence == .2 &&
          result.turns.back().start_ms == 29000 && result.turns.back().confidence == .7,
          "unchanged confidence and stable neighbor must survive");
    for (double time = 0; time < 33000; time += 73) {
        int before = 0, after = 0;
        for (const auto& t : original) before += t.start_ms <= time && time < t.end_ms;
        for (const auto& t : result.turns) {
            after += t.start_ms <= time && time < t.end_ms;
            if (t.confidence == -2 && t.start_ms <= time && time < t.end_ms) {
                const auto support = std::count_if(evidence.begin(), evidence.end(), [&](const auto& w) {
                    return w.first <= time && time < w.second;
                });
                check(support >= 2, "changed audio needs two supporting windows at every point");
            }
        }
        check(before == after, "no extra or missing audio coverage");
    }
    auto permuted = original;
    for (auto& t : permuted) t.speaker = t.speaker == 10 ? 900 : 2;
    auto renamed = refine_speaker_turns(permuted, acoustic).turns;
    for (auto& t : renamed) {
        t.speaker = t.speaker == 900 ? 10 : 20;
        if (t.original_speaker >= 0) t.original_speaker = t.original_speaker == 900 ? 10 : 20;
    }
    check(same(result.turns, renamed), "speaker-ID permutation must not affect decisions");
    auto audited = original;
    audited[6].original_speaker = 77;
    const auto preserved = refine_speaker_turns(audited, acoustic);
    check(preserved.turns[6].original_speaker == 77 && preserved.turns[8].original_speaker == 77 &&
          preserved.turns[7].original_speaker == 10, "unchanged metadata survives while changed region records prior speaker");
}

void test_abstentions() {
    const auto original = fixture();
    for (const Vector& invalid : std::vector<Vector>{{}, {NAN, 0, 0}, {INFINITY, 0, 0},
                                                    {0, 0, 0}, {1, 0}, {.7071f, .7071f, 0}, {0, 0, 1}}) {
        const auto result = refine_speaker_turns(original, [&](double start, double end) {
            return start >= 24000 && end <= 29000 ? invalid : acoustic(start, end);
        });
        check(result.changed_regions == 0 && same(result.turns, original), "bad/tied/missing query evidence must preserve exact input");
    }
    const auto inconsistent = refine_speaker_turns(original, [&](double start, double end) {
        return start >= 4000 && end <= 7000 ? b : acoustic(start, end);
    });
    check(inconsistent.changed_regions == 0 && same(inconsistent.turns, original), "unreliable old prototype must disable changes");
    const auto bad_winner = refine_speaker_turns(original, [&](double start, double end) {
        return start >= 16000 && end <= 19000 ? a : acoustic(start, end);
    });
    check(bad_winner.changed_regions == 0 && same(bad_winner.turns, original), "unreliable winning prototype must disable changes");
    const auto tied_seeds = refine_speaker_turns(original, [](double, double) { return a; });
    check(tied_seeds.reliable_speakers == 0 && same(tied_seeds.turns, original), "tied centroids must abstain");
    auto missing = original;
    missing[6].speaker = 99;
    check(same(refine_speaker_turns(missing, acoustic).turns, missing), "missing original prototype must abstain");
    auto too_few = original;
    too_few.erase(too_few.begin());
    check(same(refine_speaker_turns(too_few, acoustic).turns, too_few), "three distinct turns required");
    const auto broken_seed = refine_speaker_turns(original, [](double, double) { return Vector{NAN}; });
    check(!broken_seed.notice.empty() && same(broken_seed.turns, original), "invalid seed fallback");
    int calls = 0;
    const auto failure = refine_speaker_turns(original, [&](double start, double end) {
        if (++calls == 15) throw std::runtime_error("test failure");
        return acoustic(start, end);
    });
    check(!failure.notice.empty() && same(failure.turns, original), "late callback failure must roll back all changes");
}

void test_overlap_and_support() {
    auto original = fixture();
    original[6].end_ms = 31000;
    original[7].start_ms = 31000;
    original[7].end_ms = 34000;
    original.push_back({26900, 27100, 30, .1});
    const auto result = refine_speaker_turns(original, [&](double start, double end) {
        check(end <= 26900 || start >= 27100, "never embed across original overlap");
        return acoustic(start, end);
    });
    check(result.changed_regions > 0, "clean double-supported tail can still change");
    for (const auto& t : result.turns) {
        if (t.confidence == -2) check(t.start_ms >= 27100, "overlap must not be relabeled or bridged");
        if (t.speaker == 30) check(t.start_ms == 26900 && t.end_ms == 27100 && t.confidence == .1, "overlapping voice preserved");
    }
    const auto one_vote = refine_speaker_turns(fixture(), [&](double start, double end) {
        if (start >= 24000 && end <= 29000) return start == 26000 ? b : a;
        return acoustic(start, end);
    });
    check(same(one_vote.turns, fixture()), "one confident window cannot change a region");
    auto three_speakers = fixture();
    three_speakers.insert(three_speakers.end(), {{40000, 43000, 30, .6},
                          {44000, 47000, 30, .6}, {48000, 51000, 30, .6}});
    for (const auto& contrary : {a, Vector{0, 0, 1}}) {
        const auto conflict = refine_speaker_turns(three_speakers, [&](double start, double end) {
            if (start >= 40000) return Vector{0, 0, 1};
            if (start == 26500) return contrary;
            return acoustic(start, end);
        });
        check(same(conflict.turns, three_speakers), "strong original or third-speaker evidence must veto overlapping patches");
    }
    auto short_support = fixture();
    short_support[6].end_ms = 25500;
    check(same(refine_speaker_turns(short_support, acoustic).turns, short_support), "one-window turn must stay exact");
    auto fragmented = fixture();
    fragmented.insert(fragmented.end(), {{0, 100, 30, .1}, {4000, 4100, 30, .1}, {8000, 8100, 30, .1}});
    const auto seeds = refine_speaker_turns(fragmented, [&](double start, double end) {
        if (end <= 11000) check(start >= 100 && (start >= 4100 || end <= 4000) &&
                               (start >= 8100 || end <= 8000), "seeds must subtract overlap");
        return acoustic(start, end);
    });
    check(seeds.changed_regions == 1, "overlap subtraction must retain clean seeds from distinct turns");
}

void test_unreliable_contrary_vote() {
    auto original = fixture();
    original.insert(original.end(), {{40000, 43000, 30, .6}, {44000, 47000, 30, .6},
                                    {48000, 51000, 30, .6}});
    const Vector c_centroid{0, 0, 2 / std::sqrt(5.0f), 1 / std::sqrt(5.0f)};
    const auto callback = [&](double start, double end) {
        if (start >= 48000) return Vector{0, 0, 0, 1};
        if (start >= 40000) return Vector{0, 0, 1, 0};
        if (start == 26500) return c_centroid;
        auto v = acoustic(start, end);
        v.push_back(0);
        return v;
    };
    const auto vetoed = refine_speaker_turns(original, callback);
    check(vetoed.reliable_speakers == 2 && vetoed.notice.empty(), "A/B reliable, inconsistent C unreliable");
    check(vetoed.changed_regions == 0 && same(vetoed.turns, original),
          "strong C vote must veto B intersections even when C prototype is unreliable");
    const auto no_c_patch = refine_speaker_turns(original, [&](double start, double end) {
        return start >= 24000 && end <= 29000 ? c_centroid : callback(start, end);
    });
    check(no_c_patch.changed_regions == 0 && same(no_c_patch.turns, original),
          "unreliable C votes can veto but cannot create changes");
}

void test_long_turn_pairs() {
    for (double duration : {12000.0, 100000.0}) {
        auto original = fixture();
        original[6].end_ms = 24000 + duration;
        original[6].original_speaker = 77;
        original[7].start_ms = original[6].end_ms;
        original[7].end_ms = original[7].start_ms + 3000;
        std::vector<std::pair<double, double>> windows;
        const auto result = refine_speaker_turns(original, [&](double start, double end) {
            if (start >= 24000 && end <= original[6].end_ms) {
                windows.emplace_back(start, end);
                return b;
            }
            return acoustic(start, end);
        });
        check(result.notice.empty() && result.changed_regions == 4 && windows.size() == 8 &&
              result.embeddings_requested == 15, "long turns need four supported patches within eight queries");
        check(windows.front().first == 24000 && windows.back().second == original[6].end_ms,
              "pairs must span the turn timeline");
        for (size_t k = 0; k < windows.size(); k += 2)
            check(windows[k + 1].first - windows[k].first == 500 &&
                  windows[k].second - windows[k].first == 1500 &&
                  windows[k + 1].second - windows[k + 1].first == 1500,
                  "each sampled pair needs fixed windows and a 500ms hop");
        size_t changed = 0;
        double cursor = 24000;
        for (const auto& t : result.turns) {
            if (t.start_ms < 24000 || t.start_ms >= original[6].end_ms) continue;
            check(t.start_ms == cursor, "query turn must retain exact contiguous coverage");
            cursor = t.end_ms;
            if (t.confidence == -2) {
                check(changed < 4 && t.start_ms == windows[changed * 2 + 1].first &&
                      t.end_ms == windows[changed * 2].second && t.speaker == 20 && t.original_speaker == 10,
                      "only double-supported pair intersections may change");
                ++changed;
            } else check(t.speaker == 10 && t.confidence == .2 && t.original_speaker == 77,
                         "unsampled gaps must retain original identity and metadata");
        }
        check(changed == 4 && cursor == original[6].end_ms, "all supported patches and remaining coverage retained");
        check(same({result.turns.back()}, {original.back()}), "stable neighbor must stay exact");
    }
}

void test_rollback_after_staged_patch() {
    auto original = fixture();
    original.push_back({33000, 38000, 10, .2, 77});
    check(refine_speaker_turns(fixture(), acoustic).changed_regions == 1, "first ambiguous turn must stage a patch");
    for (bool throw_error : {false, true}) {
        int first_queries = 0, second_queries = 0;
        const auto result = refine_speaker_turns(original, [&](double start, double end) {
            if (start >= 24000 && end <= 29000) ++first_queries;
            if (start >= 33000 && ++second_queries == 2) {
                if (throw_error) throw std::runtime_error("failure during second ambiguous turn");
                return Vector{NAN, 0, 0};
            }
            return acoustic(start, end);
        });
        check(first_queries == 8 && second_queries == 2 && result.embeddings_requested == 17,
              "failure must occur after completing first turn and starting second");
        check(!result.notice.empty() && result.changed_regions == 0 && same(result.turns, original),
              "second-turn failure must roll back staged patches and preserve exact original metadata");
    }
}

void test_bounds() {
    auto original = fixture();
    for (int i = 0; i < 70; ++i) original.push_back({40000.0 + i * 6000, 45000.0 + i * 6000, 10, .2});
    int calls = 0;
    const auto capped = refine_speaker_turns(original, [&](double, double) { ++calls; return a; });
    check(calls == 0 && !capped.notice.empty() && same(capped.turns, original), "over-budget workload must return exact input before inference");
    auto many_seeds = fixture();
    for (int i = 0; i < 100; ++i) many_seeds.push_back({40000.0 + i * 4000, 43000.0 + i * 4000, 10, .6});
    int a_seeds = 0;
    const auto bounded = refine_speaker_turns(many_seeds, [&](double start, double end) {
        if (end <= 11000 || start >= 40000) { ++a_seeds; return a; }
        return acoustic(start, end);
    });
    check(a_seeds == 8 && bounded.embeddings_requested == 20, "seed cap must bound distinct-turn work");
    check(refine_speaker_turns({}, acoustic).turns.empty(), "empty input");
}
}  // namespace

int main() {
    try {
        test_split_and_coverage();
        test_abstentions();
        test_overlap_and_support();
        test_unreliable_contrary_vote();
        test_long_turn_pairs();
        test_rollback_after_staged_patch();
        test_bounds();
        std::cout << "speaker refinement tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
