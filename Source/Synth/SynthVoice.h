#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Modulation/ModMatrix.h"
#include "MPE/TuningClient.h"

// One MPE voice. Tracks all five MPE dimensions live and feeds them
// into the ModMatrix each block to drive filter, VCA, and pitch.
//
// Currently renders silence — oscillator and filter are the next step.
class SynthVoice : public juce::MPESynthesiserVoice {
public:
    SynthVoice(ModMatrix& matrix, TuningClient& tuning);

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
    float baseFrequencyHz() const;  // MTS-resolved note pitch (no bend yet)

    ModMatrix&   modMatrix;
    TuningClient& tuning;

    double sampleRate   = 44100.0;

    // Live MPE state — updated via noteXxxChanged() callbacks
    float pressure      = 0.0f;   // 0..1
    float slide         = 0.0f;   // 0..1 (CC74)
    float pitchbend     = 0.0f;   // -1..1
    float velocity      = 0.0f;   // 0..1

    // Render state
    float baseHz        = 440.0f;
    float phase         = 0.0f;
    bool  active        = false;
    bool  isTailingOff  = false;
    float tailLevel     = 0.0f;
};
