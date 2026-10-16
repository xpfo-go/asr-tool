#include "token_beam.hpp"

#include <iostream>
#include <limits>

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void decode_step(TokenBeam& beam) {
    std::vector<std::vector<float>> scores;
    for (const auto& h : beam.hypotheses()) {
        if (h.ended) scores.push_back({});
        else if (h.tokens.empty()) scores.push_back({std::log(.55f), std::log(.44f), std::log(.01f)});
        else if (h.tokens.back() == 0) scores.push_back({std::log(.34f), std::log(.33f), std::log(.33f)});
        else scores.push_back({std::log(.005f), std::log(.005f), std::log(.99f)});
    }
    beam.advance(scores);
}

}  // namespace

int main() {
    try {
        TokenBeam beam(3, 2, 1, 0);
        decode_step(beam);
        decode_step(beam);
        check(beam.best().tokens == std::vector<int>{1},
              "search must retain a weaker first token that yields the better complete path");
        check(beam.best().parent == 1, "the selected path must retain its decoder-cache parent");
        const auto score = beam.best().score;
        for (int i = 0; i < 3 && !beam.finished(); ++i) decode_step(beam);
        check(beam.best().tokens == std::vector<int>{1} && beam.best().score == score,
              "finished hypotheses must retain their score and text without extra EOS penalties");

        TokenBeam greedy(1, 2, 1, 0);
        decode_step(greedy);
        decode_step(greedy);
        bool rejected = false;
        try { (void)greedy.best(); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "an exhausted decode must not silently return unfinished text");
        check(greedy.hypotheses()[0].tokens == std::vector<int>({0, 0}),
              "width one must follow the per-step argmax");

        TokenBeam repeated(1, 2, 1, 0);
        repeated.advance({{3, 0, -1}});
        repeated.advance({{3, 0, -1}});
        repeated.advance({{0, 0, 3}});
        check(repeated.finished() && repeated.best().tokens == std::vector<int>({0, 0}),
              "legitimate repeated tokens must survive");

        for (double penalty : {0.0, 0.6}) {
            TokenBeam ranked(2, 2, 1, penalty);
            ranked.advance({{std::log(.49f), -INFINITY, std::log(.51f)}});
            ranked.advance({{}, {std::log(.01f), -INFINITY, std::log(.99f)}});
            check(ranked.finished() && ranked.best().tokens.size() == (penalty == 0 ? 0 : 1),
                  "final length normalization must include SOS and exclude EOS");
        }

        TokenBeam stable(1, 2, 1, 0);
        stable.advance({{10000, 9999, 9998}});
        check(std::isfinite(stable.hypotheses()[0].score), "softmax must remain stable for large logits");
        rejected = false;
        try { stable.advance({{0, std::numeric_limits<float>::quiet_NaN(), 0}}); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "NaN logits must invalidate the decode");
        std::cout << "token beam tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
