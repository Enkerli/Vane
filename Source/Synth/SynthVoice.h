#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Modulation/ModMatrix.h"
#include "MPE/TuningClient.h"
#include "Synth/Oscillator.h"
#include "Synth/SVFilter.h"

class SynthVoice : public juce::MPESynthesiserVoice {
public:
    // Raw APVTS parameter pointers — safe to read on the audio thread
    SynthVoice(ModMatrix& matrix, TuningClient& tuning,
               std::atomic<float>* paramWave,        std::atomic<float>* paramDetune,
               std::atomic<float>* paramCutoff,      std::atomic<float>* paramRes,
               std::atomic<float>* paramFilterMode,  std::atomic<float>* paramVelocityMix);

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

    Oscillator osc;
    SVFilter   filter;
    double sampleRate = 44100.0;

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
