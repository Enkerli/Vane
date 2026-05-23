#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class VaneEditor : public juce::AudioProcessorEditor,
                   private juce::Timer {
public:
    explicit VaneEditor(VaneProcessor&);
    ~VaneEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    VaneProcessor&   vaneProcessor;
    juce::TextButton reconnectMtsButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VaneEditor)
};
