#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Modulation/ModMatrix.h"
#include "MPE/TuningClient.h"
#include "Synth/Oscillator.h"
#include "Synth/SVFilter.h"

class SynthVoice : public juce::MPESynthesiserVoice {
public:
    // Raw APVTS parameter pointers — safe to read on the audio thread.
    // lastNoteHz: shared across all voices; read in noteStarted() to glide
    //             from the previous pitch when glideTime > 0.
    SynthVoice(ModMatrix& matrix, TuningClient& tuning,
               std::atomic<float>*    paramWave,      std::atomic<float>*    paramDetune,
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
               // Pitchbend range in semitones. When null, falls back to 2 st
               // (MIDI standard default). Stored as an APVTS param so presets carry it.
               std::atomic<float>*    pitchbendRange  = nullptr,
               // Master-channel (global) pitchbend captured from the raw MIDI
               // stream in processBlock.  Used for notes on channel 1 (non-MPE
               // controllers) which never receive notePitchbendChanged() from JUCE.
               std::atomic<float>*    globalPitchbend  = nullptr,
               // Pitchbend range for channel-1 legacy notes (default 2 st).
               // pitchbendRange above is for MPE member-channel notes (default 48 st).
               std::atomic<float>*    nonMPEPBRange    = nullptr);

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

    std::atomic<float>* paramWave        = nullptr;
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

    // Pitchbend range parameter — read each render block.
    std::atomic<float>*    paramPBRange       = nullptr;
    // Global pitchbend from master channel — fallback for non-MPE notes.
    std::atomic<float>*    sharedGlobalPB     = nullptr;
    // Pitchbend range for channel-1 legacy notes (default 2 st).
    std::atomic<float>*    paramNonMPEPBRange = nullptr;

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
