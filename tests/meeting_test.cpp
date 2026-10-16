#include "meeting.hpp"
#include "model_manager.hpp"
#include "output.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_windows() {
    const auto windows = plan_speech_windows({{0, 90000}, {89000, 96000},
        {-20, 20}, {120000, 130000}, {NAN, 4}, {4, 2}}, 125000);
    check(!windows.empty(), "speech must produce windows");
    double previous = 0;
    for (const auto& window : windows) {
        check(window.audio.start_ms >= 0 && window.audio.end_ms <= 125000,
              "context must be inside recording");
        check(window.ownership.start_ms >= previous &&
              window.ownership.end_ms > window.ownership.start_ms,
              "ownership must be ordered and positive");
        check(window.audio.end_ms - window.audio.start_ms <= 28000,
              "audio must fit Whisper context");
        check(window.audio.start_ms == window.ownership.start_ms &&
              window.audio.end_ms == window.ownership.end_ms,
              "decode each speech interval once to avoid timestamp-based text loss");
        previous = window.ownership.end_ms;
    }
    for (double time = 1; time < 96000; time += 31) {
        int owners = 0;
        for (const auto& window : windows) owners += owns_timestamp(window, time, time);
        check(owners == 1, "continuous speech needs exactly one owner");
    }
    check(plan_speech_windows({}, 1000).empty(), "silence needs no decode");
    check(plan_speech_windows({{0, 500}}, 500).size() == 1, "short speech needs a window");
}

void test_speakers() {
    std::vector<SpeakerTurn> turns{{6000, 9000, 8, .4}, {0, 4000, 2, .4},
                                  {3000, 5000, 8, .4}, {9500, 9800, 12, .1}};
    normalize_speakers(turns);
    check(turns[0].speaker == 0 && turns[1].speaker == 1 && turns[2].speaker == 1,
          "labels must follow first occurrence across recording");
    check(turns.back().speaker == -1, "tiny unsupported clusters must stay unknown");
    check(assign_speaker(100, 1000, turns).speaker == 0, "clear speech needs a label");
    check(assign_speaker(3500, 3800, turns).overlap, "simultaneous speakers need overlap");
    check(assign_speaker(5500, 5800, turns).speaker == -1, "uncovered speech stays unknown");
    check(assign_speaker(9400, 9700, turns).speaker == -1, "weak cluster stays unknown");
    check(assign_speaker(0, 0, turns).speaker == -1, "empty intervals stay unknown");
    check(assign_speaker(0, 500, {{0, 1000, 1, -.1}}).speaker == -1,
          "negative silhouette must not claim a speaker");
    check(assign_speaker(0, 1000, {{0, 300, 1, .4}, {0, 300, 1, .4}}).speaker == -1,
          "duplicate turns must not inflate coverage");
}

void test_corrections() {
    check(correction_is_conservative("没有 MAP，4 小时", "没有 map，4 小时。"),
          "safe terminology change should be allowed");
    check(!correction_is_conservative("没有 4 小时", "有 4 小时"), "protect negation");
    check(!correction_is_conservative("十二小时", "二十小时"), "protect Chinese numbers");
    check(!correction_is_conservative("1.5", "15"), "protect decimal points");
    check(!correction_is_conservative("-2", "2"), "protect signs");
    check(!correction_is_conservative("1 23", "12 3"), "protect number boundaries");
    check(!correction_is_conservative("Not allowed", "Allowed"), "protect English negation");
    check(!correction_is_conservative("don't", "do"), "protect contractions");
    check(!correction_is_conservative("The total cost is one hundred dollars",
                                      "The total cost is one thousand dollars"),
          "protect numbers written as English words");
    check(!correction_is_conservative("We cannot approve this shipment today",
                                      "We can approve this shipment today"),
          "protect cannot negation");
    check(!correction_is_conservative("We can\xe2\x80\x99t approve this shipment today",
                                      "We can approve this shipment today"),
          "protect contractions with a Unicode apostrophe");
    check(correction_is_conservative("We can\xe2\x80\x99t approve the cicle change",
                                     "We can't approve the cycle change"),
          "apostrophe typography must not block an otherwise safe terminology fix");
    check(!correction_is_conservative("合计壹佰元的费用需要复核", "合计壹仟元的费用需要复核"),
          "protect financial Chinese numerals");
    check(correction_is_conservative("Compare the cicle distance", "Compare the cycle distance."),
          "small terminology fixes must survive");
    check(correction_is_conservative("比较空车载动的数据", "比较空车载重的数据。"),
          "wording distance must count Chinese characters");
    check(correction_is_live_review_safe(
              "嗯麦内每只流苏然后在 THE DIFFERENCE FOR ENVIRONMENT FIELD THE DIFFERENCE BETWEEN",
              "My name is Liu Shu and my English name is Su. The difference for environment field is the difference between"),
          "high-confidence local review may repair a bad streaming hypothesis");
    check(!correction_is_live_review_safe("没有 4 小时", "有 4 小时"),
          "broad review must still protect negation");
    check(!correction_is_conservative("去年，Example 是甲地的，甲地乙地两份数据的原表您用过这些，但是，好，还需要重新核对",
                                      "去年那些数据，Example是甲地的，甲地乙地两份数据原表"),
          "reject removal of substantial content");
    check(!correction_is_conservative("The average speed determines the cycle distance.",
                                      "The trucks will follow a different route tomorrow."),
          "similar length must not justify a wholesale rewrite");
    check(repetition_ratio({}) == 0.0 && repetition_ratio({1, 2, 3}) == 0.0,
          "short token lists cannot contain repeated four-grams");
    const double original = repetition_ratio({1, 2, 3, 4, 5, 6, 7, 8,
                                               9, 10, 11, 12, 9, 10, 11, 12});
    const double candidate = repetition_ratio({1, 2, 3, 4, 5, 6, 7, 8,
                                                9, 10, 11, 12, 9, 10, 11, 12,
                                                9, 10, 11, 12, 9, 10, 11, 12});
    check(candidate > original && candidate < 0.5,
          "a retry can increase repetition without crossing the old rejection threshold");
}

void test_languages() {
    check(choose_language({}, -1) == -1, "empty language evidence must not invent a language");
    check(choose_language({.3f, .5f, .2f}, -1) == 1, "first window uses audio evidence");
    check(choose_language({.3f, .5f, .2f}, 0) == 0, "weak switch can use supported continuity");
    check(choose_language({.1f, .85f, .05f}, 0) == 1, "clear language changes must survive");
    check(choose_language({.05f, .55f, .4f}, 0) == 1, "unsupported prior must not lock a language");
    check(choose_language({.6f, .15f, .25f}, 1) == 0, "large evidence ratio must allow switching");
}

void test_text() {
    TranscriptionResult result;
    const SpeechWindow window{{0, 4000}, {0, 3000}};
    const std::vector<SpeakerTurn> turns{{0, 1000, 0, .4}, {1000, 3000, 1, .4}};
    append_decoded_window(result, {{"\xe4", 0, 100, 0}, {"\xbd\xa0", 100, 500, 0},
        {"cir", 500, 900, 1}, {"cle", 900, 1100, 1}, {".", 1100, 1100, 1}},
        window, turns, .8, {});
    check(result.full_text == "你circle.", "UTF-8 and English words must survive token grouping");
    for (const auto& segment : result.segments) {
        check(segment.end_ms > segment.start_ms, "subtitles must have positive duration");
        check(segment.text.find("cir") == std::string::npos || segment.text.find("circle") != std::string::npos,
              "speaker changes must not split an English word");
    }
    const auto count = result.full_text.size();
    append_decoded_window(result, {{"outside", 3000, 3100, 0}}, window, {}, .8, {});
    check(result.full_text.size() == count, "context outside ownership must be discarded");
    TranscriptionResult overlapping;
    append_decoded_window(overlapping, {{"right", 0, 500, 0}, {"right", 400, 600, 1},
        {" for", 550, 550, 1}, {" review", 550, 900, 1}}, window, {}, .8, {});
    check(overlapping.full_text == "rightright for review", "legitimate repetitions must survive bad timestamps");
    std::vector<TimedText> collapsed{{"中文", 0, 0, 0}, {" terms", 0, 0, 0},
                                   {" tail", 1000, 1000, 1}};
    check(repair_token_timestamps(collapsed, 2000), "collapsed alignment needs fallback");
    TranscriptionResult recovered;
    append_decoded_window(recovered, collapsed, {{0, 2000}, {0, 2000}}, {}, .8, {});
    check(recovered.full_text == "中文 terms tail", "fallback must retain every decoded word");
    for (const auto& segment : recovered.segments)
        check(segment.end_ms > segment.start_ms && segment.end_ms <= 2000 && segment.needs_review,
              "fallback timestamps must stay bounded and marked for review");
    append_decoded_window(recovered, {{" next", 0, 200, 0}}, {{2000, 2200}, {2000, 2200}}, {}, .8, {});
    check(recovered.full_text == "中文 terms tail next", "adjacent windows must preserve boundary words");
    std::vector<TimedText> tail{{"one", 0, 1000, 0}, {" two", 1000, 1000, 1},
                               {" three", 1000, 1000, 2}};
    check(repair_token_timestamps(tail, 1000), "collapsed tail needs fallback");
    TranscriptionResult tail_result;
    append_decoded_window(tail_result, tail, {{0, 1000}, {0, 1000}}, {}, .8, {});
    check(tail_result.full_text == "one two three", "multiple collapsed tail segments must survive");
    check(!tail_result.segments.empty() && tail_result.segments.back().end_ms == 1000,
          "collapsed tail must remain inside the recording");
    std::vector<TimedText> nonfinite{{"bad", NAN, NAN, 0}, {" timing", NAN, NAN, 0}};
    check(repair_token_timestamps(nonfinite, 1000), "nonfinite timestamps need fallback");
    for (const auto& token : nonfinite)
        check(std::isfinite(token.start_ms) && std::isfinite(token.end_ms) &&
              token.end_ms > token.start_ms, "fallback must produce finite positive intervals");
}

void test_acoustic_timing() {
    std::vector<TimedText> tokens{{"\xe4", 0, 0, 0}, {"\xbd\xa0", 0, 0, 0},
                                 {"好", 0, 0, 0}, {"。", 0, 0, 0}};
    check(align_token_timestamps(tokens, {100, 100, 600, 600}, 900, 1000),
          "valid acoustic timing should replace collapsed token estimates");
    check(tokens.size() == 2 && tokens[0].text == "你" && tokens[1].text == "好。" &&
          tokens[0].start_ms == 100 && tokens[0].end_ms == 600 &&
          tokens[1].start_ms == 600 && tokens[1].end_ms == 900,
          "shared acoustic frames must preserve complete text and ordered intervals");
    check(!repair_token_timestamps(tokens, 1000), "valid acoustic timing needs no interpolation");
    TranscriptionResult result;
    append_decoded_window(result, tokens, {{0, 1000}, {0, 1000}},
        {{0, 600, 0, .4}, {600, 1000, 1, .4}}, .8, {});
    check(result.full_text == "你好。" && result.segments.size() == 2 &&
          result.segments[0].speaker == 0 && result.segments[1].speaker == 1,
          "acoustic word boundaries must retain supported speaker changes");
    for (const auto& times : std::vector<std::vector<double>>{
             {-1, 600}, {600, 100}, {100, NAN}, {100, 1000}, {100}}) {
        check(!align_token_timestamps(tokens, times, 900, 1000) &&
              tokens.size() == 2 && tokens[0].text == "你" && tokens[0].start_ms == 100,
              "invalid acoustic timing must leave the fallback tokens intact");
    }
}

void test_approximate_speakers() {
    std::vector<TimedText> tokens{{"同", 0, 0, 0}, {"一", 0, 0, 0},
                                 {"句话", 0, 3000, 0}, {" next", 3000, 4000, 1}};
    repair_token_timestamps(tokens, 4000);
    check(tokens[0].approximate_timing && !tokens.back().approximate_timing,
          "a repaired segment must not mark unrelated timestamps approximate");
    TranscriptionResult result;
    append_decoded_window(result, tokens, {{0, 4000}, {0, 4000}},
        {{0, 1300, 0, .4}, {1600, 4000, 0, .4}}, .8, {});
    check(result.segments.size() == 2 && result.segments[0].speaker == 0,
          "approximate words around a pause must not fragment one speaker's utterance");
    check(result.segments[0].review_reasons == std::vector<std::string>{"timing_approximate"} &&
          result.segments[1].review_reasons.empty(), "review must identify only the affected segment");
    TranscriptionResult changed;
    append_decoded_window(changed, tokens, {{0, 4000}, {0, 4000}},
        {{0, 1500, 0, .4}, {1500, 4000, 1, .4}}, .8, {});
    check(changed.segments.size() == 2 && changed.segments[0].text == "同一句话" &&
          changed.segments[0].speaker == -1 && changed.segments.back().speaker == 1 &&
          changed.segments[0].review_reasons ==
              std::vector<std::string>{"timing_approximate", "speaker_uncertain"},
          "interpolated word timing must not invent viewpoint attribution across speakers");
    TranscriptionResult brief_change;
    append_decoded_window(brief_change, tokens, {{0, 4000}, {0, 4000}},
        {{0, 4000, 0, .4}, {1000, 1100, 1, -.1}}, .8, {});
    check(brief_change.segments.front().speaker == -1 && brief_change.segments.front().overlap,
          "even a brief ambiguous overlap prevents assigning approximate text to one person");
    TranscriptionResult simultaneous;
    append_decoded_window(simultaneous, tokens, {{0, 4000}, {0, 4000}},
        {{0, 4000, 0, .4}, {1000, 2000, 1, .4}}, .5, {"low_asr_confidence"});
    check(std::any_of(simultaneous.segments.begin(), simultaneous.segments.end(), [](const auto& s) {
              return s.overlap && s.speaker == -1 && s.review_reasons.size() == 3;
          }),
          "overlap and ASR uncertainty must remain visible after grouping");
}

void test_output(const std::filesystem::path& directory) {
    TranscriptionResult result;
    result.full_text = "引号\"换行\n制表\t";
    result.segments = {{"Hello", 0, 1234, 0, false, false, .8, {}},
                       {"world", 1234, 2000, -1, true, true, .5, {"overlap"}}};
    result.speaker_turns = {{0, 2000, 0, .4}};
    result.speakers_available = true;
    result.duration_seconds = 2;
    result.decoding_settings.threads = 4;
    result.decoding_settings.requested_language = "auto";
    result.decoding_settings.acoustic_timestamps = true;
    for (const auto format : {OutputFormat::Text, OutputFormat::Json, OutputFormat::Srt, OutputFormat::Vtt}) {
        const auto path = directory / (std::to_string(static_cast<int>(format)) + ".out");
        check(write_output(result, format, path), "output should write successfully");
        std::ifstream file(path);
        const std::string content((std::istreambuf_iterator<char>(file)), {});
        if (format == OutputFormat::Json) {
            check(content.find("\\u000a") != std::string::npos && content.find("\\u0009") != std::string::npos,
                  "JSON must escape controls");
            check(content.find("\"speaker_1\"") != std::string::npos, "JSON needs speaker IDs");
            check(content.find("\"speaker_turns\"") != std::string::npos &&
                  content.find("\"review_reasons\": [\"overlap\"]") != std::string::npos,
                  "JSON must expose independent speaker timing and review reasons");
            check(content.find("\"decoding_settings\"") != std::string::npos &&
                  content.find("\"threads\": 4") != std::string::npos &&
                  content.find("\"vad_threshold\": 0.5") != std::string::npos &&
                  content.find("\"history_prompt\": true") != std::string::npos &&
                  content.find("\"acoustic_timestamps\": true") != std::string::npos,
                  "JSON must expose actual decoding settings for reproducible evaluation");
        } else {
            check(content.find("[说话人 1]") != std::string::npos && content.find("[重叠发言]") != std::string::npos,
                  "human-readable formats need labels");
        }
        if (format == OutputFormat::Srt) check(content.find("00:00:01,234") != std::string::npos, "SRT time");
        if (format == OutputFormat::Vtt) check(content.find("WEBVTT\n\n") == 0, "VTT header");
        if (format == OutputFormat::Text) {
            check(content.find("[00:00:00.000] [说话人 1] Hello") != std::string::npos &&
                  content.find("[00:00:01.234] [重叠发言] [需复核] world") != std::string::npos,
                  "text must locate uncertain meeting passages without changing their words");
        }
    }
    check(!write_output(result, OutputFormat::Text, directory / "absent" / "file"), "write failure must propagate");
    check(!ensure_main_model(directory / "missing.bin"), "offline inference cannot download missing model");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    try {
        test_windows();
        test_speakers();
        test_corrections();
        test_languages();
        test_text();
        test_acoustic_timing();
        test_approximate_speakers();
        test_output(directory);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::filesystem::remove_all(directory);
}
