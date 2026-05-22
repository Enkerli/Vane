#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class VaneEditor : public juce::AudioProcessorEditor {
public:
    explicit VaneEditor(VaneProcessor&);
    ~VaneEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    VaneProcessor& vaneProcessor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VaneEditor)
};
