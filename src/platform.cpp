#include "platform.hpp"

#include <ggml-backend.h>

#include <cstdio>

Backend detect_backend() {
#if defined(__APPLE__)
    // macOS: 始终启用 GPU 路径，实际是否启用 CoreML 由 whisper.cpp 运行时决定。
    // 当缺少 .mlmodelc 时，whisper.cpp 会回退到非 CoreML 路径。
    (void)fprintf(stderr, "[INFO] 加速方案: macOS（请求 GPU 路径，CoreML 由运行时决定）\n");
    return Backend::CoreML;

#elif defined(__linux__)
    // Linux: 有可用 GPU backend 时使用 CUDA，否则回退 CPU。
    (void)fprintf(stderr, "[INFO] 检测加速方案: Linux — ");
    (void)fflush(stderr);

    if (ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU) != nullptr ||
        ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_IGPU) != nullptr) {
        (void)fprintf(stderr, "检测到可用 GPU 后端\n");
        return Backend::CUDA;
    }
    (void)fprintf(stderr, "未检测到可用 GPU 后端，回退到 CPU 模式\n");
    return Backend::CPU;

#elif defined(_WIN32)
    // Windows: 有可用 GPU backend 时使用 CUDA，否则回退 CPU。
    (void)fprintf(stderr, "[INFO] 检测加速方案: Windows — ");
    (void)fflush(stderr);

    if (ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU) != nullptr ||
        ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_IGPU) != nullptr) {
        (void)fprintf(stderr, "检测到可用 GPU 后端\n");
        return Backend::CUDA;
    }
    (void)fprintf(stderr, "未检测到可用 GPU 后端，回退到 CPU 模式\n");
    return Backend::CPU;

#else
    return Backend::CPU;
#endif
}

std::string backend_name(Backend b) {
    switch (b) {
        case Backend::CoreML: return "CoreML";
        case Backend::CUDA:   return "CUDA";
        case Backend::CPU:    return "CPU";
    }
    return "Unknown";
}
