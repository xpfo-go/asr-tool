#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

int run(const std::vector<fs::path>& arguments) {
    const char* mode = std::getenv("ASR_TEST_DOWNLOAD_MODE");
    const std::string scenario = mode ? mode : "complete";
    for (size_t i = 1; i + 1 < arguments.size(); ++i) {
        if (arguments.front().stem() == "curl" && arguments[i] == "-o") {
            const auto output = arguments[i + 1];
            std::ofstream file(output, std::ios::binary);
            if (!file) return 2;
            const auto size = scenario == "truncated" ? 100000000 : 874188075;
            file.seekp(size - 1);
            file.put('x');
            file.close();
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            return scenario == "failed" ? 7 : 0;
        }
        if (arguments[i] == "-d") {
            const auto directory = arguments[i + 1] / "ggml-large-v3-turbo-encoder.mlmodelc";
            fs::create_directories(directory / "weights");
            std::ofstream(directory / "coremldata.bin") << "fixture";
            if (scenario == "unzip_failed") return 7;
            std::ofstream(directory / "model.mil") << "fixture";
            std::ofstream(directory / "weights/weight.bin") << "fixture";
            return 0;
        }
    }
    return 1;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    try {
        std::vector<fs::path> arguments;
        for (int i = 0; i < argc; ++i) arguments.emplace_back(argv[i]);
        return run(arguments);
    } catch (...) { return 3; }
}
