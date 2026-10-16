#include "audio.hpp"
#include "meeting.hpp"

#include <sherpa-onnx/c-api/c-api.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("Expected AUDIO -m MODEL_DIR -f json -o OUTPUT.json");
        std::filesystem::path models, destination;
        for (int i = 2; i < argc; ++i) {
            const std::string option = argv[i];
            if (++i == argc) throw std::runtime_error("Missing value for " + option);
            if (option == "-m") models = argv[i];
            else if (option == "-o") destination = argv[i];
            else if (option != "-f" || std::string(argv[i]) != "json")
                throw std::runtime_error("Unsupported diarization option: " + option);
        }
        if (models.empty() || destination.empty()) throw std::runtime_error("Missing model/output directory");
        auto reader = open_audio(argv[1]);
        const auto samples = reader.read_all();
        if (samples.empty() || samples.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
            throw std::runtime_error("Invalid diarization audio length");
        const auto segmentation = (models / "segmentation.onnx").string();
        const auto embedding = (models / "embedding.onnx").string();
        SherpaOnnxOfflineSpeakerDiarizationConfig config{};
        config.segmentation.pyannote.model = segmentation.c_str();
#ifdef ASR_CANDIDATE_DENSE_WINDOWS
        config.segmentation.pyannote.window_shift_ratio = 0.25f;
#else
        config.segmentation.pyannote.window_shift_ratio = 0.5f;
#endif
        config.segmentation.num_threads = 2;
        config.segmentation.provider = "cpu";
        config.embedding.model = embedding.c_str();
        config.embedding.num_threads = 2;
        config.embedding.provider = "cpu";
        config.clustering.num_clusters = -1;
        config.clustering.threshold = 1.05f;
        config.clustering.compute_confidence = 1;
#ifdef ASR_CANDIDATE_CONSTRAINED
        config.clustering.compute_confidence = 0;
#endif
#ifdef ASR_CANDIDATE_SHORT_TURNS
        config.min_duration_on = 0.1f;
        config.min_duration_off = 0.1f;
#else
        config.min_duration_on = 0.2f;
        config.min_duration_off = 0.3f;
#endif

        using Diarizer = std::unique_ptr<const SherpaOnnxOfflineSpeakerDiarization,
                                        decltype(&SherpaOnnxDestroyOfflineSpeakerDiarization)>;
        Diarizer diarizer(SherpaOnnxCreateOfflineSpeakerDiarization(&config),
                          SherpaOnnxDestroyOfflineSpeakerDiarization);
        if (!diarizer || SherpaOnnxOfflineSpeakerDiarizationGetSampleRate(diarizer.get()) != 16000)
            throw std::runtime_error("Could not initialize local diarization candidate");
        using Result = std::unique_ptr<const SherpaOnnxOfflineSpeakerDiarizationResult,
                                      decltype(&SherpaOnnxOfflineSpeakerDiarizationDestroyResult)>;
        Result result(SherpaOnnxOfflineSpeakerDiarizationProcess(
                          diarizer.get(), samples.data(), static_cast<int32_t>(samples.size())),
                      SherpaOnnxOfflineSpeakerDiarizationDestroyResult);
        if (!result) throw std::runtime_error("Diarization failed");
        using Turns = std::unique_ptr<const SherpaOnnxOfflineSpeakerDiarizationSegment,
                                     decltype(&SherpaOnnxOfflineSpeakerDiarizationDestroySegment)>;
        Turns turns(SherpaOnnxOfflineSpeakerDiarizationResultSortByStartTime(result.get()),
                    SherpaOnnxOfflineSpeakerDiarizationDestroySegment);
        const int count = SherpaOnnxOfflineSpeakerDiarizationResultGetNumSegments(result.get());
        if (count > 0 && !turns) throw std::runtime_error("Missing diarization turns");
        std::vector<SpeakerTurn> normalized;
        for (int i = 0; i < count; ++i) {
            const auto& turn = turns.get()[i];
            normalized.push_back({turn.start * 1000.0, turn.end * 1000.0, turn.speaker, turn.confidence});
        }
        normalize_speakers(normalized);
        std::ofstream output(destination);
        if (!output) throw std::runtime_error("Could not open diarization output");
        output << std::setprecision(12) << "{\"duration\":" << reader.duration_seconds()
               << ",\"settings\":{\"threads\":2,\"provider\":\"cpu\",\"window_shift_ratio\":"
               << config.segmentation.pyannote.window_shift_ratio << ',' <<
#ifdef ASR_CANDIDATE_MARGINAL
                  "\"powerset_decoder\":\"minimum_expected_hamming\","
#endif
#ifdef ASR_CANDIDATE_SPECTRAL
                  "\"num_clusters\":-1,"
#ifdef ASR_CANDIDATE_CONSTRAINED
                  "\"compute_confidence\":false,\"assignment\":\"constrained_cosine_centroid\","
#else
                  "\"compute_confidence\":true,"
#endif
#ifdef ASR_CANDIDATE_HYBRID
                  "\"clustering\":\"native_silhouette_selection\",\"complete_threshold\":1.05,"
                  "\"p\":0.01,\"seed\":42,\"n_init\":10,"
#elif defined(ASR_CANDIDATE_NME)
                  "\"clustering\":\"native_nme_spectral\",\"max_neighbor_ratio\":0.15,"
                  "\"search_volume\":30,\"search_matrix_target\":512,\"seed\":42,\"n_init\":10,"
#else
                  "\"clustering\":\"native_spectral\",\"p\":0.01,\"seed\":42,\"n_init\":10,"
#endif
#ifdef ASR_CANDIDATE_HYBRID
                  "\"spectral_max_speakers\":20,"
#else
                  "\"max_speakers\":20,"
#endif
                  "\"max_embeddings\":4096,\"eigen_version\":\"5.0.1\","
                  "\"silhouette_distance_preserved\":true,"
#ifdef ASR_CANDIDATE_OVERLAP_MASKS
                  "\"overlap_embedding_fallback\":true,\"min_embedding_frames\":10,"
#endif
#else
                  "\"num_clusters\":-1,\"threshold\":1.05,\"compute_confidence\":true,"
#endif
#ifdef ASR_CANDIDATE_SHORT_TURNS
                  "\"min_duration_on\":0.1,\"min_duration_off\":0.1,\"normalize_speakers\":true},"
#else
                  "\"min_duration_on\":0.2,\"min_duration_off\":0.3,\"normalize_speakers\":true},"
#endif
                  "\"speaker_turns\":[";
        for (size_t i = 0; i < normalized.size(); ++i) {
            const auto& turn = normalized[i];
            if (i) output << ',';
            output << "{\"start\":" << turn.start_ms / 1000.0 << ",\"end\":" << turn.end_ms / 1000.0
                   << ",\"confidence\":" << turn.confidence << ",\"speaker\":";
            if (turn.speaker < 0) output << "null";
            else output << "\"speaker_" << turn.speaker + 1 << "\"";
            output << '}';
        }
        output << "]}\n";
        if (!output) throw std::runtime_error("Could not write diarization output");
        return 0;
    } catch (const std::exception& error) {
        (void)fprintf(stderr, "[ERROR] %s\n", error.what());
        return 1;
    }
}
