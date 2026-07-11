#pragma once
#include <cstdint>

#include "Filters.h"
#include "FractionalDelay.h"
#include "NoiseGenerator.h"
#include "ReedNonlinearity.h"

namespace minisax
{

// The normalized instrument parameters from the preset schema.
// All are 0..1 except outputGain (linear gain).
struct Parameters
{
    float breath = 0.5f;
    float embouchure = 0.5f;
    float reedStiffness = 0.5f;
    float reedAperture = 0.5f;
    float boreDamping = 0.3f;
    float bellBrightness = 0.6f;
    float noiseAmount = 0.1f;
    float growlAmount = 0.0f;
    float vibratoAirAmount = 0.0f;
    float vibratoPitchAmount = 0.0f;
    float outputGain = 0.8f;
    // v0.2: conical-bore character.  0 = pure quarter-wave loop (odd
    // harmonics only, clarinet-ish); 1 = full even-harmonic series via the
    // post-loop asymmetric waveshaper.  Optional in presets; the default was
    // grid-searched against the Silverwood reference harmonic profile.
    float conicalAmount = 0.55f;
};

// Per-sample control input to the voice.  The renderer resolves preset
// values + articulation-suite envelopes into this struct; the voice is pure
// DSP with no knowledge of events or JSON.
struct VoiceInputs
{
    Parameters params;      // possibly modulated per sample by the suite
    float pitchHz = 261.63f;
    float gate = 0.0f;      // 0..1, note on/off with renderer-side semantics
};

// Minimal reed/waveguide voice.  v0.2 = the stable v0.1 quarter-wave loop
// plus a post-loop "conical" shaper (self ring-mod against a quarter-period
// bore tap) that supplies the even harmonics of a sax-like conical bore,
// and a radiation lowpass — tuned against the Silverwood reference profile.
//
//   breath pressure (+ noise, growl, vibrato air)
//     -> reed reflection table
//     -> mouthpiece junction
//     -> fractional-delay bore (half period)  <-- feedback
//     -> loss one-pole
//     -> conical shaper (x + a * x * boreTap(T/8))
//     -> radiation lowpass -> DC blocker -> bell high-shelf -> output gain
//
// Deterministic: same seed + same input sequence => identical output.
class MiniSaxVoice
{
public:
    void prepare(double sampleRate, uint32_t noiseSeed);
    void reset(uint32_t noiseSeed);

    float processSample(const VoiceInputs& in);

    // Diagnostics for the render report.
    bool hadNonFinite() const { return nonFiniteFlag; }

    // ── Named model constants ─────────────────────────────────────────────
    // Loop reflection: near STK Clarinet's -0.95; damping reduces it further.
    static constexpr float reflectionBase = 0.97f;
    static constexpr float reflectionDampingDepth = 0.10f;
    // Bore loss lowpass cutoff sweeps dark..bright with (1 - boreDamping).
    static constexpr float lossCutoffMinHz = 700.0f;
    static constexpr float lossCutoffMaxHz = 9000.0f;
    // Breath -> mouth pressure: compressive power map.  A linear map cannot
    // span the reed's onset-to-choke pressure window (ratio only ~1.7, see
    // ReedNonlinearity.h); pressure = max * breath^exponent puts the speaking
    // threshold near breath 0.25 and full breath just under the choke point
    // at default reed/damping settings.
    static constexpr float breathPressureMax = 1.30f;
    static constexpr float breathCurveExponent = 0.38f;
    // Breath noise depth at noiseAmount = 1 (multiplicative, like Saxofony).
    static constexpr float noiseDepth = 0.40f;
    // Growl: sub-audio pressure modulation, ~31 Hz like a sung flutter.
    static constexpr float growlRateHz = 31.0f;
    static constexpr float growlDepth = 0.60f;
    // Vibrato: 5.2 Hz; air modulates pressure, pitch modulates delay length.
    static constexpr float vibratoRateHz = 5.2f;
    static constexpr float vibratoAirDepth = 0.25f;
    static constexpr float vibratoPitchDepthSemitones = 0.35f; // ±35 cents at full amount
    // Embouchure trims effective bore length by up to ±1%.
    static constexpr float embouchurePitchDepth = 0.01f;
    // Delay-length compensation for loop filter group delay (samples).
    static constexpr float loopDelayCompensation = 1.5f;
    // Bell high shelf: corner and gain range mapped from bellBrightness.
    static constexpr float bellShelfHz = 1800.0f;
    static constexpr float bellGainMinDb = -15.0f;
    static constexpr float bellGainMaxDb = 6.0f;
    // Control smoothing time constants.
    static constexpr float breathSmoothingHz = 20.0f;  // ~8 ms
    static constexpr float gateAttackHz = 60.0f;       // ~2.6 ms
    static constexpr float gateReleaseHz = 8.0f;       // ~20 ms
    // Safety clamp on the waveguide state; generous, only guards blowups.
    static constexpr float loopSafetyLimit = 3.0f;
    static constexpr float minPitchHz = 50.0f; // half-period delay: 50 Hz needs sr/100 samples
    // Conical waveshaper + output voicing (tuned against the Silverwood
    // reference profile; see the v0.2 experiment log).  MINISAX_TUNING
    // builds make these mutable so offline probes can sweep them; release
    // and test builds get compile-time constants.
#ifdef MINISAX_TUNING
    static inline float conicalShapeMax = 2.0f;
    static inline float conicalTapRatio = 0.25f;
    static inline float radiationLowpassHz = 4000.0f;
#else
    // Ring-mod blend coefficient at conicalAmount = 1.
    static constexpr float conicalShapeMax = 2.0f;
    // Shaper tap position as a fraction of the (half-period) bore delay:
    // 0.25 = T/8, which for a square-ish loop wave gives a 25%-duty 2*f0
    // rectangular product carrying H2, H4 and H6.
    static constexpr float conicalTapRatio = 0.25f;
    static constexpr float radiationLowpassHz = 4000.0f;
#endif
    // Output makeup keeping outputGain = 1.0 just under full scale
    // (worst measured peak 1.35 at outputScale 0.30, across breath,
    // conicalAmount, and pitch extremes => 0.22 leaves ~1 dB headroom).
    static constexpr float outputScale = 0.22f;

private:
    void updateBellFilter(float brightness);

    double sr = 48000.0;
    FractionalDelay bore;
    OnePole lossFilter;
    OnePole radiationFilter;
    OnePole breathSmoother;
    OnePole gateSmoother;
    DCBlocker dcBlocker;
    Biquad bellFilter;
    ReedNonlinearity reed;
    NoiseGenerator noise;

    float vibratoPhase = 0.0f;
    float growlPhase = 0.0f;
    float lastBellBrightness = -1.0f;
    bool nonFiniteFlag = false;
};

} // namespace minisax
