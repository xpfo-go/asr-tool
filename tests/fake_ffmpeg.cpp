#include <filesystem>
#include <string>

// Pass a prepared WAV through the same process boundary as real FFmpeg.
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    using String = std::wstring;
    const String input_flag = L"-i";
#else
int main(int argc, char** argv) {
    using String = std::string;
    const String input_flag = "-i";
#endif
    try {
        bool normalized = false;
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::filesystem::path(argv[i]) == "-af") {
                if (std::filesystem::path(argv[i + 1]) != "dynaudnorm=f=500:g=31:p=0.95:m=10") return 3;
                normalized = true;
            }
        }
        for (int i = 1; i + 1 < argc; ++i) {
            if (String(argv[i]) == input_flag) {
                if (normalized != (std::filesystem::path(argv[i + 1]).filename() == "normalized.wav")) return 4;
                std::filesystem::copy_file(argv[i + 1], argv[argc - 1],
                    std::filesystem::copy_options::overwrite_existing);
                return 0;
            }
        }
    } catch (...) { return 1; }
    return 2;
}
