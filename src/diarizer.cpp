#include "diarizer.hpp"
#include "model_manager.hpp"

#include <sherpa-onnx/c-api/c-api.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>

namespace {

int32_t report_progress(int32_t done, int32_t total, void* data) {
    auto& last = *static_cast<int*>(data);
    const int percent = total > 0 ? done * 100 / total : 0;
    if (percent >= last + 10) {
        (void)fprintf(stderr, "[INFO] 说话人分析: %d%%\n", percent);
        last = percent;
    }
    return 0;
}

}  // namespace

std::vector<SpeakerTurn> diarize(const std::vector<float>& samples,
                                const std::filesystem::path& model_dir,
                                std::string& error, std::string& model_name) try {
    error.clear();
    model_name.clear();
    const std::string segmentation = (model_dir / "segmentation.onnx").string();
    if (samples.empty() || samples.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        error = "音频为空或超过说话人分析长度限制";
        return {};
    }
    std::error_code ec;
    const bool updated_embedding =
        std::filesystem::file_size(model_dir / SPEAKER_MODEL_FILENAME, ec) == 39593761 && !ec;
    const std::string embedding = (model_dir / (updated_embedding ? SPEAKER_MODEL_FILENAME :
                                                               LEGACY_SPEAKER_MODEL_FILENAME)).string();
    if (std::filesystem::file_size(segmentation, ec) != 5992913 || ec ||
        std::filesystem::file_size(embedding, ec) != (updated_embedding ? 39593761 : 26530550) || ec) {
        error = "说话人模型缺失或不完整，请先运行 asr-tool --prepare-models";
        return {};
    }
    SherpaOnnxOfflineSpeakerDiarizationConfig config{};
    config.segmentation.pyannote.model = segmentation.c_str();
    config.segmentation.pyannote.window_shift_ratio = 0.5f;
    config.segmentation.num_threads = 2;
    config.segmentation.provider = "cpu";
    config.embedding.model = embedding.c_str();
    config.embedding.num_threads = 2;
    config.embedding.provider = "cpu";
    config.clustering.num_clusters = -1;
    config.clustering.threshold = updated_embedding ? 1.05f : 0.6f;
    if (!updated_embedding) {
        error = "使用已有 WeSpeaker 模型；可在联网时运行 asr-tool --prepare-models 更新会议模型";
    }
    (void)fprintf(stderr, "[INFO] 说话人模型: %s\n",
                  updated_embedding ? SPEAKER_MODEL_FILENAME : LEGACY_SPEAKER_MODEL_FILENAME);
    config.clustering.compute_confidence = 1;
    config.min_duration_on = 0.2f;
    config.min_duration_off = 0.3f;

    using Diarizer = std::unique_ptr<const SherpaOnnxOfflineSpeakerDiarization,
                                   decltype(&SherpaOnnxDestroyOfflineSpeakerDiarization)>;
    Diarizer sd(SherpaOnnxCreateOfflineSpeakerDiarization(&config),
                SherpaOnnxDestroyOfflineSpeakerDiarization);
    if (!sd || SherpaOnnxOfflineSpeakerDiarizationGetSampleRate(sd.get()) != 16000) {
        error = "说话人模型初始化失败或采样率不兼容";
        return {};
    }
    int last = -10;
    using Result = std::unique_ptr<const SherpaOnnxOfflineSpeakerDiarizationResult,
                                  decltype(&SherpaOnnxOfflineSpeakerDiarizationDestroyResult)>;
    Result result(SherpaOnnxOfflineSpeakerDiarizationProcessWithCallback(
                      sd.get(), samples.data(), static_cast<int32_t>(samples.size()),
                      report_progress, &last),
                  SherpaOnnxOfflineSpeakerDiarizationDestroyResult);
    if (!result) {
        error = "说话人分析失败";
        return {};
    }
    using Turns = std::unique_ptr<const SherpaOnnxOfflineSpeakerDiarizationSegment,
                                 decltype(&SherpaOnnxOfflineSpeakerDiarizationDestroySegment)>;
    Turns turns(SherpaOnnxOfflineSpeakerDiarizationResultSortByStartTime(result.get()),
                SherpaOnnxOfflineSpeakerDiarizationDestroySegment);
    const int count = SherpaOnnxOfflineSpeakerDiarizationResultGetNumSegments(result.get());
    std::vector<SpeakerTurn> output;
    for (int i = 0; turns && i < count; ++i) {
        output.push_back({turns.get()[i].start * 1000.0, turns.get()[i].end * 1000.0,
                          turns.get()[i].speaker, turns.get()[i].confidence});
    }
    normalize_speakers(output);
    if (output.empty()) {
        error = "未检测到可区分的说话人";
    } else {
        model_name = updated_embedding ? SPEAKER_MODEL_FILENAME : LEGACY_SPEAKER_MODEL_FILENAME;
    }
    return output;
} catch (const std::exception& e) {
    error = "说话人分析失败，请检查本地模型: " + std::string(e.what());
    return {};
} catch (...) {
    error = "Speaker analysis failed; check local models";
    return {};
}
