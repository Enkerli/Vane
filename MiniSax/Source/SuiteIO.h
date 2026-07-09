#pragma once
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace minisax
{

// One articulation event from a test-suite JSON file.
struct SuiteEvent
{
    double time = 0.0;
    std::string type;       // "noteOn", "noteOff", "control", "pitch"
    double note = -1.0;     // MIDI note number, -1 if absent
    double pitchHz = -1.0;  // direct frequency, -1 if absent
    double value = 0.0;     // for "control"
    std::string target;     // for "control": parameter name
    std::string curve;      // "linear" or empty (= step)
    double glideSeconds = -1.0; // optional per-event pitch glide override
};

struct SuiteTest
{
    std::string id;
    std::string description;
    double durationSeconds = 1.0;
    std::vector<SuiteEvent> events;
};

struct Suite
{
    std::string id;
    std::string description;
    int sampleRate = 48000;
    std::vector<SuiteTest> tests;
    nlohmann::json raw;
};

// Loads and validates an articulation suite.  Throws std::runtime_error with
// a clear message on missing required fields.
Suite loadSuite(const std::string& path);

} // namespace minisax
