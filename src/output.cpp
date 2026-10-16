#include "output.hpp"

#include "live.hpp"
#include "types.hpp"

#include <cstdio>
#include <cmath>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string ms_to_srt_time(double ms) {
    int64_t total_ms = static_cast<int64_t>(std::round(ms));
    int64_t hours = total_ms / 3600000;
    int64_t minutes = (total_ms % 3600000) / 60000;
    int64_t seconds = (total_ms % 60000) / 1000;
    int64_t millis = total_ms % 1000;

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld,%03lld",
                  static_cast<long long>(hours),
                  static_cast<long long>(minutes),
                  static_cast<long long>(seconds),
                  static_cast<long long>(millis));
    return buf;
}

std::string ms_to_vtt_time(double ms) {
    int64_t total_ms = static_cast<int64_t>(std::round(ms));
    int64_t hours = total_ms / 3600000;
    int64_t minutes = (total_ms % 3600000) / 60000;
    int64_t seconds = (total_ms % 60000) / 1000;
    int64_t millis = total_ms % 1000;

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld.%03lld",
                  static_cast<long long>(hours),
                  static_cast<long long>(minutes),
                  static_cast<long long>(seconds),
                  static_cast<long long>(millis));
    return buf;
}

std::string trim_copy(const std::string& s) {
    std::size_t begin = 0;
    while (begin < s.size() &&
           std::isspace(static_cast<unsigned char>(s[begin])) != 0) {
        ++begin;
    }
    std::size_t end = s.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) {
        --end;
    }
    return s.substr(begin, end - begin);
}

void write_json_string(std::ostream& os, const std::string& text) {
    os << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') {
            os << '\\' << c;
        } else if (c < 0x20) {
            const char* hex = "0123456789abcdef";
            os << "\\u00" << hex[c >> 4] << hex[c & 15];
        } else {
            os << c;
        }
    }
    os << '"';
}

std::string speaker_label(const Segment& segment, bool available) {
    if (!available) {
        return "";
    }
    if (segment.overlap) {
        return "[重叠发言] ";
    }
    return segment.speaker < 0 ? "[说话人不确定] " :
           "[说话人 " + std::to_string(segment.speaker + 1) + "] ";
}

void write_to_stream(std::ostream& os, const TranscriptionResult& result,
                    OutputFormat format) {
    switch (format) {
        case OutputFormat::Text: {
            if (!result.segments.empty()) {
                for (const auto& seg : result.segments) {
                    const std::string line = trim_copy(seg.text);
                    if (line.empty()) {
                        continue;
                    }
                    if (result.speakers_available || seg.needs_review) {
                        os << '[' << ms_to_vtt_time(seg.start_ms) << "] ";
                    }
                    os << speaker_label(seg, result.speakers_available);
                    if (seg.needs_review) os << "[需复核] ";
                    os << line << "\n";
                }
            } else {
                const std::string text = trim_copy(result.full_text);
                if (!text.empty()) {
                    os << text;
                }
                os << "\n";
            }
            break;
        }

        case OutputFormat::Json: {
            os << "{\n";
            os << "  \"text\": ";
            write_json_string(os, result.full_text);
            os << ",\n";

            os << "  \"segments\": [\n";
            for (size_t i = 0; i < result.segments.size(); ++i) {
                const auto& seg = result.segments[i];
                os << "    {\n";
                os << "      \"start\": " << std::fixed
                   << std::setprecision(3) << seg.start_ms / 1000.0 << ",\n";
                os << "      \"end\": " << std::fixed
                   << std::setprecision(3) << seg.end_ms / 1000.0 << ",\n";
                os << "      \"text\": ";
                write_json_string(os, seg.text);
                os << ",\n      \"speaker\": ";
                if (seg.speaker < 0) {
                    os << "null";
                } else {
                    write_json_string(os, "speaker_" + std::to_string(seg.speaker + 1));
                }
                os << ",\n      \"overlap\": " << (seg.overlap ? "true" : "false");
                os << ",\n      \"needs_review\": " << (seg.needs_review ? "true" : "false");
                os << ",\n      \"confidence\": " << std::setprecision(4) << seg.confidence;
                os << ",\n      \"review_reasons\": [";
                for (size_t j = 0; j < seg.review_reasons.size(); ++j) {
                    if (j) os << ", ";
                    write_json_string(os, seg.review_reasons[j]);
                }
                os << "]";
                os << "\n    }";
                if (i + 1 < result.segments.size()) {
                    os << ",";
                }
                os << "\n";
            }
            os << "  ],\n";

            os << "  \"language\": ";
            write_json_string(os, result.language);
            os << ",\n  \"model\": ";
            write_json_string(os, result.model_name);
            const auto& settings = result.decoding_settings;
            os << ",\n  \"decoding_settings\": {\"strategy\": \"greedy_with_selective_beam_retry\","
               << " \"threads\": " << settings.threads
               << ", \"retry_beam_size\": " << settings.retry_beam_size
               << ", \"review_threshold\": " << settings.review_threshold
               << ", \"correction_confidence_gain\": " << settings.correction_confidence_gain
               << ", \"vad_threshold\": " << settings.vad_threshold
               << ", \"acoustic_timestamps\": " << (settings.acoustic_timestamps ? "true" : "false")
               << ", \"history_prompt\": " << (settings.history_prompt ? "true" : "false")
               << ", \"requested_language\": ";
            write_json_string(os, settings.requested_language);
            os << ", \"requested_backend\": ";
            write_json_string(os, settings.backend);
            os << "}";
            os << ",\n  \"speakers_available\": " << (result.speakers_available ? "true" : "false");
            os << ",\n  \"speaker_model\": ";
            write_json_string(os, result.speaker_model);
            os << ",\n  \"speaker_turns\": [";
            for (size_t i = 0; i < result.speaker_turns.size(); ++i) {
                const auto& turn = result.speaker_turns[i];
                if (i) os << ",";
                os << "\n    {\"start\": " << std::setprecision(3) << turn.start_ms / 1000.0
                   << ", \"end\": " << turn.end_ms / 1000.0 << ", \"speaker\": ";
                if (turn.speaker < 0) os << "null";
                else write_json_string(os, "speaker_" + std::to_string(turn.speaker + 1));
                os << ", \"confidence\": " << std::setprecision(4) << turn.confidence << "}";
            }
            os << "\n  ]";
            os << ",\n  \"warnings\": [";
            for (size_t i = 0; i < result.warnings.size(); ++i) {
                if (i) os << ", ";
                write_json_string(os, result.warnings[i]);
            }
            os << "],\n  \"corrections\": [";
            for (size_t i = 0; i < result.corrections.size(); ++i) {
                const auto& change = result.corrections[i];
                if (i) os << ",";
                os << "\n    {\"start\": " << std::setprecision(2) << change.start_ms / 1000.0
                   << ", \"end\": " << change.end_ms / 1000.0 << ", \"original_text\": ";
                write_json_string(os, change.original_text);
                os << ", \"corrected_text\": ";
                write_json_string(os, change.corrected_text);
                os << ", \"reason\": ";
                write_json_string(os, change.reason);
                os << "}";
            }
            os << "\n  ],\n";
            os << "  \"duration\": " << std::fixed << std::setprecision(2)
               << result.duration_seconds << "\n";
            os << "}\n";
            break;
        }

        case OutputFormat::Srt: {
            for (size_t i = 0; i < result.segments.size(); ++i) {
                const auto& seg = result.segments[i];
                os << (i + 1) << "\n";
                os << ms_to_srt_time(seg.start_ms) << " --> "
                   << ms_to_srt_time(seg.end_ms) << "\n";
                os << speaker_label(seg, result.speakers_available) << seg.text << "\n\n";
            }
            break;
        }

        case OutputFormat::Vtt: {
            os << "WEBVTT\n\n";
            for (size_t i = 0; i < result.segments.size(); ++i) {
                const auto& seg = result.segments[i];
                os << ms_to_vtt_time(seg.start_ms) << " --> "
                   << ms_to_vtt_time(seg.end_ms) << "\n";
                os << speaker_label(seg, result.speakers_available) << seg.text << "\n\n";
            }
            break;
        }
    }
}

}  // anonymous namespace

bool write_output(const TranscriptionResult& result, OutputFormat format,
                  const std::filesystem::path& output_path) {
    if (output_path.empty()) {
        // 输出到 stdout
        write_to_stream(std::cout, result, format);
        if (!std::cout.flush()) {
            (void)fprintf(stderr, "[ERROR] 无法写入 stdout\n");
            return false;
        }
        return true;
    } else {
        std::ofstream file(output_path.string());
        if (!file.is_open()) {
            (void)fprintf(stderr, "[ERROR] 无法写入输出文件: %s\n",
                           output_path.string().c_str());
            return false;
        }
        write_to_stream(file, result, format);
        if (!file.good()) {
            (void)fprintf(stderr, "[ERROR] 写入输出文件失败: %s\n",
                           output_path.string().c_str());
            return false;
        }
        file.close();
        if (!file.good()) {
            (void)fprintf(stderr, "[ERROR] 关闭输出文件失败: %s\n",
                           output_path.string().c_str());
            return false;
        }
        (void)printf("[DONE] 输出已保存: %s\n", output_path.string().c_str());
        return true;
    }
}

void write_live_event(std::ostream& output, const LiveEvent& event) {
    const char* kind = event.kind == LiveEventKind::Provisional ? "provisional" :
                       event.kind == LiveEventKind::Revision ? "revision" : "final";
    output << "{\"type\":\"" << kind << "\",\"utterance_id\":" << event.utterance_id
           << ",\"revision\":" << event.revision
           << ",\"start\":" << std::fixed << std::setprecision(3)
           << event.start_sample / 16000.0 << ",\"end\":"
           << event.end_sample / 16000.0 << ",\"text\":";
    write_json_string(output, event.text);
    output << ",\"reviewed\":" << (event.reviewed ? "true" : "false");
    if (!event.original_text.empty()) {
        output << ",\"original_text\":";
        write_json_string(output, event.original_text);
    }
    if (!event.review_text.empty()) {
        output << ",\"review_text\":";
        write_json_string(output, event.review_text);
    }
    if (!event.reason.empty()) {
        output << ",\"reason\":";
        write_json_string(output, event.reason);
    }
    if (event.emitted_seconds >= 0) output << ",\"emitted_seconds\":" << std::setprecision(6) << event.emitted_seconds;
    if (event.received_samples >= 0) output << ",\"received_samples\":" << event.received_samples;
    output << "}\n";
}
