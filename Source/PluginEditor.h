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
    void refreshPresetBox();

    VaneProcessor&   vaneProcessor;
    juce::TextButton reconnectMtsButton;

    // ── Preset strip ─────────────────────────────────────────────────────────
    juce::ComboBox   presetBox;
    juce::TextButton savePresetButton   { "Save" };
    juce::TextButton deletePresetButton;   // label set via fromUTF8 in constructor

    // Mono/poly toggle — APVTS attachment keeps it in sync with the parameter.
    juce::TextButton monoButton { "Poly" };
    juce::AudioProcessorValueTreeState::ButtonAttachment monoAttachment {
        vaneProcessor.apvts, "monoMode", monoButton };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VaneEditor)
};
