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
//     -> conical shaper (x + a * x * boreTap)
//     -> radiation lowpass -> DC blocker -> body formant peak
//     -> bell high-shelf -> output gain
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
    // Control smoothing time constants.  Slower than v0.2: step breath
    // events and note gates were audibly thumpy; the suite's breath
    // envelopes still shape hard-vs-soft attacks on top of these floors.
    static constexpr float breathSmoothingHz = 8.0f;   // ~20 ms
    static constexpr float gateAttackHz = 15.0f;       // ~11 ms
    static constexpr float gateReleaseHz = 5.0f;       // ~32 ms
    // Safety clamp on the waveguide state; generous, only guards blowups.
    static constexpr float loopSafetyLimit = 3.0f;
    static constexpr float minPitchHz = 50.0f; // half-period delay: 50 Hz needs sr/100 samples
    // Conical waveshaper + output voicing (tuned against the Silverwood
    // reference profile; see the v0.2 experiment log).  MINISAX_TUNING
    // builds make these mutable so offline probes can sweep them; release
    // and test builds get compile-time constants.
#ifdef MINISAX_TUNING
    static inline float conicalShapeMax = 2.0f;
    static inline float conicalTapRatio = 0.30f;
    static inline float radiationLowpassHz = 4000.0f;
    static inline float bodyFormantHz = 1400.0f;
    static inline float bodyFormantDb = 10.0f;
    static inline float bodyFormantQ = 1.1f;
#else
    // Ring-mod blend coefficient at conicalAmount = 1.
    static constexpr float conicalShapeMax = 2.0f;
    // Shaper tap position as a fraction of the (half-period) bore delay.
    // The product wave is a 2*f0 rectangle whose duty = 2*tap; its harmonic
    // m carries sin(pi*m*duty)/m, so the tap sets the even-harmonic mix.
    // 0.30 (vs the naive 0.25) trades a little H4 for a better plateau fit.
    static constexpr float conicalTapRatio = 0.30f;
    static constexpr float radiationLowpassHz = 4000.0f;
    // Fixed body formant lifting the upper-mid plateau (1-2.4 kHz) that the
    // loop's 1/k rolloff otherwise leaves several dB below the Silverwood
    // reference.  Jointly grid-searched with the tap ratio (v0.3).
    static constexpr float bodyFormantHz = 1400.0f;
    static constexpr float bodyFormantDb = 10.0f;
    static constexpr float bodyFormantQ = 1.1f;
#endif
    // Output makeup keeping outputGain = 1.0 just under full scale
    // (worst measured peak 1.76 at outputScale 0.22 with the v0.3 body
    // formant, across breath, conicalAmount, and pitch extremes).
    static constexpr float outputScale = 0.12f;

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
    Biquad bodyFormant;
    ReedNonlinearity reed;
    NoiseGenerator noise;

    float vibratoPhase = 0.0f;
    float growlPhase = 0.0f;
    float lastBellBrightness = -1.0f;
    bool nonFiniteFlag = false;
};

} // namespace minisax
