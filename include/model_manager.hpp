#pragma once

#include <filesystem>

inline constexpr const char* SPEAKER_MODEL_FILENAME = "3dspeaker_speech_eres2net_base_sv_zh-cn_3dspeaker_16k.onnx";
inline constexpr const char* LEGACY_SPEAKER_MODEL_FILENAME = "wespeaker_en_voxceleb_resnet34_LM.onnx";
inline constexpr const char* STREAMING_MODEL_DIRECTORY = "streaming-zipformer-bilingual-zh-en-2023-02-20";

// 解析主模型路径。
// - model_dir 为空：使用默认缓存目录（Linux/macOS: ~/.cache/whisper, Windows: %USERPROFILE%\.cache\whisper）
// - model_dir 非空：在该目录下优先选择默认模型名，其次自动发现 ggml-large-v3*.bin
std::filesystem::path get_default_model_path(
    const std::filesystem::path& model_dir = {});

// Downloads are allowed only during explicit model preparation.
bool ensure_main_model(const std::filesystem::path& model_path, bool allow_download = false);

// Downloads are explicit; normal transcription never calls these functions.
bool prepare_meeting_models(const std::filesystem::path& model_dir);
bool prepare_streaming_model(const std::filesystem::path& model_dir);

#if defined(__APPLE__)
// macOS: 尝试准备预构建 CoreML encoder。
// 返回 true 表示 CoreML encoder 可用；false 时仍可使用 Metal。
bool ensure_coreml_encoder_model(const std::filesystem::path& model_path, bool allow_download = false);
#endif
