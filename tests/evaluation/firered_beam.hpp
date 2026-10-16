#pragma once

#include <sherpa-onnx/c-api/c-api.h>

#include <memory>
#include <string>

class FireRedBeamRecognizer {
public:
    FireRedBeamRecognizer(const SherpaOnnxOfflineRecognizerConfig& config, int beam_size);
    ~FireRedBeamRecognizer();
    std::string decode(const float* samples, int count);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
