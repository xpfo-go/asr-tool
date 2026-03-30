#pragma once

#include <filesystem>

// 解析主模型路径。
// - model_dir 为空：使用默认缓存目录（Linux/macOS: ~/.cache/whisper, Windows: %USERPROFILE%\.cache\whisper）
// - model_dir 非空：在该目录下优先选择默认模型名，其次自动发现 ggml-large-v3*.bin
std::filesystem::path get_default_model_path(
    const std::filesystem::path& model_dir = {});

// 确保主模型存在；缺失时自动下载，失败返回 false。
bool ensure_main_model(const std::filesystem::path& model_path);

#if defined(__APPLE__)
// macOS: 尝试准备预构建 CoreML encoder。
// 返回 true 表示 CoreML encoder 可用；false 表示应回退 CPU 推理。
bool ensure_coreml_encoder_model(const std::filesystem::path& model_path);
#endif
