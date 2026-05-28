#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Modulation/ModMatrix.h"
#include "MPE/TuningClient.h"
#include "Synth/Oscillator.h"
#include "Synth/SVFilter.h"

class SynthVoice : public juce::MPESynthesiserVoice {
public:
    // The constructor parameter list is long because voices need live access to
    // APVTS raw-parameter pointers (safe on the audio thread) and shared atomics
    // for cross-voice state (legato handoff).  A future refactor should pack these
    // into two structs — VoiceParams (APVTS pointers) and SharedVoiceState
    // (the cross-voice atomics) — to reduce the constructor arity and make it
    // harder to accidentally pass parameters in the wrong order.
    //
    // IMPORTANT: obtain ALL pointers as local variables in VaneProcessor's
    // constructor before calling addVoice().  Member pointers (pOutputLevel etc.)
    // are assigned AFTER the voice loop and will be null if passed directly —
    // voices silently fall back to hard-coded defaults.  See PluginProcessor.cpp.
    SynthVoice(ModMatrix& matrix, TuningClient& tuning,
               std::atomic<float>*    paramMorphPos,  std::atomic<float>*    paramDetune,
               std::atomic<float>*    paramPW,
               std::atomic<float>*    paramCutoff,    std::atomic<float>*    paramRes,
               std::atomic<float>*    paramFilterMode,std::atomic<float>*    paramVelocityMix,
               std::atomic<float>*    paramGlide,     std::atomic<float>*    lastNoteHz,
               std::atomic<float>*    lastVCALevel,
               std::atomic<uint32_t>* legatoGeneration,
               std::atomic<float>*    lastOscPhase,
               std::atomic<float>*    paramMono,
               std::atomic<float>*    lastFilterS1,
               std::atomic<float>*    lastFilterS2,
               std::atomic<float>*    lastCutoffHz,
               // Optional display meters — written lock-free for the editor UI.
               // Null-safe: ignored when not wired.
               std::atomic<float>*    meterPressure  = nullptr,
               std::atomic<float>*    meterSlide     = nullptr,
               std::atomic<float>*    meterPitchbend = nullptr,
               // Pitchbend range for MPE member-channel notes (default 48 st).
               // Stored as an APVTS param so presets carry it.
               std::atomic<float>*    pitchbendRange  = nullptr,
               // Pitchbend range for channel-1 legacy (non-MPE) notes (default 2 st).
               std::atomic<float>*    nonMPEPBRange    = nullptr,
               // Glide mode: 0 = Fixed Time (constant duration regardless of interval),
               //             1 = Fixed Rate (duration ∝ interval; glideTime = ms per semitone).
               std::atomic<float>*    glideMode        = nullptr,
               // Glide curve: 0 = Linear semitones (constant speed, Multiplicative smoother),
               //              1 = Exponential approach (fast start → smooth landing, analog style).
               std::atomic<float>*    glideCurve       = nullptr);

    void prepare(double sampleRate, int blockSize);

    // MPESynthesiserVoice overrides
    void noteStarted()                                              override;
    void noteStopped(bool allowTailOff)                            override;
    void notePressureChanged()                                      override;
    void notePitchbendChanged()                                     override;
    void noteTimbreChanged()                                        override;
    void noteKeyStateChanged()                                      override;
    void renderNextBlock(juce::AudioBuffer<float>&,
                         int startSample, int numSamples)           override;

private:
    ModMatrix&    modMatrix;
    TuningClient& tuning;

    std::atomic<float>* paramMorphPos    = nullptr;   // 0.0-3.0: Sine→Tri→Sqr→Saw
    std::atomic<float>* paramPW          = nullptr;   // 0.0-1.0: phase-distortion PW
    std::atomic<float>* paramDetune      = nullptr;
    std::atomic<float>* paramCutoff      = nullptr;
    std::atomic<float>* paramRes         = nullptr;
    std::atomic<float>* paramFilterMode  = nullptr;
    std::atomic<float>* paramVelocityMix = nullptr;
    std::atomic<float>*    paramGlide         = nullptr;
    std::atomic<float>*    sharedLastNoteHz   = nullptr;
    std::atomic<float>*    sharedLastVCALevel = nullptr;
    std::atomic<uint32_t>* sharedLegatoGen    = nullptr;  // kill-old-voice counter
    std::atomic<float>*    sharedOscPhase     = nullptr;  // oscillator phase handoff
    std::atomic<float>*    paramMono          = nullptr;  // 0 = poly, 1 = mono
    std::atomic<float>*    sharedFilterS1     = nullptr;  // SVF integrator state handoff
    std::atomic<float>*    sharedFilterS2     = nullptr;
    std::atomic<float>*    sharedCutoffHz     = nullptr;  // smoothed cutoff at handoff

    // Editor display meters — last-voice-wins, relaxed ordering sufficient.
    std::atomic<float>*    sharedMeterPressure  = nullptr;
    std::atomic<float>*    sharedMeterSlide     = nullptr;
    std::atomic<float>*    sharedMeterPitchbend = nullptr;

    // Pitchbend range for MPE member-channel notes — read each render block.
    std::atomic<float>*    paramPBRange       = nullptr;
    // Pitchbend range for channel-1 legacy (non-MPE) notes (default 2 st).
    std::atomic<float>*    paramNonMPEPBRange = nullptr;
    // Glide mode / curve — read block-rate in renderNextBlock.
    std::atomic<float>*    paramGlideMode     = nullptr;
    std::atomic<float>*    paramGlideCurve    = nullptr;

    // ── Glide curve state ─────────────────────────────────────────────────────
    //
    // Exponential (glideCurve == 1): 1-pole IIR in log2(Hz) space.
    //   glideExpLogHz += (glideTargetLogHz − glideExpLogHz) × glideExpCoeff
    //   Perceptually even across the keyboard — equal semitone intervals feel
    //   equal regardless of absolute pitch.
    //
    // RC (glideCurve == 2): 1-pole IIR in linear Hz space (true analog RC circuit).
    //   glideRcHz += (targetHz − glideRcHz) × glideRcCoeff
    //   The Hz gap is larger at higher pitches, so the glide feels snappier in the
    //   top register and heavier in the bass — the character of a physical RC filter.
    //
    // Both use the same coefficient formula: c = 1 − exp(−4.6 / N) so 99 % of
    // the interval is covered within effGlideMs (N = effGlideMs × sr / 1000).
    float glideExpLogHz    = 0.0f;   // current log2(Hz), stepped each sample (Exp mode)
    float glideTargetLogHz = 0.0f;   // log2(target Hz), shared by Exp and RC for init
    float glideExpCoeff    = 0.0f;   // 1-pole coeff for Exp mode
    float glideRcHz        = 0.0f;   // current Hz, stepped each sample (RC mode)
    float glideRcCoeff     = 0.0f;   // 1-pole coeff for RC mode

    uint32_t myLegatoGen = 0;  // generation this voice was born into

    // Per-voice slewers for voice-source routes — one entry per ModMatrix route,
    // mirroring each route's attack/release config.  Initialised in prepare() via
    // modMatrix.initVoiceSlewers().  Passed to modMatrix.evaluate() so that MPE
    // dimensions (pressure, slide) are tracked independently per voice and never
    // cross-contaminate simultaneous notes through the shared route slewers.
    std::vector<Slewer> voiceSlewers;

    Oscillator osc;
    SVFilter   filter;
    double sampleRate = 44100.0;

    // Per-sample cutoff interpolation — eliminates block-boundary coefficient steps
    // that cause audible crunchiness under breath/mod-driven filter sweeps.
    // Multiplicative (geometric) ramping matches logarithmic frequency perception.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedCutoff;

    // Portamento — glides the oscillator pitch between notes when glideTime > 0.
    // Also Multiplicative so semitone spacing stays perceptually even over the ramp.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedHz;

    // Per-sample pitchbend interpolation — turns block-boundary pitchMult steps
    // (most audible with non-MPE controllers that send coarse or square-wave PB)
    // into smooth pitch glides.  Multiplicative so semitone distance stays linear.
    // 3 ms ramp matches smoothedCutoff; fast enough not to lag live vibrato.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedPitchMult;

    // Per-sample VCA interpolation — eliminates block-boundary amplitude steps that
    // cause audible "crunchiness" (AM sidebands at 689 Hz+) during breath/CC sweeps.
    // Linear ramp is correct for amplitude (no multiplicative-from-zero issue).
    // New notes initialise this from lastVCALevel so legato transitions are seamless.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedVCA;

    // Live MPE state — updated via noteXxxChanged() callbacks
    float pressure  = 0.0f;   // 0..1
    float slide     = 0.0f;   // 0..1 (CC74)
    float pitchbend = 0.0f;   // -1..1
    float velocity  = 0.0f;   // 0..1

    // Render state
    float baseHz       = 440.0f;
    bool  active       = false;
    bool  isTailingOff = false;
    float tailLevel    = 0.0f;
};
