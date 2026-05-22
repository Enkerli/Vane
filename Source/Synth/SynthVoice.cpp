#include "SynthVoice.h"
#include <cmath>

SynthVoice::SynthVoice(ModMatrix& matrix, TuningClient& t)
    : modMatrix(matrix), tuning(t) {}

void SynthVoice::prepare(double sr, int /*blockSize*/)
{
    sampleRate = sr;
}

void SynthVoice::noteStarted()
{
    const auto& note = currentlyPlayingNote;
    velocity  = note.noteOnVelocity.asUnsignedFloat();
    pressure  = note.pressure.asUnsignedFloat();
    slide     = note.timbre.asUnsignedFloat();
    pitchbend = note.pitchbend.asSignedFloat();
    baseHz    = tuning.noteToHz(note.initialNote, note.midiChannel);

    phase        = 0.0f;
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
    // Re-resolve MTS base in case MTS accounts for pitch-bend range too
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

    const std::array<float, ModSourceID::NumVoiceSources> voiceVals {
        pressure, slide, pitchbend, velocity
    };
    auto mods = modMatrix.evaluate(voiceVals);

    // VCA: open proportionally to pressure (+ any CC mod, e.g. breath on CC2)
    // mods[ModDestID::VCALevel] is an additive offset in -1..1
    float vcaBase  = pressure;                                // breath/pressure drives amplitude
    float vcaLevel = std::clamp(vcaBase + mods[ModDestID::VCALevel], 0.0f, 1.0f);

    // Pitch: MTS base + MPE pitch bend (±48 semitones default MPE range)
    // mods[ModDestID::OscPitchFine] is in semitones, additive
    constexpr float kPitchBendRangeSemitones = 48.0f;
    float totalSemitones = pitchbend * kPitchBendRangeSemitones
                         + mods[ModDestID::OscPitchFine];
    float hz = baseHz * std::pow(2.0f, totalSemitones / 12.0f);

    // ── Oscillator placeholder (sine) ────────────────────────────────────────
    // Replace with PolyBLEP saw/square and then wavetable when ready.
    const float phaseInc = static_cast<float>(hz / sampleRate);
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

        float sample = std::sin(phase * juce::MathConstants<float>::twoPi)
                       * vcaLevel * tailLevel;

        left[i] += sample;
        if (right) right[i] += sample;

        phase += phaseInc;
        if (phase >= 1.0f) phase -= 1.0f;
    }
}

float SynthVoice::baseFrequencyHz() const
{
    return baseHz;
}
