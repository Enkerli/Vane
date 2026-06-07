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
#include <atomic>
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
    int                        lastChordRotSent { -1 };   // dedupe the rotation-index push
    bool                       lastMtsState  { false };   // dedupe tuningStatus pushes
    juce::String               lastTuningName;            // dedupe — also catches in-session preset changes
    int                        armedAux { -1 };          // aux index being MIDI-learned

    void sendControllerState();   // aux source bindings + labels → controllerState
    void sendProfileList();       // saved controller profiles → profileList
    void sendMidiProbe();         // diagnostics: host MIDI stats + CoreMIDI sources
    void sendWavetableInfo (bool ok);   // active wavetable name + frame count → UI
    void sendTuningState();             // full tuning state (source, name, deviation table) → UI
    void sendWavetableStrip();          // overview filmstrip of the table's frames → UI
    void sendLibrary();                 // factory library catalogue → UI
    void sendTransientList();           // transient sample names → UI
    void sendGlideCurve();              // glide trajectory anchors → UI
    void sendChordSeqs();               // rotating-chord interval sequences → UI
    void sendChordConfigList();         // global rotating-chord palette → UI
    std::unique_ptr<juce::FileChooser> fileChooser;   // kept alive during async pick
    bool                       probeOn { false };   // diagnostics panel active
    int                        probeTick { 0 };     // throttle source enum to ~2 Hz
    std::atomic<bool>          pageReady   { false };
    bool                       pageNavigated { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WebVaneEditor)
};
