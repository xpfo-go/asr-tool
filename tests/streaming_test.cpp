#include "streaming.hpp"
#include "temporary_directory.hpp"

#include <sherpa-onnx/c-api/c-api.h>

#include <fstream>
#include <iostream>
#include <stdexcept>

struct SherpaOnnxOnlineRecognizer {};
struct SherpaOnnxOnlineStream { mutable bool finished = false; };
namespace {
int accepted = 0;
int closed = 0;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Function>
void fails(Function action, const std::string& message) {
    try { action(); }
    catch (const std::exception& error) {
        check(error.what() == message, "wrong failure was propagated");
        return;
    }
    throw std::runtime_error("failure was lost");
}
}

// Enforce the native stream's end-of-input contract without loading model weights.
extern "C" {
const SherpaOnnxOnlineRecognizer* SherpaOnnxCreateOnlineRecognizer(const SherpaOnnxOnlineRecognizerConfig*) {
    return new SherpaOnnxOnlineRecognizer;
}
void SherpaOnnxDestroyOnlineRecognizer(const SherpaOnnxOnlineRecognizer* recognizer) { delete recognizer; }
const SherpaOnnxOnlineStream* SherpaOnnxCreateOnlineStream(const SherpaOnnxOnlineRecognizer*) {
    return new SherpaOnnxOnlineStream;
}
void SherpaOnnxDestroyOnlineStream(const SherpaOnnxOnlineStream* stream) { delete stream; }
void SherpaOnnxOnlineStreamAcceptWaveform(const SherpaOnnxOnlineStream* stream, int32_t, const float*, int32_t) {
    check(stream && !stream->finished, "audio sent after input finished");
    ++accepted;
}
void SherpaOnnxOnlineStreamInputFinished(const SherpaOnnxOnlineStream* stream) {
    check(stream && !stream->finished, "input finished twice");
    stream->finished = true;
    ++closed;
}
int32_t SherpaOnnxIsOnlineStreamReady(const SherpaOnnxOnlineRecognizer*, const SherpaOnnxOnlineStream*) { return 0; }
void SherpaOnnxDecodeOnlineStream(const SherpaOnnxOnlineRecognizer*, const SherpaOnnxOnlineStream*) {}
const SherpaOnnxOnlineRecognizerResult* SherpaOnnxGetOnlineStreamResult(
        const SherpaOnnxOnlineRecognizer*, const SherpaOnnxOnlineStream*) {
    auto* result = new SherpaOnnxOnlineRecognizerResult{};
    result->text = "test";
    return result;
}
void SherpaOnnxDestroyOnlineRecognizerResult(const SherpaOnnxOnlineRecognizerResult* result) { delete result; }
int32_t SherpaOnnxOnlineStreamIsEndpoint(const SherpaOnnxOnlineRecognizer*, const SherpaOnnxOnlineStream* stream) {
    check(stream != nullptr, "callback destroyed active stream");
    return 0;
}
}

int main() {
    try {
        TemporaryDirectory directory(std::filesystem::temp_directory_path());
        for (const char* name : {"encoder-epoch-99-avg-1.int8.onnx", "decoder-epoch-99-avg-1.onnx",
                                 "joiner-epoch-99-avg-1.int8.onnx", "tokens.txt", "bpe.vocab"}) {
            std::ofstream file(directory.path() / name);
        }
        const float sample = .1f;
        auto reviewer = [](const auto&) { return LiveReviewResult{"test", true, {}}; };
        {
            LiveReviewQueue queue(reviewer, [](const auto&) {});
            StreamingTranscriber stream(directory.path(), "", queue, [](const auto& event) {
                if (event.reason == "endpoint") throw std::runtime_error("output failed");
            });
            stream.push(&sample, 1);
            fails([&] { stream.finish(); }, "output failed");
            const int after_failure = accepted;
            fails([&] { stream.finish(); }, "output failed");
            fails([&] { stream.push(&sample, 1); }, "output failed");
            check(accepted == after_failure && closed == 1, "failure must not reenter native stream");
            queue.finish();
        }
        {
            LiveReviewQueue queue(reviewer, [](const auto&) {});
            queue.finish();
            StreamingTranscriber stream(directory.path(), "", queue, [](const auto&) {});
            stream.push(&sample, 1);
            fails([&] { stream.finish(); }, "Live review input is already closed");
            fails([&] { stream.finish(); }, "Live review input is already closed");
        }
        {
            int finals = 0;
            LiveReviewQueue queue(reviewer, [&](const auto& event) {
                if (event.kind == LiveEventKind::Final) ++finals;
            });
            StreamingTranscriber* current = nullptr;
            StreamingTranscriber stream(directory.path(), "", queue, [&](const auto&) {
                fails([&] { current->finish(); }, "Streaming callbacks must not reenter the transcriber");
                fails([&] { current->push(&sample, 1); }, "Streaming callbacks must not reenter the transcriber");
            });
            current = &stream;
            stream.push(&sample, 1);
            stream.finish();
            stream.finish();
            queue.finish();
            check(finals == 1, "valid stream must still finalize exactly once");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
