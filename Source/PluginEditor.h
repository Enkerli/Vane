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

    // ── Preset navigation ─────────────────────────────────────────────────────
    // ComboBox popups are broken in AUv3 on iOS (PopupMenu can't escape the
    // plugin's sandboxed view hierarchy). We use ◄ / label / ► instead — no
    // popup, fully touch-friendly, works in every host.
    void navigatePreset(int delta);   // delta = -1 (prev) or +1 (next)
    void refreshPresetDisplay();

    // ── Inline preset naming ──────────────────────────────────────────────────
    // No AlertWindow / modal loop: UIAlertController is unreliable in AUv3.
    // Clicking Save swaps the nav strip for an inline TextEditor + confirm.
    void enterNamingMode();
    void exitNamingMode();
    void doSavePreset();

    VaneProcessor&   vaneProcessor;
    juce::TextButton reconnectMtsButton;

    // ── Preset strip — navigation mode (default) ──────────────────────────────
    juce::TextButton prevPresetButton  { "<" };
    juce::Label      presetNameLabel;
    juce::TextButton nextPresetButton  { ">" };
    juce::TextButton savePresetButton  { "Save" };
    juce::TextButton deletePresetButton;   // label set via fromUTF8 in constructor

    // ── Preset strip — naming mode (shown when Save is clicked) ──────────────
    bool             inNamingMode      { false };
    juce::TextEditor presetNameEditor;
    juce::TextButton confirmSaveButton { "Save" };
    juce::TextButton cancelNamingButton{ "Cancel" };

    // Mono/poly toggle — APVTS attachment keeps it in sync with the parameter.
    juce::TextButton monoButton { "Poly" };
    juce::AudioProcessorValueTreeState::ButtonAttachment monoAttachment {
        vaneProcessor.apvts, "monoMode", monoButton };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VaneEditor)
};
