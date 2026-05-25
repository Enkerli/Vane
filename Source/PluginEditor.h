#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "ModMatrixEditor.h"
#include "PatchPanel.h"

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

    // ── Panel area — only one visible at a time (meters / matrix / patch) ───────
    // matrixEditor and patchPanel occupy the same metersArea rectangle.
    // Toggled by matrixButton / patchButton; both hidden = meters visible.
    ModMatrixEditor matrixEditor;
    bool            showMatrix { false };

    PatchPanel      patchPanel;
    bool            showPatch  { false };

    // ── Controls row ──────────────────────────────────────────────────────────
    juce::TextButton monoButton   { "Poly" };
    juce::TextButton matrixButton { "Matrix" };
    juce::TextButton patchButton  { "Patch" };
    juce::AudioProcessorValueTreeState::ButtonAttachment monoAttachment {
        vaneProcessor.apvts, "monoMode", monoButton };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VaneEditor)
};
