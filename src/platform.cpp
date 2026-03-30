#include "platform.hpp"

#include <cstdio>

Backend detect_backend() {
#if defined(__APPLE__)
    // macOS: 始终启用 GPU 路径，实际是否启用 CoreML 由 whisper.cpp 运行时决定。
    // 当缺少 .mlmodelc 时，whisper.cpp 会回退到非 CoreML 路径。
    (void)fprintf(stderr, "[INFO] 加速方案: macOS（请求 GPU 路径，CoreML 由运行时决定）\n");
    return Backend::CoreML;

#elif defined(__linux__)
    // Linux: 优先 CUDA，回退 CPU
    (void)fprintf(stderr, "[INFO] 检测加速方案: Linux — ");
    (void)fflush(stderr);

    FILE* fp = popen("nvidia-smi -L 2>/dev/null", "r");
    if (fp) {
        char buf[256] = {0};
        if (fgets(buf, sizeof(buf), fp) != nullptr && buf[0] != '\0') {
            (void)pclose(fp);
            (void)fprintf(stderr, "检测到 CUDA\n");
            return Backend::CUDA;
        }
        (void)pclose(fp);
    }
    (void)fprintf(stderr, "未找到 NVIDIA 驱动，回退到 CPU 模式\n");
    return Backend::CPU;

#elif defined(_WIN32)
    // Windows: 优先 CUDA，回退 CPU
    (void)fprintf(stderr, "[INFO] 检测加速方案: Windows — ");
    (void)fflush(stderr);

    FILE* fp = _popen("nvidia-smi -L 2>NUL", "r");
    if (fp) {
        char buf[256] = {0};
        if (fgets(buf, sizeof(buf), fp) != nullptr && buf[0] != '\0') {
            (void)_pclose(fp);
            (void)fprintf(stderr, "检测到 CUDA\n");
            return Backend::CUDA;
        }
        (void)_pclose(fp);
    }
    (void)fprintf(stderr, "未找到 NVIDIA 驱动，回退到 CPU 模式\n");
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
