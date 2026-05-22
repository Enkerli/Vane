#include "SynthVoice.h"
#include <cmath>

SynthVoice::SynthVoice(ModMatrix& matrix, TuningClient& t,
                        std::atomic<float>* wave,   std::atomic<float>* detune,
                        std::atomic<float>* cutoff, std::atomic<float>* res,
                        std::atomic<float>* filterMode)
    : modMatrix(matrix), tuning(t)
    , paramWave(wave), paramDetune(detune)
    , paramCutoff(cutoff), paramRes(res), paramFilterMode(filterMode) {}

void SynthVoice::prepare(double sr, int /*blockSize*/)
{
    sampleRate = sr;
    osc.prepare(sr);
    filter.prepare(sr);
}

void SynthVoice::noteStarted()
{
    const auto& note = currentlyPlayingNote;
    velocity  = note.noteOnVelocity.asUnsignedFloat();
    pressure  = note.pressure.asUnsignedFloat();
    slide     = note.timbre.asUnsignedFloat();
    pitchbend = note.pitchbend.asSignedFloat();
    baseHz    = tuning.noteToHz(note.initialNote, note.midiChannel);

    osc.reset();
    filter.reset();
    tailLevel    = 1.0f;
    isTailingOff = false;
    active       = true;
}

void SynthVoice::noteStopped(bool allowTailOff)
{
    if (allowTailOff) {
        isTailingOff = true;
    } else {
        active       = false;
        isTailingOff = false;
        clearCurrentNote();
    }
}

void SynthVoice::notePressureChanged()
{
    pressure = currentlyPlayingNote.pressure.asUnsignedFloat();
}

void SynthVoice::notePitchbendChanged()
{
    pitchbend = currentlyPlayingNote.pitchbend.asSignedFloat();
    baseHz = tuning.noteToHz(currentlyPlayingNote.initialNote,
                              currentlyPlayingNote.midiChannel);
}

void SynthVoice::noteTimbreChanged()
{
    slide = currentlyPlayingNote.timbre.asUnsignedFloat();
}

void SynthVoice::noteKeyStateChanged() {}

void SynthVoice::renderNextBlock(juce::AudioBuffer<float>& buffer,
                                  int startSample, int numSamples)
{
    if (!active) return;

    // Read waveform and detune from APVTS (atomic, safe on audio thread)
    auto waveIndex = static_cast<int>(paramWave   ? paramWave->load()   : 2.0f); // default: Saw
    auto detuneCents =                paramDetune ? paramDetune->load() : 0.0f;

    osc.setWaveform(static_cast<Oscillator::Waveform>(
        juce::jlimit(0, 4, waveIndex)));

    // Evaluate modulation for this voice
    const std::array<float, ModSourceID::NumVoiceSources> voiceVals {
        pressure, slide, pitchbend, velocity
    };
    auto mods = modMatrix.evaluate(voiceVals);

    // VCA base = velocity, so keyboard notes always play without any CC or pressure.
    // The mod matrix routes (CC2, MPE pressure) ADD to this:
    //   wind controller at full breath  → mods[VCA] = 1.0 → clamped to 1.0
    //   wind controller at zero breath  → mods[VCA] = 0.0 → VCA = velocity (quiet but audible)
    // To get "totally silent at zero breath", route breath with amount = -velocity and
    // combine in the patch — that's a later concern once the mod matrix has an editor.
    float vcaLevel = std::clamp(velocity + mods[ModDestID::VCALevel], 0.0f, 1.0f);

    // Pitch: MTS base + MPE pitch bend (±48 st) + mod matrix fine tune + APVTS detune
    constexpr float kBendRangeSemitones = 48.0f;
    float totalSemitones = pitchbend * kBendRangeSemitones
                         + mods[ModDestID::OscPitchFine]
                         + detuneCents / 100.0f;
    float hz = baseHz * std::pow(2.0f, totalSemitones / 12.0f);
    osc.setFrequency(hz);

    // Filter: base cutoff from APVTS, offset by mod matrix (additive, in normalised 0..1)
    // Map normalised mod offset to Hz: ±1 spans ±4 octaves of the base cutoff
    float baseCutoff = paramCutoff ? paramCutoff->load() : 4000.0f;
    float baseRes    = paramRes    ? paramRes->load()    : 0.3f;
    float cutoffMod  = mods[ModDestID::FilterCutoff];  // -1..1
    float cutoffHz   = baseCutoff * std::pow(2.0f, cutoffMod * 4.0f);  // ±4 octaves
    float resonance  = std::clamp(baseRes + mods[ModDestID::FilterRes], 0.0f, 1.0f);
    int   modeIndex  = paramFilterMode ? static_cast<int>(paramFilterMode->load()) : 0;
    auto  filterMode = static_cast<SVFilter::Mode>(juce::jlimit(0, 2, modeIndex));
    filter.setParameters(cutoffHz, resonance);

    auto* left  = buffer.getWritePointer(0, startSample);
    auto* right = buffer.getNumChannels() > 1
                  ? buffer.getWritePointer(1, startSample) : nullptr;

    for (int i = 0; i < numSamples; ++i) {
        if (isTailingOff) {
            tailLevel *= 0.9995f;
            if (tailLevel < 0.0001f) {
                active       = false;
                isTailingOff = false;
                clearCurrentNote();
                break;
            }
        }

        float sample = filter.process(osc.next(), filterMode) * vcaLevel * tailLevel;
        left[i] += sample;
        if (right) right[i] += sample;
    }
}
