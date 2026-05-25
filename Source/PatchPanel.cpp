#include "PatchPanel.h"
#include "PluginProcessor.h"

// ── ContentComponent ──────────────────────────────────────────────────────────
// The scrollable body.  paint() draws section headers and row labels directly;
// all interactive controls are child components with APVTS attachments.
struct PatchPanel::ContentComponent : public juce::Component
{
    // ── Column geometry ───────────────────────────────────────────────────────
    static constexpr int kPadL    = 10;
    static constexpr int kPadR    = 10;
    static constexpr int kLblW    = 88;    // row label column
    static constexpr int kRowH    = 22;    // one parameter row height
    static constexpr int kSecHdrH = 14;   // section header height

    // Painted-text records rebuilt each layout() call.
    struct LabelRec { juce::String text; int x, y, w; bool isHeader; };
    std::vector<LabelRec> labelRecs;

    // ── FILTER ────────────────────────────────────────────────────────────────
    juce::Slider   cutoffSlider, resSlider;
    juce::ComboBox filterModeBox;

    // ── OSCILLATOR ────────────────────────────────────────────────────────────
    juce::ComboBox waveBox;
    juce::Slider   detuneSlider;

    // ── PERFORMANCE ───────────────────────────────────────────────────────────
    juce::Slider   outputSlider, glideSlider, velMixSlider,
                   pbRangeSlider, nonMPEPBSlider;

    // ── MACROS ────────────────────────────────────────────────────────────────
    juce::ComboBox breathSrcBox, exprSrcBox;
    juce::Slider   breathCCSlider, exprCCSlider;

    // ── APVTS attachments ─────────────────────────────────────────────────────
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SA> cutoffAtt, resAtt, detuneAtt,
                        outputAtt, glideAtt, velMixAtt,
                        pbRangeAtt, nonMPEPBAtt,
                        breathCCAtt, exprCCAtt;
    std::unique_ptr<CA> filterModeAtt, waveAtt,
                        breathSrcAtt, exprSrcAtt;

    explicit ContentComponent(VaneProcessor& p)
    {
        auto& apvts = p.apvts;

        // Accent colours — borrowed from the mod-source palette
        const auto filterCol = juce::Colour(0xff4b86c7);   // breath blue
        const auto oscCol    = juce::Colour(0xff2d9d8a);   // expr teal
        const auto perfCol   = juce::Colour(0xff897f6c);   // muted
        const auto macroCol  = juce::Colour(0xffd28330);   // pressure amber

        auto styleSlider = [&](juce::Slider& s, juce::Colour fill) {
            s.setSliderStyle(juce::Slider::LinearBar);
            s.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
            s.setColour(juce::Slider::trackColourId,      fill.withAlpha(0.60f));
            s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff14130f));
            s.setColour(juce::Slider::thumbColourId,      fill);
            // Show current value in a popup while dragging
            s.setPopupDisplayEnabled(true, false, nullptr);
        };

        auto styleCombo = [](juce::ComboBox& c) {
            c.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff14130f));
            c.setColour(juce::ComboBox::textColourId,       juce::Colour(0xffe8e1d2));
            c.setColour(juce::ComboBox::outlineColourId,    juce::Colour(0xff38332b));
            c.setColour(juce::ComboBox::arrowColourId,      juce::Colour(0xff897f6c));
        };

        // ── FILTER ────────────────────────────────────────────────────────────
        styleSlider(cutoffSlider, filterCol); addAndMakeVisible(cutoffSlider);
        styleSlider(resSlider,    filterCol); addAndMakeVisible(resSlider);
        filterModeBox.addItem("LP", 1);
        filterModeBox.addItem("BP", 2);
        filterModeBox.addItem("HP", 3);
        styleCombo(filterModeBox); addAndMakeVisible(filterModeBox);
        cutoffAtt     = std::make_unique<SA>(apvts, "filterCutoff", cutoffSlider);
        resAtt        = std::make_unique<SA>(apvts, "filterRes",    resSlider);
        filterModeAtt = std::make_unique<CA>(apvts, "filterMode",   filterModeBox);

        // ── OSCILLATOR ────────────────────────────────────────────────────────
        waveBox.addItem("Sine",     1);
        waveBox.addItem("Triangle", 2);
        waveBox.addItem("Saw",      3);
        waveBox.addItem("Square",   4);
        waveBox.addItem("Noise",    5);
        styleCombo(waveBox); addAndMakeVisible(waveBox);
        styleSlider(detuneSlider, oscCol); addAndMakeVisible(detuneSlider);
        waveAtt   = std::make_unique<CA>(apvts, "oscWave",   waveBox);
        detuneAtt = std::make_unique<SA>(apvts, "oscDetune", detuneSlider);

        // ── PERFORMANCE ───────────────────────────────────────────────────────
        styleSlider(outputSlider,   perfCol); addAndMakeVisible(outputSlider);
        styleSlider(glideSlider,    perfCol); addAndMakeVisible(glideSlider);
        styleSlider(velMixSlider,   perfCol); addAndMakeVisible(velMixSlider);
        styleSlider(pbRangeSlider,  perfCol); addAndMakeVisible(pbRangeSlider);
        styleSlider(nonMPEPBSlider, perfCol); addAndMakeVisible(nonMPEPBSlider);
        outputAtt   = std::make_unique<SA>(apvts, "outputLevel",    outputSlider);
        glideAtt    = std::make_unique<SA>(apvts, "glideTime",      glideSlider);
        velMixAtt   = std::make_unique<SA>(apvts, "velocityMix",    velMixSlider);
        pbRangeAtt  = std::make_unique<SA>(apvts, "pitchbendRange", pbRangeSlider);
        nonMPEPBAtt = std::make_unique<SA>(apvts, "nonMPEPBRange",  nonMPEPBSlider);

        // ── MACROS ────────────────────────────────────────────────────────────
        // Breath source: which physical signal drives the Breath macro.
        breathSrcBox.addItem("CC",           1);
        breathSrcBox.addItem("Aftertouch",   2);
        breathSrcBox.addItem("MPE Pressure", 3);
        styleCombo(breathSrcBox); addAndMakeVisible(breathSrcBox);
        styleSlider(breathCCSlider, macroCol);
        breathCCSlider.setNumDecimalPlacesToDisplay(0);
        addAndMakeVisible(breathCCSlider);

        // Expression source
        exprSrcBox.addItem("CC",         1);
        exprSrcBox.addItem("Aftertouch", 2);
        styleCombo(exprSrcBox); addAndMakeVisible(exprSrcBox);
        styleSlider(exprCCSlider, macroCol);
        exprCCSlider.setNumDecimalPlacesToDisplay(0);
        addAndMakeVisible(exprCCSlider);

        // Attachments AFTER items are populated
        breathSrcAtt = std::make_unique<CA>(apvts, "macroBreathSrc", breathSrcBox);
        breathCCAtt  = std::make_unique<SA>(apvts, "macroBreathCC",  breathCCSlider);
        exprSrcAtt   = std::make_unique<CA>(apvts, "macroExprSrc",   exprSrcBox);
        exprCCAtt    = std::make_unique<SA>(apvts, "macroExprCC",    exprCCSlider);
    }

    // Called by PatchPanel::resized().  Positions all child widgets and records
    // text labels for paint().  setSize() at the end triggers JUCE's layout pass
    // but ContentComponent intentionally has no resized() override to avoid a
    // recursive layout() call from setSize().
    void layout(int cW)
    {
        labelRecs.clear();

        const int ctrlX = kPadL + kLblW;       // widget left edge
        const int ctrlW = cW - ctrlX - kPadR;  // widget width
        int y = 4;

        auto addHdr = [&](const char* title) {
            labelRecs.push_back({ juce::String(title), kPadL, y,
                                  cW - kPadL - kPadR, true });
            y += kSecHdrH + 4;
        };
        auto addSlider = [&](const char* label, juce::Slider& s) {
            labelRecs.push_back({ juce::String(label), kPadL, y, kLblW, false });
            s.setBounds(ctrlX, y + 3, ctrlW, kRowH - 6);
            y += kRowH;
        };
        auto addCombo = [&](const char* label, juce::ComboBox& c) {
            labelRecs.push_back({ juce::String(label), kPadL, y, kLblW, false });
            c.setBounds(ctrlX, y + 2, ctrlW, kRowH - 4);
            y += kRowH;
        };
        // Macro row: [src combo 108px] [cc# slider — remainder]
        auto addMacroRow = [&](const char* label,
                               juce::ComboBox& src, juce::Slider& cc) {
            labelRecs.push_back({ juce::String(label), kPadL, y, kLblW, false });
            const int srcW = 108;
            const int ccX  = ctrlX + srcW + 4;
            const int ccW  = cW - ccX - kPadR;
            src.setBounds(ctrlX, y + 2, srcW,  kRowH - 4);
            cc.setBounds (ccX,   y + 3, ccW,   kRowH - 6);
            y += kRowH;
        };

        addHdr("FILTER");
        addSlider("Cutoff",    cutoffSlider);
        addSlider("Resonance", resSlider);
        addCombo ("Mode",      filterModeBox);
        y += 8;

        addHdr("OSCILLATOR");
        addCombo ("Wave",      waveBox);
        addSlider("Detune",    detuneSlider);
        y += 8;

        addHdr("PERFORMANCE");
        addSlider("Output",    outputSlider);
        addSlider("Glide",     glideSlider);
        addSlider("Vel/VCA",   velMixSlider);
        addSlider("PB (MPE)",  pbRangeSlider);
        addSlider("PB (std)",  nonMPEPBSlider);
        y += 8;

        addHdr("MACROS");
        addMacroRow("Breath",  breathSrcBox, breathCCSlider);
        addMacroRow("Expr",    exprSrcBox,   exprCCSlider);
        y += 4;  // bottom padding

        setSize(cW, y);
    }

    void paint(juce::Graphics& g) override
    {
        for (auto& r : labelRecs) {
            if (r.isHeader) {
                g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
                g.setColour(juce::Colour(0xff897f6c));
                g.drawText(r.text, r.x, r.y, r.w, kSecHdrH,
                           juce::Justification::centredLeft);
            } else {
                g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
                g.setColour(juce::Colour(0xffccc4b4));
                g.drawText(r.text, r.x, r.y, r.w, kRowH,
                           juce::Justification::centredLeft);
            }
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContentComponent)
};

// ── PatchPanel ────────────────────────────────────────────────────────────────

PatchPanel::PatchPanel(VaneProcessor& p) : processor(p)
{
    content = std::make_unique<ContentComponent>(p);
    viewport.setViewedComponent(content.get(), false);
    viewport.setScrollBarsShown(true, false);  // vertical only
    viewport.setScrollBarThickness(8);
    addAndMakeVisible(viewport);
}

PatchPanel::~PatchPanel() = default;

void PatchPanel::paint(juce::Graphics& g)
{
    // Eyebrow label matching ModMatrixEditor's style
    g.setColour(juce::Colour(0xff897f6c));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    g.drawText("PATCH PARAMETERS",
               getLocalBounds().removeFromTop(kHdrH).reduced(2, 0),
               juce::Justification::centredLeft);
}

void PatchPanel::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(kHdrH);
    viewport.setBounds(bounds);

    const int cW = viewport.getMaximumVisibleWidth();
    if (cW > 0)
        content->layout(cW);
}
