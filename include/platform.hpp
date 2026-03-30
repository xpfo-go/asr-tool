#pragma once

#include "types.hpp"
#include <string>

Backend detect_backend();

std::string backend_name(Backend b);
