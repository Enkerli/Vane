#include "PluginProcessor.h"
#include "PluginEditor.h"

VaneProcessor::VaneProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Vane", createParameterLayout()),
      presetManager(apvts)
{
    // 15 voices: one per MPE member channel in the lower zone
    // MPESynthesiser has no "sound" concept — voices handle everything.
    // Parameter raw pointers are safe to read on the audio thread.
    auto* pWave       = apvts.getRawParameterValue("oscWave");
    auto* pDetune     = apvts.getRawParameterValue("oscDetune");
    auto* pCutoff     = apvts.getRawParameterValue("filterCutoff");
    auto* pRes        = apvts.getRawParameterValue("filterRes");
    auto* pFilterMode = apvts.getRawParameterValue("filterMode");
    auto* pVeloMix    = apvts.getRawParameterValue("velocityMix");
    auto* pGlide      = apvts.getRawParameterValue("glideTime");
    auto* pMono       = apvts.getRawParameterValue("monoMode");
    for (int i = 0; i < 15; ++i)
        synth.addVoice(new SynthVoice(modMatrix, tuning,
                                      pWave, pDetune, pCutoff, pRes, pFilterMode, pVeloMix,
                                      pGlide, &lastNoteHz, &lastVCALevel,
                                      &legatoGeneration, &lastOscPhase, pMono,
                                      &lastFilterS1, &lastFilterS2, &lastCutoffHz,
                                      &meterPressure, &meterSlide, &meterPitchbend, pPBRange));

    // Lower zone: channel 1 is master, channels 2–16 are member channels
    juce::MPEZoneLayout zone;
    zone.setLowerZone(15);
    synth.setZoneLayout(zone);

    // ── Mod-matrix route amount parameters ────────────────────────────────────
    // These pointers are passed to addRoute() so the synth reads live values
    // from the APVTS on every audio block — no rebuild required to tweak amounts.
    auto* pCC74CutAmt   = apvts.getRawParameterValue("cc74CutoffAmt");
    auto* pBreathCutAmt = apvts.getRawParameterValue("breathCutoffAmt");
    auto* pPressCutAmt  = apvts.getRawParameterValue("pressCutoffAmt");
    auto* pVeloCutAmt   = apvts.getRawParameterValue("veloCutoffAmt");
    auto* pBreathResAmt = apvts.getRawParameterValue("breathResAmt");
    auto* pCC74ResAmt   = apvts.getRawParameterValue("cc74ResAmt");

    pOutputLevel    = apvts.getRawParameterValue("outputLevel");
    pPBRange        = apvts.getRawParameterValue("pitchbendRange");
    pMacroBreathSrc = apvts.getRawParameterValue("macroBreathSrc");
    pMacroBreathCC  = apvts.getRawParameterValue("macroBreathCC");
    pMacroExprSrc   = apvts.getRawParameterValue("macroExprSrc");
    pMacroExprCC    = apvts.getRawParameterValue("macroExprCC");

    // ── Default modulation routes ─────────────────────────────────────────────
    //
    // All routes use abstract macro IDs rather than raw CC numbers so the
    // concrete MIDI binding (which CC, or Aftertouch, or MPE dim) can be
    // changed at runtime via the macro-binding parameters without rebuilding routes.
    //
    // Route index map (order below = index used by per-voice slewer array):
    //   0  MacroBreath    → VCALevel       (fixed 1.0)
    //   1  MacroExpr      → VCALevel       (fixed 1.0)
    //   2  MacroPressure  → VCALevel       (fixed 0.5)
    //   3  MacroSlide     → FilterCutoff   (pCC74CutAmt)
    //   4  MacroBreath    → FilterCutoff   (pBreathCutAmt, Exponential)
    //   5  MacroExpr      → FilterCutoff   (pBreathCutAmt, Exponential) — shared param
    //   6  MacroPressure  → FilterCutoff   (pPressCutAmt,  Exponential)
    //   7  MacroBreath    → FilterRes      (pBreathResAmt, Exponential)
    //   8  MacroExpr      → FilterRes      (pBreathResAmt, Exponential) — shared param
    //   9  Velocity       → FilterCutoff   (pVeloCutAmt)
    //  10  MacroSlide     → FilterRes      (pCC74ResAmt)

    // VCA: breath and pressure control amplitude (amounts are calibrated so
    // MacroBreath and MacroExpr never both output 1.0 simultaneously in practice).
    modMatrix.addRoute(ModSourceID::MacroBreath,    ModDestID::VCALevel, 1.0f,  5.0f, 80.0f);
    modMatrix.addRoute(ModSourceID::MacroExpr,      ModDestID::VCALevel, 1.0f,  5.0f, 80.0f);
    modMatrix.addRoute(ModSourceID::MacroPressure,  ModDestID::VCALevel, 0.5f,  3.0f, 50.0f);

    // Slide → FilterCutoff: primary timbre sweep, full audible range.
    // Slide is bipolar in SynthVoice: neutral=0, up=+1, down=-1.
    // With default amount 0.9 and 5-oct scale in SynthVoice:
    //   baseCutoff/32 at slide bottom  (~53 Hz at 1200 Hz base)
    //   baseCutoff×32 at slide top     (~20 kHz clamped)
    modMatrix.addRoute(ModSourceID::MacroSlide, ModDestID::FilterCutoff,
                       0.9f, 2.0f, 20.0f, ModRoute::CurveShape::Linear, pCC74CutAmt);

    // Breath → FilterCutoff: secondary brightness accent, exponential curve.
    // MacroBreath and MacroExpr share the same amount parameter.
    modMatrix.addRoute(ModSourceID::MacroBreath, ModDestID::FilterCutoff,
                       0.25f, 5.0f, 80.0f, ModRoute::CurveShape::Exponential, pBreathCutAmt);
    modMatrix.addRoute(ModSourceID::MacroExpr,   ModDestID::FilterCutoff,
                       0.25f, 5.0f, 80.0f, ModRoute::CurveShape::Exponential, pBreathCutAmt);

    // Pressure → FilterCutoff: per-note brightness, independent of slide.
    modMatrix.addRoute(ModSourceID::MacroPressure, ModDestID::FilterCutoff,
                       0.2f, 2.0f, 30.0f, ModRoute::CurveShape::Exponential, pPressCutAmt);

    // Breath → FilterRes: more air = more resonant peak (classic wind character).
    modMatrix.addRoute(ModSourceID::MacroBreath, ModDestID::FilterRes,
                       0.15f, 5.0f, 80.0f, ModRoute::CurveShape::Exponential, pBreathResAmt);
    modMatrix.addRoute(ModSourceID::MacroExpr,   ModDestID::FilterRes,
                       0.15f, 5.0f, 80.0f, ModRoute::CurveShape::Exponential, pBreathResAmt);

    // Velocity → FilterCutoff: initial timbre accent, harder attacks are brighter.
    modMatrix.addRoute(ModSourceID::Velocity, ModDestID::FilterCutoff,
                       0.15f, 20.0f, 0.0f, ModRoute::CurveShape::Linear, pVeloCutAmt);

    // Slide → FilterRes: optional resonance sweep via slide.
    // Default amount 0.0 (off); player dials in the flavour they want.
    modMatrix.addRoute(ModSourceID::MacroSlide, ModDestID::FilterRes,
                       0.0f, 2.0f, 20.0f, ModRoute::CurveShape::Linear, pCC74ResAmt);

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

VaneProcessor::~VaneProcessor() = default;

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
}

void VaneProcessor::releaseResources() {}

void VaneProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Feed all incoming CCs and channel pressure into the mod matrix before rendering.
    for (const auto meta : midi) {
        auto msg = meta.getMessage();
        if (msg.isController())
            modMatrix.setCCValue(msg.getControllerNumber(),
                                  static_cast<float>(msg.getControllerValue()) / 127.0f);
        else if (msg.isChannelPressure())
            modMatrix.setAftertouch(static_cast<float>(msg.getChannelPressureValue()) / 127.0f);
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

    synth.renderNextBlock(buffer, midi, 0, buffer.getNumSamples());

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
}

juce::AudioProcessorEditor* VaneProcessor::createEditor()
{
    return new VaneEditor(*this);
}

void VaneProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto xml = apvts.copyState().createXml();
    copyXmlToBinary(*xml, dest);
}

void VaneProcessor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
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

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"oscWave", 1}, "Osc Waveform",
        juce::StringArray{"Sine", "Triangle", "Saw", "Square", "Noise"}, 2));

    // ── Performance ──────────────────────────────────────────────────────────
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"glideTime", 1}, "Glide Time",
        juce::NormalisableRange<float>(0.0f, 2000.0f, 0.0f, 0.5f), 0.0f));

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

    // ── Pitchbend range ───────────────────────────────────────────────────────
    // Semitones for full ± pitchbend.  Controllers vary widely: keyboards are
    // typically ±2 st, Sylphyo defaults to ±48 st, and most devices are
    // configurable.  Setting this correctly prevents pitch overshoot or
    // underexpressive bends.  Step size 1 st keeps it integer and preset-safe.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"pitchbendRange", 1}, "Pitchbend Range (st)",
        juce::NormalisableRange<float>(1.0f, 96.0f, 1.0f), 48.0f));

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
