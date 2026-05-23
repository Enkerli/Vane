#include "SynthVoice.h"
#include <cmath>

SynthVoice::SynthVoice(ModMatrix& matrix, TuningClient& t,
                        std::atomic<float>*    wave,      std::atomic<float>*    detune,
                        std::atomic<float>*    cutoff,    std::atomic<float>*    res,
                        std::atomic<float>*    filterMode,std::atomic<float>*    velocityMix,
                        std::atomic<float>*    glide,     std::atomic<float>*    lastNoteHz,
                        std::atomic<float>*    lastVCALevel,
                        std::atomic<uint32_t>* legatoGen, std::atomic<float>*    lastOscPhase)
    : modMatrix(matrix), tuning(t)
    , paramWave(wave), paramDetune(detune)
    , paramCutoff(cutoff), paramRes(res)
    , paramFilterMode(filterMode), paramVelocityMix(velocityMix)
    , paramGlide(glide), sharedLastNoteHz(lastNoteHz)
    , sharedLastVCALevel(lastVCALevel)
    , sharedLegatoGen(legatoGen), sharedOscPhase(lastOscPhase) {}

void SynthVoice::prepare(double sr, int /*blockSize*/)
{
    sampleRate = sr;
    osc.prepare(sr);
    filter.prepare(sr);

    // 3 ms ramp: long enough to bridge any block boundary without audible lag.
    // Geometric interpolation keeps filter sweeps perceptually linear in pitch.
    float initCutoff = paramCutoff ? paramCutoff->load() : 4000.0f;
    smoothedCutoff.reset(sr, 0.003);
    smoothedCutoff.setCurrentAndTargetValue(initCutoff);

    // Pitch smoother: time is set per-note from glideTime; snap to 440 for now.
    smoothedHz.reset(sr, 0.0);
    smoothedHz.setCurrentAndTargetValue(440.0f);

    // VCA smoother: 3 ms ramp eliminates amplitude staircase at block boundaries.
    // Linear (not Multiplicative) because amplitude can start from zero.
    smoothedVCA.reset(sr, 0.003);
    smoothedVCA.setCurrentAndTargetValue(0.0f);
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

    // Do NOT reset the oscillator phase: starting from an arbitrary frozen phase
    // causes no discontinuity (the waveform is continuous), whereas osc.reset()
    // would snap to phase=0 mid-signal — the audible click on every legato note.
    //
    // Do NOT reset the filter state: after tail-off, s1/s2 have decayed toward
    // zero alongside the signal, so the state is already clean.  Resetting them
    // to 0 when the oscillator is not at phase=0 would cause a brief step-response
    // transient — the "attack bloop" on legato transitions.

    // Snap the cutoff smoother; target is recalculated on the first renderNextBlock.
    float initCutoff = paramCutoff ? paramCutoff->load() : 4000.0f;
    smoothedCutoff.setCurrentAndTargetValue(initCutoff);

    // VCA: initialise from the shared last-active level so that a new voice on a
    // legato note starts at the current breath amplitude, not at zero.
    float initVCA = sharedLastVCALevel ? sharedLastVCALevel->load() : 0.0f;
    smoothedVCA.setCurrentAndTargetValue(initVCA);

    // Portamento: glide from the previous note's pitch if glideTime > 0.
    float glideMs = paramGlide ? paramGlide->load() : 0.0f;
    float prevHz  = sharedLastNoteHz ? sharedLastNoteHz->load() : 0.0f;
    smoothedHz.reset(sampleRate, static_cast<double>(glideMs) * 0.001);
    if (glideMs > 0.0f && prevHz > 0.0f) {
        smoothedHz.setCurrentAndTargetValue(prevHz);
        smoothedHz.setTargetValue(baseHz);
    } else {
        smoothedHz.setCurrentAndTargetValue(baseHz);
    }
    if (sharedLastNoteHz) sharedLastNoteHz->store(baseHz);

    // Legato phase continuity + voice kill.
    // When breath is flowing (isLegato), this note must be the only voice sounding.
    // We:
    //   a) increment legatoGeneration so any currently tailing voice sees a new
    //      generation at the top of its next renderNextBlock and dies immediately
    //      — eliminating the two-voice phase-beating that causes the audible bloop.
    //   b) sync the oscillator phase from sharedOscPhase (published each block by
    //      the previous voice) so the waveform is continuous at the handoff point.
    bool isLegato = (initVCA > 0.02f);
    if (isLegato && sharedLegatoGen) {
        myLegatoGen = ++(*sharedLegatoGen);   // atomically bump + read new value
        // Phase sync: start one sample ahead of where the old voice left off.
        // sharedOscPhase holds the phase at the last sample of the old voice's
        // most recent block; advancing by phaseInc gives sample 0 of this block.
        if (sharedOscPhase) {
            // sharedOscPhase is written by osc.getPhase() after the old voice's
            // final next() call.  Because next() advances phase *before* returning,
            // getPhase() already holds the phase for the first sample of this block —
            // exactly where the new voice should start.  Adding an extra phaseInc
            // would overshoot by one sample and create a ~6 % amplitude step click.
            osc.reset(sharedOscPhase->load());
        }
    } else {
        // Non-legato (first note from silence): just adopt the current generation
        // without killing anything, and leave the oscillator at its frozen phase.
        myLegatoGen = sharedLegatoGen ? sharedLegatoGen->load() : 0;
    }

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

    // Legato voice-kill: if a newer note has started while we were tailing off,
    // die immediately rather than ghost alongside the new voice and cause the
    // phase-beating bloop.  Checked at the top of every sub-block so mid-block
    // MIDI events (JUCE splits blocks at note positions) are handled correctly.
    if (sharedLegatoGen && sharedLegatoGen->load() != myLegatoGen) {
        active = false; isTailingOff = false;
        clearCurrentNote();
        return;
    }

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

    // VCA target for this block — computed once, then interpolated per-sample via
    // smoothedVCA to eliminate the block-boundary amplitude steps (AM sidebands at
    // ~689 Hz) that cause audible crunchiness during breath/aftertouch sweeps.
    float veloMix  = paramVelocityMix ? paramVelocityMix->load() : 0.0f;
    float vcaLevel = std::clamp(veloMix * std::sqrt(velocity)
                                + mods[ModDestID::VCALevel], 0.0f, 1.0f);
    smoothedVCA.setTargetValue(vcaLevel);

    // Pitch: pitchbend (±48 st) + mod matrix fine tune + detune, as a per-block
    // multiplier applied on top of the per-sample smoothed base frequency.
    // Keeping std::pow() out of the sample loop avoids per-sample exp() cost.
    constexpr float kBendRangeSemitones = 48.0f;
    float totalSemitones = pitchbend * kBendRangeSemitones
                         + mods[ModDestID::OscPitchFine]
                         + detuneCents / 100.0f;
    float pitchMult = std::pow(2.0f, totalSemitones / 12.0f);
    if (!std::isfinite(pitchMult) || pitchMult <= 0.0f) pitchMult = 1.0f;

    // Filter: resonance and mode are block-rate parameters — set once per block.
    // Cutoff is smoothed per-sample via smoothedCutoff to eliminate the coefficient
    // step changes at block boundaries that cause crunchiness during breath sweeps.
    float baseCutoff   = paramCutoff ? paramCutoff->load() : 4000.0f;
    float baseRes      = paramRes    ? paramRes->load()    : 0.3f;
    float targetCutoff = baseCutoff * std::pow(2.0f, mods[ModDestID::FilterCutoff] * 4.0f);
    float resonance    = std::clamp(baseRes + mods[ModDestID::FilterRes], 0.0f, 1.0f);
    int   modeIndex    = paramFilterMode ? static_cast<int>(paramFilterMode->load()) : 0;
    auto  filterMode   = static_cast<SVFilter::Mode>(juce::jlimit(0, 2, modeIndex));

    // Resonance sets k; cutoff (g, a1-a3) updates happen per-sample below.
    filter.setResonance(resonance);
    smoothedCutoff.setTargetValue(juce::jlimit(20.0f, 20000.0f, targetCutoff));

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
        // Advance all three smoothers one sample each.
        filter.setCutoff(smoothedCutoff.getNextValue());  // smooth filter cutoff
        osc.setFrequency(smoothedHz.getNextValue() * pitchMult);  // portamento
        float gain = smoothedVCA.getNextValue();          // smooth VCA amplitude
        float sample = filter.process(osc.next(), filterMode) * gain * tailLevel;
        left[i] += sample;
        if (right) right[i] += sample;
    }

    // Publish state for the next voice to inherit.
    // VCA: next noteStarted() reads this to initialise smoothedVCA seamlessly.
    // Phase: osc.next() increments phase before returning, so getPhase() already
    //   holds the phase for the *next* sample.  noteStarted() passes this directly
    //   to osc.reset() — no extra advance needed — giving a sample-exact handoff.
    if (sharedLastVCALevel) sharedLastVCALevel->store(smoothedVCA.getCurrentValue());
    if (sharedOscPhase)     sharedOscPhase->store(osc.getPhase());
}
