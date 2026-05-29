#pragma once

// WebVaneEditor — JUCE 8 WebBrowserComponent-based plugin editor.
//
// Renders the Vane UI (Source/WebUI/index.html, compiled into BinaryData by
// juce_add_binary_data(VaneWebUI …)) inside a WKWebView, served by a resource
// provider — no local server.  Mirrors DrawnQurve's WebCurveEditor.
//
// Bridge (event names per design_handoff README §2):
//   C++ → JS : emitEventIfBrowserIsVisible("paramChanged"|"meters"|"slotState"|
//              "presetList"|"tuningStatus"|"controllerLabel", var)
//   JS → C++ : withEventListener("setParam"|"slotEdit"|"presetLoad"|"presetSave"|
//              "presetDelete"|"panic"|"reconnectMts"|"uiReady"|"log", cb)
//
// Friendly UI keys (Morph, Cutoff, …) ↔ APVTS IDs (oscMorphPos, filterCutoff …)
// are mapped entirely on the C++ side (see kParamMap in the .cpp) so the bundled
// HTML stays close to the design prototype.

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "../PluginProcessor.h"

class WebVaneEditor : public juce::AudioProcessorEditor,
                      private juce::AudioProcessorValueTreeState::Listener,
                      private juce::Timer
{
public:
    explicit WebVaneEditor (VaneProcessor& p);
    ~WebVaneEditor() override;

    void paint (juce::Graphics&) override {}
    void resized() override;

    // AUv3 hosts parent the view AFTER construction — navigate then.
    void parentHierarchyChanged() override;

private:
    void parameterChanged (const juce::String& paramID, float newValue) override;
    void timerCallback() override;

    void navigateIfNeeded();
    static std::optional<juce::WebBrowserComponent::Resource>
        provideResource (const juce::String& path);
    static juce::WebBrowserComponent::Options buildOptions (WebVaneEditor* owner);

    // Bridge helpers
    void sendInitialState();   // on uiReady: params + slots + presets + tuning
    void sendSlotState();
    void sendPresetList();
    void emitParam (const juce::String& apvtsId);   // → paramChanged (friendly key)

    VaneProcessor&             proc;
    juce::WebBrowserComponent  webView;
    juce::StringArray          listenedParams;
    juce::String               lastNoteSent;   // dedupe the ♪ note readout
    std::atomic<bool>          pageReady   { false };
    bool                       pageNavigated { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebVaneEditor)
};
