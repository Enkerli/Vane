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
                                      &lastFilterS1, &lastFilterS2, &lastCutoffHz));

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

    pOutputLevel = apvts.getRawParameterValue("outputLevel");

    // ── Default modulation routes ─────────────────────────────────────────────
    //
    // Breath: CC2 (WX-11, EWI) and CC11 (Sylphyo, expression pedals).
    // Controllers typically send one or the other; they sum but since only one
    // is active at a time the combined result stays in range.
    //
    // Route index map (order below = index used by per-voice slewer array):
    //   0  CC2       → VCALevel       (fixed 1.0)
    //   1  CC11      → VCALevel       (fixed 1.0)
    //   2  Pressure  → VCALevel       (fixed 0.5)
    //   3  Slide     → FilterCutoff   (pCC74CutAmt)
    //   4  CC2       → FilterCutoff   (pBreathCutAmt, Exponential)
    //   5  CC11      → FilterCutoff   (pBreathCutAmt, Exponential) — shared param
    //   6  Pressure  → FilterCutoff   (pPressCutAmt,  Exponential)
    //   7  CC2       → FilterRes      (pBreathResAmt, Exponential)
    //   8  CC11      → FilterRes      (pBreathResAmt, Exponential) — shared param
    //   9  Velocity  → FilterCutoff   (pVeloCutAmt)
    //  10  Slide     → FilterRes      (pCC74ResAmt)

    // VCA: breath and MPE pressure control amplitude (not user-editable amounts —
    // they are calibrated so CC2 and CC11 never sum above 1.0 in practice).
    modMatrix.addRoute(ModSourceID::CC + 2,       ModDestID::VCALevel, 1.0f,  5.0f, 80.0f);
    modMatrix.addRoute(ModSourceID::CC + 11,      ModDestID::VCALevel, 1.0f,  5.0f, 80.0f);
    modMatrix.addRoute(ModSourceID::MPE_Pressure, ModDestID::VCALevel, 0.5f,  3.0f, 50.0f);

    // CC74 (slide) → FilterCutoff: primary timbre sweep, full audible range.
    // Slide is bipolar in SynthVoice: neutral=0, up=+1, down=-1.
    // With default amount 0.9 and 5-oct scale in SynthVoice:
    //   baseCutoff/32 at slide bottom  (~53 Hz at 1200 Hz base)
    //   baseCutoff×32 at slide top     (~20 kHz clamped)
    modMatrix.addRoute(ModSourceID::MPE_Slide, ModDestID::FilterCutoff,
                       0.9f, 2.0f, 20.0f, ModRoute::CurveShape::Linear, pCC74CutAmt);

    // Breath → FilterCutoff: secondary brightness accent, exponential curve.
    // Both CC2 and CC11 share the same amount parameter.
    modMatrix.addRoute(ModSourceID::CC + 2,  ModDestID::FilterCutoff,
                       0.25f, 5.0f, 80.0f, ModRoute::CurveShape::Exponential, pBreathCutAmt);
    modMatrix.addRoute(ModSourceID::CC + 11, ModDestID::FilterCutoff,
                       0.25f, 5.0f, 80.0f, ModRoute::CurveShape::Exponential, pBreathCutAmt);

    // MPE Pressure → FilterCutoff: per-note brightness, independent of slide.
    modMatrix.addRoute(ModSourceID::MPE_Pressure, ModDestID::FilterCutoff,
                       0.2f, 2.0f, 30.0f, ModRoute::CurveShape::Exponential, pPressCutAmt);

    // Breath → FilterRes: more air = more resonant peak (classic wind character).
    modMatrix.addRoute(ModSourceID::CC + 2,  ModDestID::FilterRes,
                       0.15f, 5.0f, 80.0f, ModRoute::CurveShape::Exponential, pBreathResAmt);
    modMatrix.addRoute(ModSourceID::CC + 11, ModDestID::FilterRes,
                       0.15f, 5.0f, 80.0f, ModRoute::CurveShape::Exponential, pBreathResAmt);

    // Velocity → FilterCutoff: initial timbre accent, harder attacks are brighter.
    modMatrix.addRoute(ModSourceID::Velocity, ModDestID::FilterCutoff,
                       0.15f, 20.0f, 0.0f, ModRoute::CurveShape::Linear, pVeloCutAmt);

    // CC74 (slide) → FilterRes: optional resonance sweep via slide.
    // Default amount 0.0 (off); player dials in the flavour they want.
    modMatrix.addRoute(ModSourceID::MPE_Slide, ModDestID::FilterRes,
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

    // Feed all incoming CCs into the mod matrix before rendering voices
    for (const auto meta : midi) {
        auto msg = meta.getMessage();
        if (msg.isController())
            modMatrix.setCCValue(msg.getControllerNumber(),
                                  static_cast<float>(msg.getControllerValue()) / 127.0f);
    }

    synth.renderNextBlock(buffer, midi, 0, buffer.getNumSamples());

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

    return layout;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VaneProcessor();
}
