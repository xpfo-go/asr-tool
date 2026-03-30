#pragma once

#include <filesystem>

// 返回默认主模型路径：
//   Linux/macOS: $XDG_CACHE_HOME/whisper 或 ~/.cache/whisper
//   Windows:     %XDG_CACHE_HOME%/whisper 或 %USERPROFILE%\.cache\whisper
std::filesystem::path get_default_model_path();

// 确保主模型存在；缺失时自动下载，失败返回 false。
bool ensure_main_model(const std::filesystem::path& model_path);

#if defined(__APPLE__)
// macOS: 尝试准备预构建 CoreML encoder（失败自动回退）。
void ensure_coreml_encoder_model(const std::filesystem::path& model_path);
#endif
