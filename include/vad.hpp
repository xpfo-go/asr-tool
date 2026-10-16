#pragma once

#include "meeting.hpp"

#include <filesystem>
#include <vector>

std::vector<TimeRange> detect_speech(const std::vector<float>& samples,
                                    const std::filesystem::path& model_path,
                                    float threshold = .5f);
