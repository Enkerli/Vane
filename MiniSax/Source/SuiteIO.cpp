#include "SuiteIO.h"

#include <fstream>
#include <stdexcept>

namespace minisax
{
namespace
{
    using nlohmann::json;

    double numberOr(const json& j, const char* key, double fallback)
    {
        if (j.contains(key) && j[key].is_number())
            return j[key].get<double>();
        return fallback;
    }

    std::string stringOr(const json& j, const char* key, const std::string& fallback = {})
    {
        if (j.contains(key) && j[key].is_string())
            return j[key].get<std::string>();
        return fallback;
    }
} // namespace

Suite loadSuite(const std::string& path)
{
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("suite file not found: " + path);

    json j;
    try
    {
        j = json::parse(in);
    }
    catch (const json::parse_error& e)
    {
        throw std::runtime_error("suite is not valid JSON (" + path + "): " + e.what());
    }

    Suite suite;
    suite.raw = j;

    if (!j.contains("id") || !j["id"].is_string())
        throw std::runtime_error("suite " + path + ": missing required string field \"id\"");
    suite.id = j["id"].get<std::string>();
    suite.description = stringOr(j, "description");

    if (!j.contains("sampleRate") || !j["sampleRate"].is_number_integer())
        throw std::runtime_error("suite " + path + ": missing required integer field \"sampleRate\"");
    suite.sampleRate = j["sampleRate"].get<int>();
    if (suite.sampleRate < 8000 || suite.sampleRate > 192000)
        throw std::runtime_error("suite " + path + ": sampleRate out of supported range 8000..192000");

    if (!j.contains("tests") || !j["tests"].is_array() || j["tests"].empty())
        throw std::runtime_error("suite " + path + ": missing non-empty \"tests\" array");

    for (const json& jt : j["tests"])
    {
        SuiteTest test;
        if (!jt.contains("id") || !jt["id"].is_string())
            throw std::runtime_error("suite " + path + ": every test needs a string \"id\"");
        test.id = jt["id"].get<std::string>();
        test.description = stringOr(jt, "description");
        test.durationSeconds = numberOr(jt, "durationSeconds", -1.0);
        if (test.durationSeconds <= 0.0 || test.durationSeconds > 120.0)
            throw std::runtime_error("suite " + path + ": test \"" + test.id
                                     + "\" needs durationSeconds in (0, 120]");

        if (!jt.contains("events") || !jt["events"].is_array())
            throw std::runtime_error("suite " + path + ": test \"" + test.id + "\" needs an \"events\" array");

        for (const json& je : jt["events"])
        {
            SuiteEvent ev;
            if (!je.contains("time") || !je["time"].is_number())
                throw std::runtime_error("suite " + path + ": event in \"" + test.id + "\" missing \"time\"");
            if (!je.contains("type") || !je["type"].is_string())
                throw std::runtime_error("suite " + path + ": event in \"" + test.id + "\" missing \"type\"");
            ev.time = je["time"].get<double>();
            ev.type = je["type"].get<std::string>();
            ev.note = numberOr(je, "note", -1.0);
            ev.pitchHz = numberOr(je, "pitchHz", -1.0);
            ev.value = numberOr(je, "value", 0.0);
            ev.target = stringOr(je, "target");
            ev.curve = stringOr(je, "curve");
            ev.glideSeconds = numberOr(je, "glideSeconds", -1.0);

            if (ev.type == "control" && ev.target.empty())
                throw std::runtime_error("suite " + path + ": control event in \"" + test.id
                                         + "\" missing \"target\"");
            test.events.push_back(ev);
        }
        suite.tests.push_back(std::move(test));
    }

    return suite;
}

} // namespace minisax
