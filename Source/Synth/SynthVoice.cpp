#include "SynthVoice.h"
#include <cmath>

SynthVoice::SynthVoice(ModMatrix& matrix, TuningClient& t,
                        std::atomic<float>*    morphPos,  std::atomic<float>*    detune,
                        std::atomic<float>*    pw,        std::atomic<float>*    cutoff,
                        std::atomic<float>*    res,
                        std::atomic<float>*    filterMode,std::atomic<float>*    velocityMix,
                        std::atomic<float>*    glide,     std::atomic<float>*    lastNoteHz,
                        std::atomic<float>*    lastVCALevel,
                        std::atomic<uint32_t>* legatoGen, std::atomic<float>*    lastOscPhase,
                        std::atomic<float>*    lastPmPhase,
                        std::atomic<float>*    mono,
                        std::atomic<float>*    filterS1,  std::atomic<float>*    filterS2,
                        std::atomic<float>*    cutoffHz,
                        std::atomic<float>*    meterPressure,
                        std::atomic<float>*    meterSlide,
                        std::atomic<float>*    meterPitchbend,
                        std::atomic<float>*    pbRange,
                        std::atomic<float>*    nonMPEPBRange,
                        std::atomic<float>*    glideMode,
                        std::atomic<float>*    glideCurve,
                        std::atomic<float>*    noiseBlend,
                        std::atomic<float>*    noiseType,
                        std::atomic<float>*    fold,
                        std::atomic<float>*    inharm,
                        std::atomic<float>*    sync)
    : modMatrix(matrix), tuning(t)
    , paramMorphPos(morphPos), paramPW(pw), paramDetune(detune)
    , paramCutoff(cutoff), paramRes(res)
    , paramFilterMode(filterMode), paramVelocityMix(velocityMix)
    , paramGlide(glide), sharedLastNoteHz(lastNoteHz)
    , sharedLastVCALevel(lastVCALevel)
    , sharedLegatoGen(legatoGen), sharedOscPhase(lastOscPhase)
    , sharedPmPhase(lastPmPhase)
    , paramMono(mono)
    , sharedFilterS1(filterS1), sharedFilterS2(filterS2), sharedCutoffHz(cutoffHz)
    , sharedMeterPressure(meterPressure)
    , sharedMeterSlide(meterSlide)
    , sharedMeterPitchbend(meterPitchbend)
    , paramPBRange(pbRange)
    , paramNonMPEPBRange(nonMPEPBRange)
    , paramGlideMode(glideMode)
    , paramGlideCurve(glideCurve)
    , paramNoiseBlend(noiseBlend)
    , paramNoiseType(noiseType)
    , paramFold(fold)
    , paramInharm(inharm)
    , paramSync(sync) {}

void SynthVoice::prepare(double sr, int blockSize)
{
    sampleRate = sr;
    osc.prepare(sr);
    for (auto& o : unisonOscs) o.prepare(sr);
    filter.prepare(sr);
    filterR.prepare(sr);
    transientFilter.prepare(sr);   // shares the voice filter's coeffs when routed
    transientReso.prepare(sr);     // pitch resonator delay line

    // Build per-voice slewers matching each route's attack/release config.
    // Routes are finalized in the VaneProcessor constructor before prepare() is
    // ever called, so this snapshot is stable for the lifetime of the session.
    // Each voice gets its own set so that two simultaneous MPE notes with different
    // slide positions don't bleed into each other through shared slewer state.
    modMatrix.initVoiceSlewers(voiceSlewers, sr, blockSize);

    // 3 ms ramp: long enough to bridge any block boundary without audible lag.
    // Geometric interpolation keeps filter sweeps perceptually linear in pitch.
    float initCutoff = paramCutoff ? paramCutoff->load() : 4000.0f;
    smoothedCutoff.reset(sr, 0.003);
    smoothedCutoff.setCurrentAndTargetValue(initCutoff);

    // Pitch smoother: time is set per-note from glideTime; snap to 440 for now.
    smoothedHz.reset(sr, 0.0);
    smoothedHz.setCurrentAndTargetValue(440.0f);

    // Pitchbend multiplier smoother: 3 ms ramp eliminates block-boundary steps.
    //
    // Why Multiplicative: the pitchbend is a frequency multiplier (2^(semitones/12)).
    // Linear interpolation between multipliers compresses the lower semitones
    // and stretches the upper ones — audible as asymmetric vibrato width.
    // Multiplicative interpolation (geometric ramp) keeps semitone distance
    // perceptually even throughout the glide, matching how pitch is heard.
    //
    // Why 3 ms: fast enough not to lag live vibrato (Sylphyo shake rate ~5 Hz),
    // slow enough to bridge the ~11 ms block boundary at 44100 / 512 without
    // audible stepping.  The same 3 ms is used for smoothedCutoff for consistency.
    //
    // What this fixed: without smoothing, each block-rate PB update produced a
    // step in the oscillator frequency.  At ±2 st range, each step was ~1.2 Hz
    // at 440 Hz — barely audible.  At ±48 st (Exquis), each step spanned up to
    // 28 Hz, making vibrato sound like a trill of discrete pitches.
    smoothedPitchMult.reset(sr, 0.003);
    smoothedPitchMult.setCurrentAndTargetValue(1.0f);

    // VCA smoother: 3 ms ramp eliminates amplitude staircase at block boundaries.
    // Linear (not Multiplicative) because amplitude can start from zero.
    smoothedVCA.reset(sr, 0.003);
    smoothedVCA.setCurrentAndTargetValue(0.0f);

    // PW smoother: 3 ms ramp prevents the spectral pop at note-on when CC74
    // drives PW to an extreme value before the VCA has opened.  See header.
    smoothedPW.reset(sr, 0.003);
    smoothedPW.setCurrentAndTargetValue(0.5f);

    // Fold-drive smoother: 3 ms ramp prevents zipper on fold-depth changes.
    // Holds drive (1 = transparent), not the 0..1 amount.
    smoothedFoldDrive.reset(sr, 0.003);
    smoothedFoldDrive.setCurrentAndTargetValue(1.0f);

    // Inharmonicity smoother: 3 ms ramp prevents zipper on FM-index changes.
    smoothedInharm.reset(sr, 0.003);
    smoothedInharm.setCurrentAndTargetValue(0.0f);
    smoothedSync.reset(sr, 0.003);
    smoothedSync.setCurrentAndTargetValue(1.0f);   // 1 = sync off

    // Pre-allocate the mono scratch buffer used to render the transient into
    // before adding to both output channels.  Sized to blockSize so no heap
    // activity occurs during renderNextBlock.
    transientScratch.assign(static_cast<size_t>(blockSize), 0.0f);
}

void SynthVoice::noteStarted()
{
    const auto& note = currentlyPlayingNote;
    velocity  = note.noteOnVelocity.asUnsignedFloat();
    pressure  = note.pressure.asUnsignedFloat();
    slide     = note.timbre.asUnsignedFloat();

    // Channel-1 (non-MPE master channel): JUCE never writes note.pitchbend for
    // master-channel PB — only totalPitchbendInSemitones is updated.
    // For the lower zone with default masterPitchbendRange = 2:
    //   totalPitchbendInSemitones = normalizedPB × 2
    // Dividing by 2 recovers normalizedPB in −1..1.
    // Member channels: use note.pitchbend.asSignedFloat() as usual.
    pitchbend = (note.midiChannel <= 1)
                ? static_cast<float>(note.totalPitchbendInSemitones) / 2.0f
                : note.pitchbend.asSignedFloat();

    baseHz    = tuning.noteToHz(note.initialNote, note.midiChannel);

    // Snap the pitchbend smoother to the current PB so note-on is click-free
    // even when PB was already active before the note started.
    {
        const float kBend = (note.midiChannel <= 1)
                            ? (paramNonMPEPBRange ? paramNonMPEPBRange->load() : 2.0f)
                            : (paramPBRange       ? paramPBRange->load()       : 48.0f);
        float pm = std::pow(2.0f, pitchbend * kBend / 12.0f);
        if (!std::isfinite(pm) || pm <= 0.0f) pm = 1.0f;
        smoothedPitchMult.setCurrentAndTargetValue(pm);
    }

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

    // Mono/poly must be known before VCA initialisation — it gates several decisions.
    bool isMono = paramMono && (paramMono->load() > 0.5f);

    // Cutoff smoother: default to the base parameter value; overridden below for
    // mono legato so the new voice's filter starts at the exact Hz the old voice
    // was using, avoiding a coefficient mismatch that would corrupt the state transfer.
    float initCutoff = paramCutoff ? paramCutoff->load() : 4000.0f;
    smoothedCutoff.setCurrentAndTargetValue(initCutoff);

    // VCA: in mono mode, inherit the previous voice's amplitude for seamless legato.
    // In poly mode each note must start from 0 — reading sharedLastVCALevel in poly
    // would pick up a concurrently-playing voice's amplitude (e.g. 0.08), making
    // isLegato fire falsely and producing a VCA blip (0.08 → 0 → ramp up) at every
    // poly attack.  Published as smoothedVCA × tailLevel, so tailing-off voices
    // correctly read near-zero and don't trigger false legato in mono mode either.
    float initVCA  = (isMono && sharedLastVCALevel) ? sharedLastVCALevel->load() : 0.0f;
    // isLegato gates portamento, phase sync, and filter sync.
    // Threshold 0.02: breath clearly off → non-legato attack; clearly on → legato.
    // Chosen empirically — 0.02 (~−34 dB) is inaudible and safely above the
    // ~0.0001 floor of a tailing-off voice near silence.  A threshold too low
    // (e.g. 0.001) would treat any voice that's still releasing as legato, causing
    // glide from an arbitrary previous note that the player never intended.
    // TODO: test whether this threshold needs to be different for non-breath-controller
    // players (keyboards with sustain pedal) where VCA doesn't track air pressure.
    bool  isLegato = (initVCA > 0.02f);
    smoothedVCA.setCurrentAndTargetValue(initVCA);

    // Per-voice slewer reset for non-legato attacks.
    // Each voice slot retains slewer state from the previous note it played.
    // Without a reset, the slide slewer might be at an arbitrary stale position
    // (e.g. the last slide value before this slot was idle), causing a brief
    // filter-position mismatch at the start of the new note — audible as a click,
    // especially at low base cutoffs where the mismatch spans more octaves.
    //
    // At MPE note-on, slide = neutral (0.5 → bipolar 0), pressure = 0, bend = 0.
    // Velocity is snapped immediately (no ramp needed; VCA is still at 0 during
    // the initial 0ms, making any filter transient inaudible anyway).
    //
    // For mono legato we leave slewers untouched — the continuous breath means
    // the slewer was already tracking the physical controls, and the filter-state
    // transfer via sharedCutoffHz handles the frequency continuity.
    if (!isLegato) {
        float slideBp = (slide - 0.5f) * 2.0f;
        const std::array<float, ModSourceID::NumVoiceSources> noteOnVals {
            pressure, slideBp, pitchbend, velocity
        };
        modMatrix.resetVoiceSlewers(voiceSlewers, noteOnVals);

        // Snap PW smoother to identity so the timbral warp arrives with
        // the breath rather than preceding it.  The target is set in the
        // first renderNextBlock() call once activePW is known.
        smoothedPW.setCurrentAndTargetValue(0.5f);
    }
    // Mono legato: smoothedPW is intentionally NOT reset — the PW continues
    // smoothly from the previous note, matching filter-state handoff behaviour.

    // Portamento: glide from the previous note's pitch if glideTime > 0 AND the
    // transition is legato (breath was continuously on).  Non-legato attacks snap
    // to pitch immediately regardless of glideTime — otherwise a note played after
    // a long silence glides in from the previous pitch (audibly wrong), and a
    // false-legato from a tailing-off voice would also produce the wrong initial pitch.
    float glideMs   = paramGlide     ? paramGlide->load()     : 0.0f;
    float prevHz    = sharedLastNoteHz ? sharedLastNoteHz->load() : 0.0f;
    int   glideMode = paramGlideMode  ? static_cast<int>(std::round(paramGlideMode->load()))  : 0;
    int   glideCurv = paramGlideCurve ? static_cast<int>(std::round(paramGlideCurve->load())) : 0;

    // ── Glide duration ───────────────────────────────────────────────────────
    //
    // Fixed Time (0): glideMs is the total ramp time regardless of interval.
    //   A semitone and an octave take the same number of milliseconds.
    //
    // Fixed Rate (1): glideMs is the cost per semitone.  Larger intervals take
    //   proportionally longer, like a cello or theremin slide.  Capped at 5 s
    //   to prevent absurdly long glides across multiple octaves at high settings.
    float nominalGlideMs = glideMs;
    if (glideMode == 1 && isLegato && prevHz > 0.0f && glideMs > 0.0f) {
        float semitones = std::abs(12.0f * std::log2f(baseHz / prevHz));
        nominalGlideMs  = std::min(glideMs * semitones, 5000.0f);
    }

    // ── Minimum legato slope-continuity ramp ─────────────────────────────────
    //
    // Even with glideTime = 0, apply at minimum one period at the target pitch.
    // Without this, phaseInc snaps from prevHz/sr to baseHz/sr in one sample —
    // a slope discontinuity that registers as a brief click, increasingly audible
    // at higher frequencies where the sine changes steeply per sample.
    //
    // One period at baseHz (0.23 ms at 4 kHz, 2.3 ms at 440 Hz) is well below
    // the perceptible portamento threshold (~20 ms), so the player never hears a
    // glide, but the kink is spread across a full cycle instead of a single sample.
    float minLegatoMs = (isLegato && prevHz > 0.0f) ? (1000.0f / baseHz) : 0.0f;
    float effGlideMs  = std::max(nominalGlideMs, minLegatoMs);

    // ── Glide curve ──────────────────────────────────────────────────────────
    //
    // Linear (0): constant semitone rate — Multiplicative smoother.
    // Exponential (1): 1-pole IIR in log2(Hz) — even semitone feel at all pitches.
    // RC (2): 1-pole IIR in linear Hz — true analog circuit; faster in high register.
    //
    // All modes start at prevHz so Voice B's first sample uses the same phaseInc
    // as Voice A's last, preserving both waveform value and slope continuity.
    //
    // Coefficient formula (Exp and RC): c = 1 − exp(−4.6 / N) → 99 % arrival
    // within N = effGlideMs × sr / 1000 samples.
    glideTargetLogHz = std::log2f(std::max(baseHz, 1.0f));  // used by Exp init; RC uses baseHz

    if (glideCurv == 1 /* Exponential — 1-pole IIR in log2(Hz) space */) {
        if (isLegato && effGlideMs > 0.0f && prevHz > 0.0f) {
            glideExpLogHz = std::log2f(prevHz);
            float N        = effGlideMs * 0.001f * static_cast<float>(sampleRate);
            glideExpCoeff  = (N > 0.0f) ? (1.0f - std::exp(-4.6f / N)) : 1.0f;
        } else {
            glideExpLogHz = glideTargetLogHz;
            glideExpCoeff = 1.0f;   // instant snap — non-legato attack
        }
        glideRcHz = baseHz;  glideRcCoeff = 0.0f;   // keep RC state neutral
        smoothedHz.reset(sampleRate, 0.0);
        smoothedHz.setCurrentAndTargetValue(baseHz);

    } else if (glideCurv == 2 /* RC — 1-pole IIR in linear Hz space */) {
        // Same formula as Exponential but operated on raw Hz.
        // Effect: the glide feels faster in the high register (larger Hz gap)
        // and heavier in the bass — classic analog RC-circuit portamento character.
        if (isLegato && effGlideMs > 0.0f && prevHz > 0.0f) {
            glideRcHz   = prevHz;
            float N     = effGlideMs * 0.001f * static_cast<float>(sampleRate);
            glideRcCoeff = (N > 0.0f) ? (1.0f - std::exp(-4.6f / N)) : 1.0f;
        } else {
            glideRcHz   = baseHz;
            glideRcCoeff = 1.0f;    // instant snap — non-legato attack
        }
        glideExpLogHz = glideTargetLogHz;  glideExpCoeff = 0.0f;  // keep Exp state neutral
        smoothedHz.reset(sampleRate, 0.0);
        smoothedHz.setCurrentAndTargetValue(baseHz);

    } else /* Linear in semitones — Multiplicative smoother */ {
        smoothedHz.reset(sampleRate, static_cast<double>(effGlideMs) * 0.001);
        if (isLegato && effGlideMs > 0.0f && prevHz > 0.0f) {
            smoothedHz.setCurrentAndTargetValue(prevHz);
            smoothedHz.setTargetValue(baseHz);
        } else {
            smoothedHz.setCurrentAndTargetValue(baseHz);
        }
        glideExpLogHz = glideTargetLogHz;  glideExpCoeff = 0.0f;
        glideRcHz = baseHz;               glideRcCoeff = 0.0f;
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
    // (isMono was computed at the top of this function.)
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
        // Tell the first render block to snap the timbre smoothers to their
        // continuous targets rather than ramp from stale per-voice state.
        legatoHandoffPending = true;

        if (sharedOscPhase)
            osc.reset(sharedOscPhase->load());

        // Continue the inharmonicity FM modulator from the dying voice so its
        // sidebands don't step at the boundary.
        if (sharedPmPhase)
            osc.setPmPhase(sharedPmPhase->load());

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

    // Spread the unison oscillators' phases around the centre osc so the detuned
    // stack doesn't start phase-coherent (which would comb/flam at the attack).
    {
        float baseP = osc.getPhase();
        for (size_t k = 0; k < unisonOscs.size(); ++k) {
            float p = baseP + static_cast<float>(k + 1) / static_cast<float>(kMaxUnison);
            p -= std::floor(p);
            unisonOscs[k].reset(p);
        }
    }

    // ── Transient trigger ────────────────────────────────────────────────────────
    // "Always" fires on every note-on (legato or not) — useful for key clicks or
    // string bow attacks that should repeat with every articulation.
    // "Non-legato only" fires only when breath was off at note-on (the common wind-
    // controller case: breath attack / reed buzz at the start of a new phrase).
    if (paramTransientGain != nullptr && transientLib != nullptr
        && paramTransientChoice != nullptr) {
        int  triggerMode   = paramTransientTrigger
            ? static_cast<int>(std::round(paramTransientTrigger->load())) : 1;
        bool shouldTrigger = (triggerMode == 0) || !isLegato;  // 0=Always, 1=Non-legato

        if (shouldTrigger) {
            int choice = static_cast<int>(std::round(paramTransientChoice->load()));
            if (const TransientSample* ts = transientLib->getSample(choice)) {
                // Pitched (tonal) samples track the note: speed = voiceHz / nativeHz.
                // Inharmonic (noise) samples play at a fixed speed — the attack is
                // identical across the keyboard and there is no harmonic series to
                // smear under transposition.  Both still correct for sample-rate.
                const float pitchTrack = ts->pitched
                    ? juce::jlimit(0.125f, 8.0f, baseHz / ts->nativeHz)
                    : 1.0f;
                float speedRatio = pitchTrack
                                   * static_cast<float>(ts->sampleRate / sampleRate);

                // ── Per-trigger variation (round-robin) ──────────────────────────────
                // Decorrelate successive hits so a fixed sample stops sounding like a
                // looped object: jitter gain, micro-pitch, and (for noise) the start
                // offset, which reads a fresh excerpt.  Uses the per-voice noise LCG.
                const float var = paramTransientVariation ? paramTransientVariation->load() : 0.0f;
                auto rnd = [this]() {   // uniform in [-1, 1]
                    noiseWhiteState = noiseWhiteState * 6364136223846793005ULL
                                                      + 1442695040888963407ULL;
                    return static_cast<float>(static_cast<int32_t>(noiseWhiteState >> 33))
                           / 2147483648.0f;
                };
                transientGainMul = 1.0f + var * 0.30f * rnd();        // ~±2.5 dB at var=1
                speedRatio      *= 1.0f + var * 0.05f * rnd();         // ±5% micro-pitch
                // Start offset only for inharmonic noise (no defined attack to skip);
                // up to ~4 ms of fresh excerpt.  Tonal samples keep their onset intact.
                int startOff = 0;
                if (!ts->pitched) {
                    const float u = 0.5f * (rnd() + 1.0f);             // [0,1]
                    startOff = static_cast<int>(var * u * 0.004f * static_cast<float>(ts->sampleRate));
                }

                transientPlayer.setSample(ts->buffer.getReadPointer(0),
                                          ts->buffer.getNumSamples());
                transientEnvLevel = 1.0f;
                transientFilter.reset();   // clear stale state from a previous note
                transientPlayer.trigger(speedRatio, startOff);

                // Pitch resonator: tune to the played note so the noise rings at
                // its pitch (Karplus-Strong excitation → harmonic fusion).
                transientReso.reset();
                transientReso.setTuning(baseHz > 0.0f ? baseHz : 110.0f);
                transientResoActive = true;
                transientResoSilent = 0;

                // Noise→tone morph: arm the note-body fade-in so the oscillator
                // emerges from under the transient instead of starting alongside it.
                const float morphMs = paramTransientMorph ? paramTransientMorph->load() : 0.0f;
                if (morphMs > 0.5f) {
                    oscMorphRamp  = 0.0f;
                    oscMorphInc   = 1.0f / (morphMs * 0.001f * static_cast<float>(sampleRate));
                    oscMorphArmed = true;
                } else {
                    oscMorphRamp = 1.0f; oscMorphArmed = false;
                }
            }
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
    if (sharedMeterPressure)
        sharedMeterPressure->store(pressure, std::memory_order_relaxed);
}

void SynthVoice::notePitchbendChanged()
{
    // Channel-1 (non-MPE): JUCE updates totalPitchbendInSemitones at each PB event
    // but never touches note.pitchbend, which stays at the note-on value (typically 0).
    // Divide totalPitchbendInSemitones by the lower zone's masterPitchbendRange (2)
    // to recover the normalised −1..1 value.
    // Member channels: note.pitchbend is updated per-event as normal.
    pitchbend = (currentlyPlayingNote.midiChannel <= 1)
                ? static_cast<float>(currentlyPlayingNote.totalPitchbendInSemitones) / 2.0f
                : currentlyPlayingNote.pitchbend.asSignedFloat();

    // baseHz is intentionally NOT re-queried here.  It was set in noteStarted()
    // and feeds smoothedHz, which is already running toward the correct target.
    // Re-calling tuning.noteToHz() on every PB event would: (a) make unnecessary
    // MTS-ESP calls mid-note (MTS is note-on only), and (b) overwrite baseHz with
    // the same value while leaving smoothedHz's target unchanged — pure dead code.
    if (sharedMeterPitchbend)
        sharedMeterPitchbend->store(pitchbend, std::memory_order_relaxed);
}

void SynthVoice::noteTimbreChanged()
{
    slide = currentlyPlayingNote.timbre.asUnsignedFloat();
    if (sharedMeterSlide)
        sharedMeterSlide->store(slide, std::memory_order_relaxed);
}

void SynthVoice::noteKeyStateChanged()
{
    // Called when sustain pedal or sostenuto changes while this note is active.
    // Currently a stub — sustain pedal support would be implemented here:
    //   if (currentlyPlayingNote.keyState == juce::MPENote::sustained)
    //       allow the note to continue even after key-up.
    // For wind controllers this is rarely needed; for keyboard players it would
    // make legato phrases work without requiring continuous breath.
}

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

    float morphPos   = paramMorphPos ? paramMorphPos->load() : 0.0f;  // normalised 0..1 across table
    float basePW     = paramPW       ? paramPW->load()       : 0.5f;  // default identity
    auto detuneCents =                 paramDetune ? paramDetune->load() : 0.0f;
    // Read once per block — branch predictor handles the inner if trivially.
    int  glideCurveNow = paramGlideCurve
                       ? static_cast<int>(std::round(paramGlideCurve->load())) : 0;
    bool useExpGlide   = (glideCurveNow == 1);
    bool useRcGlide    = (glideCurveNow == 2);

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

    // Pitch: pitchbend + mod matrix fine tune + detune, as a per-block multiplier
    // applied on top of the per-sample smoothed base frequency.
    // Keeping std::pow() out of the sample loop avoids per-sample exp() cost.
    //
    // Pitchbend source selection:
    //   Member-channel notes (2–16): JUCE calls notePitchbendChanged() → per-voice
    //     `pitchbend` member is up to date.  Range = pitchbendRange (default 48 st,
    //     matching the MPE spec for full-range per-note expression).
    //   Channel-1 notes (non-MPE controllers, Sylphyo in standard mode, keyboards):
    //     JUCE updates note.totalPitchbendInSemitones at each PB event (sub-block accurate).
    //     noteStarted()/notePitchbendChanged() extract normalised PB from that field
    //     (divide by masterPitchbendRange=2) and store it in `pitchbend`, so it is
    //     always correct here without any extra sharedGlobalPB indirection.
    //   Range = nonMPEPBRange (default 2 st, the MIDI standard).
    const bool  isMasterChannelNote = (currentlyPlayingNote.midiChannel <= 1);
    const float kBendRangeSemitones = isMasterChannelNote
                                      ? (paramNonMPEPBRange ? paramNonMPEPBRange->load() : 2.0f)
                                      : (paramPBRange       ? paramPBRange->load()       : 48.0f);
    float totalSemitones = pitchbend * kBendRangeSemitones
                         + mods[ModDestID::OscPitchFine]
                         + detuneCents / 100.0f;
    float pitchMult = std::pow(2.0f, totalSemitones / 12.0f);
    if (!std::isfinite(pitchMult) || pitchMult <= 0.0f) pitchMult = 1.0f;
    smoothedPitchMult.setTargetValue(pitchMult);

    // Morph + PW: apply mod matrix offsets once per block and clamp.
    // Morph is normalised 0..1 across the wavetable, so a ±1 OscWaveshape route
    // sweeps the whole table (scale ×1).
    // OscPulseWidth mod is additive; range [0.5, 0.999] — negative mods floor
    // at 0.5 (identity), so you cannot cross to the mirror-symmetric side.
    float activeMorphPos = std::clamp(morphPos + mods[ModDestID::OscWaveshape] * 1.0f,
                                      0.0f, 1.0f);
    activeMorphForMeter = activeMorphPos;   // publish for the live WT display
    float activePW       = std::clamp(basePW   + mods[ModDestID::OscPulseWidth],
                                      0.5f, 0.999f);

    // Noise: blend amount (0=pure wave, 1=pure noise) and character.
    // OscNoiseMix mod is additive — a +1 route sweeps from 0 to full noise.
    // Block-rate param; noise is spectrally diffuse so block-boundary steps
    // are inaudible (unlike PW or cutoff which have instantaneous spectral effects).
    float noiseBlendBase = paramNoiseBlend ? paramNoiseBlend->load() : 0.0f;
    float activeNoiseMix = std::clamp(noiseBlendBase + mods[ModDestID::OscNoiseMix],
                                      0.0f, 1.0f);
    int noiseTypeNow = paramNoiseType
                       ? static_cast<int>(std::round(paramNoiseType->load())) : 0;

    // Wavefold depth: base param + per-voice OscFold mod, clamped 0..1.
    // Map the 0..1 amount to an exponential drive ONCE per block (std::pow is too
    // costly per sample), then ramp the drive per sample to avoid zipper.
    float foldBase   = paramFold ? paramFold->load() : 0.0f;
    float activeFold = std::clamp(foldBase + mods[ModDestID::OscFold], 0.0f, 1.0f);
    float foldDriveTgt = Oscillator::foldDrive(activeFold);

    // Inharmonicity: base param + per-voice OscInharm mod, clamped 0..1.
    float inharmBase   = paramInharm ? paramInharm->load() : 0.0f;
    float activeInharm = std::clamp(inharmBase + mods[ModDestID::OscInharm], 0.0f, 1.0f);

    // Wavetable sync / transpose ratio: base param + OscSync mod (a ±1 route
    // sweeps the whole 1..kSyncMax range), clamped.  1 = off.
    constexpr float kSyncMax = 8.0f;
    float syncBase   = paramSync ? paramSync->load() : 1.0f;
    float activeSync = std::clamp(syncBase + mods[ModDestID::OscSync] * (kSyncMax - 1.0f),
                                  1.0f, kSyncMax);

    // Timbre smoothers: on a mono-legato handoff snap straight to target (the
    // previous note's value is continuous with this one), otherwise ramp per
    // sample.  Snapping avoids an audible 3 ms ramp from this voice's stale
    // per-voice state at the note boundary when PW / Fold / Inharm are active.
    if (legatoHandoffPending) {
        smoothedPW.setCurrentAndTargetValue(activePW);
        smoothedFoldDrive.setCurrentAndTargetValue(foldDriveTgt);
        smoothedInharm.setCurrentAndTargetValue(activeInharm);
        smoothedSync.setCurrentAndTargetValue(activeSync);
        legatoHandoffPending = false;
    } else {
        smoothedPW.setTargetValue(activePW);
        smoothedFoldDrive.setTargetValue(foldDriveTgt);
        smoothedInharm.setTargetValue(activeInharm);
        smoothedSync.setTargetValue(activeSync);
    }

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

    // ── Stereo unison ───────────────────────────────────────────────────────────
    // Spread `uN` detuned voices across the field; each channel gets its own filter
    // so the detune becomes a true stereo image.  uN == 1 → the original mono path.
    static constexpr int kUnisonChoice[] = { 1, 2, 3, 4, 6 };
    int uN = 1;
    if (paramUnisonVoices) {
        int ci = juce::jlimit(0, 4, static_cast<int>(std::round(paramUnisonVoices->load())));
        uN = kUnisonChoice[ci];
    }
    const float uDetune = paramUnisonDetune ? paramUnisonDetune->load() : 0.0f;   // cents
    const float uWidth  = paramUnisonWidth  ? paramUnisonWidth->load()  : 0.0f;   // 0..1
    const bool  unisonOn = uN > 1;
    float uDetMul[kMaxUnison], uPanL[kMaxUnison], uPanR[kMaxUnison];
    {
        float pwr = 0.0f;
        for (int j = 0; j < uN; ++j) {
            const float spread = (uN > 1) ? (static_cast<float>(j) / static_cast<float>(uN - 1)) * 2.0f - 1.0f : 0.0f;
            uDetMul[j] = std::pow(2.0f, spread * uDetune / 1200.0f);
            const float ang = (spread * uWidth + 1.0f) * 0.25f * juce::MathConstants<float>::pi;  // equal-power
            uPanL[j] = std::cos(ang);
            uPanR[j] = std::sin(ang);
            pwr += uPanL[j] * uPanL[j] + uPanR[j] * uPanR[j];
        }
        // Normalise so the average PER-CHANNEL power matches a single centre voice,
        // so engaging unison doesn't drop the level (pwr is the L+R total → ÷2).
        const float norm = (pwr > 0.0f) ? std::sqrt(2.0f / pwr) : 1.0f;
        for (int j = 0; j < uN; ++j) { uPanL[j] *= norm; uPanR[j] *= norm; }
    }
    if (unisonOn) filterR.setResonance(resonance);

    // ── Transient: pre-render this block into the scratch buffer ─────────────────
    // Rendered up front (env × gain × per-trigger jitter, but NOT tailLevel and NOT
    // the filter) so it can be mixed sample-by-sample inside the loop.  When the
    // "Filter Route" is on, each transient sample passes through transientFilter,
    // which shares the voice filter's coefficients — so the attack sits in the
    // note's spectral space instead of being pasted on post-filter.
    int   transientN = 0;
    bool  transientRouteFilter = false;
    if (paramTransientGain != nullptr && transientPlayer.isPlaying()
        && !transientScratch.empty()) {
        const float tGain = paramTransientGain->load();
        const float tMod  = mods[ModDestID::TransientLevel];   // e.g. velocity → transient
        const float gain0 = std::clamp(tGain + tMod, 0.0f, 2.0f) * transientGainMul;
        if (gain0 > 1.0e-6f) {
            const float tDecayMs   = paramTransientDecay ? paramTransientDecay->load() : 200.0f;
            const float decayCoeff = tDecayMs > 0.0f
                ? std::exp(-1.0f / (tDecayMs * 0.001f * static_cast<float>(sampleRate)))
                : 0.0f;
            transientN = juce::jmin(numSamples, static_cast<int>(transientScratch.size()));
            std::fill(transientScratch.begin(), transientScratch.begin() + transientN, 0.0f);
            transientPlayer.renderAdding(transientScratch.data(), transientN,
                                         transientEnvLevel, decayCoeff, gain0);
            transientRouteFilter = paramTransientFilter && paramTransientFilter->load() > 0.5f;
            if (transientRouteFilter)
                transientFilter.setResonance(resonance);   // match the voice filter
        }
    }
    // Dynamics: 0 = fixed transient level, 1 = fully gated by the breath/VCA
    // envelope (so the transient never exceeds the sustained sound and tracks the
    // breath attack — the fix for fixed-velocity wind controllers).
    const float transientDyn = paramTransientDynamics ? paramTransientDynamics->load() : 0.0f;

    // Pitch resonator (Karplus-Strong): feedback from the Resonate depth, loop
    // damping from the Damping knob (bright→dark).  The comb keeps ringing after
    // the noise sample ends, so process the transient path while it is active.
    const float resoDepth = paramTransientResonate ? paramTransientResonate->load() : 0.0f;
    const float resoFb     = 0.90f * resoDepth;                       // up to 0.9 feedback
    const float resoDamp   = 1.0f - 0.95f * (paramTransientDamping ? paramTransientDamping->load() : 0.5f);
    const bool  resoOn     = resoDepth > 1.0e-4f && transientResoActive;
    // The transient path runs this block if the sample is feeding or the ring is alive.
    const bool  transientOn = (transientN > 0) || resoOn;

    auto* left  = buffer.getWritePointer(0, startSample);
    auto* right = buffer.getNumChannels() > 1
                  ? buffer.getWritePointer(1, startSample) : nullptr;

    for (int i = 0; i < numSamples; ++i) {
        if (isTailingOff) {
            // 0.9995 per sample ≈ −60 dB in 138 ms at 44100 Hz — smooth enough
            // that the release is inaudible as a step but short enough not to
            // ghost audibly under a new note in mono mode.
            // TODO: make the tail coefficient a parameter (or derive it from a
            // release-time knob) so the fade length matches the player's style.
            tailLevel *= 0.9995f;
            if (tailLevel < 0.0001f) {
                active = false; isTailingOff = false;
                clearCurrentNote();
                break;
            }
        }
        // Advance all smoothers one sample each.
        const float cutoffNow = smoothedCutoff.getNextValue();
        filter.setCutoff(cutoffNow);
        float subGain = 1.0f;
        {
            float hzBase;
            if (useExpGlide) {
                // 1-pole IIR in log2(Hz): fast start, smooth arrival, even semitones.
                glideExpLogHz += (glideTargetLogHz - glideExpLogHz) * glideExpCoeff;
                if (std::abs(glideExpLogHz - glideTargetLogHz) < 8.33e-6f)  // 0.01 ¢
                    glideExpLogHz = glideTargetLogHz;
                hzBase = std::exp2(glideExpLogHz);
            } else if (useRcGlide) {
                // 1-pole IIR in linear Hz: true RC-circuit response.
                // Snappier at high pitches (larger Hz gap), heavier at low ones.
                glideRcHz += (baseHz - glideRcHz) * glideRcCoeff;
                if (std::abs(glideRcHz - baseHz) < 0.01f)   // 0.01 Hz ≈ 0.04 ¢ at A4
                    glideRcHz = baseHz;
                hzBase = glideRcHz;
            } else {
                hzBase = smoothedHz.getNextValue();
            }
            const float reqHz = hzBase * smoothedPitchMult.getNextValue();
            osc.setFrequency(reqHz * uDetMul[0]);
            for (int j = 1; j < uN; ++j) unisonOscs[j - 1].setFrequency(reqHz * uDetMul[j]);

            // Anti-DC-thump: setFrequency() floors hz at 1, so a gesture that
            // drives the pitch sub-audio (a deep glide or a large pitchbend with
            // a wide PB range) would freeze the oscillator at a near-constant DC
            // level and emit a thump rather than a clean low/silent transition.
            // Fade the output to silence below the audible range.  The ramp
            // (4 → 12 Hz) is well below any musical note (A0 ≈ 27.5 Hz), so real
            // low notes are unaffected, and a glide passing through is click-free.
            subGain = juce::jlimit(0.0f, 1.0f, (reqHz - 4.0f) * (1.0f / 8.0f));
        }
        float gain = smoothedVCA.getNextValue();

        // Oscillator output (morphed wavetable + PD warp + FM inharmonicity).
        // Smoothers advance once per sample and are shared by every unison voice.
        const float pwNow = smoothedPW.getNextValue();
        const float ihNow = smoothedInharm.getNextValue();
        const float syNow = smoothedSync.getNextValue();
        float waveL, waveR;
        if (!unisonOn) {
            float w = osc.nextMorphed(activeMorphPos, pwNow, ihNow, syNow);
            waveL = waveR = w;
        } else {
            // Detuned voices panned across the field → genuine stereo image.
            float w0 = osc.nextMorphed(activeMorphPos, pwNow, ihNow, syNow);
            waveL = w0 * uPanL[0]; waveR = w0 * uPanR[0];
            for (int j = 1; j < uN; ++j) {
                float w = unisonOscs[j - 1].nextMorphed(activeMorphPos, pwNow, ihNow, syNow);
                waveL += w * uPanL[j]; waveR += w * uPanR[j];
            }
        }

        // Noise blend: mix noise pre-filter so both wave and noise share the same
        // tonal character from the SVF.  Generation is skipped when blend is off
        // (the branch is fully predicted after the first block).  Noise is mono
        // (centred) — added equally to both channels.
        float rawL = waveL, rawR = waveR;
        if (activeNoiseMix > 0.0005f)
        {
            // White noise: 64-bit LCG, uniform [-1, 1].
            noiseWhiteState = noiseWhiteState * 6364136223846793005ULL
                                              + 1442695040888963407ULL;
            float white = static_cast<float>(
                              static_cast<int32_t>(noiseWhiteState >> 33))
                          / 2147483648.0f;

            float noiseOut;
            if (noiseTypeNow == 1)
            {
                // Pink noise: Paul Kellett 7-state ~1/f approximation.
                // Output sum / 9 normalises to approximately [-1, 1].
                noisePinkB[0] = 0.99886f * noisePinkB[0] + white * 0.0555179f;
                noisePinkB[1] = 0.99332f * noisePinkB[1] + white * 0.0750759f;
                noisePinkB[2] = 0.96900f * noisePinkB[2] + white * 0.1538520f;
                noisePinkB[3] = 0.86650f * noisePinkB[3] + white * 0.3104856f;
                noisePinkB[4] = 0.55000f * noisePinkB[4] + white * 0.5329522f;
                noisePinkB[5] = -0.7616f * noisePinkB[5] - white * 0.0168980f;
                noiseOut = (noisePinkB[0] + noisePinkB[1] + noisePinkB[2]
                           + noisePinkB[3] + noisePinkB[4] + noisePinkB[5]
                           + noisePinkB[6] + white * 0.5362f) * (1.0f / 9.0f);
                noisePinkB[6] = white * 0.115926f;
            }
            else if (noiseTypeNow == 2)
            {
                // Brown noise: 1-pole leaky integrator in linear amplitude.
                //   y[n] = 0.999·y[n-1] + 0.025·white[n]
                // ×3 brings output RMS to approximately match white/pink.
                noiseBrownAcc = 0.999f * noiseBrownAcc + 0.025f * white;
                noiseOut = noiseBrownAcc * 3.0f;
            }
            else
            {
                noiseOut = white;  // White: use LCG output directly.
            }

            const float dry = 1.0f - activeNoiseMix, wet = noiseOut * activeNoiseMix;
            rawL = waveL * dry + wet;
            rawR = waveR * dry + wet;
        }

        // Wavefold (pre-filter): reshapes the osc+noise signal, multiplying
        // harmonics.  Transparent at drive 1; the filter tames fold-induced
        // brightness.  wavefold() early-outs when drive is negligible.
        // subGain mutes the pre-filter signal when the pitch is sub-audio, so the
        // filter settles to zero (no DC charge) instead of holding a thump.
        const float foldDrive = smoothedFoldDrive.getNextValue();
        const float foldedL = Oscillator::wavefold(rawL, foldDrive) * subGain;

        // Filter L (and R separately when unison is active → true stereo width).
        const float vgain = gain * tailLevel;
        float sampleL = filter.process(foldedL, filterMode) * vgain;
        float sampleR;
        if (unisonOn) {
            filterR.setCutoff(cutoffNow);
            const float foldedR = Oscillator::wavefold(rawR, foldDrive) * subGain;
            sampleR = filterR.process(foldedR, filterMode) * vgain;
        } else {
            sampleR = sampleL;
        }

        // Noise→tone morph: duck the note body so the oscillator emerges from
        // under the transient rather than starting alongside it.
        if (oscMorphArmed && oscMorphRamp < 1.0f) {
            sampleL *= oscMorphRamp; sampleR *= oscMorphRamp;
            oscMorphRamp += oscMorphInc;
            if (oscMorphRamp > 1.0f) oscMorphRamp = 1.0f;
        }

        // Transient path: excitation (the noise sample) → pitch resonator (rings
        // at the note pitch, continues after the sample ends) → filter coupling
        // (shared spectral space) → breath/VCA gating.  Scaled by tailLevel.  The
        // transient is mono — added equally to both channels.
        if (transientOn) {
            float t = (i < transientN) ? transientScratch[(size_t) i] * tailLevel : 0.0f;
            if (resoOn) {
                t = transientReso.process(t, resoFb, resoDamp);
                if (std::abs(t) < 1.0e-4f) { if (++transientResoSilent > (int) (sampleRate * 0.05))
                                                 transientResoActive = false; }
                else transientResoSilent = 0;
            }
            if (transientRouteFilter) {
                transientFilter.setCutoff(cutoffNow);
                t = transientFilter.process(t, filterMode);
            }
            t *= (1.0f - transientDyn) + transientDyn * gain;   // breath/VCA gating
            sampleL += t; sampleR += t;
        }

        if (right) { left[i] += sampleL; right[i] += sampleR; }
        else        left[i] += 0.5f * (sampleL + sampleR);   // mono bus: fold the field down
    }

    // Publish state for the next voice to inherit.
    //
    // VCA: published as smoothedVCA × tailLevel — the ACTUAL output amplitude.
    //
    // This multiplication is non-obvious but critical.  A tailing-off voice may
    // have smoothedVCA = 0.87 (breath was recently high) while tailLevel = 0.04
    // (the signal has nearly faded).  Publishing 0.87 would make the next voice
    // start with initVCA = 0.87, triggering isLegato = true and a sudden 22×
    // amplitude jump at the note boundary — a loud click on every attack.
    // Publishing the product (≈ 0.035) correctly marks the state as non-legato
    // (< 0.02 threshold) and the new voice ramps up from near-silence as intended.
    //
    // Testing note: this is hard to catch in normal play because it only fires
    // when a new note arrives while the tail is still decaying AND breath is on.
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
    if (sharedPmPhase)      sharedPmPhase->store(osc.getPmPhase());
    if (sharedFilterS1 && sharedFilterS2) {
        float s1, s2;
        filter.getState(s1, s2);
        sharedFilterS1->store(s1);
        sharedFilterS2->store(s2);
    }
    if (sharedCutoffHz) sharedCutoffHz->store(smoothedCutoff.getCurrentValue());
}
