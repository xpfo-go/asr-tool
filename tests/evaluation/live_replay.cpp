#include "audio.hpp"
#include "live.hpp"
#include "model_manager.hpp"
#include "output.hpp"
#include "platform.hpp"
#include "streaming.hpp"
#include "transcriber.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("Expected AUDIO -m REVIEW_MODEL_DIR --stream-model-dir STREAM_MODEL_DIR -o OUTPUT.jsonl");
        std::filesystem::path model_dir, streaming_dir, destination;
        std::string prompt;
        bool paced = true;
        for (int i = 2; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--unpaced") { paced = false; continue; }
            if (++i == argc) throw std::runtime_error("Missing value for " + option);
            if (option == "-m") model_dir = argv[i];
            else if (option == "--stream-model-dir") streaming_dir = argv[i];
            else if (option == "-p") prompt = argv[i];
            else if (option == "-o") destination = argv[i];
            else throw std::runtime_error("Unsupported replay option: " + option);
        }
        if (model_dir.empty() || streaming_dir.empty() || destination.empty())
            throw std::runtime_error("Missing replay model/output path");
        auto reader = open_audio(argv[1]);
        const auto samples = reader.read_all();
        if (samples.empty()) throw std::runtime_error("Empty public replay");
        using Clock = std::chrono::steady_clock;
        const auto loading = Clock::now();
        Transcriber transcriber(get_default_model_path(model_dir).string(), "auto", prompt, detect_backend(), false);
        if (!transcriber.is_ready()) throw std::runtime_error(transcriber.error_message());
        auto review = [&](const std::vector<float>& audio) {
            const auto result = transcriber.transcribe_samples(audio);
            if (!result.success) throw std::runtime_error(transcriber.error_message());
            bool reliable = true;
            bool broad = true;
            for (const auto& segment : result.segments) {
                for (const auto& reason : segment.review_reasons) {
                    if (reason == "low_asr_confidence" || reason == "repetition")
                        reliable = false;
                }
                broad = broad && segment.confidence >= 0.65;
            }
            return LiveReviewResult{result.full_text, reliable,
                                    reliable ? "" : "low_asr_confidence", reliable && broad};
        };
        const auto loaded = Clock::now();
        // Warm both actual models from public audio only. Decode state is reset
        // at each utterance; no reference text or previous hypothesis is supplied.
        const std::vector<float> warm(samples.begin(), samples.begin() + std::min(samples.size(), size_t(10 * LIVE_SAMPLE_RATE)));
        review(warm);
        {
            LiveReviewQueue warm_queue([](const auto&) { return LiveReviewResult{}; }, [](const auto&) {});
            StreamingTranscriber warm_stream(streaming_dir, prompt, warm_queue, [](const auto&) {});
            warm_stream.push(warm.data(), warm.size());
            warm_stream.finish();
            warm_queue.finish();
        }
        std::ofstream output(destination);
        if (!output) throw std::runtime_error("Could not open replay output");
        std::mutex output_mutex;
        std::atomic<int64_t> received{0};
        Clock::time_point epoch;
        auto sink = [&](const LiveEvent& original) {
            std::lock_guard<std::mutex> lock(output_mutex);
            auto event = original;
            event.emitted_seconds = std::chrono::duration<double>(Clock::now() - epoch).count();
            event.received_samples = received.load();
            write_live_event(output, event);
            if (!output.flush()) throw std::runtime_error("Could not write replay event");
        };
        LiveReviewQueue queue(review, sink);
        StreamingTranscriber streaming(streaming_dir, prompt, queue, sink);
        epoch = Clock::now();
        output << std::setprecision(12) << "{\"type\":\"run_start\",\"duration\":" << samples.size() / 16000.0
               << ",\"paced\":" << (paced ? "true" : "false") << ",\"sample_rate\":16000,\"chunk_samples\":2560,"
                  "\"max_utterance_seconds\":20,\"review_limit_seconds\":20,\"stream_threads\":2,"
                  "\"stream_beam_paths\":4,\"endpoint_silence_seconds\":1.6,\"final_padding_seconds\":1,"
                  "\"review_model_load_seconds\":" << std::chrono::duration<double>(loaded - loading).count()
               << ",\"warmup_seconds\":" << std::chrono::duration<double>(epoch - loaded).count() << "}\n";
        for (size_t offset = 0; offset < samples.size();) {
            const size_t count = std::min(size_t(2560), samples.size() - offset);
            const size_t end = offset + count;
            if (paced) std::this_thread::sleep_until(epoch + std::chrono::microseconds(end * 1000000 / LIVE_SAMPLE_RATE));
            received.store(static_cast<int64_t>(end));
            streaming.push(samples.data() + offset, count);
            offset = end;
        }
        streaming.finish();
        queue.finish();
        output << "{\"type\":\"run_end\",\"received_samples\":" << received.load()
               << ",\"peak_review_samples\":" << queue.peak_pending_samples()
               << ",\"pending_review_samples\":" << queue.pending_samples()
               << ",\"elapsed_seconds\":" << std::chrono::duration<double>(Clock::now() - epoch).count() << "}\n";
        if (!output.flush()) throw std::runtime_error("Could not finalize replay output");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[ERROR] %s\n", error.what());
        return 1;
    }
}
