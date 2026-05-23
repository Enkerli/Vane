#include "SynthVoice.h"
#include <cmath>

SynthVoice::SynthVoice(ModMatrix& matrix, TuningClient& t,
                        std::atomic<float>* wave,        std::atomic<float>* detune,
                        std::atomic<float>* cutoff,      std::atomic<float>* res,
                        std::atomic<float>* filterMode,  std::atomic<float>* velocityMix)
    : modMatrix(matrix), tuning(t)
    , paramWave(wave), paramDetune(detune)
    , paramCutoff(cutoff), paramRes(res)
    , paramFilterMode(filterMode), paramVelocityMix(velocityMix) {}

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

    // MTS_ShouldFilterNote: this pitch is silenced in the current tuning.
    // Mark inactive immediately so renderNextBlock produces nothing and the
    // SVFilter never sees a 0-Hz (DC) signal that would poison its state.
    if (baseHz <= 0.0f) {
        active       = false;
        isTailingOff = false;
        clearCurrentNote();
        return;
    }

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

    auto waveIndex   = static_cast<int>(paramWave   ? paramWave->load()   : 2.0f);
    auto detuneCents =                  paramDetune ? paramDetune->load() : 0.0f;

    osc.setWaveform(static_cast<Oscillator::Waveform>(juce::jlimit(0, 4, waveIndex)));

    // MPE slide (CC74) is unipolar 0..1 with neutral at 0.5.
    // Convert to bipolar -1..+1 so the ModMatrix route sweeps symmetrically
    // around the base cutoff rather than always pushing it upward.
    // At neutral (0.5) → 0 contribution; full up → +1; full down → -1.
    float slideBipolar = (slide - 0.5f) * 2.0f;

    const std::array<float, ModSourceID::NumVoiceSources> voiceVals {
        pressure, slideBipolar, pitchbend, velocity
    };
    auto mods = modMatrix.evaluate(voiceVals);

    // VCA: velocityMix blends between pure-breath (0) and keyboard (1) behaviour.
    //   Wind mode (0): VCA = mod matrix only → silence at zero CC2/pressure.
    //   Keyboard (1):  VCA = sqrt(velocity) + mod matrix.
    float veloMix  = paramVelocityMix ? paramVelocityMix->load() : 0.0f;
    float vcaLevel = std::clamp(veloMix * std::sqrt(velocity)
                                + mods[ModDestID::VCALevel], 0.0f, 1.0f);

    // Pitch: MTS base + MPE pitchbend (±48 st) + mod matrix fine tune + detune
    constexpr float kBendRangeSemitones = 48.0f;
    float totalSemitones = pitchbend * kBendRangeSemitones
                         + mods[ModDestID::OscPitchFine]
                         + detuneCents / 100.0f;
    float hz = baseHz * std::pow(2.0f, totalSemitones / 12.0f);
    if (!std::isfinite(hz) || hz <= 0.0f) hz = 440.0f;  // guard against bad MTS + pitchbend
    osc.setFrequency(hz);

    // Filter: base cutoff from APVTS; mod matrix offset is ±4 octaves at ±1.
    float baseCutoff = paramCutoff ? paramCutoff->load() : 4000.0f;
    float baseRes    = paramRes    ? paramRes->load()    : 0.3f;
    float cutoffHz   = baseCutoff * std::pow(2.0f, mods[ModDestID::FilterCutoff] * 4.0f);
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
                active = false; isTailingOff = false;
                clearCurrentNote();
                break;
            }
        }
        float sample = filter.process(osc.next(), filterMode) * vcaLevel * tailLevel;
        left[i] += sample;
        if (right) right[i] += sample;
    }
}
