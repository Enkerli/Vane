// WebVaneEditor.cpp — JUCE 8 WebBrowserComponent editor for Vane.
// The UI bundle (Source/WebUI/index.html) is compiled into BinaryData by
// juce_add_binary_data(VaneWebUI …) and served by the resource provider.

// Headless builds (Linux / MODEP) drop the WebView entirely so WebKitGTK is not
// a build dependency; this whole TU compiles to nothing there.
#if !VANE_HEADLESS

#include "WebVaneEditor.h"
#include <BinaryDataWebUI.h>
#include "../Modulation/ModSource.h"
#include <cstring>

// ── Friendly UI key ↔ APVTS id map ──────────────────────────────────────────────
namespace {
struct ParamPair { const char* ui; const char* apvts; };
const ParamPair kParamMap[] = {
    { "Morph",   "oscMorphPos" }, { "PW",      "oscPW"      }, { "Fold",   "oscFold"     },
    { "Inharm",  "oscInharm"   }, { "Sync",    "oscSync"    },
    { "Noise",   "noiseBlend" }, { "NoiseType","noiseType" },
    { "Detune",  "oscDetune"   }, { "Cutoff",  "filterCutoff" }, { "Reso", "filterRes"  },
    { "Mode",    "filterMode"  }, { "Output",  "outputLevel" }, { "VelVCA", "velocityMix" },
    { "Glide",   "glideTime"   }, { "GlideMode","glideMode"  }, { "GlideCurve","glideCurve" },
    { "MasterTune","masterTune"}, { "monoMode","monoMode"    },
    { "UniVox",  "unisonVoices"}, { "UniDet","unisonDetune"  }, { "UniWid","unisonWidth" },
    { "UniMode", "unisonMode"  },
    { "TrGain",  "transientGain"  }, { "TrDecay","transientDecay"  },
    { "TrChoice","transientChoice"}, { "TrTrigger","transientTrigger"},
    { "TrVar",   "transientVariation" }, { "TrFilt","transientFilter" },
    { "TrDyn",   "transientDynamics" },
    { "TrReso",  "transientResonate" }, { "TrDamp","transientDamping" }, { "TrMorph","transientMorph" },
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
    // prependBom: emit a UTF-8 BOM before the payload.  The BOM is the
    // highest-priority character-encoding signal in HTML — it overrides the
    // HTTP Content-Type charset, the <meta charset>, and any locale default.
    // WKWebView (macOS) infers UTF-8 fine, but WebKitGTK (Linux/MODEP-Pi) does
    // not always honour our charset and falls back to the system locale codec
    // (→ mojibake: "♪" shown as "â™ª").  The BOM forces UTF-8 everywhere,
    // including the inline <script> string literals (decoded with the document).
    auto serve = [] (const char* data, int size, juce::String mime,
                     bool prependBom = false)
        -> juce::WebBrowserComponent::Resource {
        static constexpr unsigned char kBom[] = { 0xEF, 0xBB, 0xBF };
        const std::size_t pre = prependBom ? sizeof (kBom) : 0;
        std::vector<std::byte> bytes (pre + static_cast<std::size_t> (size));
        if (pre != 0)
            std::memcpy (bytes.data(), kBom, pre);
        std::memcpy (bytes.data() + pre, data, static_cast<std::size_t> (size));
        return { std::move (bytes), std::move (mime) };
    };

    if (path == "/" || path == "/index.html")
        return serve (BinaryData::index_html, BinaryData::index_htmlSize,
                      "text/html; charset=utf-8", /*prependBom*/ true);
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
            setActual (b + "_scale", static_cast<float> (static_cast<int> (o["scale"])));   // mod-of-mod
            // Per-slot enable: the UI toggle.  Without this, disabling a route in
            // the matrix had no effect on the audio (the route kept modulating).
            setActual (b + "_en",    (bool) o["on"] ? 1.0f : 0.0f);
            // Editable response-curve anchors → engine LUT + state persistence.
            std::vector<std::pair<float, float>> pts;
            if (auto* arr = o["anchors"].getArray())
                for (auto& p : *arr)
                    pts.emplace_back (static_cast<float> (static_cast<double> (p["x"])),
                                      static_cast<float> (static_cast<double> (p["y"])));
            owner->proc.setSlotCurveAnchors (n, pts);
        })
        // Glide trajectory curve (Bézier glide mode): {anchors:[{x,y}…]}.
        .withEventListener ("glideCurveEdit", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            std::vector<std::pair<float, float>> pts;
            if (auto* arr = a[0]["anchors"].getArray())
                for (auto& p : *arr)
                    pts.emplace_back (static_cast<float> (static_cast<double> (p["x"])),
                                      static_cast<float> (static_cast<double> (p["y"])));
            owner->proc.setGlideAnchors (pts);
        })
        // Rotating-chord interval sequences (chord-mode unison): {seqs:"4,7;12,5;…"}.
        .withEventListener ("chordSeqsEdit", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            owner->proc.setChordSeqs (a[0]["seqs"].toString());
        })
        // ── Global rotating-chord config palette ──────────────────────────────
        .withEventListener ("chordConfigSave", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            const auto& o = a[0];
            ChordConfigStore::Config c;
            c.name   = o["name"].toString().trim();
            c.seqs   = o["seqs"].toString();
            c.voices = static_cast<int> (o["voices"]);
            c.mode   = static_cast<int> (o["mode"]);
            if (c.name.isNotEmpty()) { owner->proc.chordConfigStore.save (c); owner->sendChordConfigList(); }
        })
        .withEventListener ("chordConfigDelete", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            owner->proc.chordConfigStore.remove (a[0]["name"].toString());
            owner->sendChordConfigList();
        })
        .withEventListener ("chordConfigLoad", [owner, setActual] (const Array<var>& a) {
            if (a.isEmpty()) return;
            const auto name = a[0]["name"].toString();
            for (const auto& c : owner->proc.chordConfigStore.all()) {
                if (c.name != name) continue;
                // voice COUNT -> unisonVoices choice index ({1,2,3,4,6}).
                static const int kVoiceCounts[] = { 1, 2, 3, 4, 6 };
                int idx = 1;
                for (int i = 0; i < 5; ++i) if (kVoiceCounts[i] == c.voices) idx = i;
                setActual ("unisonVoices", static_cast<float> (idx));
                setActual ("unisonMode",   static_cast<float> (c.mode));
                owner->proc.setChordSeqs (c.seqs);
                owner->sendChordSeqs();   // push the sequences last so the UI lands on them
                break;
            }
        })
        .withEventListener ("requestChordConfigs", [owner] (const Array<var>&) {
            owner->sendChordConfigList();
        })
        .withEventListener ("chordResetRotation", [owner] (const Array<var>&) {
            owner->proc.resetChordRotation();
        })
        .withEventListener ("presetLoad", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            const int id = static_cast<int> (a[0]["id"]);
            auto names = owner->proc.presetManager.getPresetNames();
            if (id >= 0 && id < names.size()) {
                owner->proc.presetManager.loadPreset (names[id]);
                owner->proc.restoreWavetableFromState();   // preset's table (or built-in)
                owner->proc.restoreAllSlotCurves();        // rebuild mod-curve LUTs
                owner->proc.restoreGlideCurve();           // rebuild glide trajectory LUT
                owner->sendGlideCurve();
                owner->proc.restoreChordSeqs();            // rebuild rotating-chord sequences
                owner->sendChordSeqs();
                owner->sendSlotState();      // patch params echo via parameterChanged
                owner->sendControllerState(); // the preset carries its own bindings
                owner->sendWavetableInfo (true);
                owner->sendPresetList();      // refresh the selected/current name
                // Preset↔profile association: if the patch was built for a saved
                // profile that isn't the active one, offer to apply it.
                const auto pp = owner->proc.apvts.state.getProperty ("profileName", "").toString();
                const bool suggest = pp.isNotEmpty()
                                  && pp != owner->proc.profileManager.getCurrentProfileName()
                                  && owner->proc.profileManager.getProfileNames().contains (pp);
                owner->webView.emitEventIfBrowserIsVisible ("profileSuggest",
                    makeObj ({ { "name", suggest ? juce::var (pp) : juce::var() } }));
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
            if (auto* p = owner->proc.apvts.getParameter (id)) {
                p->setValueNotifyingHost (p->getDefaultValue());
                owner->emitParam (id);   // push the reset value straight to the UI
            }
        })
        .withEventListener ("panic",        [owner] (const Array<var>&) { owner->proc.panic(); })
        // Portable text export/import of the CURRENT patch (full APVTS state).
        .withEventListener ("requestExport", [owner] (const Array<var>&) {
            const auto xml = owner->proc.apvts.copyState().toXmlString();
            owner->webView.emitEventIfBrowserIsVisible ("exportData", makeObj ({ { "text", xml } }));
        })
        .withEventListener ("importState", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            if (auto x = juce::parseXML (a[0]["text"].toString()))
                if (x->hasTagName (owner->proc.apvts.state.getType())) {
                    owner->proc.apvts.replaceState (juce::ValueTree::fromXml (*x));
                    juce::MessageManager::callAsync ([owner] { owner->sendInitialState(); });
                }
        })
        .withEventListener ("reconnectMts", [owner] (const Array<var>&) {
            owner->proc.reconnectMTS();
            owner->sendTuningState();
        })
        // ── Tuning source + internal tuning control ───────────────────────────
        .withEventListener ("setTuningSource", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            const auto s = a[0]["source"].toString();
            if      (s == "mts")      owner->proc.tuning.setTuningSource (TuningSource::FollowMTS);
            else if (s == "internal") owner->proc.tuning.setTuningSource (TuningSource::Internal);
            else                      owner->proc.tuning.setTuningSource (TuningSource::Bypass);
            owner->proc.apvts.state.setProperty ("tuningSource", s, nullptr);
            owner->sendTuningState();
        })
        .withEventListener ("setInternalTuning", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            const auto id = a[0]["id"].toString();
            owner->proc.tuning.setInternalTuning (id);
            owner->proc.apvts.state.setProperty ("internalTuningId", id, nullptr);
            owner->sendTuningState();
        })
        .withEventListener ("requestTuningState", [owner] (const Array<var>&) {
            owner->sendTuningState();
        })
        // ── MIDI probe (diagnostics) ───────────────────────────────────────────
        .withEventListener ("requestMidiProbe", [owner] (const Array<var>&) {
            owner->probeOn = true;          // start including the probe in timer emits
            owner->proc.startVirtualPorts(); // publish "Vane Notes"/"Vane Mod" to route to
            owner->proc.openAllSources();    // direct-open every visible source
            owner->sendMidiProbe();
        })
        .withEventListener ("stopMidiProbe", [owner] (const Array<var>&) {
            owner->probeOn = false;
            owner->proc.stopVirtualPorts();
            owner->proc.closeAllSources();
        })
        .withEventListener ("resetMidiProbe", [owner] (const Array<var>&) {
            owner->proc.resetMidiProbe();
            owner->proc.resetVirtualPorts();
            owner->proc.resetOpenedSources();
            owner->sendMidiProbe();
        })
        // ── Controller profile: aux source binding + MIDI-learn ────────────────
        .withEventListener ("requestControllerState", [owner] (const Array<var>&) {
            owner->sendControllerState();
        })
        .withEventListener ("auxLearn", [owner] (const Array<var>& a) {       // arm learn
            if (a.isEmpty()) return;
            const int aux = static_cast<int> (a[0]["aux"]);
            if (aux < 0 || aux >= ModSlots::NumAux) return;
            owner->armedAux = aux;
            owner->proc.ccLearnResult.store (-1, std::memory_order_relaxed);
            owner->proc.ccLearnArm.store (aux, std::memory_order_relaxed);
        })
        .withEventListener ("auxLearnCancel", [owner] (const Array<var>&) {
            owner->proc.ccLearnArm.store (-1, std::memory_order_relaxed);
            owner->armedAux = -1;
        })
        .withEventListener ("setAuxCC", [owner] (const Array<var>& a) {       // typed bind
            if (a.isEmpty()) return;
            const int aux = static_cast<int> (a[0]["aux"]);
            const int cc  = juce::jlimit (0, 127, static_cast<int> (a[0]["cc"]));
            if (aux < 0 || aux >= ModSlots::NumAux) return;
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (
                              owner->proc.apvts.getParameter ("aux" + juce::String (aux) + "_cc")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) cc));
        })
        .withEventListener ("setAuxLabel", [owner] (const Array<var>& a) {    // rename
            if (a.isEmpty()) return;
            const int aux = static_cast<int> (a[0]["aux"]);
            if (aux < 0 || aux >= ModSlots::NumAux) return;
            owner->proc.apvts.state.setProperty ("auxLabel" + juce::String (aux),
                                                 a[0]["label"].toString(), nullptr);
        })
        .withEventListener ("setController", [owner] (const Array<var>& a) {  // controller name
            if (a.isEmpty()) return;
            owner->proc.apvts.state.setProperty ("controllerName", a[0]["name"].toString(), nullptr);
        })
        .withEventListener ("setRig", [owner] (const Array<var>& a) {         // rig structure (JSON)
            if (a.isEmpty()) return;
            owner->proc.apvts.state.setProperty ("rig", a[0]["rig"].toString(), nullptr);
        })
        // ── Wavetable load (.wav) ──────────────────────────────────────────────
        .withEventListener ("loadWavetable", [owner] (const Array<var>&) {
            owner->fileChooser = std::make_unique<juce::FileChooser> (
                "Load wavetable (.wav)", juce::File(), "*.wav;*.WAV");
            owner->fileChooser->launchAsync (
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [owner] (const juce::FileChooser& fc) {
                    // Read via the URL, not getResult(): on iOS the pick is a
                    // security-scoped URL with no usable local file path.
                    const auto url = fc.getURLResult();
                    bool ok = false;
                    if (! url.isEmpty()) {
                        juce::MemoryBlock mb;
                        if (auto in = url.createInputStream (juce::URL::InputStreamOptions (
                                          juce::URL::ParameterHandling::inAddress)))
                            in->readIntoMemoryBlock (mb);
                        if (mb.getSize() > 0)
                            ok = owner->proc.loadWavetableFromData (
                                     mb, url.getFileName().upToLastOccurrenceOf (".", false, false));
                    }
                    owner->sendWavetableInfo (ok);
                    owner->fileChooser.reset();
                });
        })
        .withEventListener ("requestWavetableInfo", [owner] (const Array<var>&) {
            owner->sendWavetableInfo (true);
        })
        .withEventListener ("useBuiltinWavetable", [owner] (const Array<var>&) {
            owner->proc.useBuiltInWavetable();
            owner->sendWavetableInfo (true);
        })
        .withEventListener ("loadLibraryTable", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            const bool ok = owner->proc.loadLibraryTable (a[0]["id"].toString());
            owner->sendWavetableInfo (ok);
            owner->sendLibrary();   // refresh so the active badge updates
        })
        .withEventListener ("requestLibrary", [owner] (const Array<var>&) {
            owner->sendLibrary();
        })
        .withEventListener ("setMorphPhaseAlign", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            owner->proc.setMorphPhaseAlign (static_cast<bool> (a[0]["on"]));
            owner->sendWavetableInfo (true);
        })
        .withEventListener ("setMidiRouting", [owner] (const Array<var>& a) { // per-channel role masks
            if (a.isEmpty()) return;
            owner->proc.setMidiRouting ((int) a[0]["notesMask"], (int) a[0]["modMask"]);
        })
        .withEventListener ("setMacroCC", [owner] (const Array<var>& a) {     // breath/expr CC# bind
            if (a.isEmpty()) return;
            const auto which = a[0]["which"].toString();
            const int  cc    = juce::jlimit (0, 127, static_cast<int> (a[0]["cc"]));
            const char* id   = which == "expr" ? "macroExprCC" : "macroBreathCC";
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (owner->proc.apvts.getParameter (id)))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) cc));
        })
        // ── Controller profiles (save/load named binding sets) ─────────────────
        .withEventListener ("requestProfileList", [owner] (const Array<var>&) {
            owner->sendProfileList();
        })
        .withEventListener ("saveProfile", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            const auto name = a[0]["name"].toString();
            if (name.isNotEmpty()) owner->proc.profileManager.saveProfile (name);
            owner->sendProfileList();
        })
        .withEventListener ("loadProfile", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            owner->proc.profileManager.loadProfile (a[0]["name"].toString());
            // Refresh the UI: bindings, controller name, profile list.
            owner->sendControllerState();
            owner->webView.emitEventIfBrowserIsVisible ("controllerLabel",
                makeObj ({ { "name", owner->proc.apvts.state.getProperty ("controllerName", "Generic MPE").toString() } }));
            owner->sendProfileList();
        })
        .withEventListener ("deleteProfile", [owner] (const Array<var>& a) {
            if (a.isEmpty()) return;
            owner->proc.profileManager.deleteProfile (a[0]["name"].toString());
            owner->sendProfileList();
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
    // Empty when no note — never a non-ASCII char* (juce::String(const char*)
    // asserts on bytes > 127, which traps/aborts on iOS).  The JS shows "—" as
    // the placeholder for an empty note.
    if (hz < 20.0f) return {};
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
        { "Velocity",   proc.meterVelocity.load() },
    }));

    // ── Live aux-source values (configurable global CCs) → "auxMeters" ─────────
    {
        juce::Array<juce::var> av;
        for (int g = 0; g < ModSlots::NumAux; ++g) av.add (proc.modMatrix.auxValue (g));
        webView.emitEventIfBrowserIsVisible ("auxMeters", makeObj ({ { "v", juce::var (av) } }));
    }

    // ── Modulation OUTPUTS (mods[NumDests]) → "modOut" (Stage Outputs view) ────
    {
        juce::Array<juce::var> mo;
        for (int d = 0; d < ModDestID::NumDests; ++d) mo.add (proc.modOutValue (d));
        webView.emitEventIfBrowserIsVisible ("modOut", makeObj ({ { "v", juce::var (mo) } }));
    }

    // ── Per-voice MPE expression → "voices" (for the per-note visualiser) ──────
    {
        juce::Array<juce::var> vs;
        for (int i = 0; i < VaneProcessor::kNumVoices; ++i) {
            auto& m = proc.voiceMeters[i];
            const float lvl = m.level.load();
            if (lvl <= 0.001f) continue;               // only sounding voices
            const float hz = m.noteHz.load();
            vs.add (makeObj ({
                { "vi",       i },                 // stable per-voice id (fixes same-note collision)
                { "note",     hzToNote (hz) },
                { "hz",       hz },
                { "level",    lvl },
                { "pressure", m.pressure.load() },
                { "slide",    m.slide.load() },
                { "bend",     m.bend.load() },
                { "vel",      m.velocity.load() },
            }));
        }
        webView.emitEventIfBrowserIsVisible ("voices", makeObj ({ { "v", juce::var (vs) } }));
    }

    // ── Live wavetable frame waveform → WT display ─────────────────────────────
    {
        juce::Array<juce::var> pts; float frameF = 0.0f; int frames = 0;
        proc.wavetableDisplay (pts, 96, frameF, frames);
        webView.emitEventIfBrowserIsVisible ("wavetableWave",
            makeObj ({ { "pts", juce::var (pts) }, { "frame", frameF }, { "frames", frames } }));
    }

    // ── Real output spectrum → Spectrum view ───────────────────────────────────
    {
        juce::Array<juce::var> bins; proc.spectrumSnapshot (bins);
        webView.emitEventIfBrowserIsVisible ("spectrum", makeObj ({ { "bins", juce::var (bins) } }));
    }

    // ♪ note readout — emit only when it changes.
    const auto note = hzToNote (proc.currentNoteHz());
    if (note != lastNoteSent) {
        lastNoteSent = note;
        webView.emitEventIfBrowserIsVisible ("voice", makeObj ({ { "note", note } }));
    }

    // Live rotating-chord index → editor playhead (emit only on change).
    const int rot = proc.chordRotation();
    if (rot != lastChordRotSent) {
        lastChordRotSent = rot;
        webView.emitEventIfBrowserIsVisible ("chordRot", makeObj ({ { "rot", rot } }));
    }

    // Push tuning state whenever the connection state OR the active tuning name
    // changes.  Checking the name catches in-session tuning switches (Entonal
    // Studio changing presets while connected) which don't flip the connected bool.
    const bool   mts  = proc.mtsConnected();
    const auto   name = proc.tuning.tuningName();
    if (mts != lastMtsState || name != lastTuningName) {
        lastMtsState  = mts;
        lastTuningName = name;
        sendTuningState();
    }

    // MIDI probe (diagnostics): ~2 Hz while the panel is open.
    if (probeOn && (++probeTick % 15 == 0))
        sendMidiProbe();

    // MIDI-learn result: the audio thread captured a CC while armed → bind it.
    const int learned = proc.ccLearnResult.exchange (-1, std::memory_order_relaxed);
    if (learned >= 0 && armedAux >= 0) {
        const int aux = armedAux; armedAux = -1;
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (
                          proc.apvts.getParameter ("aux" + juce::String (aux) + "_cc")))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) learned));
        webView.emitEventIfBrowserIsVisible ("auxLearned",
            makeObj ({ { "aux", aux }, { "cc", learned } }));
    }
}

void WebVaneEditor::sendControllerState()
{
    juce::Array<juce::var> aux;
    for (int g = 0; g < ModSlots::NumAux; ++g) {
        int cc = 0;
        if (auto* p = proc.apvts.getRawParameterValue ("aux" + juce::String (g) + "_cc"))
            cc = static_cast<int> (p->load());
        const auto label = proc.apvts.state.getProperty (
                               "auxLabel" + juce::String (g), "Aux " + juce::String (g + 1)).toString();
        aux.add (makeObj ({ { "cc", cc }, { "label", label } }));
    }
    auto ccOf = [this] (const char* id, int dflt) {
        if (auto* p = proc.apvts.getRawParameterValue (id)) return (int) std::lround (p->load());
        return dflt; };
    webView.emitEventIfBrowserIsVisible ("controllerState", makeObj ({
        { "aux",        juce::var (aux) },
        { "breathCC",   ccOf ("macroBreathCC", 2) },
        { "exprCC",     ccOf ("macroExprCC", 11) },
        { "controller", proc.apvts.state.getProperty ("controllerName", "Generic MPE").toString() },
        { "rig",        proc.apvts.state.getProperty ("rig", "").toString() },
    }));
}

void WebVaneEditor::sendMidiProbe()
{
    // Path A — what the HOST routed to us (counted on the audio thread).
    const auto events = (double) proc.midiProbe.events.load (std::memory_order_relaxed);
    const auto mask   = proc.midiProbe.channelMask.load (std::memory_order_relaxed);
    juce::Array<juce::var> channels;
    for (int c = 0; c < 16; ++c)
        if (mask & (1u << c)) channels.add (c + 1);

    // Path B — what the SYSTEM exposes, and whether we can OPEN each source
    // directly (per-source event counts).  juce::MidiInput wraps CoreMIDI.
    proc.refreshOpenSources();   // catch sources connected after probe start
    juce::Array<juce::var> sources;
    if (proc.srcsOpen) {
        for (auto& s : proc.openedSrcs)
            sources.add (makeObj ({ { "name", s->name }, { "opened", s->ok },
                                    { "events", (double) s->events.load (std::memory_order_relaxed) } }));
    } else {
        for (const auto& d : juce::MidiInput::getAvailableDevices())
            sources.add (makeObj ({ { "name", d.name } }));
    }

    // Virtual ports we publish — did creation succeed, and what arrives on each?
    juce::Array<juce::var> vps;
    for (auto& v : proc.vports) {
        juce::Array<juce::var> vch;
        const auto vm = v.channelMask.load (std::memory_order_relaxed);
        for (int c = 0; c < 16; ++c) if (vm & (1u << c)) vch.add (c + 1);
        vps.add (makeObj ({
            { "name",     v.name.isNotEmpty() ? juce::var (v.name) : juce::var ("(uncreated)") },
            { "created",  v.created() },
            { "events",   (double) v.events.load (std::memory_order_relaxed) },
            { "channels", juce::var (vch) },
        }));
    }

    webView.emitEventIfBrowserIsVisible ("midiProbe", makeObj ({
        { "events",   events },
        { "channels", juce::var (channels) },
        { "sources",  juce::var (sources) },
        { "vports",   juce::var (vps) },
    }));
}

void WebVaneEditor::sendTuningState()
{
    const bool connected = proc.mtsConnected();
    const auto src = proc.tuning.getTuningSource();
    const juce::String srcStr = (src == TuningSource::Internal) ? "internal"
                              : (src == TuningSource::Bypass)   ? "off"
                              :                                    "mts";
    // 12-note deviation table (MIDI 60-71 = C4..B4) for the stage map.
    // The UI JS replicates this across octaves; 12 values is cheap to push.
    juce::Array<juce::var> devTable;
    for (int n = 60; n < 72; ++n)
        devTable.add (proc.tuning.deviationCents (n, 1));

    // Hole flags for the same 12 notes.
    juce::Array<juce::var> holeFlags;
    for (int n = 60; n < 72; ++n)
        holeFlags.add (proc.tuning.isHole (n, 1));

    webView.emitEventIfBrowserIsVisible ("tuningStatus", makeObj ({
        { "connected",    connected },
        { "source",       srcStr },
        { "name",         proc.tuning.tuningName() },
        { "internalId",   proc.tuning.getInternalTuningId() },
        { "devTable",     juce::var (devTable) },
        { "holeFlags",    juce::var (holeFlags) },
    }));
}

void WebVaneEditor::sendChordSeqs()
{
    webView.emitEventIfBrowserIsVisible ("chordSeqs",
        makeObj ({ { "seqs", proc.chordSeqsString() } }));
}

void WebVaneEditor::sendChordConfigList()
{
    juce::Array<juce::var> arr;
    for (const auto& c : proc.chordConfigStore.all())
        arr.add (makeObj ({ { "name",   c.name },
                            { "seqs",   c.seqs },
                            { "voices", c.voices },
                            { "mode",   c.mode } }));
    webView.emitEventIfBrowserIsVisible ("chordConfigList",
        makeObj ({ { "configs", juce::var (arr) } }));
}

void WebVaneEditor::sendGlideCurve()
{
    juce::Array<juce::var> anchors;
    const auto s = proc.apvts.state.getProperty ("glideAnchors", "").toString();
    for (const auto& p : VaneProcessor::parseAnchors (s))
        anchors.add (makeObj ({ { "x", p.first }, { "y", p.second } }));
    webView.emitEventIfBrowserIsVisible ("glideCurve",
        makeObj ({ { "anchors", juce::var (anchors) } }));
}

void WebVaneEditor::sendTransientList()
{
    juce::Array<juce::var> names;
    for (const auto& n : proc.getTransientLibrary().names())   // includes "None" at 0
        names.add (n);
    webView.emitEventIfBrowserIsVisible ("transientList",
        makeObj ({ { "names", juce::var (names) } }));
}

void WebVaneEditor::sendLibrary()
{
    juce::Array<juce::var> entries;
    const auto activeId = proc.apvts.state.getProperty ("wavetableLibraryId", "").toString();
    for (const auto& e : proc.getLibrary()) {
        juce::Array<juce::var> tags;
        for (auto& t : e.tags) tags.add (t);
        entries.add (makeObj ({
            { "id",          e.id },
            { "title",       e.title },
            { "family",      e.family },
            { "morphIntent", e.morphIntent },
            { "license",     e.license },
            { "frameCount",  e.frameCount },
            { "tags",        juce::var (tags) },
            { "active",      e.id == activeId },
        }));
    }
    webView.emitEventIfBrowserIsVisible ("libraryData",
        makeObj ({ { "tables", juce::var (entries) } }));
}

void WebVaneEditor::sendWavetableInfo (bool ok)
{
    webView.emitEventIfBrowserIsVisible ("wavetableInfo", makeObj ({
        { "name",   proc.wavetableName() },
        { "frames", proc.wavetableFrames() },
        { "phaseAlign", proc.wavetablePhaseAlign() },
        { "ok",     ok },
    }));
    if (ok) sendWavetableStrip();   // table changed → refresh the overview
}

void WebVaneEditor::sendWavetableStrip()
{
    juce::Array<juce::var> cols; int frames = 0;
    proc.wavetableFilmstrip (cols, 28, 24, frames);
    webView.emitEventIfBrowserIsVisible ("wavetableStrip",
        makeObj ({ { "cols", juce::var (cols) }, { "frames", frames } }));
}

void WebVaneEditor::sendProfileList()
{
    auto names = proc.profileManager.getProfileNames();
    juce::Array<juce::var> arr;
    for (auto& n : names) arr.add (n);
    webView.emitEventIfBrowserIsVisible ("profileList", makeObj ({
        { "profiles", juce::var (arr) },
        { "current",  proc.profileManager.getCurrentProfileName() },
    }));
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
        // Editable response-curve anchors (persisted in state, not a param).
        juce::Array<juce::var> anchors;
        const auto aStr = proc.apvts.state.getProperty (b + "_anchors", "").toString();
        for (const auto& p : VaneProcessor::parseAnchors (aStr))
            anchors.add (makeObj ({ { "x", p.first }, { "y", p.second } }));
        slots.add (makeObj ({
            { "src",   static_cast<int> (get ("_src")) },
            { "dst",   static_cast<int> (get ("_dst")) },
            { "amt",   get ("_amt") },
            { "curve", static_cast<int> (get ("_curve")) },
            { "scale", static_cast<int> (get ("_scale")) },
            { "on",    get ("_en") > 0.5f },
            { "anchors", juce::var (anchors) },
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
    const auto cur = proc.presetManager.getCurrentPresetName();
    const int  sel = names.indexOf (cur);   // -1 if unsaved / not found — do NOT default to 0
    webView.emitEventIfBrowserIsVisible ("presetList",
        makeObj ({ { "presets", juce::var (arr) }, { "selected", sel },
                   { "current", cur } }));
}

void WebVaneEditor::sendInitialState()
{
    for (auto& p : kParamMap) emitParam (p.apvts);
    sendSlotState();
    sendPresetList();
    sendControllerState();
    sendProfileList();
    sendWavetableInfo (true);
    sendLibrary();
    sendTransientList();
    sendGlideCurve();
    sendChordSeqs();
    sendChordConfigList();
    sendTuningState();
    webView.emitEventIfBrowserIsVisible ("controllerLabel",
        makeObj ({ { "name", proc.apvts.state.getProperty ("controllerName", "Generic MPE").toString() } }));
    // tuningStatus is now sent by sendTuningState() called above.
}

#endif  // !VANE_HEADLESS
