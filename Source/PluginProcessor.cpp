#include "PluginProcessor.h"
#include "PluginEditor.h"

VaneProcessor::VaneProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Vane", createParameterLayout())
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

    // Default modulation routes — replaceable once a patch editor exists.
    //
    // Breath: CC2 (Yamaha WX-11, many others) and CC11 (Sylphyo default,
    // Expression pedals) are both wired to VCA and filter cutoff.
    // Controllers typically send one or the other; they sum here but since
    // only one is active at a time the result is always in range.
    modMatrix.addRoute(ModSourceID::CC + 2,  ModDestID::VCALevel,     1.0f,  5.0f, 80.0f);
    modMatrix.addRoute(ModSourceID::CC + 11, ModDestID::VCALevel,     1.0f,  5.0f, 80.0f);
    modMatrix.addRoute(ModSourceID::CC + 2,  ModDestID::FilterCutoff, 0.5f,  5.0f, 80.0f);
    modMatrix.addRoute(ModSourceID::CC + 11, ModDestID::FilterCutoff, 0.5f,  5.0f, 80.0f);
    // MPE pressure → VCA (per-note expressive swell on pads / aftertouch)
    modMatrix.addRoute(ModSourceID::MPE_Pressure, ModDestID::VCALevel, 0.5f, 3.0f, 50.0f);
    // MPE slide (CC74, per-note) → filter cutoff.
    // Slide is bipolar in SynthVoice: neutral = 0, full up = +1, full down = -1.
    // amount 0.5 → ±2 octave sweep around the base cutoff (audible but not extreme).
    modMatrix.addRoute(ModSourceID::MPE_Slide, ModDestID::FilterCutoff, 0.5f, 2.0f, 20.0f);
}

VaneProcessor::~VaneProcessor() = default;

void VaneProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);
    modMatrix.prepare(sampleRate, samplesPerBlock);

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
            v->prepare(sampleRate, samplesPerBlock);
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

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filterCutoff", 1}, "Filter Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.0f, 0.25f), 8000.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"filterRes", 1}, "Filter Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.3f));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"filterMode", 1}, "Filter Mode",
        juce::StringArray{"LP", "BP", "HP"}, 0));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"oscDetune", 1}, "Osc Detune",
        juce::NormalisableRange<float>(-100.0f, 100.0f), 0.0f));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"oscWave", 1}, "Osc Waveform",
        juce::StringArray{"Sine", "Triangle", "Saw", "Square", "Noise"}, 2));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"glideTime", 1}, "Glide Time",
        juce::NormalisableRange<float>(0.0f, 2000.0f, 0.0f, 0.5f), 0.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"masterTune", 1}, "Master Tune (cents)",
        juce::NormalisableRange<float>(-100.0f, 100.0f), 0.0f));

    // How much note-on velocity contributes to the VCA floor.
    //   0.0 = Wind/breath mode: VCA is entirely from the mod matrix (CC2/CC11/pressure).
    //         Silence at zero breath regardless of which key was struck. Correct for
    //         Sylphyo, WX-11, EWI, and similar instruments.
    //   1.0 = Keyboard mode: velocity is the amplitude base; CC/pressure add on top.
    //   0.x = Hybrid: partial velocity accent on top of breath control.
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"velocityMix", 1}, "Velocity → VCA",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Mono mode: single-voice legato with oscillator phase continuity across note
    // transitions.  In poly mode the voice-kill mechanism is disabled so chords work
    // normally.  Default off (poly) so keyboard players get expected polyphony.
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"monoMode", 1}, "Mono", false));

    return layout;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VaneProcessor();
}
