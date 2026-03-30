#include "output.hpp"

#include "types.hpp"

#include <cstdio>
#include <cmath>
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

void write_to_stream(std::ostream& os, const TranscriptionResult& result,
                    OutputFormat format) {
    switch (format) {
        case OutputFormat::Text: {
            os << result.full_text << "\n";
            break;
        }

        case OutputFormat::Json: {
            os << "{\n";
            os << "  \"text\": \"";
            // JSON 转义
            for (char c : result.full_text) {
                if (c == '"') {
                    os << "\\\"";
                } else if (c == '\\') {
                    os << "\\\\";
                } else if (c == '\n') {
                    os << "\\n";
                } else if (c == '\r') {
                    os << "\\r";
                } else if (c == '\t') {
                    os << "\\t";
                } else if (static_cast<unsigned char>(c) < 0x20) {
                    // ASCII 控制字符转义为 \u00xx
                    os << "\\u00"
                       << std::hex << std::setfill('0') << std::setw(2)
                       << static_cast<int>(static_cast<unsigned char>(c))
                       << std::dec;
                } else {
                    os << c;
                }
            }
            os << "\",\n";

            os << "  \"segments\": [\n";
            for (size_t i = 0; i < result.segments.size(); ++i) {
                const auto& seg = result.segments[i];
                os << "    {\n";
                os << "      \"start\": " << std::fixed
                   << std::setprecision(2) << seg.start_ms / 1000.0 << ",\n";
                os << "      \"end\": " << std::fixed
                   << std::setprecision(2) << seg.end_ms / 1000.0 << ",\n";
                os << "      \"text\": \"";
                for (char c : seg.text) {
                    if (c == '"') {
                        os << "\\\"";
                    } else if (c == '\\') {
                        os << "\\\\";
                    } else if (c == '\n') {
                        os << "\\n";
                    } else if (c == '\r') {
                        os << "\\r";
                    } else if (c == '\t') {
                        os << "\\t";
                    } else if (static_cast<unsigned char>(c) < 0x20) {
                        os << "\\u00"
                           << std::hex << std::setfill('0') << std::setw(2)
                           << static_cast<int>(static_cast<unsigned char>(c))
                           << std::dec;
                    } else {
                        os << c;
                    }
                }
                os << "\"\n    }";
                if (i + 1 < result.segments.size()) {
                    os << ",";
                }
                os << "\n";
            }
            os << "  ],\n";

            os << "  \"language\": \"" << result.language << "\",\n";
            os << "  \"model\": \"" << result.model_name << "\",\n";
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
                os << seg.text << "\n\n";
            }
            break;
        }

        case OutputFormat::Vtt: {
            os << "WEBVTT\n\n";
            for (size_t i = 0; i < result.segments.size(); ++i) {
                const auto& seg = result.segments[i];
                os << ms_to_vtt_time(seg.start_ms) << " --> "
                   << ms_to_vtt_time(seg.end_ms) << "\n";
                os << seg.text << "\n\n";
            }
            break;
        }
    }
}

}  // anonymous namespace

void write_output(const TranscriptionResult& result, OutputFormat format,
                  const std::filesystem::path& output_path) {
    if (output_path.empty()) {
        // 输出到 stdout
        write_to_stream(std::cout, result, format);
        std::cout.flush();
    } else {
        std::ofstream file(output_path.string());
        if (!file.is_open()) {
            (void)fprintf(stderr, "[ERROR] 无法写入输出文件: %s\n",
                           output_path.string().c_str());
            return;
        }
        write_to_stream(file, result, format);
        file.close();
        (void)printf("[DONE] 输出已保存: %s\n", output_path.string().c_str());
    }
}
