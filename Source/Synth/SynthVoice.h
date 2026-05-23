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
               std::atomic<float>*    paramMono);

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

    uint32_t myLegatoGen = 0;  // generation this voice was born into

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
