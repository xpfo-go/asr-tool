#include "audio.hpp"
#include "temporary_directory.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void u32(std::ostream& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out.put(static_cast<char>(value >> (8 * i)));
}
void fixture(const std::filesystem::path& path, bool truncated, bool unaligned = false) {
    std::ofstream out(path, std::ios::binary);
    out.write("RIFF", 4); u32(out, 64); out.write("WAVE", 4);
    out.write("JUNK", 4); u32(out, 1); out.write("x\0", 2);
    out.write("fmt ", 4); u32(out, 16);
    const char format[16] = {3, 0, 1, 0, char(0x80), char(0x3e), 0, 0,
                            0, char(0xfa), 0, 0, 4, 0, 32, 0};
    out.write(format, sizeof(format));
    out.write("data", 4); u32(out, unaligned ? 9 : 8);
    const float samples[] = {.25f, -.5f};
    out.write(reinterpret_cast<const char*>(samples), truncated ? 4 : 8);
    if (!truncated) { out.write("LIST", 4); u32(out, 4); out.write("tail", 4); }
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    try {
        const char* old_path = std::getenv("PATH");
        const std::string path = std::string(argv[1]) +
#ifdef _WIN32
            ";" + (old_path ? old_path : "");
        _putenv_s("PATH", path.c_str());
#else
            ":" + (old_path ? old_path : "");
        setenv("PATH", path.c_str(), 1);
#endif
        TemporaryDirectory directory(std::filesystem::temp_directory_path());
        const auto input = directory.path() / std::filesystem::u8path(u8"meeting \u4f1a\u8bae.wav");
        fixture(input, false);
        {
            AudioReader first(input), second(input);
            const auto chunk = first.read_chunk();
            check(chunk.samples == std::vector<float>({.25f, -.5f}), "read only declared data, skipping odd chunk padding");
            check(first.is_eof() && first.read_chunk().samples.empty(), "EOF must match data boundary");
            check(second.read_all() == chunk.samples, "simultaneous readers must own separate temporary files");
        }
        const auto normalized = directory.path() / "normalized.wav";
        fixture(normalized, false);
        check(AudioReader(normalized, true).read_all() == std::vector<float>({.25f, -.5f}),
              "offline normalization must pass the bounded filter to FFmpeg");
        bool filter_rejected = false;
        try { AudioReader reader(normalized); }
        catch (const std::runtime_error&) { filter_rejected = true; }
        check(filter_rejected, "a failed FFmpeg preprocessing command must fail the read");
        fixture(input, true);
        bool rejected = false;
        try { AudioReader reader(input); reader.read_chunk(); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "chunk API must reject truncated data");
        fixture(input, false, true);
        rejected = false;
        try { AudioReader reader(input); }
        catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "reject partial float sample");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
