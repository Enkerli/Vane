#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Synth/SynthVoice.h"
#include "Modulation/ModMatrix.h"
#include "MPE/TuningClient.h"

class VaneProcessor : public juce::AudioProcessor {
public:
    VaneProcessor();
    ~VaneProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool supportsMPE()  const override { return true; }
    double getTailLengthSeconds() const override { return 0.5; }

    int  getNumPrograms() override                              { return 1; }
    int  getCurrentProgram() override                          { return 0; }
    void setCurrentProgram(int) override                       {}
    const juce::String getProgramName(int) override            { return {}; }
    void changeProgramName(int, const juce::String&) override  {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    ModMatrix    modMatrix;
    TuningClient tuning;

    void reconnectMTS()      { tuning.reconnect(); }
    bool mtsConnected() const { return tuning.hasMaster(); }

private:
    juce::MPESynthesiser synth;

    // Shared across all voices: the Hz of the most recently started note.
    // New voices read this in noteStarted() to glide from the previous pitch
    // when glideTime > 0. Monophonic controllers only — MPE poly is unaffected
    // because players set glideTime = 0 for poly use.
    std::atomic<float> lastNoteHz { 0.0f };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VaneProcessor)
};
