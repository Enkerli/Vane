#include "Renderer.h"

#include <cmath>
#include <map>
#include <stdexcept>

#include "Envelope.h"
#include "MiniSaxVoice.h"

namespace minisax
{
namespace
{
    double midiToHz(double note)
    {
        return 440.0 * std::exp2((note - 69.0) / 12.0);
    }

    double eventFrequency(const SuiteEvent& ev, double fallbackHz)
    {
        if (ev.pitchHz > 0.0)
            return ev.pitchHz;
        if (ev.note >= 0.0)
            return midiToHz(ev.note);
        return fallbackHz;
    }

    float* parameterSlot(Parameters& p, const std::string& name)
    {
        if (name == "breath") return &p.breath;
        if (name == "embouchure") return &p.embouchure;
        if (name == "reedStiffness") return &p.reedStiffness;
        if (name == "reedAperture") return &p.reedAperture;
        if (name == "boreDamping") return &p.boreDamping;
        if (name == "bellBrightness") return &p.bellBrightness;
        if (name == "noiseAmount") return &p.noiseAmount;
        if (name == "growlAmount") return &p.growlAmount;
        if (name == "vibratoAirAmount") return &p.vibratoAirAmount;
        if (name == "vibratoPitchAmount") return &p.vibratoPitchAmount;
        if (name == "outputGain") return &p.outputGain;
        return nullptr;
    }
} // namespace

uint32_t testSeed(uint32_t baseSeed, const std::string& testId)
{
    // FNV-1a 32-bit over the test id, mixed with the base seed.
    uint32_t hash = 2166136261u ^ baseSeed;
    for (const char c : testId)
    {
        hash ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
        hash *= 16777619u;
    }
    return hash != 0 ? hash : 1u; // xorshift seed must be nonzero
}

RenderResult Renderer::renderTest(const Preset& preset, const SuiteTest& test,
                                  int sampleRate, uint32_t noiseSeed)
{
    // ── Resolve events into envelopes ─────────────────────────────────────
    Envelope gate(0.0f);
    Envelope pitch(0.0f); // Hz; 0 until the first noteOn sets it
    std::map<std::string, Envelope> controls;
    double lastPitchHz = 0.0;

    for (const SuiteEvent& ev : test.events)
    {
        if (ev.type == "noteOn")
        {
            const double hz = eventFrequency(ev, midiToHz(60.0));
            gate.add(ev.time, 1.0f, false);
            pitch.add(ev.time, static_cast<float>(hz), false);
            lastPitchHz = hz;
        }
        else if (ev.type == "noteOff")
        {
            gate.add(ev.time, 0.0f, false);
        }
        else if (ev.type == "pitch")
        {
            const double hz = eventFrequency(ev, lastPitchHz);
            if (ev.curve == "linear")
            {
                const double glide = ev.glideSeconds > 0.0 ? ev.glideSeconds
                                                           : defaultPitchGlideSeconds;
                // Hold the old pitch until the event, then glide to the new one.
                pitch.add(ev.time, static_cast<float>(lastPitchHz), false);
                pitch.add(ev.time + glide, static_cast<float>(hz), true);
            }
            else
            {
                pitch.add(ev.time, static_cast<float>(hz), false);
            }
            lastPitchHz = hz;
        }
        else if (ev.type == "control")
        {
            Parameters probe = preset.parameters;
            const float* slot = parameterSlot(probe, ev.target);
            if (slot == nullptr)
                throw std::runtime_error("test \"" + test.id + "\": control event targets unknown parameter \""
                                         + ev.target + "\"");
            auto [it, inserted] = controls.try_emplace(ev.target, Envelope(*slot));
            it->second.add(ev.time, static_cast<float>(ev.value), ev.curve == "linear");
        }
        else
        {
            throw std::runtime_error("test \"" + test.id + "\": unknown event type \"" + ev.type + "\"");
        }
    }

    gate.finalize();
    pitch.finalize();
    for (auto& [name, env] : controls)
        env.finalize();

    // Pre-resolve control targets to struct member pointers so the render
    // loop does no string lookups.
    Parameters params = preset.parameters;
    std::vector<std::pair<float*, Envelope*>> controlSlots;
    controlSlots.reserve(controls.size());
    for (auto& [name, env] : controls)
        controlSlots.emplace_back(parameterSlot(params, name), &env);

    // ── Render ────────────────────────────────────────────────────────────
    MiniSaxVoice voice;
    voice.prepare(sampleRate, noiseSeed);

    const auto numSamples = static_cast<size_t>(test.durationSeconds * sampleRate);
    RenderResult result;
    result.sampleRate = sampleRate;
    result.noiseSeed = noiseSeed;
    result.samples.resize(numSamples);

    VoiceInputs in;
    const double dt = 1.0 / sampleRate;
    for (size_t n = 0; n < numSamples; ++n)
    {
        const double t = static_cast<double>(n) * dt;
        for (auto& [slot, env] : controlSlots)
            *slot = env->value(t);
        in.params = params;
        in.gate = gate.value(t);
        const float pitchHz = pitch.value(t);
        in.pitchHz = pitchHz > 0.0f ? pitchHz : MiniSaxVoice::minPitchHz;

        const float s = voice.processSample(in);
        result.samples[n] = s;
        result.peak = std::max(result.peak, std::abs(s));
    }

    result.hadNonFinite = voice.hadNonFinite();
    result.clipped = result.peak >= 0.999f;
    result.silent = result.peak < silenceFloor;
    if (result.hadNonFinite)
        result.warnings.emplace_back("non_finite_samples_flushed");
    if (result.clipped)
        result.warnings.emplace_back("clipping_or_near_clipping");
    if (result.silent)
        result.warnings.emplace_back("silent_or_nearly_silent");

    return result;
}

} // namespace minisax
