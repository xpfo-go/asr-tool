#pragma once

#include "meeting.hpp"

#include <filesystem>
#include <string>
#include <vector>

std::vector<SpeakerTurn> diarize(const std::vector<float>& samples,
                                const std::filesystem::path& model_dir,
                                std::string& error, std::string& model_name);

// Call after planning ASR windows from the original turns. Returns changed regions.
size_t refine_diarization(const std::vector<float>& samples,
                         const std::filesystem::path& model_dir,
                         std::vector<SpeakerTurn>& turns, std::string& error);
