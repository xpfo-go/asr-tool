#pragma once

#include "meeting.hpp"

#include <filesystem>
#include <string>
#include <vector>

std::vector<SpeakerTurn> diarize(const std::vector<float>& samples,
                                const std::filesystem::path& model_dir,
                                std::string& error, std::string& model_name);
