#include "model_manager.hpp"
#include "temporary_directory.hpp"

#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void environment(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    try {
        const char* old_path = std::getenv("PATH");
        environment("PATH", std::string(argv[1]) +
#ifdef _WIN32
            ";" + (old_path ? old_path : ""));
        // CreateProcess searches the current directory before the system curl.
        fs::current_path(fs::absolute(argv[1]));
#else
            ":" + (old_path ? old_path : ""));
#endif
        TemporaryDirectory root(fs::temp_directory_path());
        const auto directory = root.path() / fs::u8path(u8"model \u6a21\u578b %USERNAME% ' &");
        fs::create_directory(directory);
        const auto model = directory / "ggml-large-v3-turbo-q8_0.bin";
        const auto old_partial = directory / "ggml-large-v3-turbo-q8_0.bin.part";
        std::ofstream(old_partial) << "another download";
        std::ofstream(model).close();
        fs::resize_file(model, 100000000);
        check(!ensure_main_model(model), "reject truncated default model without downloading");
        environment("ASR_TEST_DOWNLOAD_MODE", "truncated");
        check(!ensure_main_model(model, true), "reject a truncated successful download");
        check(fs::file_size(model) == 100000000, "failed preparation preserves old model");
        environment("ASR_TEST_DOWNLOAD_MODE", "complete");
        auto first = std::async(std::launch::async, [&] { return ensure_main_model(model, true); });
        auto second = std::async(std::launch::async, [&] { return ensure_main_model(model, true); });
        check(first.get() && second.get(), "independent concurrent downloads complete");
        check(fs::file_size(model) == 874188075, "publish complete model to literal path");
        check(fs::file_size(old_partial) == 16, "do not remove another download's partial file");
        check(ensure_main_model(model), "reuse complete default model");
        check(get_default_model_path(directory) == model, "discover complete default model");
#ifdef __APPLE__
        const auto encoder = directory / "ggml-large-v3-turbo-encoder.mlmodelc";
        fs::create_directory(encoder);
        std::ofstream(encoder / "existing") << "preserve";
        check(!ensure_coreml_encoder_model(model), "empty encoder is not ready");
        environment("ASR_TEST_DOWNLOAD_MODE", "failed");
        check(!ensure_coreml_encoder_model(model, true), "failed encoder download is rejected");
        check(fs::exists(encoder / "existing"), "failed encoder preparation preserves old directory");
        environment("ASR_TEST_DOWNLOAD_MODE", "unzip_failed");
        check(!ensure_coreml_encoder_model(model, true), "incomplete encoder extraction is rejected");
        check(fs::exists(encoder / "existing"), "failed extraction preserves old directory");
        environment("ASR_TEST_DOWNLOAD_MODE", "complete");
        check(ensure_coreml_encoder_model(model, true), "repair incomplete encoder");
        check(ensure_coreml_encoder_model(model), "reuse staged encoder");
#endif
        for (const auto& entry : fs::directory_iterator(directory))
            check(entry.path().filename().u8string().find(".asr-tool-") != 0, "clean only owned staging directories");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
