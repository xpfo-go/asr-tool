#pragma once

#include "types.hpp"
#include <filesystem>
#include <string>

bool write_output(const TranscriptionResult& result,
                  OutputFormat format,
                  const std::filesystem::path& output_path);
