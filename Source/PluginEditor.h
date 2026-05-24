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

    // ── Inline preset naming ──────────────────────────────────────────────────
    // iOS (AUv3): enterModalState blocks the host's event loop, so we never
    // use AlertWindow for text input. Instead, clicking Save flips inNamingMode
    // and swaps the combo+save+delete strip for a text editor + confirm + cancel.
    void enterNamingMode();
    void exitNamingMode();
    void doSavePreset();

    VaneProcessor&   vaneProcessor;
    juce::TextButton reconnectMtsButton;

    // ── Preset strip — select/load mode ──────────────────────────────────────
    juce::ComboBox   presetBox;
    juce::TextButton savePresetButton   { "Save" };
    juce::TextButton deletePresetButton;   // label set via fromUTF8 in constructor

    // ── Preset strip — naming mode (shown when Save is clicked) ──────────────
    bool             inNamingMode       { false };
    juce::TextEditor presetNameEditor;
    juce::TextButton confirmSaveButton  { "Save" };
    juce::TextButton cancelNamingButton { "Cancel" };

    // Mono/poly toggle — APVTS attachment keeps it in sync with the parameter.
    juce::TextButton monoButton { "Poly" };
    juce::AudioProcessorValueTreeState::ButtonAttachment monoAttachment {
        vaneProcessor.apvts, "monoMode", monoButton };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VaneEditor)
};
