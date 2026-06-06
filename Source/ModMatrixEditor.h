#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include "Modulation/ModMatrix.h"

class VaneProcessor;

// MMStack-style modulation matrix editor.
//
// Shows all 11 default routes as horizontal rows inside a scrolling panel.
// Each row: coloured dot · source label · live meter bar · → · dest label
//           · amount slider (APVTS-wired, live) · curve badge
//
// Amount sliders are attached directly to APVTS parameters — changes take
// effect on the next audio block and are saved/loaded with presets.
//
// Live meter bars read from the processor's relaxed-atomic meters (same
// source as the main expression meters panel), updated at 20 Hz.
//
// ── Thread model ──────────────────────────────────────────────────────────
// Everything here runs on the message thread.  Meter atomics are written by
// the audio thread and read here with relaxed ordering (stale-by-one-tick
// is imperceptible for a visual meter).
class ModMatrixEditor : public juce::Component,
                        private juce::Timer {
public:
    explicit ModMatrixEditor(VaneProcessor&);
    ~ModMatrixEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // ── Per-route data ────────────────────────────────────────────────────
    struct RouteDesc {
        int                  source;       // ModSourceID constant
        int                  dest;         // ModDestID constant
        juce::String         sourceLabel;  // short display name ("Breath")
        juce::String         destLabel;    // destination display name ("Cutoff")
        juce::Colour         colour;       // source-specific palette colour
        juce::String         paramId;      // APVTS param ID for the amount
        juce::String         curveParamId; // APVTS param ID for the curve (0=lin,1=exp,2=S)
        ModRoute::CurveShape curve;        // fallback when curveParam is null
        float                atkMs;        // slew attack (display only)
        float                relMs;        // slew release (display only)
        float                liveValue = 0.0f;     // updated each timer tick
        std::atomic<float>*  curveParam = nullptr;  // live curve pointer; may be null
    };

    // ── One route row ─────────────────────────────────────────────────────
    // Paints: dot, source/dest labels, live meter bar, curve badge.
    // Hosts the amount Slider as a child component.
    // Clicking the curve badge cycles lin → exp → S → lin.
    class RouteRow final : public juce::Component {
    public:
        explicit RouteRow(const RouteDesc& d) : desc(d)
        {
            addAndMakeVisible(amtSlider);
        }

        void paint(juce::Graphics&) override;
        void resized() override;
        void mouseDown(const juce::MouseEvent&) override;

        juce::Slider amtSlider;

    private:
        const RouteDesc& desc;

        // Shared column constants (all in row-local coordinates)
        static constexpr int kDotCX = 12;  // dot centre X
        static constexpr int kSrcX  = 20;  // source label start
        static constexpr int kSrcW  = 56;
        static constexpr int kMtrX  = 78;  // meter bar start
        static constexpr int kMtrW  = 38;
        static constexpr int kArrX  = 120; // arrow "→"
        static constexpr int kDstX  = 136; // dest label start
        static constexpr int kDstW  = 56;
        static constexpr int kSlrX  = 196; // slider start
        static constexpr int kCrvW  = 26;  // curve badge width
        static constexpr int kPadR  = 8;   // right padding

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RouteRow)
    };

    // ── Internals ─────────────────────────────────────────────────────────
    void timerCallback() override;
    void buildRoutes();
    float meterForSource(int sourceId) const;

    VaneProcessor& processor;

    std::vector<RouteDesc>     routeDescs;  // owned; rows hold const refs
    std::vector<std::unique_ptr<RouteRow>> rows;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;

    // container is declared before viewport so it outlives it (members are
    // destroyed in reverse declaration order).
    juce::Component container;
    juce::Viewport  viewport;

    static constexpr int kRowH = 28;  // row height including gap
    static constexpr int kPad  = 6;   // vertical padding inside container
    static constexpr int kHdrH = 18;  // header strip height

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModMatrixEditor)
};
