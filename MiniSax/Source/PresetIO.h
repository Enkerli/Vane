#pragma once
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "MiniSaxVoice.h"

namespace minisax
{

struct Preset
{
    std::string id;
    std::string modelName;
    std::string modelVersion;
    std::string createdAt;
    std::string derivedFrom; // empty if null/absent
    std::string gitCommit;   // empty if null/absent
    std::string description;
    Parameters parameters;
    nlohmann::json raw; // full document, preserved for provenance
};

// Loads and validates a preset.  Throws std::runtime_error with a clear
// message on missing required fields or malformed values.  Normalized
// parameters are clamped to [0, 1]; unknown parameter fields are preserved
// in `raw` but ignored by the engine (explicitly, per docs/FIRST_ISSUES.md).
Preset loadPreset(const std::string& path);

// FNV-1a 64-bit hash over the canonical "name=value" parameter serialization.
// Included in render reports so descriptor rows can be matched to exact
// parameter sets even if preset files are edited later.
uint64_t parametersHash(const Parameters& p);
std::string parametersHashHex(const Parameters& p);

} // namespace minisax
