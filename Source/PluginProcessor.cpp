#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WebUI/WebVaneEditor.h"

VaneProcessor::VaneProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Vane", createParameterLayout()),
      presetManager(apvts),
      profileManager(apvts)
{
    // 15 voices: one per MPE member channel in the lower zone (channels 2–16).
    // MPESynthesiser has no "sound" concept — voices handle everything directly.
    //
    // All APVTS parameter pointers are obtained as LOCAL variables BEFORE the
    // voice-construction loop.  Do NOT use member variable pointers here.
    //
    // Pitfall: member pointers (pOutputLevel, pMacroBreathSrc …) are assigned
    // AFTER the voice loop below.  If you pass a member pointer into a voice
    // constructor, the voice receives the pre-assignment value — null — and
    // silently falls back to hard-coded defaults for pitchbend range, etc.
    // This was a real bug: all 15 voices played with ±2 st range regardless of
    // the parameter value because pPBRange was null inside every voice.
    auto* pMorphPos    = apvts.getRawParameterValue("oscMorphPos");
    auto* pPW          = apvts.getRawParameterValue("oscPW");
    auto* pDetune      = apvts.getRawParameterValue("oscDetune");
    auto* pCutoff      = apvts.getRawParameterValue("filterCutoff");
    auto* pRes         = apvts.getRawParameterValue("filterRes");
    auto* pFilterMode  = apvts.getRawParameterValue("filterMode");
    auto* pVeloMix     = apvts.getRawParameterValue("velocityMix");
    auto* pGlide       = apvts.getRawParameterValue("glideTime");
    auto* pMono        = apvts.getRawParameterValue("monoMode");
    auto* pPBRangeLocal       = apvts.getRawParameterValue("pitchbendRange");
    auto* pNonMPEPBRangeLocal = apvts.getRawParameterValue("nonMPEPBRange");
    auto* pGlideMode  = apvts.getRawParameterValue("glideMode");
    auto* pGlideCurve = apvts.getRawParameterValue("glideCurve");
    auto* pNoiseBlend = apvts.getRawParameterValue("noiseBlend");
    auto* pNoiseType  = apvts.getRawParameterValue("noiseType");
    auto* pFold       = apvts.getRawParameterValue("oscFold");
    auto* pInharm     = apvts.getRawParameterValue("oscInharm");
    auto* pSync       = apvts.getRawParameterValue("oscSync");
    for (int i = 0; i < 15; ++i)
        synth.addVoice(new SynthVoice(modMatrix, tuning,
                                      pMorphPos, pDetune, pPW, pCutoff, pRes, pFilterMode, pVeloMix,
                                      pGlide, &lastNoteHz, &lastVCALevel,
                                      &legatoGeneration, &lastOscPhase, &lastPmPhase, pMono,
                                      &lastFilterS1, &lastFilterS2, &lastCutoffHz,
                                      &meterPressure, &meterSlide, &meterPitchbend,
                                      pPBRangeLocal, pNonMPEPBRangeLocal,
                                      pGlideMode, pGlideCurve,
                                      pNoiseBlend, pNoiseType, pFold, pInharm, pSync));

    // Lower zone: channel 1 is master, channels 2–16 are member channels
    juce::MPEZoneLayout zone;
    zone.setLowerZone(15);
    synth.setZoneLayout(zone);

    // Cache SynthVoice* once (avoid per-block dynamic_cast) for the per-voice meters.
    for (int i = 0; i < synth.getNumVoices(); ++i)
        voicePtrs.push_back(dynamic_cast<SynthVoice*>(synth.getVoice(i)));

    pOutputLevel    = apvts.getRawParameterValue("outputLevel");
    pMacroBreathSrc = apvts.getRawParameterValue("macroBreathSrc");
    pMacroBreathCC  = apvts.getRawParameterValue("macroBreathCC");
    pMacroExprSrc   = apvts.getRawParameterValue("macroExprSrc");
    pMacroExprCC    = apvts.getRawParameterValue("macroExprCC");
    for (int g = 0; g < ModSlots::NumAux; ++g)
        pAuxCC[(size_t) g] = apvts.getRawParameterValue("aux" + juce::String(g) + "_cc");


    // ── Generic mod-slot pool — the routing source of truth ───────────────────
    // 24 configurable slots.  Factory routing lives in the slot param DEFAULTS
    // (see createParameterLayout); the old fixed routes were removed.  Pre-slot
    // presets are reconstructed into slots on load (see setStateInformation).
    for (int n = 0; n < ModSlots::NumSlots; ++n) {
        const juce::String base = "modSlot" + juce::String(n);
        modMatrix.addSlot(apvts.getRawParameterValue(base + "_src"),
                          apvts.getRawParameterValue(base + "_dst"),
                          apvts.getRawParameterValue(base + "_amt"),
                          apvts.getRawParameterValue(base + "_curve"));
    }

#if JUCE_DEBUG
    // Run unit tests on every Debug build so regressions surface immediately.
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();
    bool anyFailed = false;
    for (int i = 0; i < runner.getNumResults(); ++i) {
        if (auto* r = runner.getResult(i)) {
            DBG("[TEST] " << r->unitTestName << " / " << r->subcategoryName
                << ": " << r->passes << " passed, " << r->failures << " failed");
            if (r->failures > 0) anyFailed = true;
        }
    }
    jassert(!anyFailed);   // breaks in the debugger at the failing assertion
#endif
}

VaneProcessor::~VaneProcessor() { stopVirtualPorts(); closeAllSources(); }

void VaneProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
    modMatrix.prepare(sampleRate, samplesPerBlock);

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
            v->prepare(sampleRate, samplesPerBlock);

    // 20 ms ramp so output-level automation never causes a zipper click.
    masterGain.reset(sampleRate, 0.020);
    masterGain.setCurrentAndTargetValue(pOutputLevel ? pOutputLevel->load() : 1.0f);

    // Oscillator::prepare() reset each voice to the built-in default; re-apply the
    // loaded table (if any) so it survives a sample-rate change.
    applyWavetableToVoices();

    // Spectrum analyser: Hann window (sample-rate independent).
    for (int i = 0; i < kSpecSize; ++i)
        specWindow[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                         * (float) i / (float) (kSpecSize - 1));
    specPos = 0; specReady = false;
}

void VaneProcessor::pushSpectrumSamples (const float* x, int n)
{
    for (int i = 0; i < n; ++i) {
        specAccum[(size_t) specPos++] = x[i];
        if (specPos >= kSpecSize) { computeSpectrum(); specPos = 0; }
    }
}

void VaneProcessor::computeSpectrum()
{
    const double sr = getSampleRate();
    if (sr <= 0.0) return;

    for (int i = 0; i < kSpecSize; ++i) specWork[(size_t) i] = specAccum[(size_t) i] * specWindow[(size_t) i];
    std::fill (specWork.begin() + kSpecSize, specWork.end(), 0.0f);
    specFFT.performFrequencyOnlyForwardTransform (specWork.data());   // magnitudes in [0..kSpecSize/2]

    const float fmin = 30.0f;
    const float fmax = juce::jmax (8000.0f, (float) sr * 0.5f);
    const float norm = 2.0f / (float) kSpecSize;
    std::array<float, kSpecBins> binMax {};
    for (int k = 1; k <= kSpecSize / 2; ++k) {
        const float f = (float) k * (float) sr / (float) kSpecSize;
        if (f < fmin) continue;
        int b = (int) (std::log (f / fmin) / std::log (fmax / fmin) * (kSpecBins - 1) + 0.5f);
        b = juce::jlimit (0, kSpecBins - 1, b);
        binMax[(size_t) b] = juce::jmax (binMax[(size_t) b], specWork[(size_t) k]);
    }
    for (int b = 0; b < kSpecBins; ++b) {
        const float db = 20.0f * std::log10 (binMax[(size_t) b] * norm + 1.0e-9f);
        specBins[(size_t) b].store (juce::jlimit (0.0f, 1.0f, (db + 80.0f) / 80.0f),
                                    std::memory_order_relaxed);   // −80..0 dB → 0..1
    }
    specReady = true;
}

void VaneProcessor::applyWavetableToVoices()
{
    for (auto* v : voicePtrs)
        if (v != nullptr)
            v->setWavetable (activeWavetable);   // nullptr → built-in default (Oscillator falls back)
}

void VaneProcessor::wavetableDisplay (juce::Array<juce::var>& out, int n,
                                      float& frameOut, int& framesOut) const
{
    frameOut = 0.0f; framesOut = 0;
    const Wavetable* wt = activeWavetable;
    if (wt == nullptr || wt->numFrames() < 1) return;
    const int nf = wt->numFrames();
    framesOut = nf;

    // Live morph while a voice sounds; otherwise the knob position.
    bool sounding = false;
    for (auto* v : voicePtrs) if (v != nullptr && v->meterActive()) { sounding = true; break; }
    float m = meterMorph.load (std::memory_order_relaxed);
    if (! sounding) { if (auto* p = apvts.getRawParameterValue ("oscMorphPos")) m = p->load(); }
    m = juce::jlimit (0.0f, 1.0f, m);

    const float pos = m * (float) (nf - 1);
    int   iA   = (int) pos;
    if (iA > nf - 2) iA = (nf >= 2 ? nf - 2 : 0);
    const float blend = (nf >= 2) ? pos - (float) iA : 0.0f;
    frameOut = pos;

    const int top = Wavetable::kNumMipLevels - 1;   // full-bandwidth shape for display
    const int iB  = (nf >= 2) ? iA + 1 : iA;
    for (int p = 0; p < n; ++p) {
        const float ph = (float) p / (float) n;
        const float a = wt->read (iA, top, ph);
        const float b = wt->read (iB, top, ph);
        out.add (juce::var (a + blend * (b - a)));
    }
}

void VaneProcessor::wavetableFilmstrip (juce::Array<juce::var>& cols, int numCols,
                                        int pts, int& framesOut) const
{
    framesOut = 0;
    const Wavetable* wt = activeWavetable;
    if (wt == nullptr || wt->numFrames() < 1) return;
    const int nf = wt->numFrames();
    framesOut = nf;
    const int n   = juce::jmin (numCols, nf);
    const int top = Wavetable::kNumMipLevels - 1;
    for (int c = 0; c < n; ++c) {
        const int frame = (n > 1) ? (int) std::lround ((double) c / (n - 1) * (nf - 1)) : 0;
        juce::Array<juce::var> pcol;
        for (int p = 0; p < pts; ++p)
            pcol.add (juce::var (wt->read (frame, top, (float) p / (float) pts)));
        cols.add (juce::var (pcol));
    }
}

bool VaneProcessor::loadWavetable (const juce::File& file)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb)) return false;
    activeWtName = file.getFileNameWithoutExtension();
    return setWavetableFromData (mb);
}

// Content-addressed wavetable library: loaded tables are stored once here keyed
// by hash, so the same table across many presets is a single file.  Presets
// reference the hash; DAW projects still embed (self-contained).
static juce::File wavetableLibraryDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("Vane").getChildFile ("Wavetables");
    if (! d.exists()) d.createDirectory();
    return d;
}

// FNV-1a 64-bit content hash (dependency-free; juce::MD5 lives in juce_cryptography
// which we don't link).  64 bits is ample for dedup of a personal table library.
static juce::String hashBytes (const void* data, size_t n)
{
    const auto* p = static_cast<const uint8_t*> (data);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return juce::String::toHexString ((juce::int64) h).paddedLeft ('0', 16);
}

bool VaneProcessor::setWavetableFromData (const juce::MemoryBlock& mb)
{
    auto wt = std::make_unique<Wavetable> (
        Wavetable::loadFromMemory (mb.getData(), mb.getSize(), Wavetable::kTableSize, wtPhaseAlign));
    if (! wt->isValid()) return false;

    activeWtFrames  = wt->numFrames();
    activeWavetable = wt.get();          // raw ptr handed to voices; pool keeps it alive
    wavetablePool.push_back (std::move (wt));
    lastWtData      = mb;                // keep for phase-align rebuilds

    // Store once in the library (dedup) and record the reference.
    const auto hash = hashBytes (mb.getData(), mb.getSize());
    auto libFile = wavetableLibraryDir().getChildFile (hash + ".wav");
    if (! libFile.existsAsFile()) libFile.replaceWithData (mb.getData(), mb.getSize());

    apvts.state.setProperty ("wavetableHash", hash, nullptr);          // preset reference
    apvts.state.setProperty ("wavetableName", activeWtName, nullptr);
    apvts.state.setProperty ("wavetableData",                          // project self-containment
                             juce::Base64::toBase64 (mb.getData(), mb.getSize()), nullptr);

    applyWavetableToVoices();
    return true;
}

void VaneProcessor::restoreWavetableFromState()
{
    wtPhaseAlign = (bool) apvts.state.getProperty ("wavetablePhaseAlign", false);
    activeWtName = apvts.state.getProperty ("wavetableName", activeWtName).toString();

    // 1. Embedded bytes (DAW projects, and pre-library presets).
    const auto b64 = apvts.state.getProperty ("wavetableData", juce::String()).toString();
    if (b64.isNotEmpty()) {
        juce::MemoryOutputStream mos;
        if (juce::Base64::convertFromBase64 (mos, b64) && mos.getDataSize() > 0) {
            juce::MemoryBlock mb (mos.getData(), mos.getDataSize());
            if (setWavetableFromData (mb)) return;
        }
    }
    // 2. Reference by hash → resolve from the library (lean presets).
    const auto hash = apvts.state.getProperty ("wavetableHash", juce::String()).toString();
    if (hash.isNotEmpty()) {
        auto libFile = wavetableLibraryDir().getChildFile (hash + ".wav");
        juce::MemoryBlock mb;
        if (libFile.existsAsFile() && libFile.loadFileAsData (mb) && setWavetableFromData (mb))
            return;
    }
    // 3. Nothing available → built-in.
    useBuiltInWavetable();
}

void VaneProcessor::releaseResources() {}

void VaneProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Feed all incoming CCs, channel pressure, and master-channel pitchbend
    // into the mod matrix / shared state before rendering voices.
    for (const auto meta : midi) {
        auto msg = meta.getMessage();
        const int  ch      = msg.getChannel();              // 1..16, or 0 if none
        const bool chValid = ch >= 1 && ch <= 16;
        // MIDI probe (diagnostics): count host-routed events + record channels.
        if (chValid) {
            midiProbe.events.fetch_add (1, std::memory_order_relaxed);
            midiProbe.channelMask.fetch_or (1u << (ch - 1), std::memory_order_relaxed);
        }
        // Rig routing: a channel only feeds the mod matrix if its "mod" role is on.
        const bool modOK = ! chValid
                        || (routeModMask.load (std::memory_order_relaxed) & (1u << (ch - 1)));
        if (msg.isController()) {
            const int cc = msg.getControllerNumber();
            if (modOK) modMatrix.setCCValue(cc, static_cast<float>(msg.getControllerValue()) / 127.0f);
            // MIDI-learn: capture the first CC seen while armed — but EXCLUDE the
            // CCs already used by the fixed sources (CC74 = MPE timbre/slide, and
            // the breath/expression CCs), so just playing a note (which streams
            // CC74) doesn't false-capture. Aux controls are some other CC.
            if (ccLearnArm.load(std::memory_order_relaxed) >= 0) {
                const int breathCC = pMacroBreathCC ? (int) std::lround(pMacroBreathCC->load()) : 2;
                const int exprCC   = pMacroExprCC   ? (int) std::lround(pMacroExprCC->load())   : 11;
                if (cc != 74 && cc != breathCC && cc != exprCC) {
                    ccLearnResult.store(cc, std::memory_order_relaxed);
                    ccLearnArm.store(-1, std::memory_order_relaxed);
                }
            }
        }
        else if (msg.isChannelPressure()) {
            if (modOK) modMatrix.setAftertouch(static_cast<float>(msg.getChannelPressureValue()) / 127.0f);
        }
        else if (msg.isNoteOn())
            meterVelocity.store (msg.getFloatVelocity(), std::memory_order_relaxed);  // held until next note
        // Channel-1 (non-MPE) pitchbend is intentionally NOT captured here.
        //
        // Discovery: for master-channel notes, JUCE's MPEInstrument updates
        // note.totalPitchbendInSemitones at each PB event during renderNextBlock
        // but never writes note.pitchbend.  Reading note.pitchbend.asSignedFloat()
        // in notePitchbendChanged() always returns 0 — the note-on value.
        //
        // The old approach of capturing pitchbend to a shared atomic here and
        // reading it in the voice caused "trill" artifacts on the Sylphyo: the
        // shared value was updated once per block (block-rate), whereas JUCE
        // calls notePitchbendChanged() mid-block with sub-sample accuracy.
        //
        // The correct approach: SynthVoice reads totalPitchbendInSemitones / 2
        // directly from currentlyPlayingNote in notePitchbendChanged(), which
        // gives the same per-event accuracy as member-channel MPE pitchbend.
    }

    // Resolve macro source bindings once per block, before voices render.
    // The choice param returns 0.0/1.0/2.0 — round before casting to int.
    {
        // Breath macro: 0=CC, 1=Aftertouch, 2=MPE Pressure (per-voice)
        int breathSrc = pMacroBreathSrc ? static_cast<int>(std::round(pMacroBreathSrc->load())) : 0;
        if (breathSrc == 2) {
            // Per-voice MPE pressure — setMacroSlot marks it as voice-backed so
            // evaluate() reads it from voiceVals, not from macroValues[].
            modMatrix.setMacroSlot(0, 0.0f, ModSourceID::MPE_Pressure);
        } else if (breathSrc == 1) {
            modMatrix.setMacroSlot(0, modMatrix.getAftertouch(), -1);
        } else {
            int breathCC = pMacroBreathCC ? static_cast<int>(std::round(pMacroBreathCC->load())) : 2;
            breathCC = juce::jlimit(0, 127, breathCC);
            modMatrix.setMacroSlot(0, modMatrix.getCCValue(breathCC), -1);
        }

        // Expression macro: 0=CC, 1=Aftertouch
        int exprSrc = pMacroExprSrc ? static_cast<int>(std::round(pMacroExprSrc->load())) : 0;
        if (exprSrc == 1) {
            modMatrix.setMacroSlot(1, modMatrix.getAftertouch(), -1);
        } else {
            int exprCC = pMacroExprCC ? static_cast<int>(std::round(pMacroExprCC->load())) : 11;
            exprCC = juce::jlimit(0, 127, exprCC);
            modMatrix.setMacroSlot(1, modMatrix.getCCValue(exprCC), -1);
        }
        // MacroPressure (2), MacroSlide (3), MacroPitchbend (4) are always per-voice;
        // their macroVoiceBacking is set correctly in ModMatrix's defaults — no action needed.
    }

    // Push aux global-source CC bindings to the matrix (controller profiles).
    for (int g = 0; g < ModSlots::NumAux; ++g)
        if (pAuxCC[(size_t) g])
            modMatrix.setAuxCC(g, static_cast<int>(std::lround(pAuxCC[(size_t) g]->load())));

    // Rig routing: a channel's note-ons sound only if its "notes" role is on.
    // Default mask 0xFFFF passes everything through unchanged (no extra copy).
    const uint16_t nmask = routeNotesMask.load (std::memory_order_relaxed);
    if (nmask == 0xFFFF) {
        synth.renderNextBlock(buffer, midi, 0, buffer.getNumSamples());
    } else {
        juce::MidiBuffer forSynth;
        for (const auto meta : midi) {
            const auto m = meta.getMessage();
            if (m.isNoteOnOrOff()) {
                const int c = m.getChannel();
                if (c >= 1 && c <= 16 && ! (nmask & (1u << (c - 1)))) continue;  // drop note
            }
            forSynth.addEvent (m, meta.samplePosition);
        }
        synth.renderNextBlock(buffer, forSynth, 0, buffer.getNumSamples());
    }

    // ── Per-voice MPE snapshot for the per-note visualiser ─────────────────────
    for (int i = 0; i < (int) voicePtrs.size() && i < kNumVoices; ++i) {
        auto* v = voicePtrs[i];
        if (v == nullptr) continue;
        auto& m = voiceMeters[i];
        const bool on = v->meterActive();
        m.level.store(on ? v->meterLevel() : 0.0f, std::memory_order_relaxed);
        if (on) {
            meterMorph.store(v->meterMorph(), std::memory_order_relaxed);   // live morph for WT display
            m.noteHz  .store(v->meterNoteHz(),   std::memory_order_relaxed);
            m.pressure.store(v->meterPressure(), std::memory_order_relaxed);
            m.slide   .store(v->meterSlide(),    std::memory_order_relaxed);
            m.bend    .store(v->meterBend(),     std::memory_order_relaxed);
            m.velocity.store(v->meterVelocity(), std::memory_order_relaxed);
        }
    }

    // Publish macro-resolved meter values for the editor (relaxed: stale by one block is fine).
    // CC-backed macros: macroValues[] was just written above by setMacroSlot().
    // Per-voice macros (pressure, slide, pitchbend): written by the last active voice callback.
    meterBreath.store(modMatrix.getMacroValue(0), std::memory_order_relaxed);  // MacroBreath
    meterExpr.store  (modMatrix.getMacroValue(1), std::memory_order_relaxed);  // MacroExpr

    // Apply master output level per-sample so automation ramps smoothly.
    masterGain.setTargetValue(pOutputLevel ? pOutputLevel->load() : 1.0f);
    auto* L = buffer.getWritePointer(0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < buffer.getNumSamples(); ++i) {
        float g = masterGain.getNextValue();
        L[i] *= g;
        if (R) R[i] *= g;
    }

    pushSpectrumSamples (L, buffer.getNumSamples());   // real FFT of the output
}

juce::AudioProcessorEditor* VaneProcessor::createEditor()
{
    // WebView UI (design_handoff v2).  The legacy native editor (VaneEditor) is
    // retained in the build for reference/fallback but no longer instantiated.
    return new WebVaneEditor(*this);
}

void VaneProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    // Routing-format version tag.  v2 = generic mod slots are the routing source
    // of truth.  Its absence on load marks a pre-slot preset to migrate.
    state.setProperty("routingV", 2, nullptr);
    auto xml = state.createXml();
    copyXmlToBinary(*xml, dest);
}

// Reconstruct the generic mod slots from a pre-slot preset's legacy fixed-route
// params (read straight from the loaded XML).  Only runs when routingV < 2.
void VaneProcessor::migrateLegacyRoutingToSlots(const juce::XmlElement& xml)
{
    struct Legacy { int src, dst; const char* amt; const char* curve; };
    // src: 1=Breath 2=Expr 3=Pressure 4=Slide 6=Velocity (ModSlots source choices)
    // dst: 0=VCA 1=Cutoff 2=Reso 4=Morph 5=PW 6=Fold 7=Noise 8=Inharm
    static const Legacy kLegacy[] = {
        { 1,0,"breathVCAamt","breathVCACurve" }, { 2,0,"exprVCAamt","exprVCACurve" },
        { 3,0,"pressVCAamt","pressVCACurve" },   { 4,1,"cc74CutoffAmt","cc74CutoffCurve" },
        { 1,1,"breathCutoffAmt","breathCutoffCurve" }, { 2,1,"breathCutoffAmt","exprCutoffCurve" },
        { 3,1,"pressCutoffAmt","pressCutoffCurve" },   { 1,2,"breathResAmt","breathResCurve" },
        { 2,2,"breathResAmt","exprResCurve" },         { 6,1,"veloCutoffAmt","veloCutoffCurve" },
        { 4,2,"cc74ResAmt","cc74ResCurve" },
        { 4,4,"slideMorphAmt","slideMorphCurve" }, { 3,4,"pressMorphAmt","pressMorphCurve" },
        { 1,4,"breathMorphAmt","breathMorphCurve" },
        { 4,5,"slidePWAmt","slidePWCurve" }, { 3,5,"pressPWAmt","pressPWCurve" }, { 1,5,"breathPWAmt","breathPWCurve" },
        { 4,6,"slideFoldAmt","slideFoldCurve" }, { 3,6,"pressFoldAmt","pressFoldCurve" }, { 1,6,"breathFoldAmt","breathFoldCurve" },
        { 4,7,"slideNoiseAmt","slideNoiseCurve" }, { 3,7,"pressNoiseAmt","pressNoiseCurve" }, { 1,7,"breathNoiseAmt","breathNoiseCurve" },
        { 4,8,"slideInharmAmt","slideInharmCurve" }, { 3,8,"pressInharmAmt","pressInharmCurve" }, { 1,8,"breathInharmAmt","breathInharmCurve" },
    };

    // Read a legacy param's value from the loaded XML (APVTS stores each param as
    // a <PARAM id=".." value=".."/> child); fall back to the live default.
    auto readParam = [&] (const char* id, double fallback) -> double {
        if (auto* el = xml.getChildByAttribute("id", id))
            return el->getDoubleAttribute("value", fallback);
        if (auto* p = apvts.getRawParameterValue(id))
            return p->load();
        return fallback;
    };
    auto setSlot = [&] (int n, int src, int dst, float amt, int curve) {
        const juce::String b = "modSlot" + juce::String(n);
        auto put = [&] (const juce::String& id, float actual) {
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id)))
                p->setValueNotifyingHost(p->convertTo0to1(actual));
        };
        put(b + "_src",   (float) src);   put(b + "_dst",   (float) dst);
        put(b + "_amt",   amt);           put(b + "_curve", (float) curve);
    };

    int slot = 0;
    for (const auto& L : kLegacy) {
        if (slot >= ModSlots::NumSlots) break;
        const double amt = readParam(L.amt, 0.0);
        if (std::abs(amt) < 1.0e-4) continue;             // skip inactive routes
        const int curve = (int) std::lround(readParam(L.curve, 0.0));
        setSlot(slot++, L.src, L.dst, (float) amt, curve);
    }
    // Clear any remaining slots so stale (Off-but-leftover) state can't linger.
    for (; slot < ModSlots::NumSlots; ++slot)
        setSlot(slot, 0, 0, 0.0f, 0);
}

void VaneProcessor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;

    apvts.replaceState(juce::ValueTree::fromXml(*xml));

    // Rebuild the embedded wavetable (or fall back to the built-in) before the
    // param migrations below, which may early-return.
    restoreWavetableFromState();

    // ── Routing migration (pre-slot → generic slots) ─────────────────────────
    // Presets saved before the slot system carry no routingV tag and store the
    // routing in the old fixed-route params.  Reconstruct it into the slots.
    // (Runs first — the Phase-2 block below can early-return.)
    if (xml->getIntAttribute("routingV", 1) < 2)
        migrateLegacyRoutingToSlots(*xml);

    // ── Backward-compat migration for Phase 2 (oscMorphPos / oscPW) ──────────
    //
    // Phase 2 replaced the discrete oscWave choice (0=Sine 1=Tri 2=Saw
    // 3=Square 4=Noise) with a continuous oscMorphPos (0=Sine 1=Tri 2=Sqr
    // 3=Saw) and a phase-distortion pw (0.5 = identity).
    //
    // Two failure modes when loading old presets:
    //
    // The decisive test is whether oscMorphPos is PRESENT in the loaded XML:
    //
    //   • oscMorphPos ABSENT  → genuinely pre-Phase-2 preset.  Map the old
    //     oscWave index to a morph position and reset pw.
    //   • oscMorphPos PRESENT → post-Phase-2 preset; its value is authoritative
    //     and MUST be trusted, even if a stale oscWave child is still in the XML.
    //     (oscWave is no longer a registered parameter, but old project files /
    //     carried-over presets can still carry it.  Migrating off it here would
    //     clobber the user's saved morph — e.g. a deliberate Sine (morphPos 0)
    //     would be forced back to Saw because the leftover oscWave reads 2.)
    //
    //   A) oscWave present, oscMorphPos / oscPW absent:
    //      JUCE sets absent params to normalised 0.0 (range minimum, not the
    //      declared default).  Both new params end up at 0.0 → Sine + extreme
    //      narrow pulse.  Correct by mapping the old discrete index.
    //
    //   B) oscWave present, oscMorphPos / oscPW also present (value 0.0):
    //      The project was saved AFTER a bad initial load of the old preset —
    //      JUCE wrote the range-minimum values into the saved state.  The
    //      presence of oscWave in the XML is the reliable indicator that this
    //      was originally a pre-Phase-2 preset; treat it the same as (A).
    //
    // In both cases: if oscWave is in the XML, map it to morphPos and reset pw.
    //
    // Old oscWave:   0=Sine  1=Triangle  2=Saw  3=Square  4=Noise
    // New morph pos: 0=Sine  1=Triangle  2=Sqr  3=Saw     (Noise → Phase 3)
    const bool hasMorphPos = (xml->getChildByAttribute("id", "oscMorphPos") != nullptr);
    if (!hasMorphPos)
    {
        if (auto* oldWaveEl = xml->getChildByAttribute("id", "oscWave")) {
            float morphPos = 3.0f;   // default: Saw
            switch (static_cast<int>(oldWaveEl->getDoubleAttribute("value", 2.0))) {
                case 0: morphPos = 0.0f; break;   // Sine
                case 1: morphPos = 1.0f; break;   // Triangle
                case 2: morphPos = 3.0f; break;   // Saw
                case 3: morphPos = 2.0f; break;   // Square
                default: morphPos = 3.0f; break;  // Noise / unknown → Saw
            }
            // setValueNotifyingHost() takes a normalised [0,1] value.
            // oscMorphPos range [0,3] linear → divide by 3.
            // oscPW: use getDefaultValue() so this stays correct regardless of range/skew.
            //   With range [0.5, 0.999] skew 2: normalised default = 0.0 → actual 0.5. ✓
            if (auto* p = apvts.getParameter("oscMorphPos"))
                p->setValueNotifyingHost(morphPos / 3.0f);
            if (auto* p = apvts.getParameter("oscPW"))
                p->setValueNotifyingHost(p->getDefaultValue());
            return;   // genuinely old preset migrated.
        }
    }
    // oscMorphPos present (or no oscWave to migrate from): trust the loaded state.

    // Belt-and-suspenders: a preset with no oscWave but also no oscMorphPos/oscPW
    // (shouldn't exist in practice, but guards against partial saves).
    auto restoreDefault = [&](const char* id) {
        if (xml->getChildByAttribute("id", id) == nullptr)
            if (auto* p = apvts.getParameter(id))
                p->setValueNotifyingHost(p->getDefaultValue());
    };
    restoreDefault("oscMorphPos");
    restoreDefault("oscPW");
}

juce::AudioProcessorValueTreeState::ParameterLayout VaneProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ── Output level ─────────────────────────────────────────────────────────
    // Linear 0..1 gain applied post-synth.  Default 0.8 gives ~2 dB headroom
    // before clipping into downstream effects.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"outputLevel", 1}, "Output Level",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f, 0.5f), 0.8f));

    // ── Filter ───────────────────────────────────────────────────────────────
    // Default 1200 Hz: slide neutral + full breath opens to ~3.5 kHz;
    // slide full-up hits 20 kHz, slide full-down hits ~53 Hz.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filterCutoff", 1}, "Filter Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.0f, 0.25f), 1200.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filterRes", 1}, "Filter Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.3f));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"filterMode", 1}, "Filter Mode",
        juce::StringArray{"LP", "BP", "HP"}, 0));

    // ── Oscillator ───────────────────────────────────────────────────────────
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"oscDetune", 1}, "Osc Detune",
        juce::NormalisableRange<float>(-100.0f, 100.0f), 0.0f));

    // Continuous morph through Sine → Triangle → Square → Sawtooth.
    // Default 3.0 (Sawtooth) matches the original fixed-waveform default.
    // Modulated by OscWaveshape destination (×3 scale inside SynthVoice).
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"oscMorphPos", 1}, "Osc Morph",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));   // normalised position across the table

    // Noise blend: 0 = pure wavetable, 1 = pure noise.  Applied pre-filter so the
    // noise shares the same filter character as the oscillator signal.
    // Modulated by OscNoiseMix destination (direct additive offset, clamped 0..1).
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"noiseBlend", 1}, "Noise Blend",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f, 0.5f), 0.0f));

    // Noise character: White = flat spectrum, Pink ≈ −3 dB/oct, Brown ≈ −6 dB/oct.
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"noiseType", 1}, "Noise Type",
        juce::StringArray{"White", "Pink", "Brown"}, 0));

    // Wavefold depth: 0 = transparent, 1 = heavy folding (pre-filter).
    // Reflective triangle fold — multiplies harmonic content for West-Coast timbres.
    // LINEAR range: the perceptual curve lives in Oscillator::foldDrive() (an
    // exponential amount→drive map), so the knob position already tracks perceived
    // brightness evenly — a param skew here would double-curve it.
    // Modulated by OscFold destination (direct additive offset, clamped 0..1).
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"oscFold", 1}, "Wavefold",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Inharmonicity (FM index): 0 = harmonic, 1 = strong metallic/bell.
    // Phase-modulates the table read at a √2 (irrational) modulator ratio, so the
    // sidebands are inharmonic.  Modulated by OscInharm destination (additive 0..1).
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"oscInharm", 1}, "Inharmonicity",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Wavetable hard-sync / transpose ratio: 1 = off, up to 8× (formant sweep).
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"oscSync", 1}, "Osc Sync",
        juce::NormalisableRange<float>(1.0f, 8.0f), 1.0f));

    // Phase-distortion pulse width: 0.5 = identity (no warp), 0.999 = near-Dirac.
    //
    // Range [0.5, 0.999] — the PD warp is symmetric around 0.5, so the two halves
    // [0, 0.5) and (0.5, 1] produce mirror-image spectra.  We keep only the upper
    // half so modulation is unambiguous and the UI has a clear "identity" minimum.
    //
    // Skew factor 2 (quadratic): proportion² mapping → more resolution near 0.5
    // (subtle warp in the lower 70 % of the knob range) and compressed extreme
    // values toward the top — matching the "partly exponential" perceptual feel.
    //
    // Modulated by OscPulseWidth destination (direct additive offset, clamped to range).
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"oscPW", 1}, "Osc Pulse Width",
        juce::NormalisableRange<float>(0.5f, 0.999f, 0.0f, 2.0f), 0.5f));

    // ── Performance ──────────────────────────────────────────────────────────
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"glideTime", 1}, "Glide Time",
        juce::NormalisableRange<float>(0.0f, 2000.0f, 0.0f, 0.5f), 0.0f));

    // Fixed Time: glideTime = total ramp duration in ms (same for any interval).
    // Fixed Rate: glideTime = ms per semitone; an octave takes 12× longer.
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"glideMode", 1}, "Glide Mode",
        juce::StringArray{"Fixed Time", "Fixed Rate"}, 0));

    // Linear:      constant pitch rate in semitones/ms (Multiplicative smoother).
    // Exponential: 1-pole IIR in log2(Hz) — even semitone feel across the keyboard.
    // RC:          1-pole IIR in linear Hz — true analog RC circuit; snappier in the
    //              high register (larger Hz gap), heavier in the bass.
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"glideCurve", 1}, "Glide Curve",
        juce::StringArray{"Linear", "Exponential", "RC"}, 0));

    // TODO: masterTune is declared and saved in presets but is NOT yet applied
    // in SynthVoice::renderNextBlock.  To wire it up:
    //   1. Pass the raw pointer to SynthVoice alongside pDetune.
    //   2. In renderNextBlock, add it to totalSemitones (in cents / 100).
    //   3. Consider whether it should also offset baseHz before smoothedHz is set
    //      in noteStarted() so portamento and MTS are both affected.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"masterTune", 1}, "Master Tune (cents)",
        juce::NormalisableRange<float>(-100.0f, 100.0f), 0.0f));

    // 0.0 = pure wind/breath mode (VCA entirely from CC2/CC11/pressure)
    // 1.0 = pure keyboard mode  (velocity is the amplitude base)
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"velocityMix", 1}, "Velocity to VCA",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"monoMode", 1}, "Mono", false));

    // ── Modulation route amounts ──────────────────────────────────────────────
    // How far each source sweeps its destination.  All are live parameters —
    // changing them mid-performance takes effect on the next audio block.

    // VCA route amounts — making all routes adjustable from the ModMatrix UI.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"breathVCAamt", 1}, "Breath to VCA",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"exprVCAamt", 1}, "Expression to VCA",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"pressVCAamt", 1}, "Pressure to VCA",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    // CC74 (slide): 0 = no filter movement, 1 = full ±5-octave sweep.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"cc74CutoffAmt", 1}, "CC74 to Cutoff",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.9f));

    // Breath: how much CC2/CC11 brightens the filter (secondary to CC74).
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"breathCutoffAmt", 1}, "Breath to Cutoff",
        juce::NormalisableRange<float>(0.0f, 0.5f), 0.25f));

    // MPE pressure: per-note brightness add-on, independent of slide.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"pressCutoffAmt", 1}, "Pressure to Cutoff",
        juce::NormalisableRange<float>(0.0f, 0.5f), 0.2f));

    // Velocity: initial timbre accent at note-on (tone, not loudness).
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"veloCutoffAmt", 1}, "Velocity to Cutoff",
        juce::NormalisableRange<float>(0.0f, 0.5f), 0.15f));

    // Breath → resonance: more air = more resonant peak.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"breathResAmt", 1}, "Breath to Resonance",
        juce::NormalisableRange<float>(0.0f, 0.5f), 0.15f));

    // CC74 → resonance: slide also sweeps resonance.  Default 0 (off) so
    // players can dial it in as a separate texture layer.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"cc74ResAmt", 1}, "CC74 to Resonance",
        juce::NormalisableRange<float>(0.0f, 0.5f), 0.0f));

    // ── Oscillator-timbre route amounts (per-voice, opt-in) ────────────────────
    // Slide (CC74) and Pressure → Morph / PW / Noise.  All default 0 (off).
    //
    // Range rationale (the mod is added to the base param inside SynthVoice):
    //   Morph: mods[OscWaveshape] is scaled ×3, so amount 1.0 with full slide
    //          sweeps the entire Sine→Saw span → range 0..1.
    //   PW:    mods[OscPulseWidth] is added directly to a [0.5,0.999] param, so
    //          amount 0.5 covers the full warp range → range 0..0.5.
    //   Noise: mods[OscNoiseMix] is added directly to a [0,1] param → range 0..1.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"slideMorphAmt", 1}, "Slide to Morph",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"pressMorphAmt", 1}, "Pressure to Morph",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"slidePWAmt", 1}, "Slide to PW",
        juce::NormalisableRange<float>(0.0f, 0.5f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"pressPWAmt", 1}, "Pressure to PW",
        juce::NormalisableRange<float>(0.0f, 0.5f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"slideNoiseAmt", 1}, "Slide to Noise",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"pressNoiseAmt", 1}, "Pressure to Noise",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    // Fold mod is added directly to a [0,1] param → range 0..1.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"slideFoldAmt", 1}, "Slide to Fold",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"pressFoldAmt", 1}, "Pressure to Fold",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    // Inharmonicity mod is added directly to a [0,1] param → range 0..1.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"slideInharmAmt", 1}, "Slide to Inharm",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"pressInharmAmt", 1}, "Pressure to Inharm",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    // Breath → osc-timbre amounts (ranges match each destination's scaling).
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"breathMorphAmt", 1}, "Breath to Morph",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"breathPWAmt", 1}, "Breath to PW",
        juce::NormalisableRange<float>(0.0f, 0.5f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"breathNoiseAmt", 1}, "Breath to Noise",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"breathFoldAmt", 1}, "Breath to Fold",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"breathInharmAmt", 1}, "Breath to Inharm",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // ── Modulation route curve shapes ─────────────────────────────────────────
    // One integer-stepped parameter per route (0=lin, 1=exp, 2=S).
    // These match the ModRoute::CurveShape enum values and are read by
    // ModMatrix::evaluate() via curveParam pointers.
    // Default values reflect the audio-design intent: VCA and slide routes are
    // linear; breath-to-filter routes are exponential for natural response.
    auto makeCurveParam = [&](const char* id, const char* name, float defaultVal) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id, 1}, name,
            juce::NormalisableRange<float>(0.0f, 2.0f, 1.0f), defaultVal));
    };
    // IMPORTANT: display names must be pure ASCII — juce::String(const char*)
    // asserts on any byte > 127.  Use " to " not "→" (U+2192 = \xe2\x86\x92).
    makeCurveParam("breathVCACurve",    "Breath to VCA Curve",        0.0f); // lin
    makeCurveParam("exprVCACurve",      "Expr to VCA Curve",          0.0f); // lin
    makeCurveParam("pressVCACurve",     "Pressure to VCA Curve",      0.0f); // lin
    makeCurveParam("cc74CutoffCurve",   "CC74 to Cutoff Curve",       0.0f); // lin
    makeCurveParam("breathCutoffCurve", "Breath to Cutoff Curve",     1.0f); // exp
    makeCurveParam("exprCutoffCurve",   "Expr to Cutoff Curve",       1.0f); // exp
    makeCurveParam("pressCutoffCurve",  "Pressure to Cutoff Curve",   1.0f); // exp
    makeCurveParam("breathResCurve",    "Breath to Reso Curve",       1.0f); // exp
    makeCurveParam("exprResCurve",      "Expr to Reso Curve",         1.0f); // exp
    makeCurveParam("veloCutoffCurve",   "Velocity to Cutoff Curve",   0.0f); // lin
    makeCurveParam("cc74ResCurve",      "CC74 to Reso Curve",         0.0f); // lin
    // Oscillator-timbre route curves (default linear).
    makeCurveParam("slideMorphCurve",   "Slide to Morph Curve",       0.0f); // lin
    makeCurveParam("pressMorphCurve",   "Pressure to Morph Curve",    0.0f); // lin
    makeCurveParam("slidePWCurve",      "Slide to PW Curve",          0.0f); // lin
    makeCurveParam("pressPWCurve",      "Pressure to PW Curve",       0.0f); // lin
    makeCurveParam("slideNoiseCurve",   "Slide to Noise Curve",       0.0f); // lin
    makeCurveParam("pressNoiseCurve",   "Pressure to Noise Curve",    0.0f); // lin
    makeCurveParam("slideFoldCurve",    "Slide to Fold Curve",        0.0f); // lin
    makeCurveParam("pressFoldCurve",    "Pressure to Fold Curve",     0.0f); // lin
    makeCurveParam("slideInharmCurve",  "Slide to Inharm Curve",      0.0f); // lin
    makeCurveParam("pressInharmCurve",  "Pressure to Inharm Curve",   0.0f); // lin
    makeCurveParam("breathMorphCurve",  "Breath to Morph Curve",      0.0f); // lin
    makeCurveParam("breathPWCurve",     "Breath to PW Curve",         0.0f); // lin
    makeCurveParam("breathNoiseCurve",  "Breath to Noise Curve",      0.0f); // lin
    makeCurveParam("breathFoldCurve",   "Breath to Fold Curve",       0.0f); // lin
    makeCurveParam("breathInharmCurve", "Breath to Inharm Curve",     0.0f); // lin

    // ── Generic mod-slot pool — the routing source of truth ───────────────────
    // NumSlots configurable slots (source/dest/amount/curve).  The FACTORY routing
    // lives in the defaults of slots 0..9 (the old fixed routes, one per slot);
    // the rest default to OFF.  Source choices: Off/Breath/Expr/Pressure/Slide/
    // Pitchbend/Velocity; dest: VCA/Cutoff/Reso/Pitch/Morph/PW/Fold/Noise/Inharm;
    // curve: Lin/Exp/S.
    {
        struct SlotDef { int src, dst, curve; float amt; };
        // Mirrors the previous fixed factory routes exactly.
        static const SlotDef kFactory[] = {
            { 1, 0, 0, 1.00f },  // 0  Breath     → VCA     lin
            { 2, 0, 0, 1.00f },  // 1  Expression → VCA     lin
            { 3, 0, 0, 0.50f },  // 2  Pressure   → VCA     lin
            { 4, 1, 0, 0.90f },  // 3  Slide      → Cutoff  lin
            { 1, 1, 1, 0.25f },  // 4  Breath     → Cutoff  exp
            { 2, 1, 1, 0.25f },  // 5  Expression → Cutoff  exp
            { 3, 1, 1, 0.20f },  // 6  Pressure   → Cutoff  exp
            { 1, 2, 1, 0.15f },  // 7  Breath     → Reso    exp
            { 2, 2, 1, 0.15f },  // 8  Expression → Reso    exp
            { 6, 1, 0, 0.15f },  // 9  Velocity   → Cutoff  lin
        };
        constexpr int kNumFactory = (int) (sizeof (kFactory) / sizeof (kFactory[0]));

        juce::StringArray srcChoices, dstChoices, curveChoices;
        for (int i = 0; i < ModSlots::NumSourceChoices; ++i) srcChoices.add(ModSlots::kSourceNames[i]);
        for (int i = 0; i < ModSlots::NumDestChoices;   ++i) dstChoices.add(ModSlots::kDestNames[i]);
        for (int i = 0; i < ModSlots::NumCurveChoices;  ++i) curveChoices.add(ModSlots::kCurveNames[i]);

        for (int n = 0; n < ModSlots::NumSlots; ++n) {
            const SlotDef d = (n < kNumFactory) ? kFactory[n] : SlotDef{ 0, 0, 0, 0.0f };
            const juce::String base = "modSlot" + juce::String(n);
            const juce::String human = "Slot " + juce::String(n) + " ";
            layout.add(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID{ base + "_src", 1 },   human + "Source",      srcChoices,   d.src));
            layout.add(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID{ base + "_dst", 1 },   human + "Destination", dstChoices,   d.dst));
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{ base + "_amt", 1 },   human + "Amount",
                juce::NormalisableRange<float>(-1.0f, 1.0f), d.amt));
            layout.add(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID{ base + "_curve", 1 }, human + "Curve",       curveChoices, d.curve));
        }
    }

    // ── Configurable global aux source bindings (controller profiles) ──────────
    // Each aux source (selectable in the matrix as "Aux N") reads the CC# bound
    // here.  0 = unbound (reads CC0 ≈ 0).  A controller profile sets these; the
    // Controllers UI / MIDI-learn lands in a later phase.
    for (int g = 0; g < ModSlots::NumAux; ++g)
        layout.add(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID{ "aux" + juce::String(g) + "_cc", 1 },
            "Aux " + juce::String(g + 1) + " CC", 0, 127, 0));

    // ── Pitchbend ranges ──────────────────────────────────────────────────────
    // MPE and non-MPE controllers use very different ranges, so each has its
    // own parameter.  renderNextBlock selects the right one from the note's
    // midiChannel: channel 1 = master/legacy (non-MPE), 2–16 = MPE members.

    // MPE member-channel notes (Exquis, Seaboard, LinnStrument, …).
    // MPE spec standard is ±48 st per note so the full chromatic range can be
    // swept from any starting pitch.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"pitchbendRange", 1}, "MPE Pitchbend Range (st)",
        juce::NormalisableRange<float>(1.0f, 96.0f, 1.0f), 48.0f));

    // Non-MPE / legacy notes on channel 1 (keyboards, Sylphyo in standard mode,
    // breath controllers, …).  MIDI standard default is ±2 st.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"nonMPEPBRange", 1}, "Pitchbend Range (st)",
        juce::NormalisableRange<float>(1.0f, 96.0f, 1.0f), 2.0f));

    // ── Macro source bindings ─────────────────────────────────────────────────
    // These parameters decouple abstract macro names from concrete MIDI sources.
    // Changing them live re-routes the signal without altering any route table.

    // Breath macro source: which physical MIDI dimension drives the Breath slot.
    // 0 = CC  (use macroBreathCC number below)
    // 1 = Aftertouch (global channel pressure)
    // 2 = MPE Pressure (per-note, polyphonic)
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"macroBreathSrc", 1}, "Breath Source",
        juce::StringArray{"CC", "Aftertouch", "MPE Pressure"}, 0));

    // Which CC number drives Breath when macroBreathSrc == CC.
    // Default 2 (breath controller, EWI/WX). Change to 11 for Sylphyo
    // expression mode or any other controller's breath CC.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"macroBreathCC", 1}, "Breath CC Number",
        juce::NormalisableRange<float>(0.0f, 127.0f, 1.0f), 2.0f));

    // Expression macro source: secondary breath / expression slot.
    // 0 = CC  (use macroExprCC number below)
    // 1 = Aftertouch
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"macroExprSrc", 1}, "Expression Source",
        juce::StringArray{"CC", "Aftertouch"}, 0));

    // Which CC number drives Expression when macroExprSrc == CC.
    // Default 11 (expression pedal / Sylphyo).
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"macroExprCC", 1}, "Expression CC Number",
        juce::NormalisableRange<float>(0.0f, 127.0f, 1.0f), 11.0f));

    return layout;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VaneProcessor();
}
