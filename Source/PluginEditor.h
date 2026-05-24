#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "ModMatrixEditor.h"

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
    void navigatePreset(int delta);   // delta = -1 (prev) or +1 (next)
    void refreshPresetDisplay();

    // ── Inline preset naming ──────────────────────────────────────────────────
    void enterNamingMode();
    void exitNamingMode();
    void doSavePreset();

    // ── Paint helpers ─────────────────────────────────────────────────────────
    void drawBreathCurves(juce::Graphics&, juce::Rectangle<float> bounds);
    void drawMeters(juce::Graphics&, juce::Rectangle<int> bounds);

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
    juce::TextButton confirmSaveButton  { "Save" };
    juce::TextButton pastePresetButton  { "Paste" };
    juce::TextButton cancelNamingButton { "Cancel" };

    // ── Preset strip — normal mode extras ────────────────────────────────────
    juce::TextButton copyPresetButton   { "Copy" };

    // ── Layout cache (set in resized, read in paint) ──────────────────────────
    juce::Rectangle<int> metersArea;

    // ── ModMatrix editor panel ────────────────────────────────────────────────
    // matrixEditor is declared before matrixButton so the toggle button can
    // reference it safely.  Toggled by matrixButton in the controls row.
    ModMatrixEditor matrixEditor;
    bool            showMatrix { false };

    // ── Controls row ──────────────────────────────────────────────────────────
    // Mono/poly toggle — APVTS attachment keeps it in sync with the parameter.
    juce::TextButton monoButton   { "Poly" };
    juce::TextButton matrixButton { "Matrix" };
    juce::AudioProcessorValueTreeState::ButtonAttachment monoAttachment {
        vaneProcessor.apvts, "monoMode", monoButton };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VaneEditor)
};
