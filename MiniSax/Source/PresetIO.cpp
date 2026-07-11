#include "PresetIO.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace minisax
{
namespace
{
    using nlohmann::json;

    json loadJsonFile(const std::string& path, const char* what)
    {
        std::ifstream in(path);
        if (!in)
            throw std::runtime_error(std::string(what) + " file not found: " + path);
        try
        {
            return json::parse(in);
        }
        catch (const json::parse_error& e)
        {
            throw std::runtime_error(std::string(what) + " is not valid JSON (" + path + "): " + e.what());
        }
    }

    std::string requireString(const json& j, const char* key, const std::string& path)
    {
        if (!j.contains(key) || !j[key].is_string())
            throw std::runtime_error("preset " + path + ": missing required string field \"" + key + "\"");
        return j[key].get<std::string>();
    }

    std::string optionalString(const json& j, const char* key)
    {
        if (j.contains(key) && j[key].is_string())
            return j[key].get<std::string>();
        return {};
    }

    float requireNormalized(const json& params, const char* key, const std::string& path)
    {
        if (!params.contains(key) || !params[key].is_number())
            throw std::runtime_error("preset " + path + ": parameters missing required number \"" + key + "\"");
        return std::clamp(params[key].get<float>(), 0.0f, 1.0f);
    }

    float optionalNormalized(const json& params, const char* key, float fallback)
    {
        if (params.contains(key) && params[key].is_number())
            return std::clamp(params[key].get<float>(), 0.0f, 1.0f);
        return fallback;
    }
} // namespace

Preset loadPreset(const std::string& path)
{
    const json j = loadJsonFile(path, "preset");

    Preset preset;
    preset.raw = j;
    preset.id = requireString(j, "id", path);
    preset.modelName = requireString(j, "modelName", path);
    preset.modelVersion = requireString(j, "modelVersion", path);
    preset.createdAt = requireString(j, "createdAt", path);
    preset.derivedFrom = optionalString(j, "derivedFrom");
    preset.gitCommit = optionalString(j, "gitCommit");
    preset.description = optionalString(j, "description");

    if (!j.contains("parameters") || !j["parameters"].is_object())
        throw std::runtime_error("preset " + path + ": missing required object \"parameters\"");
    const json& params = j["parameters"];

    Parameters& p = preset.parameters;
    p.breath = requireNormalized(params, "breath", path);
    p.embouchure = requireNormalized(params, "embouchure", path);
    p.reedStiffness = requireNormalized(params, "reedStiffness", path);
    p.reedAperture = requireNormalized(params, "reedAperture", path);
    p.boreDamping = requireNormalized(params, "boreDamping", path);
    p.bellBrightness = requireNormalized(params, "bellBrightness", path);
    p.noiseAmount = requireNormalized(params, "noiseAmount", path);
    p.growlAmount = optionalNormalized(params, "growlAmount", 0.0f);
    p.vibratoAirAmount = optionalNormalized(params, "vibratoAirAmount", 0.0f);
    p.vibratoPitchAmount = optionalNormalized(params, "vibratoPitchAmount", 0.0f);
    p.conicalAmount = optionalNormalized(params, "conicalAmount", Parameters{}.conicalAmount);

    if (!params.contains("outputGain") || !params["outputGain"].is_number())
        throw std::runtime_error("preset " + path + ": parameters missing required number \"outputGain\"");
    // outputGain is linear gain, not normalized; clamp to a sane exploration range.
    p.outputGain = std::clamp(params["outputGain"].get<float>(), 0.0f, 4.0f);

    return preset;
}

uint64_t parametersHash(const Parameters& p)
{
    // Canonical serialization: fixed field order, fixed %.6f formatting.
    const std::array<std::pair<const char*, float>, 12> fields = {{
        {"bellBrightness", p.bellBrightness},
        {"boreDamping", p.boreDamping},
        {"conicalAmount", p.conicalAmount},
        {"breath", p.breath},
        {"embouchure", p.embouchure},
        {"growlAmount", p.growlAmount},
        {"noiseAmount", p.noiseAmount},
        {"outputGain", p.outputGain},
        {"reedAperture", p.reedAperture},
        {"reedStiffness", p.reedStiffness},
        {"vibratoAirAmount", p.vibratoAirAmount},
        {"vibratoPitchAmount", p.vibratoPitchAmount},
    }};

    constexpr uint64_t fnvOffset = 1469598103934665603ull;
    constexpr uint64_t fnvPrime = 1099511628211ull;
    uint64_t hash = fnvOffset;
    char line[64];
    for (const auto& [name, value] : fields)
    {
        std::snprintf(line, sizeof(line), "%s=%.6f;", name, static_cast<double>(value));
        for (const char* c = line; *c != '\0'; ++c)
        {
            hash ^= static_cast<uint64_t>(static_cast<unsigned char>(*c));
            hash *= fnvPrime;
        }
    }
    return hash;
}

std::string parametersHashHex(const Parameters& p)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(parametersHash(p)));
    return buf;
}

} // namespace minisax
