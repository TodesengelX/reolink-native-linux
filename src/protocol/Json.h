#pragma once

// Single include point for the vendored nlohmann/json (MIT).
// Exceptions are disabled repo-wide for parsing: use json::parse(..., nullptr, false)
// and check is_discarded() — protocol code must never throw across Qt event loops.
#include <nlohmann/json.hpp>

namespace rl {
using Json = nlohmann::json;
}
