// WebVaneEditor.cpp — JUCE 8 WebBrowserComponent editor for Vane.
// The UI bundle (Source/WebUI/index.html) is compiled into BinaryData by
// juce_add_binary_data(VaneWebUI …) and served by the resource provider.

#include "WebVaneEditor.h"
#include <BinaryDataWebUI.h>
#include "../Modulation/ModSource.h"

// ── Friendly UI key ↔ APVTS id map ──────────────────────────────────────────────
namespace {
struct ParamPair { const char* ui; const char* apvts; };
const ParamPair kParamMap[] = {
    { "Morph",   "oscMorphPos" }, { "PW",      "oscPW"      }, { "Fold",   "oscFold"     },
    { "Inharm",  "oscInharm"   }, { "Noise",   "noiseBlend" }, { "NoiseType","noiseType" },
    { "Detune",  "oscDetune"   }, { "Cutoff",  "filterCutoff" }, { "Reso", "filterRes"  },
    { "Mode",    "filterMode"  }, { "Output",  "outputLevel" }, { "VelVCA", "velocityMix" },
    { "Glide",   "glideTime"   }, { "GlideMode","glideMode"  }, { "GlideCurve","glideCurve" },
    { "MasterTune","masterTune"}, { "monoMode","monoMode"    },
};
juce::String uiToApvts (const juce::String& ui) {
    for (auto& p : kParamMap) if (ui == p.ui) return p.apvts;
    return ui;   // pass-through (e.g. already an APVTS id)
}
juce::String apvtsToUi (const juce::String& apvts) {
    for (auto& p : kParamMap) if (apvts == p.apvts) return p.ui;
    return {};   // empty = not a mapped patch param
}
juce::var makeObj (std::initializer_list<std::pair<juce::String, juce::var>> pairs) {
    auto obj = std::make_unique<juce::DynamicObject>();
    for (auto& [k, v] : pairs) obj->setProperty (k, v);
    return juce::var (obj.release());
}
} // namespace

// ── Resource provider ───────────────────────────────────────────────────────────
std::optional<juce::WebBrowserComponent::Resource>
WebVaneEditor::provideResource (const juce::String& path)
{
    auto serve = [] (const char* data, int size, juce::String mime)
        -> juce::WebBrowserComponent::Resource {
        std::vector<std::byte> bytes (static_cast<std::size_t> (size));
        std::memcpy (bytes.data(), data, static_cast<std::size_t> (size));
        return { std::move (bytes), std::move (mime) };
    };

    if (path == "/" || path == "/index.html")
        return serve (BinaryData::index_html, BinaryData::index_htmlSize,
                      "text/html; charset=utf-8");
    return std::nullopt;
}

// ── Options builder (bridge listeners) ──────────────────────────────────────────
juce::WebBrowserComponent::Options WebVaneEditor::buildOptions (WebVaneEditor* owner)
{
    using juce::WebBrowserComponent;
    using juce::var;
    using juce::Array;
    auto& apvts = owner->proc.apvts;

    auto setActual = [&apvts] (const juce::String& id, float actual) {
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id)))
            p->setValueNotifyingHost (p->convertTo0to1 (actual));
    };

    return WebBrowserComponent::Options{}
        .withResourceProvider ([] (const juce::String& path)
            { return WebVaneEditor::provideResource (path); },
            WebBrowserComponent::getResourceProviderRoot())
        .withNativeIntegrationEnabled()
       #if JUCE_MAC
        .withKeepPageLoadedWhenBrowserIsHidden()
       #endif
        .withEventListener ("log", [] (const Array<var>& a) {
            if (a.isEmpty()) return;
            std::fprintf (stderr, "[js:%s] %s\n",
                a[0]["level"].toString().toRawUTF8(), a[0]["msg"].toString().toRawUTF8());
        })
        .withEventListener ("uiReady", [owner] (const Array<var>&) {
            owner->pageReady = true;
            juce::MessageManager::callAsync ([owner] { owner->sendInitialState(); });
        })
        // Patch params: {id: friendly|apvts, value: actual}
        .withEventListener ("setParam", [owner, setActual] (const Array<var>& a) {
            if (a.isEmpty()) return;
            setActual (uiToApvts (a[0]["id"].toString()),
                       static_cast<float> (static_cast<double> (a[0]["value"])));
        })
        // Mod slot: {slot, src, dst, amt, curve, atk, rel, on}
        // Backend currently persists src/dst/amt/curve; atk/rel/on are UI-side.
        .withEventListener ("slotEdit", [owner, setActual] (const Array<var>& a) {
            if (a.isEmpty()) return;
            const auto& o = a[0];
            const int n = static_cast<int> (o["slot"]);
            if (n < 0 || n >= ModSlots::NumSlots) return;
            const juce::String b = "modSlot" + juce::String (n);
            setActual (b + "_src",   static_cast<float> (static_cast<int> (o["src"])));
            setActual (b + "_dst",   static_cast<float> (static_cast<int> (o["dst"])));
            setActual (b + "_amt",   static_cast<float> (static_cast<double> (o["amt"])));
            setActual (b + "_curve", static_cast<float> (static_cast<int> (o["curve"])));
        })
        .withEventListener ("presetLoad", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            const int id = static_cast<int> (a[0]["id"]);
            auto names = owner->proc.presetManager.getPresetNames();
            if (id >= 0 && id < names.size()) {
                owner->proc.presetManager.loadPreset (names[id]);
                owner->sendSlotState();     // patch params echo via parameterChanged
            }
        })
        .withEventListener ("presetSave", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            const auto name = a[0]["name"].toString();
            if (name.isNotEmpty()) owner->proc.presetManager.savePreset (name);
            owner->sendPresetList();
        })
        .withEventListener ("presetDelete", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            const int id = static_cast<int> (a[0]["id"]);
            auto names = owner->proc.presetManager.getPresetNames();
            if (id >= 0 && id < names.size())
                owner->proc.presetManager.deletePreset (names[id]);
            owner->sendPresetList();
        })
        // Double-click a patch thumb → reset that param to its declared default.
        .withEventListener ("resetParam", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            const auto id = uiToApvts (a[0]["id"].toString());
            if (auto* p = owner->proc.apvts.getParameter (id))
                p->setValueNotifyingHost (p->getDefaultValue());
        })
        .withEventListener ("panic",        [owner] (const Array<var>&) { owner->proc.panic(); })
        .withEventListener ("reconnectMts", [owner] (const Array<var>&) {
            owner->proc.reconnectMTS();
            owner->webView.emitEventIfBrowserIsVisible ("tuningStatus",
                makeObj ({ { "connected", owner->proc.mtsConnected() } }));
        });
}

// ── Construction ────────────────────────────────────────────────────────────────
WebVaneEditor::WebVaneEditor (VaneProcessor& p)
    : AudioProcessorEditor (p), proc (p), webView (buildOptions (this))
{
    addAndMakeVisible (webView);
    setResizable (true, true);
    setResizeLimits (640, 420, 2200, 1600);
    setSize (920, 600);

    for (auto* param : proc.apvts.processor.getParameters())
        if (auto* pwid = dynamic_cast<juce::AudioProcessorParameterWithID*> (param)) {
            proc.apvts.addParameterListener (pwid->paramID, this);
            listenedParams.add (pwid->paramID);
        }

    juce::MessageManager::callAsync ([this] { navigateIfNeeded(); });
    startTimerHz (30);
}

WebVaneEditor::~WebVaneEditor()
{
    stopTimer();
    for (const auto& id : listenedParams)
        proc.apvts.removeParameterListener (id, this);
}

void WebVaneEditor::resized()                 { webView.setBounds (getLocalBounds()); }

void WebVaneEditor::parentHierarchyChanged()
{
    AudioProcessorEditor::parentHierarchyChanged();
    if (getParentComponent() != nullptr) navigateIfNeeded();
}

void WebVaneEditor::navigateIfNeeded()
{
    if (pageNavigated) return;
    pageNavigated = true;
    webView.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
}

// ── Outbound: meters at 30 Hz ─────────────────────────────────────────────────
static juce::String hzToNote (float hz)
{
    if (hz < 20.0f) return "—";
    const int midi = static_cast<int> (std::lround (69.0 + 12.0 * std::log2 (hz / 440.0)));
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}

void WebVaneEditor::timerCallback()
{
    if (! pageReady) return;
    webView.emitEventIfBrowserIsVisible ("meters", makeObj ({
        { "Breath",     proc.meterBreath.load() },
        { "Expression", proc.meterExpr.load() },
        { "Pressure",   proc.meterPressure.load() },
        { "Slide",      proc.meterSlide.load() },
        { "Pitchbend",  (proc.meterPitchbend.load() + 1.0f) * 0.5f },
        { "Velocity",   0.0f },
    }));

    // ♪ note readout — emit only when it changes.
    const auto note = hzToNote (proc.currentNoteHz());
    if (note != lastNoteSent) {
        lastNoteSent = note;
        webView.emitEventIfBrowserIsVisible ("voice", makeObj ({ { "note", note } }));
    }

    // Real MTS-ESP connection state — push on change (the chip reflects this,
    // never a faked UI toggle).
    const bool mts = proc.mtsConnected();
    if (mts != lastMtsState) {
        lastMtsState = mts;
        webView.emitEventIfBrowserIsVisible ("tuningStatus", makeObj ({ { "connected", mts } }));
    }
}

// ── Outbound: param echo ──────────────────────────────────────────────────────
void WebVaneEditor::parameterChanged (const juce::String& paramID, float newValue)
{
    if (! pageReady) return;
    juce::MessageManager::callAsync ([this, paramID, newValue] {
        if (! pageReady) return;
        const auto ui = apvtsToUi (paramID);
        if (ui.isNotEmpty())
            webView.emitEventIfBrowserIsVisible ("paramChanged",
                makeObj ({ { "id", ui }, { "value", newValue } }));
    });
}

void WebVaneEditor::emitParam (const juce::String& apvtsId)
{
    const auto ui = apvtsToUi (apvtsId);
    if (ui.isEmpty()) return;
    if (auto* raw = proc.apvts.getRawParameterValue (apvtsId))
        webView.emitEventIfBrowserIsVisible ("paramChanged",
            makeObj ({ { "id", ui }, { "value", raw->load() } }));
}

// ── Outbound: slot snapshot ───────────────────────────────────────────────────
void WebVaneEditor::sendSlotState()
{
    juce::Array<juce::var> slots;
    for (int n = 0; n < ModSlots::NumSlots; ++n) {
        const juce::String b = "modSlot" + juce::String (n);
        auto get = [&] (const char* s) {
            auto* p = proc.apvts.getRawParameterValue (b + s); return p ? p->load() : 0.0f; };
        slots.add (makeObj ({
            { "src",   static_cast<int> (get ("_src")) },
            { "dst",   static_cast<int> (get ("_dst")) },
            { "amt",   get ("_amt") },
            { "curve", static_cast<int> (get ("_curve")) },
        }));
    }
    webView.emitEventIfBrowserIsVisible ("slotState", makeObj ({ { "slots", juce::var (slots) } }));
}

// ── Outbound: preset list ─────────────────────────────────────────────────────
void WebVaneEditor::sendPresetList()
{
    auto names = proc.presetManager.getPresetNames();
    juce::Array<juce::var> arr;
    for (auto& n : names) arr.add (makeObj ({ { "name", n }, { "ctrl", "" }, { "scale", "" } }));
    const int sel = juce::jmax (0, names.indexOf (proc.presetManager.getCurrentPresetName()));
    webView.emitEventIfBrowserIsVisible ("presetList",
        makeObj ({ { "presets", juce::var (arr) }, { "selected", sel } }));
}

void WebVaneEditor::sendInitialState()
{
    for (auto& p : kParamMap) emitParam (p.apvts);
    sendSlotState();
    sendPresetList();
    webView.emitEventIfBrowserIsVisible ("tuningStatus",
        makeObj ({ { "connected", proc.mtsConnected() } }));
}
