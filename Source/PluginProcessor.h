#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cstdint>
#include "Synth/SynthVoice.h"
#include "Synth/FormantFilter.h"
#include "Synth/TransientLibrary.h"
#include "Modulation/ModMatrix.h"
#include "MPE/TuningClient.h"
#include "Preset/PresetManager.h"
#include "Preset/ChordConfigStore.h"
#include "Profile/ProfileManager.h"

class VaneProcessor : public juce::AudioProcessor {
public:
    VaneProcessor();
    ~VaneProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
#if VANE_HEADLESS
    bool hasEditor() const override { return false; }   // no GUI on Linux/MODEP
#else
    bool hasEditor() const override { return true; }
#endif

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool supportsMPE()  const override { return true; }
    double getTailLengthSeconds() const override { return 0.5; }

    // No host programs are exposed (single default).  The headless build used to
    // expose the .vanepreset library as LV2 "programs", but on MODEP those load
    // by re-reading the files at runtime from the home of the user mod-host runs
    // as ('modep', not where the files live), so they only ever appeared as
    // broken "Factory" entries.  Presets ship as self-contained LV2 User-preset
    // bundles instead (Tools/PresetExport/); GUI builds use the WebView browser.
    int  getNumPrograms() override                             { return 1; }
    int  getCurrentProgram() override                         { return 0; }
    void setCurrentProgram(int) override                      {}
    const juce::String getProgramName(int) override           { return "Default"; }
    void changeProgramName(int, const juce::String&) override { }

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager;
    ChordConfigStore chordConfigStore;   // global rotating-chord palette (JSON on disk)
    ProfileManager profileManager;
    ModMatrix    modMatrix;
    TuningClient tuning;

    // ── Editor meter values ────────────────────────────────────────────────────
    // Written on the audio thread (processBlock / MPE voice callbacks).
    // Read on the message thread (editor timer) — lock-free via relaxed atomics.
    // CC-based sources: updated every processBlock from the mod matrix.
    // MPE per-voice sources: written by the most recently active SynthVoice;
    // "last voice wins" is acceptable for visual meters.
    std::atomic<float> meterBreath    { 0.0f };   // CC2,  0..1
    std::atomic<float> meterExpr      { 0.0f };   // CC11, 0..1
    std::atomic<float> meterPressure  { 0.0f };   // MPE Z channel pressure, 0..1
    std::atomic<float> meterSlide     { 0.0f };   // CC74 / MPE Y timbre, 0..1
    std::atomic<float> meterPitchbend { 0.0f };   // MPE pitchbend, -1..1 (signed)
    // Latched the first time a breath/expression CC or channel pressure arrives.
    // Synthetic breath in Auto mode steps aside once it is set — see
    // docs/sequencer-playability.md and BreathEnvelope.h.
    std::atomic<bool> sawRealExpression { false };

    std::atomic<float> meterVelocity  { 0.0f };   // last note-on velocity, 0..1 (held)
    std::atomic<float> meterMorph     { 0.0f };   // live modulated morph 0..1 (for the WT display)

    // ── Real spectrum analyser ─────────────────────────────────────────────────
    // FFT of the actual output → log-spaced magnitude bins (0..1, −80..0 dB) for
    // the UI Spectrum view.  Computed on the audio thread (cheap, throttled to
    // once per FFT window); the editor reads the atomic bins.
    static constexpr int kSpecOrder = 10;            // 1024-point FFT
    static constexpr int kSpecSize  = 1 << kSpecOrder;
    static constexpr int kSpecBins  = 64;            // log-spaced display bins
    std::array<std::atomic<float>, kSpecBins> specBins {};
    void spectrumSnapshot (juce::Array<juce::var>& out) const {
        for (auto& b : specBins) out.add ((double) b.load (std::memory_order_relaxed));
    }

    // Fill `out` with the current morph frame's waveform (n points, ±1), and
    // report the fractional frame + frame count — drives the live WT display.
    void wavetableDisplay (juce::Array<juce::var>& out, int n, float& frameOut, int& framesOut) const;

    // Fill `cols` with up to numCols evenly-spaced frame waveforms (each `pts`
    // points, ±1) — a static overview filmstrip of the whole table.
    void wavetableFilmstrip (juce::Array<juce::var>& cols, int numCols, int pts, int& framesOut) const;

    // ── Per-voice MPE expression snapshot (for the per-note visualiser) ─────────
    // Filled each processBlock (audio thread) from each SynthVoice's live state;
    // read by the editor timer (message thread).  Index = MPESynthesiser voice.
    struct VoiceMeter {
        std::atomic<float> level    { 0.0f };   // 0 = off; >0 = sounding (fades on release)
        std::atomic<float> noteHz   { 0.0f };
        std::atomic<float> pressure { 0.0f };   // 0..1  (MPE Z)
        std::atomic<float> slide    { 0.5f };   // 0..1  (CC74 / Y)
        std::atomic<float> bend     { 0.0f };   // -1..1 (X)
        std::atomic<float> velocity { 0.0f };   // 0..1
    };
    static constexpr int kNumVoices = 15;
    std::array<VoiceMeter, kNumVoices> voiceMeters;

    // ── Aux-source MIDI-learn ──────────────────────────────────────────────────
    // The editor arms an aux index (ccLearnArm = 0..NumAux-1); the audio thread
    // captures the next incoming CC number into ccLearnResult and disarms.  The
    // editor polls the result, writes the aux{g}_cc param, and clears it.
    std::atomic<int> ccLearnArm    { -1 };
    std::atomic<int> ccLearnResult { -1 };

    // ── Rig routing (Stage 2b) ─────────────────────────────────────────────────
    // Per-channel role masks compiled from the rig by the UI.  A channel's bit in
    // routeNotesMask gates whether its note-ons sound; in routeModMask whether its
    // CC/pressure feeds the mod matrix.  Default 0xFFFF = accept everything (no
    // behaviour change).  This is what makes "sequencer notes + wind mods" work:
    // notes on the sequencer's channel, modulation on the wind's.
    std::atomic<uint16_t> routeNotesMask { 0xFFFF };
    std::atomic<uint16_t> routeModMask   { 0xFFFF };
    void setMidiRouting (int notes, int mod) {
        routeNotesMask.store ((uint16_t) (notes & 0xFFFF), std::memory_order_relaxed);
        routeModMask  .store ((uint16_t) (mod   & 0xFFFF), std::memory_order_relaxed);
    }

    // ── MIDI probe (diagnostics) ───────────────────────────────────────────────
    // Measures what the HOST routes to us in processBlock: how many events, and
    // which channels they arrive on.  Pairs with the editor's CoreMIDI source
    // enumeration to answer "can a rig instance be matched to a device, or only
    // to a channel?" on each platform (esp. AUv3 in AUM).  Audio-thread writes
    // are relaxed atomics; the editor reads + resets from the message thread.
    struct MidiProbe {
        std::atomic<uint64_t> events     { 0 };   // total channel-voice events seen
        std::atomic<uint32_t> channelMask { 0 };  // bit (ch-1) set if channel ch seen
    };
    MidiProbe midiProbe;
    void resetMidiProbe() {
        midiProbe.events.store (0, std::memory_order_relaxed);
        midiProbe.channelMask.store (0, std::memory_order_relaxed);
    }

    // ── Virtual MIDI input ports (experiment) ──────────────────────────────────
    // Vane publishes named CoreMIDI virtual inputs ("Vane Notes", "Vane Mod") so a
    // host's routing matrix (AUM, apeMatrix…) can send specific sources to specific
    // ports — port == role.  Tests whether an AUv3 can create virtual ports and
    // receive MIDI tagged by port (vs the host's merged, channel-only stream).
    // Counts only; feeding the synth is 2b.  ASCII names only — non-ASCII through
    // juce::String(const char*) traps on iOS.
    struct VPort : juce::MidiInputCallback {
        std::atomic<uint64_t> events      { 0 };
        std::atomic<uint32_t> channelMask { 0 };
        std::unique_ptr<juce::MidiInput> dev;
        juce::String name;
        bool created() const { return dev != nullptr; }
        void open (const juce::String& n) {
            name = n;
            dev = juce::MidiInput::createNewDevice (n, this);
            if (dev) dev->start();
        }
        void close() { if (dev) { dev->stop(); dev.reset(); } }
        void reset() { events.store (0, std::memory_order_relaxed);
                       channelMask.store (0, std::memory_order_relaxed); }
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override {
            events.fetch_add (1, std::memory_order_relaxed);
            const int ch = m.getChannel();
            if (ch >= 1 && ch <= 16) channelMask.fetch_or (1u << (ch - 1), std::memory_order_relaxed);
        }
    };
    std::array<VPort, 2> vports;
    bool vportsOpen { false };
    void startVirtualPorts() {
        if (vportsOpen) return;
        const char* names[] = { "Vane Notes", "Vane Mod" };
        for (int i = 0; i < 2; ++i) vports[(size_t) i].open (names[i]);
        vportsOpen = true;
    }
    void stopVirtualPorts() { for (auto& v : vports) v.close(); vportsOpen = false; }
    void resetVirtualPorts() { for (auto& v : vports) v.reset(); }

    // ── Direct-open experiment ─────────────────────────────────────────────────
    // The OTHER route to per-message identity on iOS: since enumeration works,
    // open every visible source ourselves and count per-source.  Tests whether an
    // AUv3 may open named sources directly, and exposes double-delivery (a source
    // also arrives in the host's merged stream).  Counts only; feeding = 2b.
    struct OpenedSrc : juce::MidiInputCallback {
        std::atomic<uint64_t> events { 0 };
        std::unique_ptr<juce::MidiInput> dev;
        juce::String name;
        bool ok { false };
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override {
            events.fetch_add (1, std::memory_order_relaxed);
        }
    };
    std::vector<std::unique_ptr<OpenedSrc>> openedSrcs;
    bool srcsOpen { false };
    void openAllSources() { srcsOpen = true; refreshOpenSources(); }
    void refreshOpenSources() {   // open any visible source we haven't yet (~2 Hz)
        if (! srcsOpen) return;
        for (const auto& d : juce::MidiInput::getAvailableDevices()) {
            bool have = false;
            for (auto& s : openedSrcs) if (s->name == d.name) { have = true; break; }
            if (have) continue;
            auto s = std::make_unique<OpenedSrc>();
            s->name = d.name;
            s->dev  = juce::MidiInput::openDevice (d.identifier, s.get());
            if (s->dev) { s->dev->start(); s->ok = true; }
            openedSrcs.push_back (std::move (s));
        }
    }
    void closeAllSources() { for (auto& s : openedSrcs) if (s->dev) s->dev->stop();
                             openedSrcs.clear(); srcsOpen = false; }
    void resetOpenedSources() { for (auto& s : openedSrcs) s->events.store (0, std::memory_order_relaxed); }

    void reconnectMTS()      { tuning.reconnect(); }
    bool mtsConnected() const { return tuning.hasMaster(); }

    // All-notes-off / reset — called from the UI "panic" bridge event.
    void panic() { synth.turnOffAllVoices (false); }

    // ── Wavetable loading (Stage 2c) ───────────────────────────────────────────
    // Loaded tables are kept alive for the session (no frees while the audio
    // thread may still read the previous pointer); activeWavetable is the one all
    // voices currently read.  loadWavetable() builds on the calling (message)
    // thread and hands the new pointer to every voice.
    // ── Factory wavetable library ─────────────────────────────────────────────
    struct LibraryEntry {
        juce::String id, file, title, family, morphIntent, license;
        juce::StringArray tags;
        int frameCount { 0 };
    };
    const juce::Array<LibraryEntry>& getLibrary() const { return library; }
    // Load a factory table by id (from BinaryData); returns false on bad id.
    bool loadLibraryTable (const juce::String& id);

    // ── Editable mod-curve anchors (Bezier mod-curves) ─────────────────────────
    // Anchors persist in apvts.state as "modSlotN_anchors" = "x,y;x,y;...".
    static std::vector<std::pair<float, float>> parseAnchors (const juce::String& s);
    static juce::String serializeAnchors (const std::vector<std::pair<float, float>>& pts);
    // Store a slot's anchors in state and rebuild its ModMatrix curve LUT.
    void setSlotCurveAnchors (int slot, const std::vector<std::pair<float, float>>& pts);
    // Re-apply every slot's stored anchors to the matrix (call after state load).
    void restoreAllSlotCurves();

    // ── Glide curve (Bezier glide mode) ────────────────────────────────────────
    // The glide trajectory curve; anchors persist in state "glideAnchors".
    void setGlideAnchors (const std::vector<std::pair<float, float>>& pts);
    void restoreGlideCurve();   // rebuild glideLUT from state (call after load)
    const float* glideCurveLUT() const { return glideLUT.data(); }

    // ── Rotating chords (chord-mode unison) ────────────────────────────────────
    // Per harmony voice (kMaxUnison-1): an interval sequence + loop length.  Stored
    // in state "chordSeqs" as ";"-separated voices, each a ","-list of intervals.
    // An interval token is either decimal semitones ("7", "7.02", "-5") or a just
    // ratio ("3:2", "5/4") — ratios are converted to (fractional) semitones, so a
    // 3:2 fifth is the exact 701.96c, not a rounded 700c.
    void setChordSeqs (const juce::String& serialized);
    void restoreChordSeqs();    // rebuild from state (call after load)
    juce::String chordSeqsString() const;   // serialize current sequences for the UI

    // Parse a single chord-interval token to semitones.  Accepts decimal semitones
    // or a just ratio "a:b" / "a/b" (-> 12·log2(a/b)).  Returns NaN if unparseable
    // (empty, zero/negative ratio term, etc.) so callers can skip it.  Static +
    // public so it is unit-testable without driving the whole processor.
    static float parseChordInterval (const juce::String& token);

    // Live rotation index for the UI playhead.  chordRotIndex is the NEXT step to
    // play; chordRotPlayed is the step actually SOUNDING (set at note-on).  The UI
    // wants the latter, so the highlight matches what you hear, not the step to come.
    int  chordRotation() const { return chordRotPlayed.load (std::memory_order_relaxed); }
    void resetChordRotation()  { chordRotIndex.store (0, std::memory_order_relaxed);
                                 chordRotPlayed.store (0, std::memory_order_relaxed); }

    // Live modulation-output value for destination d (ModDestID order) — Stage Outputs.
    float modOutValue (int d) const {
        return (d >= 0 && d < ModDestID::NumDests) ? modOut[(size_t) d].load (std::memory_order_relaxed) : 0.0f;
    }

    bool loadWavetable (const juce::File& file);           // load + embed in state
    bool setWavetableFromData (const juce::MemoryBlock&);   // build from bytes + embed
    // Load from already-read bytes + a display name.  iOS picks files as
    // security-scoped URLs (no usable local path), so the editor reads the URL
    // into memory and hands it here.
    bool loadWavetableFromData (const juce::MemoryBlock& mb, const juce::String& name) {
        activeWtName = name.isNotEmpty() ? name : juce::String ("Wavetable");
        return setWavetableFromData (mb);
    }
    void restoreWavetableFromState();                      // rebuild from embedded state
    void applyWavetableToVoices();
    // Revert to the built-in Harmonic Stack (drops any loaded table).
    void useBuiltInWavetable() {
        activeWavetable = &Wavetable::builtInDefault();
        activeWavetableHold.reset();   // release our reference to any loaded table
        activeWtName    = "Harmonic Stack (built-in)";
        activeWtFrames  = activeWavetable->numFrames();
        lastWtData.reset();
        apvts.state.removeProperty ("wavetableData", nullptr);
        apvts.state.removeProperty ("wavetableHash", nullptr);
        apvts.state.removeProperty ("wavetableName", nullptr);
        applyWavetableToVoices();
    }
    juce::String wavetableName()   const { return activeWtName; }
    int          wavetableFrames() const { return activeWtFrames; }
    bool         wavetablePhaseAlign() const { return wtPhaseAlign; }
    // Toggle phase-aligned morphing; rebuilds the loaded table from its kept bytes
    // (built-in is already phase-coherent so it's a no-op there).
    void setMorphPhaseAlign (bool on) {
        wtPhaseAlign = on;
        apvts.state.setProperty ("wavetablePhaseAlign", on, nullptr);
        if (lastWtData.getSize() > 0) setWavetableFromData (lastWtData);
    }

private:
    juce::Array<LibraryEntry>               library;          // parsed once at construction
    void parseLibraryManifest();
    // Active loaded table: shared_ptr keeps this instance's table alive; the
    // build itself is shared process-wide (see buildOrGetCachedWavetable).  Null
    // when the built-in default is active (that one is a static singleton).
    std::shared_ptr<const Wavetable>        activeWavetableHold;
    const Wavetable* activeWavetable { &Wavetable::builtInDefault() };
    juce::String activeWtName { "Harmonic Stack (built-in)" };
    int          activeWtFrames { 16 };
    bool         wtPhaseAlign { false };
    juce::MemoryBlock lastWtData;   // raw .wav bytes of the loaded table (rebuild + state embed)
public:

    // Hz of the most recently started note (for the UI ♪ note readout).
    float currentNoteHz() const { return lastNoteHz.load(); }

private:
    juce::MPESynthesiser synth;

    // SynthVoice* for each synth voice, cast once at construction so processBlock
    // can read per-voice meter state without a per-block dynamic_cast.
    std::vector<SynthVoice*> voicePtrs;

    // Shared across all voices: the Hz of the most recently started note.
    // New voices read this in noteStarted() to glide from the previous pitch
    // when glideTime > 0. Monophonic controllers only — MPE poly is unaffected
    // because players set glideTime = 0 for poly use.
    std::atomic<float> lastNoteHz { 0.0f };

    // Shared VCA level: the smoothed VCA amplitude of the most recently active voice.
    // New voices initialise their smoothedVCA here so legato note transitions start
    // at the current breath level rather than snapping in from zero.
    std::atomic<float> lastVCALevel { 0.0f };

    // Legato voice-kill counter. Incremented by each new legato note; old voices
    // compare their stored generation against this and die immediately at the top
    // of their next renderNextBlock, eliminating the two-voice phase-beating artifact.
    std::atomic<uint32_t> legatoGeneration { 0 };

    // Phase of the most recently active oscillator, published at end of every block.
    // New legato voices read this + advance one phaseInc to continue the waveform
    // without a phase discontinuity (audible as a click at the note boundary).
    std::atomic<float> lastOscPhase { 0.0f };

    // Phase of the inharmonicity FM modulator, published alongside lastOscPhase.
    // Handed to new legato voices so the √2-ratio modulator stays continuous across
    // the note boundary — without this it jumps, stepping the FM sidebands (a subtle
    // discontinuity, masked by the inharmonic spectrum but real).
    std::atomic<float> lastPmPhase { 0.0f };

    // Filter state for legato handoff.  Even with perfect oscillator phase sync,
    // switching to a new voice with different SVF integrator states (s1, s2) causes
    // a step-response transient (filter ringing) audible as a multi-sample click.
    // Publishing s1/s2 + the exact cutoff Hz lets the new voice prime its filter
    // coefficients and restore the state before its first process() call.
    std::atomic<float> lastFilterS1   { 0.0f };
    std::atomic<float> lastFilterS2   { 0.0f };
    std::atomic<float> lastCutoffHz   { 1200.0f };
    // Stereo-unison cross-voice handoff: the detuned osc phases + right filter state.
    std::array<std::atomic<float>, SynthVoice::kMaxUnison - 1> lastUnisonPhase {};
    std::atomic<float> lastFilterRS1  { 0.0f };
    std::atomic<float> lastFilterRS2  { 0.0f };
    // Waveguide bore handoff: the last voice that rendered waveguide audio.
    // A mono-legato successor copies its bores at noteStarted (audio thread only).
    std::atomic<SynthVoice*> lastWaveguideVoice { nullptr };
    // Glide-curve LUT (Bezier glide), shared read-only with the voices.
    std::array<float, ModRoute::kCurveLUT> glideLUT {};
    // Rotating-chord sequences (fixed-capacity so audio-thread reads are realloc-safe).
    // Semitones (fractional, to preserve just-ratio precision), row-major per voice.
    std::array<std::array<float, SynthVoice::kChordSteps>, SynthVoice::kMaxUnison - 1> chordSeq {};
    std::array<int, SynthVoice::kMaxUnison - 1> chordLen {};
    std::atomic<int> chordRotIndex  { 0 };   // next step to play
    std::atomic<int> chordRotPlayed { 0 };   // step currently sounding (UI playhead)
    // Live modulation OUTPUTS (mods[NumDests]) for the Stage Outputs view — written
    // by the sounding voice, decayed toward 0 each block when silent (processBlock).
    std::array<std::atomic<float>, ModDestID::NumDests> modOut {};

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Reconstruct generic mod slots from a pre-slot preset's legacy route params.
    void migrateLegacyRoutingToSlots(const juce::XmlElement& loadedState);

    // Spectrum analyser internals (audio thread).
    juce::dsp::FFT               specFFT { kSpecOrder };
    std::array<float, kSpecSize>     specAccum {};
    std::array<float, kSpecSize * 2> specWork {};
    std::array<float, kSpecSize>     specWindow {};
    int                          specPos { 0 };
    bool                         specReady { false };
    void pushSpectrumSamples (const float* x, int n);
    void computeSpectrum();

    // ── Transient sample library ───────────────────────────────────────────────
    // Constructed once; all voices hold a const pointer into it.
    // Never mutated after the processor constructor returns.
    TransientLibrary transientLib;
public:
    const TransientLibrary& getTransientLibrary() const { return transientLib; }
private:

    // Cached raw parameter pointers — safe to read on the audio thread.
    // Initialised in the constructor after the APVTS is constructed.
    std::atomic<float>* pOutputLevel      = nullptr;

    // Global vowel/formant stage (post-mix, in series after the synth).  Global —
    // not per-voice — so one persistent filter keeps mono legato seamless.
    std::atomic<float>* pVowelEnable      = nullptr;
    std::atomic<float>* pVowelPos         = nullptr;   // = Open axis (F1) / Wah sweep
    std::atomic<float>* pVowelFront       = nullptr;   // Front axis (F2)
    std::atomic<float>* pVowelRound       = nullptr;   // Round axis (lip rounding)
    std::atomic<float>* pVowelAmount      = nullptr;
    std::atomic<float>* pVowelReso        = nullptr;
    std::atomic<float>* pVowelMove        = nullptr;
    std::atomic<float>* pVowelMode        = nullptr;   // 0 = Vowel, 1 = Wah
    FormantFilter       formantL, formantR;

    // Macro source bindings — resolved each processBlock via setMacroSlot().
    // Breath: 0 = CC (pMacroBreathCC), 1 = Aftertouch, 2 = MPE Pressure.
    std::atomic<float>* pMacroBreathSrc   = nullptr;
    std::atomic<float>* pMacroBreathCC    = nullptr;
    // Expression: 0 = CC (pMacroExprCC), 1 = Aftertouch.
    std::atomic<float>* pMacroExprSrc     = nullptr;
    std::atomic<float>* pMacroExprCC      = nullptr;

    // Aux global-source CC bindings (controller profiles) — cached pointers,
    // pushed to the ModMatrix each processBlock.
    std::array<std::atomic<float>*, ModSlots::NumAux> pAuxCC {};

    // Per-block smoothed output gain to avoid clicks during automation.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VaneProcessor)
};
