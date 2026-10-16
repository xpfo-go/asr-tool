#pragma once

#include "types.hpp"
#include <filesystem>
#include <iosfwd>
#include <string>

struct LiveEvent;
void write_live_event(std::ostream& output, const LiveEvent& event);

bool write_output(const TranscriptionResult& result,
                  OutputFormat format,
                  const std::filesystem::path& output_path);
