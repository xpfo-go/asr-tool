#include "meeting.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <cctype>
#include <set>

namespace {

bool complete_utf8(const std::string& text) {
    size_t pos = 0;
    while (pos < text.size()) {
        const auto c = static_cast<unsigned char>(text[pos]);
        const size_t width = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 :
                             (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
        if (width == 0 || pos + width > text.size()) return false;
        for (size_t i = 1; i < width; ++i) {
            if ((static_cast<unsigned char>(text[pos + i]) & 0xc0) != 0x80) return false;
        }
        pos += width;
    }
    return true;
}

bool ascii_word(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) && static_cast<unsigned char>(c) < 128;
}

std::string sensitive_content(const std::string& input) {
    std::string text = input;
    for (size_t pos = 0; (pos = text.find("\xe2\x80\x99", pos)) != std::string::npos; ++pos)
        text.replace(pos, 3, "'");
    std::string result;
    const std::string numbers = "零〇一二三四五六七八九十百千万亿两点负壹贰叁肆伍陆柒捌玖拾佰仟萬億兩";
    const std::string negations = "不没无否未非";
    static const std::set<std::string> protected_words{
        "no", "not", "never", "without", "cannot", "neither", "nor", "none",
        "nobody", "nothing", "nowhere", "zero", "one", "two", "three", "four",
        "five", "six", "seven", "eight", "nine", "ten", "eleven", "twelve",
        "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen",
        "nineteen", "twenty", "thirty", "forty", "fifty", "sixty", "seventy",
        "eighty", "ninety", "hundred", "thousand", "million", "billion", "trillion",
        "first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth",
        "ninth", "tenth", "half", "quarter", "dozen", "minus", "plus", "percent"};
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c >= 128) {
            const size_t width = (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : 4;
            const auto character = text.substr(i, width);
            if (numbers.find(character) != std::string::npos ||
                negations.find(character) != std::string::npos) result += character + "|";
            i += width;
        } else if (std::isdigit(c) || ((text[i] == '-' || text[i] == '+' || text[i] == '.') &&
                   i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
            const size_t begin = i++;
            while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) ||
                    text[i] == '.' || text[i] == ',' || text[i] == '/' || text[i] == '%')) ++i;
            result += text.substr(begin, i - begin) + "|";
        } else if (ascii_word(text[i]) || text[i] == '\'') {
            const size_t begin = i++;
            while (i < text.size() && (ascii_word(text[i]) || text[i] == '\'')) ++i;
            auto word = text.substr(begin, i - begin);
            std::transform(word.begin(), word.end(), word.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (protected_words.count(word) ||
                (word.size() >= 3 && word.compare(word.size() - 3, 3, "n't") == 0)) {
                result += word + "|";
            }
            for (char ch : word) if (ch >= '0' && ch <= '9') result += std::string(1, ch) + "|";
        } else {
            ++i;
        }
    }
    return result;
}

std::vector<uint32_t> wording(const std::string& text) {
    std::vector<uint32_t> characters;
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i++]);
        uint32_t character = c;
        const size_t continuation = c < 0x80 ? 0 : (c & 0xe0) == 0xc0 ? 1 :
                                    (c & 0xf0) == 0xe0 ? 2 : 3;
        if (continuation) {
            character = c & (0x7f >> continuation);
            for (size_t j = 0; j < continuation; ++j) {
                character = (character << 6) | (static_cast<unsigned char>(text[i++]) & 0x3f);
            }
        }
        if (character < 128) {
            if (!std::isalnum(static_cast<unsigned char>(character))) continue;
            character = static_cast<uint32_t>(std::tolower(static_cast<unsigned char>(character)));
        } else if (character == 0x3000 || character == 0x3001 || character == 0x3002 ||
                   character == 0xff0c || character == 0xff1a || character == 0xff1b ||
                   character == 0xff01 || character == 0xff1f ||
                   (character >= 0x2018 && character <= 0x201d)) {
            continue;
        }
        characters.push_back(character);
    }
    return characters;
}

}  // namespace

namespace {

bool correction_is_bounded(const std::string& original, const std::string& candidate,
                           double maximum_edit_fraction) {
    if (!complete_utf8(original) || !complete_utf8(candidate) ||
        sensitive_content(original) != sensitive_content(candidate)) {
        return false;
    }
    const auto before = wording(original);
    const auto after = wording(candidate);
    if (before.empty() || after.empty()) return before == after;
    std::vector<size_t> distance(after.size() + 1);
    for (size_t j = 0; j < distance.size(); ++j) distance[j] = j;
    for (size_t i = 0; i < before.size(); ++i) {
        size_t diagonal = distance[0]++;
        for (size_t j = 0; j < after.size(); ++j) {
            const size_t previous = distance[j + 1];
            distance[j + 1] = std::min({previous + 1, distance[j] + 1,
                                       diagonal + (before[i] != after[j])});
            diagonal = previous;
        }
    }
    return distance.back() <= std::max(before.size(), after.size()) * maximum_edit_fraction;
}

}  // namespace

bool correction_is_conservative(const std::string& original, const std::string& candidate) {
    // A higher token score alone must not justify rewriting a whole passage.
    return correction_is_bounded(original, candidate, 0.25);
}

bool correction_is_live_review_safe(const std::string& original, const std::string& candidate) {
    // Independent audio review can repair a bad streaming hypothesis, but it
    // still has to preserve sensitive content and stay within a bounded edit.
    return correction_is_bounded(original, candidate, 0.60);
}

double repetition_ratio(const std::vector<int32_t>& tokens) {
    std::set<std::vector<int32_t>> ngrams;
    size_t repeated = 0;
    for (size_t i = 0; i + 4 <= tokens.size(); ++i) {
        if (!ngrams.insert({tokens.begin() + i, tokens.begin() + i + 4}).second) ++repeated;
    }
    return tokens.empty() ? 0.0 : static_cast<double>(repeated) / tokens.size();
}

int choose_language(const std::vector<float>& probabilities, int previous_language) {
    if (probabilities.empty()) return -1;
    const auto best = std::max_element(probabilities.begin(), probabilities.end());
    const int detected = static_cast<int>(best - probabilities.begin());
    if (previous_language >= 0 && previous_language < static_cast<int>(probabilities.size())) {
        const float previous = probabilities[previous_language];
        // Use continuity only when the audio itself still supports the previous language.
        if (*best < .75f && previous >= .15f && *best < previous * 4.0f) {
            return previous_language;
        }
    }
    return detected;
}

std::vector<SpeechWindow> plan_speech_windows(
    const std::vector<TimeRange>& speech, double duration_ms) {
    std::vector<TimeRange> ranges;
    for (const auto& span : speech) {
        if (!std::isfinite(span.start_ms) || !std::isfinite(span.end_ms)) {
            continue;
        }
        const TimeRange clipped{std::clamp(span.start_ms, 0.0, duration_ms),
                                std::clamp(span.end_ms, 0.0, duration_ms)};
        if (clipped.end_ms > clipped.start_ms) {
            ranges.push_back(clipped);
        }
    }
    std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) {
        return a.start_ms < b.start_ms;
    });
    std::vector<TimeRange> groups;
    for (const auto& span : ranges) {
        if (!groups.empty() && span.start_ms - groups.back().end_ms <= 4000.0) {
            groups.back().end_ms = std::max(groups.back().end_ms, span.end_ms);
        } else {
            groups.push_back(span);
        }
    }

    std::vector<SpeechWindow> windows;
    for (const auto& group : groups) {
        double begin = group.start_ms;
        while (begin < group.end_ms) {
            double end = std::min(begin + 26000.0, group.end_ms);
            if (end < group.end_ms) {
                // Prefer a real pause near the boundary; keep continuous speech intact.
                for (size_t i = 1; i < ranges.size(); ++i) {
                    const double pause = (ranges[i - 1].end_ms + ranges[i].start_ms) / 2.0;
                    if (ranges[i].start_ms > ranges[i - 1].end_ms &&
                        pause >= begin + 18000.0 && pause <= end) {
                        end = pause;
                    }
                }
            }
            windows.push_back({{begin, end}, {begin, end}});
            begin = end;
        }
    }
    return windows;
}

bool owns_timestamp(const SpeechWindow& window, double start_ms, double end_ms) {
    const double midpoint = (start_ms + end_ms) / 2.0;
    return midpoint >= window.ownership.start_ms && midpoint < window.ownership.end_ms;
}

bool align_token_timestamps(std::vector<TimedText>& tokens,
                            const std::vector<double>& acoustic_times,
                            double segment_end_ms, double duration_ms) {
    if (tokens.empty() || tokens.size() != acoustic_times.size() ||
        !std::isfinite(segment_end_ms) || !std::isfinite(duration_ms) ||
        !std::all_of(acoustic_times.begin(), acoustic_times.end(),
                     [duration_ms](double time) { return std::isfinite(time) &&
                         time >= 0.0 && time < duration_ms; }) ||
        !std::is_sorted(acoustic_times.begin(), acoustic_times.end())) return false;
    std::vector<TimedText> aligned;
    // Tokens sharing one acoustic frame (including split UTF-8) stay together.
    for (size_t a = 0; a < tokens.size();) {
        size_t b = a + 1;
        std::string text = tokens[a].text;
        while (b < tokens.size() && acoustic_times[b] == acoustic_times[a]) {
            text += tokens[b++].text;
        }
        const double end = b < tokens.size() ? acoustic_times[b]
            : std::min(duration_ms, std::max(segment_end_ms, acoustic_times[a] + 20.0));
        aligned.push_back({text, acoustic_times[a], end, tokens[a].segment});
        a = b;
    }
    tokens = std::move(aligned);
    return true;
}

bool repair_token_timestamps(std::vector<TimedText>& tokens, double duration_ms) {
    bool repaired = false;
    double previous_end = 0.0;
    for (size_t begin = 0; begin < tokens.size();) {
        size_t end = begin + 1;
        while (end < tokens.size() && tokens[end].segment == tokens[begin].segment) ++end;
        double start_ms = std::clamp(tokens[begin].start_ms, 0.0, duration_ms);
        double end_ms = std::clamp(tokens[end - 1].end_ms, start_ms, duration_ms);
        bool invalid = start_ms < previous_end;
        double last = previous_end;
        for (size_t i = begin; i < end; ++i) {
            invalid = invalid || !std::isfinite(tokens[i].start_ms) ||
                      !std::isfinite(tokens[i].end_ms) || tokens[i].start_ms < last ||
                      tokens[i].end_ms <= tokens[i].start_ms || tokens[i].end_ms > duration_ms;
            last = tokens[i].end_ms;
        }
        if (invalid) {
            repaired = true;
            start_ms = std::max(previous_end, std::isfinite(start_ms) ? start_ms : previous_end);
            if (!std::isfinite(end_ms) || end_ms <= start_ms) {
                start_ms = previous_end;
                end_ms = end < tokens.size() && std::isfinite(tokens[end].start_ms)
                    ? std::clamp(tokens[end].start_ms, start_ms, duration_ms) : duration_ms;
            }
            std::vector<size_t> weights;
            size_t total = 0;
            for (size_t i = begin; i < end; ++i) {
                size_t weight = 0;
                for (unsigned char c : tokens[i].text) weight += (c & 0xc0) != 0x80;
                weights.push_back(std::max<size_t>(1, weight));
                total += weights.back();
            }
            size_t consumed = 0;
            for (size_t i = begin; i < end; ++i) {
                tokens[i].approximate_timing = true;
                tokens[i].start_ms = start_ms + (end_ms - start_ms) * consumed / total;
                consumed += weights[i - begin];
                tokens[i].end_ms = start_ms + (end_ms - start_ms) * consumed / total;
            }
        }
        previous_end = tokens[end - 1].end_ms;
        begin = end;
    }
    return repaired;
}

void normalize_speakers(std::vector<SpeakerTurn>& turns) {
    std::stable_sort(turns.begin(), turns.end(), [](const auto& a, const auto& b) {
        return a.start_ms < b.start_ms;
    });
    std::map<int, int> labels;
    std::map<int, double> durations;
    for (const auto& turn : turns) {
        if (turn.speaker >= 0 && (turn.confidence == -2.0 || turn.confidence >= 0.0)) {
            durations[turn.speaker] += turn.end_ms - turn.start_ms;
        }
    }
    for (auto& turn : turns) {
        if (turn.speaker < 0 || durations[turn.speaker] < 2000.0) {
            turn.speaker = -1;
            continue;
        }
        const auto entry = labels.emplace(turn.speaker, static_cast<int>(labels.size()));
        turn.speaker = entry.first->second;
    }
}

SpeakerAssignment assign_speaker(double start_ms, double end_ms,
                                 const std::vector<SpeakerTurn>& turns) {
    SpeakerAssignment assignment;
    std::map<int, std::vector<TimeRange>> intervals;
    const double duration = end_ms - start_ms;
    if (duration <= 0.0) {
        return assignment;
    }
    std::vector<const SpeakerTurn*> active;
    for (const auto& turn : turns) {
        if (turn.start_ms >= end_ms) {
            break;
        }
        if (turn.end_ms <= start_ms) {
            continue;
        }
        for (const auto* other : active) {
            if ((other->speaker != turn.speaker || turn.speaker < 0) &&
                std::min({other->end_ms, turn.end_ms, end_ms}) >
                    std::max({other->start_ms, turn.start_ms, start_ms})) {
                assignment.overlap = true;
            }
        }
        active.push_back(&turn);
        // Negative silhouette values indicate an ambiguous cluster assignment.
        if (turn.speaker < 0 || (turn.confidence != -2.0 && turn.confidence < 0.0)) {
            continue;
        }
        intervals[turn.speaker].push_back({std::max(start_ms, turn.start_ms),
                                           std::min(end_ms, turn.end_ms)});
    }
    if (assignment.overlap) {
        return assignment;
    }
    double best = 0.0;
    double second = 0.0;
    for (const auto& item : intervals) {
        double covered = 0.0;
        double previous_end = start_ms;
        for (const auto& interval : item.second) {
            covered += std::max(0.0, interval.end_ms - std::max(previous_end, interval.start_ms));
            previous_end = std::max(previous_end, interval.end_ms);
        }
        if (covered > best) {
            second = best;
            best = covered;
            assignment.speaker = item.first;
        } else {
            second = std::max(second, covered);
        }
    }
    if (best < duration * 0.6 || second > duration * 0.2) {
        assignment.speaker = -1;
    }
    return assignment;
}

void append_decoded_window(TranscriptionResult& result, const std::vector<TimedText>& tokens,
                           const SpeechWindow& window, const std::vector<SpeakerTurn>& turns,
                           double confidence, const std::vector<std::string>& review_reasons) {
    std::vector<bool> group_utterance(tokens.size(), false);
    std::vector<SpeakerAssignment> utterance_speakers(tokens.size());
    for (size_t begin = 0; begin < tokens.size();) {
        size_t finish = begin + 1;
        while (finish < tokens.size() && tokens[finish].segment == tokens[begin].segment) ++finish;
        if (std::any_of(tokens.begin() + begin, tokens.begin() + finish,
                        [](const auto& token) { return token.approximate_timing; })) {
            const double a = tokens[begin].start_ms + window.audio.start_ms;
            const double b = tokens[finish - 1].end_ms + window.audio.start_ms;
            auto speaker = assign_speaker(a, b, turns);
            const int proposed = speaker.speaker;
            for (const auto& turn : turns) {
                if (turn.start_ms >= b) break;
                if (turn.end_ms > a && (turn.speaker != proposed ||
                    (turn.confidence < 0 && turn.confidence != -2.0))) speaker.speaker = -1;
            }
            std::fill(group_utterance.begin() + begin, group_utterance.begin() + finish, true);
            std::fill(utterance_speakers.begin() + begin, utterance_speakers.begin() + finish, speaker);
        }
        begin = finish;
    }
    std::string text;
    double start = 0.0;
    double end = 0.0;
    int last_segment = -1;
    bool first = true;
    bool approximate = false;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        if (text.empty()) start = token.start_ms + window.audio.start_ms;
        text += token.text;
        approximate = approximate || token.approximate_timing;
        end = std::max(end, token.end_ms + window.audio.start_ms);
        // Estimated token positions cannot locate a speaker change within an utterance.
        if (group_utterance[i] && i + 1 < tokens.size() &&
            tokens[i + 1].segment == token.segment) continue;
        // BPE may split a UTF-8 character or an English word across tokens.
        if (!complete_utf8(text)) continue;
        if (i + 1 < tokens.size() && tokens[i + 1].segment == token.segment &&
            !tokens[i + 1].text.empty() && ascii_word(text.back()) &&
            ascii_word(tokens[i + 1].text.front())) continue;
        if (owns_timestamp(window, start, end) ||
            (start == end && end == window.ownership.end_ms)) {
            start = std::max(start, window.ownership.start_ms);
            end = std::min(end, window.ownership.end_ms);
            if (!result.segments.empty() && start < result.segments.back().end_ms) {
                start = result.segments.back().end_ms;
            }
            if (end <= start && i + 1 < tokens.size()) {
                // Keep words with imprecise token timestamps attached to the next timed unit.
                continue;
            }
            if (end > start) {
                const auto speaker = group_utterance[i] ? utterance_speakers[i]
                                                       : assign_speaker(start, end, turns);
                auto reasons = review_reasons;
                if (approximate) reasons.push_back("timing_approximate");
                if (speaker.overlap) reasons.push_back("overlap");
                else if (!turns.empty() && speaker.speaker < 0) reasons.push_back("speaker_uncertain");
                if (!first && last_segment == token.segment &&
                    result.segments.back().speaker == speaker.speaker &&
                    result.segments.back().overlap == speaker.overlap &&
                    result.segments.back().review_reasons == reasons) {
                    auto& segment = result.segments.back();
                    segment.text += text;
                    segment.end_ms = end;
                } else {
                    result.segments.push_back({text, start, end, speaker.speaker, speaker.overlap,
                                               !reasons.empty(), confidence, reasons});
                }
                result.full_text += text;
                first = false;
                last_segment = token.segment;
            } else if (!result.segments.empty()) {
                result.segments.back().text += text;
                result.segments.back().needs_review = true;
                auto& reasons = result.segments.back().review_reasons;
                if (std::find(reasons.begin(), reasons.end(), "timing_approximate") == reasons.end()) {
                    reasons.push_back("timing_approximate");
                }
                result.full_text += text;
            }
        }
        text.clear();
        approximate = false;
        end = 0.0;
    }
}
