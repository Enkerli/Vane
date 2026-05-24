#include "PluginEditor.h"
#include "BuildStamp.h"   // generated every build — always shows current time

VaneEditor::VaneEditor(VaneProcessor& p)
    : AudioProcessorEditor(p), vaneProcessor(p)
{
    setSize(480, 320);

    addAndMakeVisible(reconnectMtsButton);
    reconnectMtsButton.onClick = [this] {
        vaneProcessor.reconnectMTS();
    };

    // ── Preset strip — navigation mode ───────────────────────────────────────
    // ◄ and ► navigate the preset list without any popup menu.
    // PopupMenu / ComboBox dropdowns are broken in AUv3 on iOS: the menu
    // cannot escape the plugin's sandboxed UIView hierarchy.
    addAndMakeVisible(prevPresetButton);
    prevPresetButton.onClick = [this] { navigatePreset(-1); };

    addAndMakeVisible(presetNameLabel);
    presetNameLabel.setJustificationType(juce::Justification::centred);
    presetNameLabel.setColour(juce::Label::textColourId, juce::Colour(0xffb0b8d0));

    addAndMakeVisible(nextPresetButton);
    nextPresetButton.onClick = [this] { navigatePreset(+1); };

    addAndMakeVisible(savePresetButton);
    savePresetButton.onClick = [this] { enterNamingMode(); };

    addAndMakeVisible(copyPresetButton);
    copyPresetButton.setTooltip("Copy current state to clipboard (paste on another device)");
    copyPresetButton.onClick = [this] {
        vaneProcessor.presetManager.exportToClipboard();
    };

    addAndMakeVisible(deletePresetButton);
    // × is U+00D7 (\xc3\x97 in UTF-8) — must use fromUTF8; String("×") asserts.
    deletePresetButton.setButtonText(juce::String::fromUTF8("\xc3\x97"));
    deletePresetButton.setTooltip("Delete selected preset");
    deletePresetButton.onClick = [this] {
        auto current = vaneProcessor.presetManager.getCurrentPresetName();
        if (current.isEmpty()) return;
        // No confirmation: AlertWindow::showAsync is unreliable in AUv3.
        // Presets are trivially re-saveable, so a direct delete is safe.
        vaneProcessor.presetManager.deletePreset(current);
        refreshPresetDisplay();
    };

    // ── Preset strip — naming mode ────────────────────────────────────────────
    addChildComponent(presetNameEditor);
    presetNameEditor.setMultiLine(false);
    presetNameEditor.setReturnKeyStartsNewLine(false);
    // onReturnKey fires on desktop; on iOS it may not — onFocusLost catches it.
    presetNameEditor.onReturnKey  = [this] { doSavePreset(); };
    // Tapping anywhere outside the editor on iOS dismisses the keyboard and
    // triggers onFocusLost — treat that as confirmation so the user doesn't
    // have to hunt for the Save button after typing on a software keyboard.
    presetNameEditor.onFocusLost  = [this] { if (inNamingMode) doSavePreset(); };
    presetNameEditor.onEscapeKey  = [this] { exitNamingMode(); };

    addChildComponent(confirmSaveButton);
    confirmSaveButton.onClick = [this] { doSavePreset(); };

    addChildComponent(pastePresetButton);
    pastePresetButton.setTooltip("Paste preset from clipboard (import from another device)");
    pastePresetButton.onClick = [this] {
        // Applies the clipboard XML to APVTS immediately — user hears the
        // change — then clears the name field so they type a new name.
        // On iOS 14+ the OS shows a one-time "allow paste" permission prompt.
        if (vaneProcessor.presetManager.importFromClipboard()) {
            presetNameEditor.setText({}, juce::dontSendNotification);
            presetNameEditor.grabKeyboardFocus();
        }
    };

    addChildComponent(cancelNamingButton);
    cancelNamingButton.onClick = [this] { exitNamingMode(); };

    // monoButton is a toggle button wired to "monoMode" via ButtonAttachment.
    addAndMakeVisible(monoButton);
    monoButton.setClickingTogglesState(true);

    refreshPresetDisplay();
    startTimerHz(2);   // refresh MTS label + mono button twice per second
}

VaneEditor::~VaneEditor() { stopTimer(); }

void VaneEditor::timerCallback()
{
    bool connected = vaneProcessor.mtsConnected();
    reconnectMtsButton.setButtonText(connected ? "MTS-ESP: connected" : "MTS-ESP: reconnect");
    reconnectMtsButton.setColour(juce::TextButton::buttonColourId,
        connected ? juce::Colour(0xff1a3a1a) : juce::Colour(0xff3a1a1a));

    bool isMono = monoButton.getToggleState();
    monoButton.setButtonText(isMono ? "Mono" : "Poly");
    monoButton.setColour(juce::TextButton::buttonColourId,
        isMono ? juce::Colour(0xff1a2a3a) : juce::Colour(0xff252520));
}

void VaneEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff111118));

    g.setColour(juce::Colour(0xffb0b8d0));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(22.0f)));
    g.drawText("Vane", getLocalBounds().removeFromTop(55),
               juce::Justification::centred);

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
    g.setColour(juce::Colour(0xff606878));
    g.drawText(juce::String::fromUTF8("MPE  \xc2\xb7  MTS-ESP  \xc2\xb7  CC modulation"),
               getLocalBounds().withTrimmedTop(56).removeFromTop(20),
               juce::Justification::centred);

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
    g.setColour(juce::Colour(0xffa8b4c8));
    g.drawText(juce::String("v") + JucePlugin_VersionString
                   + juce::String::fromUTF8("  \xc2\xb7  " VANE_BUILD_STAMP),
               getLocalBounds().removeFromBottom(22),
               juce::Justification::centred);
}

void VaneEditor::resized()
{
    // ── Preset strip ──────────────────────────────────────────────────────────
    // Navigation mode: [◄][label][►][Copy][Save][×]
    // Naming mode:     [TextEditor ..........][Save][Paste][Cancel]
    // Both modes share y=85, h=26.  Only one set is visible at a time.
    constexpr int margin = 20;
    constexpr int stripY = 85;
    constexpr int stripH = 26;
    constexpr int navW   = 30;   // ◄ and ►
    constexpr int saveW  = 50;
    constexpr int copyW  = 44;
    constexpr int delW   = 26;
    constexpr int pasteW = 44;
    constexpr int canW   = 54;
    constexpr int gap    = 6;

    int available = getWidth() - 2 * margin;   // 440 px at default width

    // Navigation mode: fixed right block = [Copy][gap][Save][gap][×]
    int navRightW = copyW + gap + saveW + gap + delW;   // 180
    int labelW    = available - navW - gap - navW - gap - navRightW;
    int xPrev     = margin;
    int xLabel    = xPrev  + navW  + gap;
    int xNext     = xLabel + labelW + gap;
    int xCopy     = xNext  + navW  + gap;
    int xSaveN    = xCopy  + copyW  + gap;
    int xDelN     = xSaveN + saveW  + gap;

    prevPresetButton.setBounds (xPrev,  stripY, navW,  stripH);
    presetNameLabel.setBounds  (xLabel, stripY, labelW,stripH);
    nextPresetButton.setBounds (xNext,  stripY, navW,  stripH);
    copyPresetButton.setBounds (xCopy,  stripY, copyW, stripH);
    savePresetButton.setBounds (xSaveN, stripY, saveW, stripH);
    deletePresetButton.setBounds(xDelN, stripY, delW,  stripH);

    // Naming mode: [TextEditor][Save][Paste][Cancel]
    int nameRightW = saveW + gap + pasteW + gap + canW;   // 204
    int editorW    = available - nameRightW;
    int xSaveE     = margin + editorW + gap;
    int xPasteE    = xSaveE  + saveW  + gap;
    int xCancel    = xPasteE + pasteW + gap;

    presetNameEditor.setBounds  (margin,   stripY, editorW, stripH);
    confirmSaveButton.setBounds (xSaveE,   stripY, saveW,   stripH);
    pastePresetButton.setBounds (xPasteE,  stripY, pasteW,  stripH);
    cancelNamingButton.setBounds(xCancel,  stripY, canW,    stripH);

    // MTS reconnect button: centred, slightly above centre
    reconnectMtsButton.setBounds(
        getLocalBounds().withSizeKeepingCentre(160, 26).translated(0, 20));

    // Mono/poly toggle: sits directly below the MTS button with a small gap
    monoButton.setBounds(
        getLocalBounds().withSizeKeepingCentre(80, 26).translated(0, 55));
}

// ── Preset navigation ─────────────────────────────────────────────────────────

void VaneEditor::navigatePreset(int delta)
{
    auto names   = vaneProcessor.presetManager.getPresetNames();
    if (names.isEmpty()) return;

    auto current = vaneProcessor.presetManager.getCurrentPresetName();
    int  idx     = names.indexOf(current);

    if (idx < 0)
        idx = (delta > 0) ? 0 : names.size() - 1;
    else
        idx = (idx + delta + names.size()) % names.size();

    vaneProcessor.presetManager.loadPreset(names[idx]);
    refreshPresetDisplay();
}

void VaneEditor::refreshPresetDisplay()
{
    auto names   = vaneProcessor.presetManager.getPresetNames();
    auto current = vaneProcessor.presetManager.getCurrentPresetName();

    presetNameLabel.setText(current.isEmpty() ? "-- init --" : current,
                            juce::dontSendNotification);

    bool hasPresets = !names.isEmpty();
    prevPresetButton.setEnabled(hasPresets);
    nextPresetButton.setEnabled(hasPresets);
    deletePresetButton.setEnabled(!current.isEmpty());
}

// ── Inline preset naming ──────────────────────────────────────────────────────

void VaneEditor::enterNamingMode()
{
    inNamingMode = true;

    presetNameEditor.setText(vaneProcessor.presetManager.getCurrentPresetName(),
                             juce::dontSendNotification);
    presetNameEditor.selectAll();

    prevPresetButton.setVisible(false);
    presetNameLabel.setVisible(false);
    nextPresetButton.setVisible(false);
    copyPresetButton.setVisible(false);
    savePresetButton.setVisible(false);
    deletePresetButton.setVisible(false);

    presetNameEditor.setVisible(true);
    confirmSaveButton.setVisible(true);
    pastePresetButton.setVisible(true);
    cancelNamingButton.setVisible(true);

    presetNameEditor.grabKeyboardFocus();
}

void VaneEditor::exitNamingMode()
{
    inNamingMode = false;

    // Resign keyboard focus before hiding so iOS dismisses the keyboard
    // synchronously — avoids the async keyboard-dismiss swallowing the
    // next touch event on the navigation buttons.
    presetNameEditor.giveAwayKeyboardFocus();

    presetNameEditor.setVisible(false);
    confirmSaveButton.setVisible(false);
    pastePresetButton.setVisible(false);
    cancelNamingButton.setVisible(false);

    prevPresetButton.setVisible(true);
    presetNameLabel.setVisible(true);
    nextPresetButton.setVisible(true);
    copyPresetButton.setVisible(true);
    savePresetButton.setVisible(true);
    deletePresetButton.setVisible(true);
}

void VaneEditor::doSavePreset()
{
    // Guard: onFocusLost fires even when cancelNamingButton triggers exitNamingMode,
    // because focus moves away from the editor.  The inNamingMode flag ensures we
    // only save once — exitNamingMode() clears it before focus is lost on cancel.
    if (!inNamingMode) return;

    auto name = presetNameEditor.getText().trim();
    if (name.isNotEmpty())
        vaneProcessor.presetManager.savePreset(name);

    exitNamingMode();
    refreshPresetDisplay();
}
