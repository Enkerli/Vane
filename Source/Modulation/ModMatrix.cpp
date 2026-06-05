#include "ModMatrix.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

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

// Apply an editable response curve via its LUT.  Bipolar: index by |x|, restore
// sign (odd symmetry through the origin), matching the unipolar curve editor.
static float applyCurveLUT(float x, const std::array<float, ModRoute::kCurveLUT>& lut)
{
    float u = std::abs(x);
    if (u > 1.0f) u = 1.0f;
    const float f  = u * static_cast<float>(ModRoute::kCurveLUT - 1);
    int         i0 = static_cast<int>(f);
    if (i0 >= ModRoute::kCurveLUT - 1)
        return std::copysign(lut[ModRoute::kCurveLUT - 1], x);
    const float fr = f - static_cast<float>(i0);
    const float y  = lut[(size_t) i0] + fr * (lut[(size_t)(i0 + 1)] - lut[(size_t) i0]);
    return std::copysign(y, x);
}

// Fritsch-Carlson monotone tangents for a set of (x,y) knots.
static void monotoneTangents(const std::vector<float>& xs, const std::vector<float>& ys,
                             std::vector<float>& m)
{
    const int n = static_cast<int>(xs.size());
    std::vector<float> d(static_cast<size_t>(n - 1));
    for (int i = 0; i < n - 1; ++i) d[(size_t) i] = (ys[(size_t)(i+1)] - ys[(size_t) i]) / (xs[(size_t)(i+1)] - xs[(size_t) i]);
    m.assign((size_t) n, 0.0f);
    m[0] = d[0]; m[(size_t)(n-1)] = d[(size_t)(n-2)];
    for (int i = 1; i < n - 1; ++i)
        m[(size_t) i] = (d[(size_t)(i-1)] * d[(size_t) i] <= 0.0f) ? 0.0f
                                                                   : (d[(size_t)(i-1)] + d[(size_t) i]) * 0.5f;
    for (int i = 0; i < n - 1; ++i) {
        if (d[(size_t) i] == 0.0f) { m[(size_t) i] = 0.0f; m[(size_t)(i+1)] = 0.0f; continue; }
        const float a = m[(size_t) i] / d[(size_t) i], b = m[(size_t)(i+1)] / d[(size_t) i];
        const float h = std::hypot(a, b);
        if (h > 3.0f) { const float t = 3.0f / h; m[(size_t) i] = t*a*d[(size_t) i]; m[(size_t)(i+1)] = t*b*d[(size_t) i]; }
    }
}

void ModMatrix::buildCurveLUT(const std::vector<std::pair<float, float>>& anchors,
                              std::array<float, ModRoute::kCurveLUT>& out)
{
    std::vector<std::pair<float, float>> a;
    for (const auto& p : anchors)
        a.emplace_back(std::clamp(p.first, 0.012f, 0.988f), std::clamp(p.second, 0.0f, 1.0f));
    if (a.empty()) a.emplace_back(0.5f, 0.5f);
    std::sort(a.begin(), a.end(), [](const auto& p, const auto& q){ return p.first < q.first; });

    std::vector<float> xs, ys;
    xs.push_back(0.0f); ys.push_back(0.0f);
    for (const auto& p : a) { xs.push_back(p.first); ys.push_back(p.second); }
    xs.push_back(1.0f); ys.push_back(1.0f);

    std::vector<float> m; monotoneTangents(xs, ys, m);
    const int n = static_cast<int>(xs.size());
    for (int k = 0; k < ModRoute::kCurveLUT; ++k) {
        const float x = static_cast<float>(k) / static_cast<float>(ModRoute::kCurveLUT - 1);
        int i = 0; while (i < n - 2 && x > xs[(size_t)(i+1)]) ++i;
        const float x0 = xs[(size_t) i], x1 = xs[(size_t)(i+1)], y0 = ys[(size_t) i], y1 = ys[(size_t)(i+1)];
        const float h = x1 - x0;
        float y;
        if (h <= 0.0f) y = y0;
        else {
            const float t = (x - x0) / h, t2 = t*t, t3 = t2*t;
            y = (2*t3 - 3*t2 + 1)*y0 + (t3 - 2*t2 + t)*h*m[(size_t) i]
              + (-2*t3 + 3*t2)*y1 + (t3 - t2)*h*m[(size_t)(i+1)];
        }
        out[(size_t) k] = std::clamp(y, 0.0f, 1.0f);
    }
}

void ModMatrix::setRouteCurve(int routeIndex, const std::vector<std::pair<float, float>>& anchors)
{
    if (routeIndex < 0 || routeIndex >= static_cast<int>(routes.size())) return;
    if (anchors.empty()) { routes[(size_t) routeIndex].curveLUTactive = false; return; }
    buildCurveLUT(anchors, routes[(size_t) routeIndex].curveLUT);
    routes[(size_t) routeIndex].curveLUTactive = true;
}

// Effective destination of a route (slot-aware).  -1 = invalid.
static int effDest(const ModRoute& r) {
    if (r.destParam)
        return ModSlots::destId(static_cast<int>(std::lround(r.destParam->load())));
    return r.dest;
}

// Effective SOURCE of a route.  A configurable slot reads its live source choice;
// choices 7.. are aux global sources resolved to their bound CC.  -1 = Off/invalid.
// Resolve a slot source-choice index to a ModSourceID (aux → live CC binding).
int ModMatrix::resolveChoice(int choice) const {
    if (choice >= ModSlots::FirstAuxChoice && choice < ModSlots::FirstAuxChoice + ModSlots::NumAux)
        return ModSourceID::CC + auxCC[static_cast<size_t>(choice - ModSlots::FirstAuxChoice)];
    return ModSlots::sourceId(choice);
}

int ModMatrix::effSource(const ModRoute& r) const {
    // Per-slot enable: a disabled slot is inactive regardless of its source.
    if (r.enableParam && r.enableParam->load() < 0.5f) return -1;
    if (! r.sourceParam) return r.source;   // fixed route
    return resolveChoice(static_cast<int>(std::lround(r.sourceParam->load())));
}

// Sources that range −1..+1 (vs 0..1) — used to normalise a scale source to 0..1.
bool ModMatrix::isBipolarSource(int sourceID) {
    return sourceID == ModSourceID::MPE_Slide
        || sourceID == ModSourceID::MPE_Pitchbend
        || sourceID == ModSourceID::Keytrack
        || sourceID == ModSourceID::MacroSlide
        || sourceID == ModSourceID::MacroPitchbend;
}

void ModMatrix::setAuxCC(int auxIdx, int ccNumber) {
    if (auxIdx >= 0 && auxIdx < ModSlots::NumAux)
        auxCC[static_cast<size_t>(auxIdx)] = std::clamp(ccNumber, 0, 127);
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

        // Live source/dest (slot-aware).  Skip inactive slots (source Off) and
        // any route whose mapping is out of range.
        const int src  = effSource(route);
        const int dest = effDest(route);
        if (src < 0 || dest < 0)
            continue;

        // Configurable slots derive their slew rate from the live source and push
        // it to whichever slewer they will use this block (cheap: 2 exp/block).
        if (route.sourceParam) {
            float atk, rel;
            ModSlots::slewRates(static_cast<int>(std::lround(route.sourceParam->load())),
                                atk, rel);
            route.slewer.setRates(atk, rel);
            if (i < voiceSlewers.size())
                voiceSlewers[i].setRates(atk, rel);
        }

        float raw = getSourceValue(src, voiceVals);

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
        int  macroIdx     = src - ModSourceID::Macro;
        bool isMacroVoice = (macroIdx >= 0 && macroIdx < ModSourceID::NumMacros
                             && macroVoiceBacking[macroIdx] >= 0);
        bool isVoiceSource = (src >= 0 && src < ModSourceID::NumVoiceSources)
                          || isMacroVoice;
        float slewed = isVoiceSource
                       ? (i < voiceSlewers.size() ? voiceSlewers[i].process(raw) : raw)
                       : route.slewer.process(raw);

        // Use live curve param if wired, otherwise fall back to the static enum.
        ModRoute::CurveShape effectiveCurve = route.curve;
        if (route.curveParam) {
            int ci = static_cast<int>(std::round(route.curveParam->load()));
            if (ci >= 0 && ci <= 2)
                effectiveCurve = static_cast<ModRoute::CurveShape>(ci);
        }
        // Editable response curve (LUT) takes precedence over the discrete enum.
        float shaped       = route.curveLUTactive ? applyCurveLUT(slewed, route.curveLUT)
                                                   : applyCurve(slewed, effectiveCurve);
        float eff_amount   = route.amountParam ? route.amountParam->load() : route.amount;

        // Mod-of-mod: scale this route's amount by another source's value.
        if (route.scaleParam) {
            const int scaleChoice = static_cast<int>(std::lround(route.scaleParam->load()));
            if (scaleChoice > 0) {   // 0 = Off
                const int   scSrc = resolveChoice(scaleChoice);
                const float sv    = getSourceValue(scSrc, voiceVals);
                float factor      = isBipolarSource(scSrc) ? 0.5f * (sv + 1.0f) : sv;
                eff_amount *= std::clamp(factor, 0.0f, 1.0f);
            }
        }

        float contribution = shaped * eff_amount;

        // Clamp after each route so no destination escapes ±1 mid-accumulation.
        // Multiple routes pushing the same destination are additive but bounded:
        // two routes each contributing +0.8 → +1.0, not +1.6.
        // This is intentional — destinations are normalised offsets, not voltages.
        result[dest] = std::clamp(result[dest] + contribution, -1.0f, 1.0f);
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
        int src = effSource(routes[i]);   // slot-aware
        if (src < 0) continue;

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
                          std::atomic<float>* amountParam, std::atomic<float>* curveParam)
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
    r.curveParam  = curveParam;
    r.slewer.prepare(sampleRate, blockSize);
    r.slewer.setRates(attackMs, releaseMs);
    routes.push_back(std::move(r));
}

void ModMatrix::addSlot(std::atomic<float>* sourceParam, std::atomic<float>* destParam,
                         std::atomic<float>* amountParam, std::atomic<float>* curveParam,
                         std::atomic<float>* scaleParam, std::atomic<float>* enableParam)
{
    // Called stopped-only — see addRoute / thread model.
    ModRoute r;
    r.sourceParam = sourceParam;
    r.destParam   = destParam;
    r.amountParam = amountParam;
    r.curveParam  = curveParam;
    r.scaleParam  = scaleParam;
    r.enableParam = enableParam;
    // Slew rates are set live per block from the slot's source; init to a sane
    // default so the first block before evaluate() has valid coefficients.
    r.slewer.prepare(sampleRate, blockSize);
    r.slewer.setRates(r.attackMs, r.releaseMs);
    routes.push_back(std::move(r));
}

void ModMatrix::clearRoutes()
{
    routes.clear();
}
