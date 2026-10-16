#include "audio.hpp"
#include "meeting.hpp"
#include "native_windows.hpp"
#ifdef ASR_CANDIDATE_BEAM_SIZE
#include "firered_beam.hpp"
#endif

#include <sherpa-onnx/c-api/c-api.h>
#include <whisper.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("Expected an audio file, -m MODEL_DIR, -o OUTPUT.json");
        const std::filesystem::path input = argv[1];
        std::filesystem::path models, destination;
        std::string prompt;
        for (int i = 2; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--no-speakers") continue;
            if (++i == argc) throw std::runtime_error("Missing value for " + option);
            if (option == "-m") models = argv[i];
            else if (option == "-o") destination = argv[i];
            else if (option == "-p") prompt = argv[i];
            else if (option != "-f" || std::string(argv[i]) != "json")
                throw std::runtime_error("Unsupported candidate option: " + option);
        }
        if (models.empty() || destination.empty()) throw std::runtime_error("Missing model/output directory");
        auto reader = open_audio(input);
        const auto samples = reader.read_all();

        // Keep VAD and window planning identical to the production Whisper baseline.
        auto vad_config = whisper_vad_default_context_params();
        vad_config.use_gpu = false;
        vad_config.n_threads = 2;
        const auto vad_path = (models / "ggml-silero-v6.2.0.bin").string();
        using Vad = std::unique_ptr<whisper_vad_context, decltype(&whisper_vad_free)>;
        Vad vad(whisper_vad_init_from_file_with_params(vad_path.c_str(), vad_config), whisper_vad_free);
        if (!vad) throw std::runtime_error("Missing or invalid local VAD model");
        auto speech_config = whisper_vad_default_params();
        speech_config.min_speech_duration_ms = 200;
        speech_config.min_silence_duration_ms = 300;
        speech_config.speech_pad_ms = 200;
        using Speech = std::unique_ptr<whisper_vad_segments, decltype(&whisper_vad_free_segments)>;
        Speech speech(whisper_vad_segments_from_samples(vad.get(), speech_config, samples.data(),
                                                       static_cast<int>(samples.size())), whisper_vad_free_segments);
        if (!speech) throw std::runtime_error("VAD failed");
        std::vector<TimeRange> spans;
        for (int i = 0; i < whisper_vad_segments_n_segments(speech.get()); ++i) {
            spans.push_back({whisper_vad_segments_get_segment_t0(speech.get(), i) * 10.0,
                             whisper_vad_segments_get_segment_t1(speech.get(), i) * 10.0});
        }
        auto windows = plan_speech_windows(spans, reader.duration_seconds() * 1000.0);
#if defined(ASR_CANDIDATE_PAUSE_WINDOWS)
        constexpr double max_window_ms = 12000.0;
        windows = plan_native_pause_windows(windows, spans);
#elif defined(ASR_CANDIDATE_SHORT_WINDOWS)
        constexpr double max_window_ms = 12000.0;
        std::vector<SpeechWindow> bounded;
        for (const auto& window : windows) {
            for (double begin = window.audio.start_ms; begin < window.audio.end_ms;) {
                const double end = std::min(begin + max_window_ms, window.audio.end_ms);
                bounded.push_back({{begin, end}, {begin, end}});
                begin = end;
            }
        }
        windows = std::move(bounded);
#else
        constexpr double max_window_ms = 26000.0;
#endif
#ifdef ASR_CANDIDATE_GAP_WINDOWS
        windows = plan_native_gap_windows(windows, spans);
#endif
#ifdef ASR_CANDIDATE_RESTART_WINDOWS
        windows = plan_native_restart_windows(windows, spans);
#endif

        SherpaOnnxOfflineRecognizerConfig config{};
        config.decoding_method = "greedy_search";
        config.model_config.debug = 1;
        config.model_config.num_threads = 4;
        config.model_config.provider = "cpu";
#ifdef ASR_CANDIDATE_FIRERED_CTC
        if (!prompt.empty()) throw std::runtime_error("FireRed CTC candidate does not support terminology prompts");
        const auto model = (models / "model.int8.onnx").string();
        const auto tokens = (models / "tokens.txt").string();
        config.model_config.tokens = tokens.c_str();
        config.model_config.fire_red_asr_ctc.model = model.c_str();
        constexpr const char* model_name = "sherpa-onnx-fire-red-asr2-ctc-zh_en-int8-2026-02-25";
        constexpr const char* candidate_settings = "";
#elif defined(ASR_CANDIDATE_FIRERED_AED)
        if (!prompt.empty()) throw std::runtime_error("FireRed AED candidate does not support terminology prompts");
#ifdef ASR_CANDIDATE_FIRERED_FP32
        const auto encoder = (models / "encoder.onnx").string();
        const auto decoder = (models / "decoder.onnx").string();
        constexpr const char* model_name = "sherpa-onnx-fire-red-asr2-zh_en-2026-02-26";
#else
        const auto encoder = (models / "encoder.int8.onnx").string();
        const auto decoder = (models / "decoder.int8.onnx").string();
        constexpr const char* model_name = "sherpa-onnx-fire-red-asr2-zh_en-int8-2026-02-26";
#endif
        const auto tokens = (models / "tokens.txt").string();
        config.model_config.tokens = tokens.c_str();
        config.model_config.fire_red_asr.encoder = encoder.c_str();
        config.model_config.fire_red_asr.decoder = decoder.c_str();
        constexpr const char* candidate_settings = "\"max_token_per_second\":8,";
#elif defined(ASR_CANDIDATE_SENSEVOICE)
        if (!prompt.empty()) throw std::runtime_error("SenseVoice candidate does not support terminology prompts");
        const auto model = (models / "model.int8.onnx").string();
        const auto tokens = (models / "tokens.txt").string();
        config.model_config.tokens = tokens.c_str();
        config.model_config.sense_voice.model = model.c_str();
        config.model_config.sense_voice.language = "auto";
        config.model_config.sense_voice.use_itn = 0;
        constexpr const char* model_name = "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17";
        constexpr const char* candidate_settings = "\"use_itn\":false,";
#elif defined(ASR_CANDIDATE_FUNASR_NANO)
        const auto encoder = (models / "encoder_adaptor.int8.onnx").string();
        const auto embedding = (models / "embedding.int8.onnx").string();
        const auto llm = (models / "llm.int8.onnx").string();
        const auto tokenizer = (models / "Qwen3-0.6B").string();
        auto& nano = config.model_config.funasr_nano;
        nano.encoder_adaptor = encoder.c_str();
        nano.embedding = embedding.c_str();
        nano.llm = llm.c_str();
        nano.tokenizer = tokenizer.c_str();
        nano.max_new_tokens = 256;
        nano.temperature = 1e-6f;
        nano.top_p = .8f;
        nano.seed = 42;
        nano.itn = 0;
        nano.hotwords = prompt.c_str();
        constexpr const char* model_name = "sherpa-onnx-funasr-nano-int8-2025-12-30";
        constexpr const char* candidate_settings =
            "\"use_itn\":false,\"max_new_tokens\":256,\"temperature\":0.000001,"
            "\"top_p\":0.8,\"seed\":42,";
#else
        const auto frontend = (models / "conv_frontend.onnx").string();
        const auto encoder = (models / "encoder.int8.onnx").string();
        const auto decoder = (models / "decoder.int8.onnx").string();
        const auto tokenizer = (models / "tokenizer").string();
        auto& qwen = config.model_config.qwen3_asr;
        qwen.conv_frontend = frontend.c_str();
        qwen.encoder = encoder.c_str();
        qwen.decoder = decoder.c_str();
        qwen.tokenizer = tokenizer.c_str();
        qwen.max_total_len = 1024;
        qwen.max_new_tokens = 256;
        qwen.temperature = 1e-6f;
        qwen.top_p = .8f;
        qwen.seed = 42;
        qwen.hotwords = prompt.c_str();
        constexpr const char* model_name = "sherpa-onnx-qwen3-asr-0.6B-int8-2026-03-25";
        constexpr const char* candidate_settings =
            "\"requested_max_total_len\":1024,\"max_new_tokens\":256,\"temperature\":0.000001,"
            "\"top_p\":0.8,\"seed\":42,";
#endif
        const auto load_begin = std::chrono::steady_clock::now();
#ifdef ASR_CANDIDATE_BEAM_SIZE
        FireRedBeamRecognizer recognizer(config, ASR_CANDIDATE_BEAM_SIZE);
        constexpr const char* strategy = ASR_CANDIDATE_BEAM_SIZE == 1 ? "greedy_search" : "beam_search";
#else
        using Recognizer = std::unique_ptr<const SherpaOnnxOfflineRecognizer,
                                          decltype(&SherpaOnnxDestroyOfflineRecognizer)>;
        Recognizer recognizer(SherpaOnnxCreateOfflineRecognizer(&config), SherpaOnnxDestroyOfflineRecognizer);
        if (!recognizer) throw std::runtime_error("Could not initialize local native candidate");
        constexpr const char* strategy = "greedy_search";
#endif
        const double load_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - load_begin).count();

        std::ofstream output(destination);
        if (!output) throw std::runtime_error("Could not open candidate output");
        output << std::setprecision(12) << "{\"duration\":" << reader.duration_seconds()
               << ",\"model\":\"" << model_name << "\","
                  "\"settings\":{\"strategy\":\"" << strategy << "\",\"threads\":4,\"provider\":\"cpu\","
               << candidate_settings << "\"requested_language\":\"auto\","
                  "\"acoustic_timestamps\":false,\"history_prompt\":false,\"max_window_seconds\":"
               << max_window_ms / 1000.0
#ifdef ASR_EXPERIMENT_AUDIO_FILTER
               << ",\"audio_filter\":\"" << ASR_EXPERIMENT_AUDIO_FILTER << "\""
#endif
#ifdef ASR_CANDIDATE_PAUSE_WINDOWS
               << ",\"window_policy\":\"vad_pause_balanced\""
#endif
#ifdef ASR_CANDIDATE_GAP_WINDOWS
               << ",\"window_policy\":\"vad_gap_split\",\"split_gap_seconds\":1.5"
#endif
#ifdef ASR_CANDIDATE_RESTART_WINDOWS
               << ",\"window_policy\":\"vad_gap_restart\",\"split_gap_seconds\":1.5"
#endif
#ifdef ASR_CANDIDATE_BEAM_SIZE
               << ",\"decoder_implementation\":\"native_beam_experiment\",\"beam_size\":" << ASR_CANDIDATE_BEAM_SIZE
               << ",\"softmax_smoothing\":" << (ASR_CANDIDATE_BEAM_SIZE == 1 ? 1.0 : 1.25)
               << ",\"length_penalty\":" << (ASR_CANDIDATE_BEAM_SIZE == 1 ? 0.0 : 0.6)
#endif
               << "},\"model_load_seconds\":" << load_seconds << ",\"windows\":[\n";
        for (size_t i = 0; i < windows.size(); ++i) {
            const auto& window = windows[i];
            const auto begin = static_cast<size_t>(window.audio.start_ms * 16.0);
            const auto end = std::min(samples.size(), static_cast<size_t>(window.audio.end_ms * 16.0));
            const auto decode_begin = std::chrono::steady_clock::now();
#ifdef ASR_CANDIDATE_BEAM_SIZE
            const auto result_json = recognizer.decode(samples.data() + begin, static_cast<int>(end - begin));
#else
            using Stream = std::unique_ptr<const SherpaOnnxOfflineStream, decltype(&SherpaOnnxDestroyOfflineStream)>;
            Stream stream(SherpaOnnxCreateOfflineStream(recognizer.get()), SherpaOnnxDestroyOfflineStream);
            if (!stream) throw std::runtime_error("Could not create candidate stream");
#ifdef ASR_CANDIDATE_FIRERED_AED
            SherpaOnnxOfflineStreamSetOption(stream.get(), "max_token_per_second", "8");
#endif
            SherpaOnnxAcceptWaveformOffline(stream.get(), 16000, samples.data() + begin, static_cast<int>(end - begin));
            SherpaOnnxDecodeOfflineStream(recognizer.get(), stream.get());
            using Result = std::unique_ptr<const SherpaOnnxOfflineRecognizerResult,
                                          decltype(&SherpaOnnxDestroyOfflineRecognizerResult)>;
            Result result(SherpaOnnxGetOfflineStreamResult(stream.get()), SherpaOnnxDestroyOfflineRecognizerResult);
            if (!result || !result->json) throw std::runtime_error("Missing candidate result");
            const std::string result_json = result->json;
#endif
            const double decode_seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - decode_begin).count();
            if (i) output << ",\n";
            output << "{\"start\":" << window.audio.start_ms / 1000.0
                   << ",\"end\":" << window.audio.end_ms / 1000.0
                   << ",\"samples\":" << end - begin
                   << ",\"decode_seconds\":" << decode_seconds
                   << ",\"result\":" << result_json << "}";
            (void)fprintf(stderr, "[INFO] Native candidate: %zu/%zu\n", i + 1, windows.size());
        }
        output << "\n]}\n";
        if (!output) throw std::runtime_error("Could not write candidate output");
        return 0;
    } catch (const std::exception& error) {
        (void)fprintf(stderr, "[ERROR] %s\n", error.what());
        return 1;
    }
}
