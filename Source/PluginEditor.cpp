#include "PluginEditor.h"
#include "BuildStamp.h"   // generated every build — always shows current time

// ── Design tokens (from wireframe dark variant) ───────────────────────────────
namespace VaneColors {
    const auto bg      = juce::Colour(0xff1a1814);
    const auto panel   = juce::Colour(0xff221f1a);
    const auto field   = juce::Colour(0xff14130f);
    const auto line    = juce::Colour(0xff38332b);
    const auto text    = juce::Colour(0xffe8e1d2);
    const auto muted   = juce::Colour(0xff897f6c);

    // Modulation source palette
    const auto breath   = juce::Colour(0xff4b86c7);  // CC2
    const auto expr     = juce::Colour(0xff2d9d8a);  // CC11
    const auto pressure = juce::Colour(0xffd28330);  // MPE Z
    const auto slide    = juce::Colour(0xffa35bbf);  // CC74
    const auto pitchbend = juce::Colour(0xffc39529); // PB

    // Button colors
    const auto btnBg   = juce::Colour(0xff2a2520);
    const auto btnText = juce::Colour(0xffccc4b4);
}

VaneEditor::VaneEditor(VaneProcessor& p)
    : AudioProcessorEditor(p), vaneProcessor(p)
{
    setSize(480, 340);

    // ── Apply design-system colors to all buttons ─────────────────────────────
    auto styleBtn = [](juce::TextButton& b) {
        b.setColour(juce::TextButton::buttonColourId,  VaneColors::btnBg);
        b.setColour(juce::TextButton::buttonOnColourId, VaneColors::btnBg.brighter(0.12f));
        b.setColour(juce::TextButton::textColourOffId, VaneColors::btnText);
        b.setColour(juce::TextButton::textColourOnId,  VaneColors::text);
    };

    addAndMakeVisible(reconnectMtsButton);
    reconnectMtsButton.onClick = [this] { vaneProcessor.reconnectMTS(); };
    styleBtn(reconnectMtsButton);

    // ── Preset strip — navigation mode ───────────────────────────────────────
    addAndMakeVisible(prevPresetButton);
    prevPresetButton.onClick = [this] { navigatePreset(-1); };
    styleBtn(prevPresetButton);

    addAndMakeVisible(presetNameLabel);
    presetNameLabel.setJustificationType(juce::Justification::centred);
    presetNameLabel.setColour(juce::Label::textColourId, VaneColors::text);

    addAndMakeVisible(nextPresetButton);
    nextPresetButton.onClick = [this] { navigatePreset(+1); };
    styleBtn(nextPresetButton);

    addAndMakeVisible(savePresetButton);
    savePresetButton.onClick = [this] { enterNamingMode(); };
    styleBtn(savePresetButton);

    addAndMakeVisible(copyPresetButton);
    copyPresetButton.setTooltip("Copy current state to clipboard (paste on another device)");
    copyPresetButton.onClick = [this] { vaneProcessor.presetManager.exportToClipboard(); };
    styleBtn(copyPresetButton);

    addAndMakeVisible(deletePresetButton);
    // × is U+00D7 (\xc3\x97 in UTF-8) — must use fromUTF8; String("×") asserts.
    deletePresetButton.setButtonText(juce::String::fromUTF8("\xc3\x97"));
    deletePresetButton.setTooltip("Delete selected preset");
    deletePresetButton.onClick = [this] {
        auto current = vaneProcessor.presetManager.getCurrentPresetName();
        if (current.isEmpty()) return;
        vaneProcessor.presetManager.deletePreset(current);
        refreshPresetDisplay();
    };
    styleBtn(deletePresetButton);

    // ── Preset strip — naming mode ────────────────────────────────────────────
    addChildComponent(presetNameEditor);
    presetNameEditor.setMultiLine(false);
    presetNameEditor.setReturnKeyStartsNewLine(false);
    presetNameEditor.onReturnKey = [this] { doSavePreset(); };
    presetNameEditor.onEscapeKey = [this] { exitNamingMode(); };
    // onFocusLost intentionally absent: tapping Paste or Cancel causes the
    // TextEditor to lose focus BEFORE their onClick fires, so an onFocusLost
    // save would exit naming mode before those buttons could do anything.
    presetNameEditor.setColour(juce::TextEditor::backgroundColourId, VaneColors::field);
    presetNameEditor.setColour(juce::TextEditor::textColourId,        VaneColors::text);
    presetNameEditor.setColour(juce::TextEditor::outlineColourId,     VaneColors::line);

    addChildComponent(confirmSaveButton);
    confirmSaveButton.onClick = [this] { doSavePreset(); };
    styleBtn(confirmSaveButton);

    addChildComponent(pastePresetButton);
    pastePresetButton.setTooltip("Paste preset from clipboard (import from another device)");
    pastePresetButton.onClick = [this] {
        // Applies the clipboard XML to APVTS immediately — user hears the
        // change — then returns focus so they can type/keep a name and Save.
        // On iOS 14+ the OS shows a one-time "allow paste" permission prompt.
        vaneProcessor.presetManager.importFromClipboard();
        presetNameEditor.grabKeyboardFocus();
    };
    styleBtn(pastePresetButton);

    addChildComponent(cancelNamingButton);
    cancelNamingButton.onClick = [this] { exitNamingMode(); };
    styleBtn(cancelNamingButton);

    // monoButton is a toggle button wired to "monoMode" via ButtonAttachment.
    addAndMakeVisible(monoButton);
    monoButton.setClickingTogglesState(true);
    styleBtn(monoButton);

    refreshPresetDisplay();
    startTimerHz(20);   // 20 Hz: smooth enough for live meters
}

VaneEditor::~VaneEditor() { stopTimer(); }

void VaneEditor::timerCallback()
{
    bool connected = vaneProcessor.mtsConnected();
    reconnectMtsButton.setButtonText(connected ? "MTS-ESP: connected" : "MTS-ESP: reconnect");
    reconnectMtsButton.setColour(juce::TextButton::buttonColourId,
        connected ? juce::Colour(0xff1a3020) : juce::Colour(0xff301a1a));

    bool isMono = monoButton.getToggleState();
    monoButton.setButtonText(isMono ? "Mono" : "Poly");
    monoButton.setColour(juce::TextButton::buttonColourId,
        isMono ? juce::Colour(0xff1a2535) : VaneColors::btnBg);

    repaint();   // redraw meters every tick
}

// ── Paint helpers ─────────────────────────────────────────────────────────────

void VaneEditor::drawBreathCurves(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Three overlapping sine-drift curves — the Vane breath motif.
    // Identical algorithm to the wireframe's BreathCurve component.
    struct Curve { juce::Colour col; float opacity, baseline, amp, seed; };
    const Curve curves[] = {
        { VaneColors::breath,    0.18f,  0.0f, 11.0f, 0.4f },
        { VaneColors::slide,     0.11f,  6.0f,  7.0f, 1.4f },
        { VaneColors::pressure,  0.09f, -3.0f,  5.0f, 2.6f },
    };

    const float w    = bounds.getWidth();
    const float midY = bounds.getCentreY();

    for (auto& c : curves) {
        juce::Path path;
        constexpr int steps = 80;
        for (int i = 0; i <= steps; ++i) {
            const float x = bounds.getX() + (i / float(steps)) * w;
            const float y = midY + c.baseline
                          + std::sin(i * 0.32f + c.seed) * c.amp
                          + std::sin(i * 0.13f + c.seed * 1.7f) * c.amp * 0.6f;
            if (i == 0) path.startNewSubPath(x, y);
            else        path.lineTo(x, y);
        }
        g.setColour(c.col.withAlpha(c.opacity));
        g.strokePath(path, juce::PathStrokeType(2.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void VaneEditor::drawMeters(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Panel background
    g.setColour(VaneColors::panel);
    g.fillRoundedRectangle(area.toFloat(), 10.0f);

    // Subtle panel border
    g.setColour(VaneColors::line);
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 10.0f, 1.0f);

    auto inner = area.reduced(12, 8);

    // Eyebrow label
    g.setColour(VaneColors::muted);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    g.drawText("LIVE EXPRESSION", inner.removeFromTop(13), juce::Justification::centredLeft);
    inner.removeFromTop(3);  // gap after eyebrow

    // Meter definitions — value fetched live from processor atomics
    struct MeterDef { const char* label; const char* cc; juce::Colour colour; float value; };
    const float pb = vaneProcessor.meterPitchbend.load(std::memory_order_relaxed);
    const MeterDef meters[] = {
        { "Breath",     "CC2",   VaneColors::breath,    vaneProcessor.meterBreath.load(std::memory_order_relaxed) },
        { "Expression", "CC11",  VaneColors::expr,      vaneProcessor.meterExpr.load(std::memory_order_relaxed)   },
        { "Pressure",   "MPE Z", VaneColors::pressure,  vaneProcessor.meterPressure.load(std::memory_order_relaxed) },
        { "Slide",      "CC74",  VaneColors::slide,     vaneProcessor.meterSlide.load(std::memory_order_relaxed)  },
        { "Pitchbend",  "PB",    VaneColors::pitchbend, (pb + 1.0f) * 0.5f },  // -1..1 → 0..1
    };

    const int rowH   = inner.getHeight() / 5;
    const int dotX   = inner.getX();
    const int labelX = dotX + 14;
    const int ccW    = 40;
    const int ccX    = inner.getRight() - ccW;
    const int barX   = labelX + 92;
    const int barRight = ccX - 6;

    for (int i = 0; i < 5; ++i) {
        const auto& m = meters[i];
        const int rowY = inner.getY() + i * rowH;
        const int centreY = rowY + rowH / 2;

        // Colored source dot
        g.setColour(m.colour);
        g.fillEllipse((float)dotX, (float)centreY - 4.0f, 8.0f, 8.0f);

        // Source label
        g.setColour(VaneColors::text);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        g.drawText(m.label,
                   juce::Rectangle<int>(labelX, rowY, 90, rowH),
                   juce::Justification::centredLeft);

        // CC label (muted, right-aligned)
        g.setColour(VaneColors::muted);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
        g.drawText(m.cc,
                   juce::Rectangle<int>(ccX, rowY, ccW, rowH),
                   juce::Justification::centredRight);

        // Bar track
        const int barW   = barRight - barX;
        const int barH   = 7;
        const int barY   = centreY - barH / 2;
        juce::Rectangle<float> track((float)barX, (float)barY, (float)barW, (float)barH);
        g.setColour(VaneColors::field);
        g.fillRoundedRectangle(track, 3.5f);

        // Bar fill — gradient from source color toward a lighter tint
        const float fillW = juce::jlimit(0.0f, (float)barW, m.value * (float)barW);
        if (fillW >= 2.0f) {
            juce::ColourGradient grad(m.colour, (float)barX, (float)barY,
                                      m.colour.brighter(0.45f), (float)(barX + barW), (float)barY,
                                      false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(juce::Rectangle<float>((float)barX, (float)barY, fillW, (float)barH), 3.5f);
        }

        // Pitchbend: draw a centre-line marker so neutral is obvious
        if (i == 4) {
            const int midX = barX + barW / 2;
            g.setColour(VaneColors::line.brighter(0.3f));
            g.drawLine((float)midX, (float)barY, (float)midX, (float)(barY + barH), 1.0f);
        }
    }
}

void VaneEditor::paint(juce::Graphics& g)
{
    // ── Background ────────────────────────────────────────────────────────────
    g.fillAll(VaneColors::bg);

    // ── Header breath motif (decorative curves in title area) ─────────────────
    drawBreathCurves(g, getLocalBounds().removeFromTop(54).toFloat());

    // ── Title ─────────────────────────────────────────────────────────────────
    g.setColour(VaneColors::text);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(22.0f)));
    g.drawText("Vane", getLocalBounds().removeFromTop(32), juce::Justification::centred);

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    g.setColour(VaneColors::muted);
    g.drawText(juce::String::fromUTF8("MPE  \xc2\xb7  MTS-ESP  \xc2\xb7  CC modulation"),
               getLocalBounds().withTrimmedTop(32).removeFromTop(20),
               juce::Justification::centred);

    // ── Meters section ────────────────────────────────────────────────────────
    drawMeters(g, metersArea);

    // ── Version footer ────────────────────────────────────────────────────────
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.setColour(VaneColors::muted.darker(0.3f));
    g.drawText(juce::String("v") + JucePlugin_VersionString
                   + juce::String::fromUTF8("  \xc2\xb7  " VANE_BUILD_STAMP),
               getLocalBounds().removeFromBottom(18),
               juce::Justification::centred);
}

void VaneEditor::resized()
{
    constexpr int margin  = 14;
    constexpr int stripH  = 26;
    constexpr int stripY  = 56;   // preset strip top
    constexpr int ctrlH   = 26;
    constexpr int ctrlY   = 282;  // MTS + mono row
    constexpr int navW    = 30;
    constexpr int saveW   = 50;
    constexpr int copyW   = 44;
    constexpr int delW    = 26;
    constexpr int pasteW  = 44;
    constexpr int canW    = 54;
    constexpr int gap     = 6;

    // ── Meters area (stored for paint()) ─────────────────────────────────────
    metersArea = juce::Rectangle<int>(margin, 94, getWidth() - 2 * margin, 170);

    // ── Preset strip — navigation mode ───────────────────────────────────────
    int available = getWidth() - 2 * margin;

    int navRightW = copyW + gap + saveW + gap + delW;
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

    // ── Preset strip — naming mode ────────────────────────────────────────────
    int nameRightW = saveW + gap + pasteW + gap + canW;
    int editorW    = available - nameRightW - gap;
    int xSaveE     = margin + editorW + gap;
    int xPasteE    = xSaveE  + saveW  + gap;
    int xCancel    = xPasteE + pasteW + gap;

    presetNameEditor.setBounds  (margin,  stripY, editorW, stripH);
    confirmSaveButton.setBounds (xSaveE,  stripY, saveW,   stripH);
    pastePresetButton.setBounds (xPasteE, stripY, pasteW,  stripH);
    cancelNamingButton.setBounds(xCancel, stripY, canW,    stripH);

    // ── Controls row: MTS reconnect (left) + Mono toggle (right) ─────────────
    constexpr int mtsW  = 200;
    constexpr int monoW = 70;
    reconnectMtsButton.setBounds(margin,                ctrlY, mtsW,  ctrlH);
    monoButton.setBounds        (getWidth() - margin - monoW, ctrlY, monoW, ctrlH);
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
    if (!inNamingMode) return;

    auto name = presetNameEditor.getText().trim();
    if (name.isNotEmpty())
        vaneProcessor.presetManager.savePreset(name);

    exitNamingMode();
    refreshPresetDisplay();
}
