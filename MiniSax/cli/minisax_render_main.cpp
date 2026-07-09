// minisax-render: offline articulation renderer.
//
//   minisax-render --preset presets/breathy_001.json \
//                  --suite tests/articulation_suite_001.json \
//                  --out renders/breathy_001/ [--seed N] [--git-commit HASH]
//
// Writes one 16-bit mono WAV per test case (named after the test id) plus
// render_report.json with full provenance (see docs/TRACEABILITY.md).
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "../Source/PresetIO.h"
#include "../Source/Renderer.h"
#include "../Source/SuiteIO.h"
#include "../Source/WavWriter.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace
{

constexpr const char* engineVersion = "0.1.0";
constexpr uint32_t defaultRenderSeed = 20260709u; // arbitrary fixed default, recorded in the report

void printUsage()
{
    std::cerr <<
        "Usage: minisax-render --preset <preset.json> --suite <suite.json> --out <dir>\n"
        "                      [--seed <uint32>] [--git-commit <hash>]\n";
}

// Engine commit for provenance: --git-commit flag wins, then MINISAX_GIT_COMMIT
// env var, then a best-effort `git rev-parse`; empty string if all fail.
std::string resolveGitCommit(const std::string& fromFlag)
{
    if (!fromFlag.empty())
        return fromFlag;
    if (const char* env = std::getenv("MINISAX_GIT_COMMIT"); env != nullptr && env[0] != '\0')
        return env;
#if defined(_WIN32)
    FILE* pipe = _popen("git rev-parse HEAD 2>NUL", "r");
#else
    FILE* pipe = popen("git rev-parse HEAD 2>/dev/null", "r");
#endif
    if (pipe == nullptr)
        return {};
    char buf[64] = {};
    std::string commit;
    if (std::fgets(buf, sizeof(buf), pipe) != nullptr)
    {
        commit = buf;
        while (!commit.empty() && (commit.back() == '\n' || commit.back() == '\r'))
            commit.pop_back();
    }
#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return commit;
}

} // namespace

int main(int argc, char** argv)
{
    std::string presetPath, suitePath, outDir, gitCommitFlag;
    uint32_t baseSeed = defaultRenderSeed;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto nextValue = [&]() -> std::string {
            if (i + 1 >= argc)
            {
                std::cerr << "missing value for " << arg << "\n";
                printUsage();
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--preset") presetPath = nextValue();
        else if (arg == "--suite") suitePath = nextValue();
        else if (arg == "--out") outDir = nextValue();
        else if (arg == "--seed") baseSeed = static_cast<uint32_t>(std::stoul(nextValue()));
        else if (arg == "--git-commit") gitCommitFlag = nextValue();
        else if (arg == "--help" || arg == "-h") { printUsage(); return 0; }
        else
        {
            std::cerr << "unknown argument: " << arg << "\n";
            printUsage();
            return 2;
        }
    }

    if (presetPath.empty() || suitePath.empty() || outDir.empty())
    {
        printUsage();
        return 2;
    }

    try
    {
        const minisax::Preset preset = minisax::loadPreset(presetPath);
        const minisax::Suite suite = minisax::loadSuite(suitePath);
        fs::create_directories(outDir);

        const std::string gitCommit = resolveGitCommit(gitCommitFlag);
        const std::string paramsHash = minisax::parametersHashHex(preset.parameters);

        json report;
        report["engine"] = {
            {"name", "MiniSax"},
            {"version", engineVersion},
            {"gitCommit", gitCommit.empty() ? json(nullptr) : json(gitCommit)},
        };
        report["preset"] = {
            {"path", presetPath},
            {"id", preset.id},
            {"modelName", preset.modelName},
            {"modelVersion", preset.modelVersion},
            {"derivedFrom", preset.derivedFrom.empty() ? json(nullptr) : json(preset.derivedFrom)},
            {"parametersHash", paramsHash},
            {"parameters", preset.raw["parameters"]},
        };
        report["suite"] = {
            {"path", suitePath},
            {"id", suite.id},
            {"sampleRate", suite.sampleRate},
        };
        report["baseSeed"] = baseSeed;
        report["tests"] = json::array();

        int rendered = 0;
        for (const minisax::SuiteTest& test : suite.tests)
        {
            const uint32_t seed = minisax::testSeed(baseSeed, test.id);
            const minisax::RenderResult r =
                minisax::Renderer::renderTest(preset, test, suite.sampleRate, seed);

            const fs::path wavPath = fs::path(outDir) / (test.id + ".wav");
            minisax::writeWav16(wavPath.string(), r.samples, r.sampleRate);

            report["tests"].push_back({
                {"testId", test.id},
                {"description", test.description},
                {"file", wavPath.filename().string()},
                {"durationSeconds", test.durationSeconds},
                {"noiseSeed", seed},
                {"peak", r.peak},
                {"clipped", r.clipped},
                {"silent", r.silent},
                {"hadNonFinite", r.hadNonFinite},
                {"warnings", r.warnings},
            });

            std::cout << "rendered " << wavPath.string()
                      << "  peak=" << r.peak
                      << (r.warnings.empty() ? "" : "  WARNINGS") << "\n";
            for (const std::string& w : r.warnings)
                std::cout << "  warning: " << w << "\n";
            ++rendered;
        }

        const fs::path reportPath = fs::path(outDir) / "render_report.json";
        std::ofstream(reportPath) << report.dump(2) << "\n";
        std::cout << rendered << " test(s) rendered; report: " << reportPath.string() << "\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
