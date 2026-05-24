#include "ModMatrix.h"
#include <algorithm>
#include <cmath>

// Apply per-route curve shaping to the slewed value.
// For bipolar sources the sign is preserved so curves are symmetric.
static float applyCurve(float x, ModRoute::CurveShape curve)
{
    switch (curve) {
        case ModRoute::CurveShape::Linear:
            return x;
        case ModRoute::CurveShape::Exponential:
            // x * |x| = sign(x) * x²: more resolution at low values, steeper at high.
            return x * std::abs(x);
        case ModRoute::CurveShape::SCurve: {
            // Smoothstep on |x|, sign preserved: ease-in at bottom, ease-out at top.
            float a = std::abs(x);
            a = a * a * (3.0f - 2.0f * a);
            return std::copysign(a, x);
        }
    }
    return x;
}

void ModMatrix::prepare(double sr, int bs)
{
    sampleRate = sr;
    blockSize  = bs;
    for (auto& r : routes)
        r.slewer.prepare(sr, bs);
}

void ModMatrix::setCCValue(int ccNumber, float value)
{
    if (ccNumber >= 0 && ccNumber < 128)
        ccValues[ccNumber] = std::clamp(value, 0.0f, 1.0f);
}

float ModMatrix::getCCValue(int ccNumber) const
{
    if (ccNumber >= 0 && ccNumber < 128)
        return ccValues[ccNumber];
    return 0.0f;
}

void ModMatrix::setMacroSlot(int macroIdx, float value, int voiceBacking)
{
    if (macroIdx >= 0 && macroIdx < ModSourceID::NumMacros) {
        macroValues[macroIdx]      = value;
        macroVoiceBacking[macroIdx] = voiceBacking;
    }
}

float ModMatrix::getMacroValue(int macroIdx) const
{
    if (macroIdx >= 0 && macroIdx < ModSourceID::NumMacros)
        return macroValues[macroIdx];
    return 0.0f;
}

float ModMatrix::getSourceValue(int sourceID,
    const std::array<float, ModSourceID::NumVoiceSources>& voiceVals) const
{
    // Per-voice MPE dimensions
    if (sourceID >= 0 && sourceID < ModSourceID::NumVoiceSources)
        return voiceVals[sourceID];

    // Raw CC (legacy routes and direct CC routing)
    int ccNum = sourceID - ModSourceID::CC;
    if (ccNum >= 0 && ccNum < 128)
        return ccValues[ccNum];

    // Abstract macro slots — resolve to voice dim or pre-computed CC/AT value
    int macroIdx = sourceID - ModSourceID::Macro;
    if (macroIdx >= 0 && macroIdx < ModSourceID::NumMacros) {
        int backing = macroVoiceBacking[macroIdx];
        if (backing >= 0 && backing < ModSourceID::NumVoiceSources)
            return voiceVals[backing];   // per-voice resolution
        return macroValues[macroIdx];    // global resolution (CC/Aftertouch)
    }

    return 0.0f;
}

std::array<float, ModDestID::NumDests>
ModMatrix::evaluate(const std::array<float, ModSourceID::NumVoiceSources>& voiceVals,
                    std::vector<Slewer>& voiceSlewers)
{
    // Zero-initialised: routes accumulate into each destination additively.
    std::array<float, ModDestID::NumDests> result {};

    for (size_t i = 0; i < routes.size(); ++i) {
        auto& route = routes[i];
        float raw   = getSourceValue(route.source, voiceVals);

        // Decide which slewer pool to use for this route.
        //
        // Voice sources (MPE pressure/slide/pitchbend, velocity) must be slewed
        // per-voice so that two simultaneously held notes with different slide
        // positions don't contaminate each other through the shared route slewer.
        // CC/Aftertouch sources are global so the shared route.slewer is correct.
        //
        // Macro sources add a wrinkle: their backing can change at runtime.
        // If MacroBreath is currently wired to MPE_Pressure (voiceBacking >= 0),
        // two notes with different breath inputs would bleed through the shared
        // slewer even though MacroBreath is nominally CC-backed.  isMacroVoice
        // catches this case so the per-voice slewer is used whenever a macro
        // resolves to a per-voice dimension, regardless of its default backing.
        int  macroIdx     = route.source - ModSourceID::Macro;
        bool isMacroVoice = (macroIdx >= 0 && macroIdx < ModSourceID::NumMacros
                             && macroVoiceBacking[macroIdx] >= 0);
        bool isVoiceSource = (route.source >= 0
                              && route.source < ModSourceID::NumVoiceSources)
                          || isMacroVoice;
        float slewed = isVoiceSource
                       ? (i < voiceSlewers.size() ? voiceSlewers[i].process(raw) : raw)
                       : route.slewer.process(raw);

        float shaped       = applyCurve(slewed, route.curve);
        float eff_amount   = route.amountParam ? route.amountParam->load() : route.amount;
        float contribution = shaped * eff_amount;

        // Clamp after each route so no destination escapes ±1 mid-accumulation.
        // Multiple routes pushing the same destination are additive but bounded:
        // two routes each contributing +0.8 → +1.0, not +1.6.
        // This is intentional — destinations are normalised offsets, not voltages.
        result[route.dest] = std::clamp(
            result[route.dest] + contribution, -1.0f, 1.0f);
    }

    return result;
}

void ModMatrix::initVoiceSlewers(std::vector<Slewer>& out, double sr, int blockSize) const
{
    out.resize(routes.size());
    for (size_t i = 0; i < routes.size(); ++i) {
        out[i].prepare(sr, blockSize);
        out[i].setRates(routes[i].attackMs, routes[i].releaseMs);
    }
}

void ModMatrix::resetVoiceSlewers(std::vector<Slewer>& voiceSlewers,
                                   const std::array<float, ModSourceID::NumVoiceSources>& voiceVals) const
{
    // Only reset per-voice source slewers — CC/Aftertouch slewers live on the
    // shared ModRoute and must NOT be touched here.  Resetting a CC slewer from
    // noteStarted() would snap the global CC state to the new voice's value,
    // producing a brief discontinuity audible across all simultaneous notes.
    for (size_t i = 0; i < routes.size() && i < voiceSlewers.size(); ++i) {
        int src = routes[i].source;

        // Direct voice source (MPE dims, velocity)
        if (src >= 0 && src < ModSourceID::NumVoiceSources) {
            voiceSlewers[i].reset(voiceVals[static_cast<size_t>(src)]);
            continue;
        }

        // Macro source whose current binding is a per-voice dimension:
        // e.g. MacroBreath currently routed to MPE_Pressure rather than CC.
        // CC-backed macros fall through and leave their slewer untouched.
        int macroIdx = src - ModSourceID::Macro;
        if (macroIdx >= 0 && macroIdx < ModSourceID::NumMacros) {
            int backing = macroVoiceBacking[macroIdx];
            if (backing >= 0 && backing < ModSourceID::NumVoiceSources)
                voiceSlewers[i].reset(voiceVals[static_cast<size_t>(backing)]);
        }
    }
}

void ModMatrix::addRoute(int source, int dest, float amount,
                          float attackMs, float releaseMs, ModRoute::CurveShape curve,
                          std::atomic<float>* amountParam)
{
    // Called stopped-only — routes.push_back may reallocate the vector,
    // which is a data race if any voice is mid-render.  See ModMatrix.h thread model.
    ModRoute r;
    r.source      = source;
    r.dest        = dest;
    r.amount      = amount;
    r.curve       = curve;
    r.attackMs    = attackMs;
    r.releaseMs   = releaseMs;
    r.amountParam = amountParam;
    r.slewer.prepare(sampleRate, blockSize);
    r.slewer.setRates(attackMs, releaseMs);
    routes.push_back(std::move(r));
}

void ModMatrix::clearRoutes()
{
    routes.clear();
}
