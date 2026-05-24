#include "ModMatrixEditor.h"
#include "PluginProcessor.h"

// ── File-local helpers ────────────────────────────────────────────────────────

static juce::Colour colourForSource(int src)
{
    if (src == ModSourceID::MacroBreath)    return juce::Colour(0xff4b86c7);
    if (src == ModSourceID::MacroExpr)      return juce::Colour(0xff2d9d8a);
    if (src == ModSourceID::MacroPressure)  return juce::Colour(0xffd28330);
    if (src == ModSourceID::MacroSlide)     return juce::Colour(0xffa35bbf);
    if (src == ModSourceID::MacroPitchbend) return juce::Colour(0xffc39529);
    if (src == ModSourceID::Velocity)       return juce::Colour(0xff7a4cff);
    return juce::Colour(0xff897f6c);
}

static const char* curveName(ModRoute::CurveShape c)
{
    if (c == ModRoute::CurveShape::Exponential) return "exp";
    if (c == ModRoute::CurveShape::SCurve)      return "S";
    return "lin";
}

// ── RouteRow::paint ───────────────────────────────────────────────────────────

void ModMatrixEditor::RouteRow::paint(juce::Graphics& g)
{
    const int h  = getHeight();
    const int cy = h / 2;

    // Row background
    g.setColour(juce::Colour(0xff1d1b16));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);

    // Source colour dot
    g.setColour(desc.colour);
    g.fillEllipse((float)(kDotCX - 4), (float)(cy - 4), 8.0f, 8.0f);

    // Source label (uppercase, 10 px)
    g.setColour(juce::Colour(0xffe8e1d2));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText(desc.sourceLabel.toUpperCase(),
               juce::Rectangle<int>(kSrcX, 0, kSrcW, h),
               juce::Justification::centredLeft, true);

    // Live meter bar
    {
        const auto track = juce::Rectangle<float>((float)kMtrX, (float)(cy - 3),
                                                   (float)kMtrW, 6.0f);
        g.setColour(juce::Colour(0xff14130f));
        g.fillRoundedRectangle(track, 3.0f);
        const float fillW = juce::jlimit(0.0f, (float)kMtrW, desc.liveValue * (float)kMtrW);
        if (fillW >= 1.5f) {
            g.setColour(desc.colour.withAlpha(0.75f));
            g.fillRoundedRectangle(track.withWidth(fillW), 3.0f);
        }
    }

    // Arrow (→ = U+2192, UTF-8: 0xe2 0x86 0x92)
    g.setColour(juce::Colour(0xff5a5345));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
    g.drawText(juce::String::fromUTF8("\xe2\x86\x92"),
               juce::Rectangle<int>(kArrX, 0, kDstX - kArrX, h),
               juce::Justification::centred);

    // Dest label (11 px)
    g.setColour(juce::Colour(0xffccc4b4));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText(desc.destLabel,
               juce::Rectangle<int>(kDstX, 0, kDstW, h),
               juce::Justification::centredLeft, true);

    // Curve badge — right of slider area
    const int curveX = getWidth() - kCrvW - kPadR;
    g.setColour(juce::Colour(0xff2a2520));
    g.fillRoundedRectangle((float)curveX, (float)(cy - 7), (float)kCrvW, 14.0f, 4.0f);
    g.setColour(juce::Colour(0xff897f6c));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    g.drawText(curveName(desc.curve),
               juce::Rectangle<int>(curveX, 0, kCrvW, h),
               juce::Justification::centred);
}

void ModMatrixEditor::RouteRow::resized()
{
    // Slider: from kSlrX to just before the curve badge
    const int slrEnd = getWidth() - kCrvW - kPadR - 4;
    const int slrH   = 14;
    const int slrY   = (getHeight() - slrH) / 2;
    amtSlider.setBounds(kSlrX, slrY, juce::jmax(0, slrEnd - kSlrX), slrH);
}

// ── ModMatrixEditor ───────────────────────────────────────────────────────────

ModMatrixEditor::ModMatrixEditor(VaneProcessor& p)
    : processor(p)
{
    buildRoutes();

    // container is the scrolled content; viewport takes a non-owning reference.
    // container is declared before viewport in the class, so it outlives it.
    viewport.setViewedComponent(&container, false);
    viewport.setScrollBarsShown(true, false);  // vertical scroll bar only
    viewport.setScrollBarThickness(8);
    addAndMakeVisible(viewport);

    startTimerHz(20);
}

ModMatrixEditor::~ModMatrixEditor()
{
    stopTimer();
    // Clear attachments before rows are destroyed (SliderAttachment must be
    // destroyed before its Slider).
    attachments.clear();
}

// ── buildRoutes ───────────────────────────────────────────────────────────────

void ModMatrixEditor::buildRoutes()
{
    using M = ModSourceID;
    using D = ModDestID;
    using C = ModRoute::CurveShape;

    // Mirrors the 11 default routes added in VaneProcessor::VaneProcessor().
    // Index in this table == route index in the ModMatrix (determines which
    // per-voice slewer slot is used).  Keep in sync with PluginProcessor.cpp.
    struct Spec {
        int src, dst;
        const char* srcLbl;
        const char* dstLbl;
        const char* paramId;
        C    curve;
        float atk, rel;
    };

    const Spec specs[] = {
        //  src                  dst               srcLbl      dstLbl       paramId           curve       atk  rel
        { M::MacroBreath,   D::VCALevel,    "Breath",   "VCA",    "breathVCAamt",    C::Linear,      5,  80 },
        { M::MacroExpr,     D::VCALevel,    "Expr",     "VCA",    "exprVCAamt",      C::Linear,      5,  80 },
        { M::MacroPressure, D::VCALevel,    "Pressure", "VCA",    "pressVCAamt",     C::Linear,      3,  50 },
        { M::MacroSlide,    D::FilterCutoff,"Slide",    "Cutoff", "cc74CutoffAmt",   C::Linear,      2,  20 },
        { M::MacroBreath,   D::FilterCutoff,"Breath",   "Cutoff", "breathCutoffAmt", C::Exponential, 5,  80 },
        { M::MacroExpr,     D::FilterCutoff,"Expr",     "Cutoff", "breathCutoffAmt", C::Exponential, 5,  80 },
        { M::MacroPressure, D::FilterCutoff,"Pressure", "Cutoff", "pressCutoffAmt",  C::Exponential, 2,  30 },
        { M::MacroBreath,   D::FilterRes,   "Breath",   "Reso",   "breathResAmt",    C::Exponential, 5,  80 },
        { M::MacroExpr,     D::FilterRes,   "Expr",     "Reso",   "breathResAmt",    C::Exponential, 5,  80 },
        { M::Velocity,      D::FilterCutoff,"Velocity", "Cutoff", "veloCutoffAmt",   C::Linear,     20,   0 },
        { M::MacroSlide,    D::FilterRes,   "Slide",    "Reso",   "cc74ResAmt",      C::Linear,      2,  20 },
    };

    routeDescs.reserve(std::size(specs));
    for (const auto& s : specs) {
        RouteDesc rd;
        rd.source      = s.src;
        rd.dest        = s.dst;
        rd.sourceLabel = s.srcLbl;
        rd.destLabel   = s.dstLbl;
        rd.colour      = colourForSource(s.src);
        rd.paramId     = s.paramId;
        rd.curve       = s.curve;
        rd.atkMs       = s.atk;
        rd.relMs       = s.rel;
        routeDescs.push_back(std::move(rd));
    }

    // Build row components and wire sliders to APVTS.
    // routeDescs must not reallocate after this point (rows hold const refs).
    auto& apvts = processor.apvts;
    for (auto& rd : routeDescs) {
        auto row = std::make_unique<RouteRow>(rd);

        auto& sl = row->amtSlider;
        sl.setSliderStyle(juce::Slider::LinearBar);
        sl.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        // Linear bar fill colour — JUCE V4 uses trackColourId for the filled part.
        sl.setColour(juce::Slider::trackColourId,      rd.colour.withAlpha(0.55f));
        sl.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff14130f));
        sl.setColour(juce::Slider::thumbColourId,      rd.colour);

        attachments.push_back(
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                apvts, rd.paramId, sl));

        container.addAndMakeVisible(row.get());
        rows.push_back(std::move(row));
    }
}

// ── Timer ─────────────────────────────────────────────────────────────────────

void ModMatrixEditor::timerCallback()
{
    // Update live meter values from processor atomics (relaxed: one tick stale is fine).
    for (auto& rd : routeDescs)
        rd.liveValue = meterForSource(rd.source);

    // Rows hold const refs into routeDescs, so the updated liveValue is visible
    // immediately.  Just request a repaint.
    for (auto& row : rows)
        row->repaint();
}

float ModMatrixEditor::meterForSource(int sourceId) const
{
    if (sourceId == ModSourceID::MacroBreath)
        return processor.meterBreath.load(std::memory_order_relaxed);
    if (sourceId == ModSourceID::MacroExpr)
        return processor.meterExpr.load(std::memory_order_relaxed);
    if (sourceId == ModSourceID::MacroPressure)
        return processor.meterPressure.load(std::memory_order_relaxed);
    if (sourceId == ModSourceID::MacroSlide)
        return processor.meterSlide.load(std::memory_order_relaxed);
    if (sourceId == ModSourceID::MacroPitchbend) {
        // Pitchbend is -1..1; remap to 0..1 for the meter bar
        const float pb = processor.meterPitchbend.load(std::memory_order_relaxed);
        return (pb + 1.0f) * 0.5f;
    }
    // Velocity is an event-based value, not a continuous signal — no meter
    return 0.0f;
}

// ── Layout ────────────────────────────────────────────────────────────────────

void ModMatrixEditor::paint(juce::Graphics& g)
{
    // Header eyebrow
    auto hdr = getLocalBounds().removeFromTop(kHdrH);
    g.setColour(juce::Colour(0xff897f6c));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    g.drawText("MODULATION ROUTES", hdr.reduced(2, 0), juce::Justification::centredLeft);
}

void ModMatrixEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(kHdrH);
    viewport.setBounds(bounds);

    // Size the container to fit all rows with padding
    const int cW     = viewport.getMaximumVisibleWidth();
    const int totalH = kPad + (int)rows.size() * kRowH + kPad;
    container.setSize(cW, totalH);

    // Position each row within the container
    const int rowW = cW - kPad * 2;
    for (int i = 0; i < (int)rows.size(); ++i)
        rows[i]->setBounds(kPad, kPad + i * kRowH, rowW, kRowH - 2);
}
