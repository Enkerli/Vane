#include "SynthVoice.h"
#include <cmath>

SynthVoice::SynthVoice(ModMatrix& matrix, TuningClient& t,
                        std::atomic<float>*    wave,      std::atomic<float>*    detune,
                        std::atomic<float>*    cutoff,    std::atomic<float>*    res,
                        std::atomic<float>*    filterMode,std::atomic<float>*    velocityMix,
                        std::atomic<float>*    glide,     std::atomic<float>*    lastNoteHz,
                        std::atomic<float>*    lastVCALevel,
                        std::atomic<uint32_t>* legatoGen, std::atomic<float>*    lastOscPhase,
                        std::atomic<float>*    mono,
                        std::atomic<float>*    filterS1,  std::atomic<float>*    filterS2,
                        std::atomic<float>*    cutoffHz)
    : modMatrix(matrix), tuning(t)
    , paramWave(wave), paramDetune(detune)
    , paramCutoff(cutoff), paramRes(res)
    , paramFilterMode(filterMode), paramVelocityMix(velocityMix)
    , paramGlide(glide), sharedLastNoteHz(lastNoteHz)
    , sharedLastVCALevel(lastVCALevel)
    , sharedLegatoGen(legatoGen), sharedOscPhase(lastOscPhase)
    , paramMono(mono)
    , sharedFilterS1(filterS1), sharedFilterS2(filterS2), sharedCutoffHz(cutoffHz) {}

void SynthVoice::prepare(double sr, int blockSize)
{
    sampleRate = sr;
    osc.prepare(sr);
    filter.prepare(sr);

    // Build per-voice slewers matching each route's attack/release config.
    // Routes are finalized in the VaneProcessor constructor before prepare() is
    // ever called, so this snapshot is stable for the lifetime of the session.
    modMatrix.initVoiceSlewers(voiceSlewers, sr, blockSize);

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

    // Cutoff smoother: default to the base parameter value; overridden below for
    // mono legato so the new voice's filter starts at the exact Hz the old voice
    // was using, avoiding a coefficient mismatch that would corrupt the state transfer.
    float initCutoff = paramCutoff ? paramCutoff->load() : 4000.0f;
    smoothedCutoff.setCurrentAndTargetValue(initCutoff);

    // VCA: initialise from the shared last-active level so that a new voice on a
    // legato note starts at the current breath amplitude, not at zero.
    // Published as smoothedVCA × tailLevel (see renderNextBlock publish comment),
    // so this correctly reads near-zero for a tailing-off voice.
    float initVCA  = sharedLastVCALevel ? sharedLastVCALevel->load() : 0.0f;
    // isLegato is derived early — it gates portamento, phase sync, and filter sync.
    // Threshold 0.02: breath clearly off → non-legato attack; clearly on → legato.
    bool  isLegato = (initVCA > 0.02f);
    smoothedVCA.setCurrentAndTargetValue(initVCA);

    // Portamento: glide from the previous note's pitch if glideTime > 0 AND the
    // transition is legato (breath was continuously on).  Non-legato attacks snap
    // to pitch immediately regardless of glideTime — otherwise a note played after
    // a long silence glides in from the previous pitch (audibly wrong), and a
    // false-legato from a tailing-off voice would also produce the wrong initial pitch.
    float glideMs = paramGlide ? paramGlide->load() : 0.0f;
    float prevHz  = sharedLastNoteHz ? sharedLastNoteHz->load() : 0.0f;
    smoothedHz.reset(sampleRate, static_cast<double>(glideMs) * 0.001);
    if (isLegato && glideMs > 0.0f && prevHz > 0.0f) {
        smoothedHz.setCurrentAndTargetValue(prevHz);
        smoothedHz.setTargetValue(baseHz);
    } else {
        smoothedHz.setCurrentAndTargetValue(baseHz);
    }
    if (sharedLastNoteHz) sharedLastNoteHz->store(baseHz);

    // Mono mode: voice-kill + phase-sync handoff.
    //
    // In mono mode we always increment legatoGeneration — legato or not.
    // This is the key invariant: only ONE voice holds myLegatoGen == *sharedLegatoGen,
    // so only that voice publishes sharedOscPhase at the end of renderNextBlock.
    // If non-legato notes adopted the current gen without incrementing, any surviving
    // tail-off voice with the same gen would also publish — overwriting sharedOscPhase
    // with its arbitrary frozen phase, poisoning the next legato note's phase sync.
    //
    // In poly mode the kill mechanism is disabled entirely so voices can coexist.
    bool isMono = paramMono && (paramMono->load() > 0.5f);

    if (isMono && sharedLegatoGen)
        myLegatoGen = ++(*sharedLegatoGen);
    else if (!isMono && sharedLegatoGen)
        myLegatoGen = sharedLegatoGen->load();   // adopt current gen, no kill

    // Phase + filter state sync: mono legato notes only.
    //
    // Non-legato notes start at near-zero VCA (breath was off), so the oscillator's
    // frozen phase is inaudible and there is nothing meaningful to continue from.
    // In poly mode, each voice starts independently — no shared state.
    //
    // Oscillator: sharedOscPhase was written by osc.getPhase() after the old voice's
    // last next() call.  Because next() increments phase before returning, getPhase()
    // already holds the phase for *this* voice's first sample — no extra phaseInc.
    //
    // Filter: even with identical oscillator phase, a new SVF with different integrator
    // states (s1, s2) produces a different output → multi-sample step-response transient
    // audible as a click.  Fix: prime the new filter with the same coefficients the old
    // voice was using (setResonance then setCutoff), then restore s1/s2.  The coefficient
    // prime MUST come before setState so the states are interpreted correctly.
    if (isMono && isLegato) {
        if (sharedOscPhase)
            osc.reset(sharedOscPhase->load());

        if (sharedCutoffHz && sharedFilterS1 && sharedFilterS2) {
            float c = sharedCutoffHz->load();
            float r = paramRes ? paramRes->load() : 0.3f;
            // Prime coefficients to match the old voice's last filter state.
            filter.setResonance(r);
            filter.setCutoff(c);
            // Restore integrator states — now valid under these coefficients.
            filter.setState(sharedFilterS1->load(), sharedFilterS2->load());
            // Snap the cutoff smoother to the inherited Hz so the first block's
            // per-sample setCutoff() calls start from the right place.
            smoothedCutoff.setCurrentAndTargetValue(c);
        }
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

    // Mono voice-kill: in mono mode, if a newer note has started while we were
    // tailing off, die immediately rather than ghost alongside the new voice and
    // cause phase-beating.  Checked at the top of every sub-block so mid-block
    // MIDI events (JUCE splits blocks at note positions) are handled correctly.
    // In poly mode this check is skipped so voices can coexist freely.
    bool monoNow = paramMono && (paramMono->load() > 0.5f);
    if (monoNow && sharedLegatoGen && sharedLegatoGen->load() != myLegatoGen) {
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
    auto mods = modMatrix.evaluate(voiceVals, voiceSlewers);

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
    // 5-octave scale: mods[FilterCutoff] in [-1..+1] sweeps ±5 octaves from base.
    // With baseCutoff ≈ 1200 Hz: CC74 full-down → ~53 Hz, full-up → ~20 kHz (clamped).
    // Wider than the old ×4 so slide spans the full audible range.
    float targetCutoff = baseCutoff * std::pow(2.0f, mods[ModDestID::FilterCutoff] * 5.0f);
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
    //
    // VCA: published as smoothedVCA × tailLevel — the actual output amplitude, not the
    //   raw smoothed level.  This is critical: a tailing-off voice may have
    //   smoothedVCA=0.87 (breath was recently high) but tailLevel=0.04 (signal is
    //   barely audible).  Publishing 0.87 without the tailLevel factor causes the next
    //   voice to start with initVCA=0.87, triggering a false-legato (isLegato=true)
    //   and a 22× amplitude jump at every attack-from-tail-off note.
    //   Publishing the product (≈ 0.035) correctly identifies the note as non-legato
    //   (< 0.02 threshold) and lets the new voice ramp up cleanly from near-silence.
    //
    // Phase: osc.next() increments phase before returning, so getPhase() already
    //   holds the phase for the *next* sample.  noteStarted() passes this directly
    //   to osc.reset() — no extra advance needed — giving a sample-exact handoff.
    //
    // Filter: s1/s2 are the SVF integrator states after the last process() call.
    //   Transferring them to the new voice (along with the exact cutoff Hz used)
    //   eliminates the step-response transient that caused multi-sample clicks on
    //   every mono legato transition.
    //
    // In mono mode, only the voice that passed the gen check above reaches here,
    // so these atomics are always written by exactly the current voice.
    // In poly mode, all voices publish — the last one to render "wins", which is
    // fine since state sync is only used when returning to mono.
    if (sharedLastVCALevel) sharedLastVCALevel->store(smoothedVCA.getCurrentValue() * tailLevel);
    if (sharedOscPhase)     sharedOscPhase->store(osc.getPhase());
    if (sharedFilterS1 && sharedFilterS2) {
        float s1, s2;
        filter.getState(s1, s2);
        sharedFilterS1->store(s1);
        sharedFilterS2->store(s2);
    }
    if (sharedCutoffHz) sharedCutoffHz->store(smoothedCutoff.getCurrentValue());
}
