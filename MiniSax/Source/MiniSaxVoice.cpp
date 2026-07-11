#include "MiniSaxVoice.h"

#include <algorithm>
#include <cmath>

namespace minisax
{

void MiniSaxVoice::prepare(double sampleRate, uint32_t noiseSeed)
{
    sr = sampleRate;
    const int maxDelay = static_cast<int>(sr / static_cast<double>(minPitchHz)) + 8;
    bore.prepare(maxDelay);
    breathSmoother.setCutoff(breathSmoothingHz, static_cast<float>(sr));
    reset(noiseSeed);
}

void MiniSaxVoice::reset(uint32_t noiseSeed)
{
    bore.clear();
    lossFilter.reset();
    radiationFilter.reset();
    breathSmoother.reset();
    gateSmoother.reset();
    dcBlocker.reset();
    bellFilter.reset();
    noise.seed(noiseSeed);
    vibratoPhase = 0.0f;
    growlPhase = 0.0f;
    lastBellBrightness = -1.0f;
    nonFiniteFlag = false;
}

void MiniSaxVoice::updateBellFilter(float brightness)
{
    // Recompute coefficients only when brightness actually moves; a per-sample
    // trig recompute would dominate this tiny engine's cost for nothing.
    if (std::abs(brightness - lastBellBrightness) < 1.0e-4f)
        return;
    lastBellBrightness = brightness;
    const float gainDb = bellGainMinDb + (bellGainMaxDb - bellGainMinDb) * brightness;
    bellFilter.setHighShelf(static_cast<float>(sr), bellShelfHz, gainDb);
}

float MiniSaxVoice::processSample(const VoiceInputs& in)
{
    const auto& p = in.params;
    const float srf = static_cast<float>(sr);
    constexpr float twoPi = 2.0f * OnePole::pi;

    // ── Gate + breath smoothing ────────────────────────────────────────────
    gateSmoother.setCutoff(in.gate > gateSmoother.getState() ? gateAttackHz : gateReleaseHz, srf);
    const float gate = gateSmoother.process(std::clamp(in.gate, 0.0f, 1.0f));
    const float breath = breathSmoother.process(std::clamp(p.breath, 0.0f, 1.0f)) * gate;

    // ── Modulators (advance phases every sample so timing is gate-independent)
    vibratoPhase += twoPi * vibratoRateHz / srf;
    if (vibratoPhase > twoPi) vibratoPhase -= twoPi;
    growlPhase += twoPi * growlRateHz / srf;
    if (growlPhase > twoPi) growlPhase -= twoPi;
    const float vibrato = std::sin(vibratoPhase);
    const float growlOsc = std::sin(growlPhase);
    const float noiseSample = noise.nextFloat();

    // ── Mouth pressure: compressive breath map (see header), then air
    // vibrato, growl flutter, and breath noise as multiplicative modulation.
    float mouthPressure = breathPressureMax * std::pow(breath, breathCurveExponent);
    mouthPressure *= 1.0f + vibratoAirDepth * std::clamp(p.vibratoAirAmount, 0.0f, 1.0f) * vibrato;
    mouthPressure *= 1.0f + growlDepth * std::clamp(p.growlAmount, 0.0f, 1.0f) * growlOsc;
    mouthPressure *= 1.0f + noiseDepth * std::clamp(p.noiseAmount, 0.0f, 1.0f) * noiseSample;

    // ── Pitch -> delay length.  Half-period quarter-wave loop, as in v0.1
    // and STK Clarinet.  (A Saxofony-style two-segment full-period bore was
    // tried for v0.2 and produced reference-matching spectra, but it flips
    // to the second register across most of the breath/damping range — see
    // experiments/2026-07-11_sax_oomph.md.  Even harmonics come from the
    // conical waveshaper below instead.)
    const float pitchHz = std::max(in.pitchHz, minPitchHz);
    const float vibratoSemis = vibratoPitchDepthSemitones
                             * std::clamp(p.vibratoPitchAmount, 0.0f, 1.0f) * vibrato;
    const float effectiveHz = pitchHz * std::exp2(vibratoSemis / 12.0f);
    float delaySamples = 0.5f * srf / effectiveHz - loopDelayCompensation;
    delaySamples *= 1.0f + embouchurePitchDepth * (0.5f - std::clamp(p.embouchure, 0.0f, 1.0f)) * 2.0f;
    bore.setDelay(delaySamples);

    // ── Waveguide loop: reed junction feeding the bore delay (v0.1 core) ──
    const float damping = std::clamp(p.boreDamping, 0.0f, 1.0f);
    lossFilter.setCutoff(lossCutoffMaxHz + (lossCutoffMinHz - lossCutoffMaxHz) * damping, srf);
    const float reflection = reflectionBase - reflectionDampingDepth * damping;

    reed.setControls(p.reedAperture, p.reedStiffness, p.embouchure);

    const float boreOut = lossFilter.process(bore.read());
    const float pressureDiff = -reflection * boreOut - mouthPressure;
    float boreIn = mouthPressure + pressureDiff * reed.reflection(pressureDiff);

    // Safety: the reed table bounds loop gain, but bad parameter combinations
    // during exploration must never propagate NaN/Inf into the delay line.
    if (!std::isfinite(boreIn))
    {
        boreIn = 0.0f;
        nonFiniteFlag = true;
    }
    boreIn = std::clamp(boreIn, -loopSafetyLimit, loopSafetyLimit);
    bore.write(boreIn);

    // ── Conical shaper ─────────────────────────────────────────────────────
    // The quarter-wave loop is odd-harmonic only (clarinet-like), and its
    // signal is near-square, so a static waveshaper (x^2 etc.) yields almost
    // no even harmonics (a 50%-duty square squared is DC).  Instead: self
    // ring-modulation with a quarter-period tap of the bore.  For a sine
    // this is pure 2*f0; for a square it is a 2*f0 rectangular wave (duty
    // set by the tap ratio) carrying H2/H4/H6.  `conicalAmount` scales the
    // blend.  Deliberately a synthesis device — playability over realism.
    const float conical = std::clamp(p.conicalAmount, 0.0f, 1.0f) * conicalShapeMax;
    const float octaveWave = bore.readTap(conicalTapRatio * delaySamples);
    const float shaped = boreOut + conical * boreOut * octaveWave;

    // ── Output shaping ─────────────────────────────────────────────────────
    radiationFilter.setCutoff(radiationLowpassHz, srf);
    updateBellFilter(std::clamp(p.bellBrightness, 0.0f, 1.0f));
    float out = bellFilter.process(dcBlocker.process(radiationFilter.process(shaped)))
              * p.outputGain * outputScale;

    if (!std::isfinite(out))
    {
        out = 0.0f;
        nonFiniteFlag = true;
    }
    return out;
}

} // namespace minisax
