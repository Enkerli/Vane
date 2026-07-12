#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <cstdint>
#include "Modulation/ModMatrix.h"
#include "MPE/TuningClient.h"
#include "Synth/Oscillator.h"
#include "Synth/SVFilter.h"
#include "Synth/SamplePlayer.h"
#include "Synth/CombResonator.h"
#include "Synth/TransientLibrary.h"
#include "MiniSaxVoice.h"   // MiniSax waveguide engine (MiniSax/Source, no JUCE deps)

// One MPE voice.  Signal path per sample:
//   wavetable-morph oscillator (+ PD pulse-width, √2-FM inharmonicity, hard-sync)
//   × N stereo-unison voices (detune *or* rotating-chord intervals, panned L/R)
//     → noise blend → wavefold → SVF (per channel) → VCA × tail
//     + a transient layer (sample → pitch resonator → filter coupling → dynamics)
//     + a Bézier glide trajectory on the base pitch.
// Modulation arrives pre-evaluated from the ModMatrix (per-voice + global) each
// block.  In mono mode the voice publishes its phases/filter state to shared
// atomics so the next legato voice continues click-free (see the handoff section
// at the end of renderNextBlock and README "Legato voice handoff").
class SynthVoice : public juce::MPESynthesiserVoice {
public:
    // The constructor parameter list is long because voices need live access to
    // APVTS raw-parameter pointers (safe on the audio thread) and shared atomics
    // for cross-voice state (legato handoff).  A future refactor should pack these
    // into two structs — VoiceParams (APVTS pointers) and SharedVoiceState
    // (the cross-voice atomics) — to reduce the constructor arity and make it
    // harder to accidentally pass parameters in the wrong order.
    //
    // IMPORTANT: obtain ALL pointers as local variables in VaneProcessor's
    // constructor before calling addVoice().  Member pointers (pOutputLevel etc.)
    // are assigned AFTER the voice loop and will be null if passed directly —
    // voices silently fall back to hard-coded defaults.  See PluginProcessor.cpp.
    SynthVoice(ModMatrix& matrix, TuningClient& tuning,
               std::atomic<float>*    paramMorphPos,  std::atomic<float>*    paramDetune,
               std::atomic<float>*    paramPW,
               std::atomic<float>*    paramCutoff,    std::atomic<float>*    paramRes,
               std::atomic<float>*    paramFilterMode,std::atomic<float>*    paramVelocityMix,
               std::atomic<float>*    paramGlide,     std::atomic<float>*    lastNoteHz,
               std::atomic<float>*    lastVCALevel,
               std::atomic<uint32_t>* legatoGeneration,
               std::atomic<float>*    lastOscPhase,
               std::atomic<float>*    lastPmPhase,
               std::atomic<float>*    paramMono,
               std::atomic<float>*    lastFilterS1,
               std::atomic<float>*    lastFilterS2,
               std::atomic<float>*    lastCutoffHz,
               // Optional display meters — written lock-free for the editor UI.
               // Null-safe: ignored when not wired.
               std::atomic<float>*    meterPressure  = nullptr,
               std::atomic<float>*    meterSlide     = nullptr,
               std::atomic<float>*    meterPitchbend = nullptr,
               // Pitchbend range for MPE member-channel notes (default 48 st).
               // Stored as an APVTS param so presets carry it.
               std::atomic<float>*    pitchbendRange  = nullptr,
               // Pitchbend range for channel-1 legacy (non-MPE) notes (default 2 st).
               std::atomic<float>*    nonMPEPBRange    = nullptr,
               // Glide mode: 0 = Fixed Time (constant duration regardless of interval),
               //             1 = Fixed Rate (duration ∝ interval; glideTime = ms per semitone).
               std::atomic<float>*    glideMode        = nullptr,
               // Glide curve: 0 = Linear semitones (constant speed, Multiplicative smoother),
               //              1 = Exponential approach (fast start → smooth landing, analog style).
               std::atomic<float>*    glideCurve       = nullptr,
               // Noise blend: 0 = pure wavetable, 1 = pure noise (pre-filter).
               std::atomic<float>*    noiseBlend       = nullptr,
               // Noise type: 0 = White, 1 = Pink (~1/f), 2 = Brown (~1/f²).
               std::atomic<float>*    noiseType        = nullptr,
               // Wavefold depth: 0 = transparent, 1 = heavy (pre-filter).
               std::atomic<float>*    fold             = nullptr,
               // Inharmonicity (FM index): 0 = harmonic, 1 = strong/metallic.
               std::atomic<float>*    inharm           = nullptr,
               // Wavetable sync / transpose ratio: 1 = off, higher = formant sweep.
               std::atomic<float>*    sync             = nullptr);

    void prepare(double sampleRate, int blockSize);

    // Point this voice's oscillator at a morph wavetable (shared, owned by the
    // processor).  nullptr → the built-in default.
    void setWavetable(const Wavetable* w) {
        osc.setWavetable(w);
        for (auto& o : unisonOscs) o.setWavetable(w);
    }

    // Wire the stereo-unison APVTS pointers (voices count, detune cents, width).
    // All may be null — unison gracefully disables (single centre oscillator).
    void setUnisonParams(std::atomic<float>* voices, std::atomic<float>* detune,
                         std::atomic<float>* width) noexcept {
        paramUnisonVoices = voices; paramUnisonDetune = detune; paramUnisonWidth = width;
    }

    // A/B switch for the unison-legato continuity fix (diagnostics / RenderProbe).
    static inline bool s_unisonLegatoFix = true;
    static constexpr int kMaxUnison  = 6;   // max unison voices (osc + kMaxUnison-1 extra)
    static constexpr int kChordSteps = 16;  // max steps per rotating-chord voice sequence

    // Rotating chords: the harmony voices follow per-voice interval sequences that
    // advance one step per played note (resetting on a new phrase), so a legato
    // line rotates the chord internally — Kilgore/Brecker style, monophonic.
    //   modeParam : 0 = Detune (cents), 1 = Chord (rotating intervals)
    //   seqFlat   : (kMaxUnison-1)·kChordSteps semitone intervals (fractional, so
    //               just ratios like 3:2 keep their exact 701.96c), row-major
    //   lens      : per-voice sequence loop length
    //   rotIndex  : shared rotation counter (cross-voice; advances per note-on)
    //   rotPlayed : where to publish the index actually sounding (for the UI playhead)
    void setChordParams(std::atomic<float>* modeParam, const float* seqFlat,
                        const int* lens, std::atomic<int>* rotIndex,
                        std::atomic<int>* rotPlayed = nullptr) noexcept {
        paramUnisonMode = modeParam; chordSeqFlat = seqFlat; chordLens = lens;
        chordRot = rotIndex; chordRotPlayedPtr = rotPlayed;
    }

    // Where to publish this voice's evaluated modulation OUTPUTS (mods[NumDests])
    // each block, for the Stage "Outputs" view.  Points at the processor's array.
    void setModOutSink(std::atomic<float>* sink) noexcept { modOutSink = sink; }

    // Cross-voice handoff state for stereo unison (mono legato allocates a NEW
    // voice, so the detuned oscs + right-channel filter need the same continuity
    // machinery as the centre osc).  uniPhase points at kMaxUnison-1 contiguous
    // atomics; fRS1/fRS2 are the right filter's integrator states.  May be null.
    void setUnisonHandoff(std::atomic<float>* uniPhase,
                          std::atomic<float>* fRS1, std::atomic<float>* fRS2) noexcept {
        sharedUnisonPhase = uniPhase; sharedFilterRS1 = fRS1; sharedFilterRS2 = fRS2;
    }

    // Mono-legato waveguide handoff: the processor-owned pointer to the last
    // voice that rendered waveguide audio.  Mono legato allocates a NEW voice;
    // the oscillator hands its phase across via atomics, but the bore is a whole
    // delay-line state — the new voice adopts it by copying the predecessor's
    // engines at noteStarted (noteStarted and rendering share the audio thread,
    // so the copy is race-free).  May be null.
    void setWaveguideHandoff(std::atomic<SynthVoice*>* slot) noexcept { sharedWgVoice = slot; }

    // Editable glide curve (Bezier mode): a 65-point time→progress LUT owned by the
    // processor.  Shaping how pitch travels old→new over the glide.  May be null.
    void setGlideLUT(const float* lut) noexcept { glideLUT = lut; }

    // Wire the transient library (owned by VaneProcessor, shared across all voices).
    // Safe to call before prepare() — used in the processor constructor.
    void setTransientLibrary(const TransientLibrary* lib) noexcept { transientLib = lib; }

    // Wire the waveguide (MiniSax) APVTS pointers.  When `on` reads > 0.5 the
    // MiniSax reed/bore engine replaces the wavetable oscillator as the voice's
    // sound source; everything downstream (noise blend, fold, SVF, vowel,
    // transients, VCA) is unchanged.  All pointers may be null — the mode
    // gracefully disables.  Follows the setTransientParams() wiring pattern.
    void setWaveguideParams(std::atomic<float>* on,
                            std::atomic<float>* embouchure,
                            std::atomic<float>* reedStiffness,
                            std::atomic<float>* reedAperture,
                            std::atomic<float>* boreDamping,
                            std::atomic<float>* bellBrightness,
                            std::atomic<float>* conicalAmount,
                            std::atomic<float>* breathNoise,
                            std::atomic<float>* growl) noexcept {
        paramWgOn         = on;
        paramWgEmbouchure = embouchure;
        paramWgReedStiff  = reedStiffness;
        paramWgAperture   = reedAperture;
        paramWgDamping    = boreDamping;
        paramWgBell       = bellBrightness;
        paramWgConical    = conicalAmount;
        paramWgNoise      = breathNoise;
        paramWgGrowl      = growl;
    }

    // Wire the four APVTS raw-parameter pointers for the transient section.
    // Call after addVoice() in the processor constructor, following the same
    // pattern as the wavetable wiring.  All may be nullptr — the transient
    // section gracefully disables itself when any required pointer is missing.
    void setTransientParams(std::atomic<float>* gain,
                            std::atomic<float>* decay,
                            std::atomic<float>* choice,
                            std::atomic<float>* trigger,
                            std::atomic<float>* variation = nullptr,
                            std::atomic<float>* filterRoute = nullptr,
                            std::atomic<float>* dynamics = nullptr,
                            std::atomic<float>* resonate = nullptr,
                            std::atomic<float>* damping = nullptr,
                            std::atomic<float>* morphMs = nullptr) noexcept {
        paramTransientGain      = gain;
        paramTransientDecay     = decay;
        paramTransientChoice    = choice;
        paramTransientTrigger   = trigger;
        paramTransientVariation = variation;
        paramTransientFilter    = filterRoute;
        paramTransientDynamics  = dynamics;
        paramTransientResonate  = resonate;
        paramTransientDamping   = damping;
        paramTransientMorph     = morphMs;
    }

    // MPESynthesiserVoice overrides
    void noteStarted()                                              override;
    void noteStopped(bool allowTailOff)                            override;
    void notePressureChanged()                                      override;
    void notePitchbendChanged()                                     override;
    void noteTimbreChanged()                                        override;
    void noteKeyStateChanged()                                      override;
    void renderNextBlock(juce::AudioBuffer<float>&,
                         int startSample, int numSamples)           override;

    // ── Per-voice meter snapshot (read by the processor on the audio thread) ──
    // Exposes this voice's live MPE expression for the editor's per-note
    // visualiser.  Plain reads of the per-voice state; no allocation/locking.
    bool  meterActive()    const { return active; }
    float meterLevel()     const { return active ? tailLevel : 0.0f; }   // 1 → fades on release
    float meterNoteHz()    const { return baseHz; }
    float meterPressure()  const { return pressure; }    // 0..1 (MPE Z)
    float meterSlide()     const { return slide; }       // 0..1 (CC74 / Y)
    float meterBend()      const { return pitchbend; }   // -1..1 (X)
    float meterVelocity()  const { return velocity; }    // 0..1
    float meterMorph()     const { return activeMorphForMeter; }  // live morph 0..1

private:
    ModMatrix&    modMatrix;
    TuningClient& tuning;

    std::atomic<float>* paramMorphPos    = nullptr;   // 0.0-3.0: Sine→Tri→Sqr→Saw
    std::atomic<float>* paramPW          = nullptr;   // 0.0-1.0: phase-distortion PW
    std::atomic<float>* paramDetune      = nullptr;
    std::atomic<float>* paramCutoff      = nullptr;
    std::atomic<float>* paramRes         = nullptr;
    std::atomic<float>* paramFilterMode  = nullptr;
    std::atomic<float>* paramVelocityMix = nullptr;
    std::atomic<float>*    paramGlide         = nullptr;
    std::atomic<float>*    sharedLastNoteHz   = nullptr;
    std::atomic<float>*    sharedLastVCALevel = nullptr;
    std::atomic<uint32_t>* sharedLegatoGen    = nullptr;  // kill-old-voice counter
    std::atomic<float>*    sharedOscPhase     = nullptr;  // oscillator phase handoff
    std::atomic<float>*    sharedPmPhase      = nullptr;  // inharm FM modulator phase handoff
    std::atomic<float>*    paramMono          = nullptr;  // 0 = poly, 1 = mono
    std::atomic<float>*    sharedFilterS1     = nullptr;  // SVF integrator state handoff
    std::atomic<float>*    sharedFilterS2     = nullptr;
    std::atomic<float>*    sharedCutoffHz     = nullptr;  // smoothed cutoff at handoff

    // Editor display meters — last-voice-wins, relaxed ordering sufficient.
    std::atomic<float>*    sharedMeterPressure  = nullptr;
    std::atomic<float>*    sharedMeterSlide     = nullptr;
    std::atomic<float>*    sharedMeterPitchbend = nullptr;

    // Pitchbend range for MPE member-channel notes — read each render block.
    std::atomic<float>*    paramPBRange       = nullptr;
    // Pitchbend range for channel-1 legacy (non-MPE) notes (default 2 st).
    std::atomic<float>*    paramNonMPEPBRange = nullptr;
    // Glide mode / curve — read block-rate in renderNextBlock.
    std::atomic<float>*    paramGlideMode     = nullptr;
    std::atomic<float>*    paramGlideCurve    = nullptr;
    // Noise parameters — read block-rate in renderNextBlock.
    std::atomic<float>*    paramNoiseBlend    = nullptr;
    std::atomic<float>*    paramNoiseType     = nullptr;
    // Wavefold depth — read block-rate in renderNextBlock.
    std::atomic<float>*    paramFold          = nullptr;
    // Inharmonicity (FM index) — read block-rate in renderNextBlock.
    std::atomic<float>*    paramInharm        = nullptr;
    std::atomic<float>*    paramSync          = nullptr;   // wavetable sync / transpose ratio

    // ── Glide curve state ─────────────────────────────────────────────────────
    //
    // Exponential (glideCurve == 1): 1-pole IIR in log2(Hz) space.
    //   glideExpLogHz += (glideTargetLogHz − glideExpLogHz) × glideExpCoeff
    //   Perceptually even across the keyboard — equal semitone intervals feel
    //   equal regardless of absolute pitch.
    //
    // RC (glideCurve == 2): 1-pole IIR in linear Hz space (true analog RC circuit).
    //   glideRcHz += (targetHz − glideRcHz) × glideRcCoeff
    //   The Hz gap is larger at higher pitches, so the glide feels snappier in the
    //   top register and heavier in the bass — the character of a physical RC filter.
    //
    // Both use the same coefficient formula: c = 1 − exp(−4.6 / N) so 99 % of
    // the interval is covered within effGlideMs (N = effGlideMs × sr / 1000).
    float glideExpLogHz    = 0.0f;   // current log2(Hz), stepped each sample (Exp mode)
    float glideTargetLogHz = 0.0f;   // log2(target Hz), shared by Exp and RC for init
    float glideExpCoeff    = 0.0f;   // 1-pole coeff for Exp mode
    float glideRcHz        = 0.0f;   // current Hz, stepped each sample (RC mode)
    float glideRcCoeff     = 0.0f;   // 1-pole coeff for RC mode
    // Bezier glide (time-driven): pitch = lerp(startLog, targetLog, curve(elapsed/dur)).
    const float* glideLUT  = nullptr;   // processor-owned 65-point time→progress LUT
    float glideBezStartLog = 0.0f;
    float glideBezTargetLog = 0.0f;
    int   glideBezSamples  = 0;      // total glide duration (samples); 0 = snapped/inactive
    int   glideBezElapsed  = 0;

    uint32_t myLegatoGen = 0;  // generation this voice was born into

    // Set by noteStarted() on a mono-legato attack, consumed by the first
    // renderNextBlock().  Tells the render loop to SNAP the timbre smoothers
    // (PW / Fold / Inharm) straight to their first computed target instead of
    // ramping from this voice's stale per-voice value — so legato transitions
    // stay smooth when those effects are active (their targets are CC74-driven
    // and continuous across the note boundary).
    bool legatoHandoffPending = false;

    // Per-voice slewers for voice-source routes — one entry per ModMatrix route,
    // mirroring each route's attack/release config.  Initialised in prepare() via
    // modMatrix.initVoiceSlewers().  Passed to modMatrix.evaluate() so that MPE
    // dimensions (pressure, slide) are tracked independently per voice and never
    // cross-contaminate simultaneous notes through the shared route slewers.
    std::vector<Slewer> voiceSlewers;

    // Stereo unison: `osc` is voice 0; unisonOscs holds the extra detuned voices.
    // The voices are panned across the field and each channel gets its own filter
    // so the detune spread becomes a genuine stereo image (Vane was dual-mono).
    Oscillator osc;
    std::array<Oscillator, kMaxUnison - 1> unisonOscs;
    SVFilter   filter;     // left / centre channel
    SVFilter   filterR;    // right channel (used only when unison voices > 1)
    std::atomic<float>* paramUnisonVoices = nullptr;  // choice index → {1,2,3,4,6}
    std::atomic<float>* paramUnisonDetune = nullptr;  // 0..50 cents spread
    std::atomic<float>* paramUnisonWidth  = nullptr;  // 0..1 stereo spread
    std::atomic<float>* sharedUnisonPhase = nullptr;  // kMaxUnison-1 phases (cross-voice handoff)
    std::atomic<float>* sharedFilterRS1   = nullptr;  // right filter integrator state handoff
    std::atomic<float>* sharedFilterRS2   = nullptr;
    // Rotating chords
    std::atomic<float>* paramUnisonMode   = nullptr;  // 0 = Detune, 1 = Chord
    const float*        chordSeqFlat      = nullptr;  // (kMaxUnison-1)·kChordSteps semitones (fractional)
    const int*          chordLens         = nullptr;  // per-voice loop length
    std::atomic<int>*   chordRot          = nullptr;  // shared rotation counter (next index)
    std::atomic<int>*   chordRotPlayedPtr = nullptr;  // publishes the sounding index (UI playhead)
    std::atomic<float>* modOutSink        = nullptr;  // publishes mods[NumDests] (Outputs view)
    float               chordInterval[kMaxUnison - 1] {};  // this note's harmony intervals (semitones)
    double sampleRate = 44100.0;

    // Per-sample cutoff interpolation — eliminates block-boundary coefficient steps
    // that cause audible crunchiness under breath/mod-driven filter sweeps.
    // Multiplicative (geometric) ramping matches logarithmic frequency perception.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedCutoff;

    // Portamento — glides the oscillator pitch between notes when glideTime > 0.
    // Also Multiplicative so semitone spacing stays perceptually even over the ramp.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedHz;

    // Per-sample pitchbend interpolation — turns block-boundary pitchMult steps
    // (most audible with non-MPE controllers that send coarse or square-wave PB)
    // into smooth pitch glides.  Multiplicative so semitone distance stays linear.
    // 3 ms ramp matches smoothedCutoff; fast enough not to lag live vibrato.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedPitchMult;

    // Per-sample VCA interpolation — eliminates block-boundary amplitude steps that
    // cause audible "crunchiness" (AM sidebands at 689 Hz+) during breath/CC sweeps.
    // Linear ramp is correct for amplitude (no multiplicative-from-zero issue).
    // New notes initialise this from lastVCALevel so legato transitions are seamless.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedVCA;

    // Per-sample PW interpolation — eliminates the spectral "pop" at MPE note-on
    // when a new note starts at a higher CC74 (timbre/slide) than the previous one.
    //
    // Without smoothing: the block-rate param update (BWS Expressions or mod routes)
    // immediately sets activePW to the CC74-driven extreme, so the VCA opens into
    // already-harsh harmonic content — perceived as a stronger-than-intended attack.
    //
    // Fix: every non-legato note-on snaps smoothedPW to 0.5 (identity warp) and sets
    // the target to activePW; the 3 ms ramp lets the timbre arrive with the breath
    // instead of ahead of it.  Mono legato notes do NOT reset so the PW is continuous
    // across note boundaries (matching the behaviour of smoothedCutoff / filter state).
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedPW;

    // Per-sample wavefold-DRIVE interpolation — prevents zipper when the fold
    // depth changes block-rate (UI drag or a slewed Slide/Pressure → Fold route).
    // Holds the exponential drive (1..8 from Oscillator::foldDrive), not the raw
    // 0..1 amount: the std::pow mapping is done once per block, then the resulting
    // drive is ramped per sample.  Folding sharply reshapes the waveform, so a
    // step at the block boundary would be audible; the 3 ms ramp matches the other
    // smoothers.  Not reset at note-on: it continues from the voice's previous
    // value, ramping to the new target.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedFoldDrive;

    // Per-sample inharmonicity (FM index) interpolation — prevents zipper when the
    // index changes block-rate (UI drag or a slewed Slide/Pressure → Inharm route).
    // 3 ms ramp matches the other smoothers; Linear (it's an index, not a freq).
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedInharm;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSync;

    // ── Per-voice noise generation state ────────────────────────────────────────
    // All three generators run independently per voice so simultaneous MPE notes
    // never share noise state (which would cause audible correlation between notes).
    //
    // White: 64-bit LCG (same constants as Oscillator — period 2^64, no seeding needed).
    // Pink:  Paul Kellett 7-state ~1/f approximation; output / 9 → [-1, 1].
    // Brown: 1-pole leaky integrator on white noise; output × 3 → [-1, 1] (approx).
    uint64_t noiseWhiteState = 12345u;
    float    noisePinkB[7]   = {};      // b0..b6: Kellett integrator state
    float    noiseBrownAcc   = 0.0f;   // leaky accumulator

    // Live MPE state — updated via noteXxxChanged() callbacks
    float pressure  = 0.0f;   // 0..1
    float slide     = 0.0f;   // 0..1 (CC74)
    float pitchbend = 0.0f;   // -1..1
    float velocity  = 0.0f;   // 0..1
    float keytrackVal = 0.0f; // note pitch, bipolar around C4 (note 60), ±4 oct → ±1
    float activeMorphForMeter = 0.0f;   // last block's modulated morph 0..1

    // Render state
    float baseHz       = 440.0f;
    uint32_t lastTuningEpoch = 0;   // tuning.tuningEpoch() at last baseHz resolve (live-retune)
    bool  active       = false;
    bool  isTailingOff = false;
    float tailLevel    = 0.0f;

    // ── Transient sample playback ────────────────────────────────────────────────
    // Library is shared (owned by VaneProcessor); player + envelope are per-voice.
    // transientScratch is allocated in prepare() to avoid per-block heap activity.
    const TransientLibrary* transientLib = nullptr;
    SamplePlayer            transientPlayer;
    float                   transientEnvLevel = 0.0f;   // current decay envelope (0..1)
    std::vector<float>      transientScratch;            // mono render scratch, sized in prepare()
    float                   transientGainMul  = 1.0f;   // per-trigger gain jitter (round-robin)
    SVFilter                transientFilter;             // shares the voice filter's coeffs when routed
    CombResonator           transientReso;               // pitch resonator (Karplus-Strong excitation)
    bool                    transientResoActive = false; // ring is sounding (excited this note)
    int                     transientResoSilent = 0;     // consecutive near-silent samples → stop
    float                   oscMorphRamp = 1.0f;         // note body fade-in under the transient (0..1)
    float                   oscMorphInc  = 0.0f;         // per-sample increment for the morph ramp
    bool                    oscMorphArmed = false;       // morph active for this note (transient fired)

    // ── Waveguide (MiniSax) mode ─────────────────────────────────────────────────
    // Per-voice reed/bore physical model.  Breath (the smoothed VCA signal)
    // drives the model's breath input through a floor mapping (see
    // kWaveguideBreathFloor in SynthVoice.cpp) so pp playing stays above the
    // reed's speaking threshold; pitch comes from the voice's glide/pitchbend
    // machinery per sample.  The engine is reset on non-legato attacks only —
    // a legato note re-entrains the still-ringing bore at the new delay length.
    // One bore per stereo-unison voice (mirrors `osc`/`unisonOscs`): index 0 is
    // the centre/melody voice, 1..kMaxUnison-1 the detuned or chord-interval
    // voices, each with its own delay length and noise seed so they don't sound
    // like unison-of-clones.
    std::array<minisax::MiniSaxVoice, kMaxUnison> waveguideVoices;
    std::array<uint32_t, kMaxUnison>              waveguideSeeds {};   // per-voice, derived in prepare()
    std::atomic<SynthVoice*>* sharedWgVoice = nullptr;  // last waveguide-rendering voice (bore handoff)
    std::atomic<float>* paramWgOn         = nullptr;  // > 0.5 = waveguide replaces the oscillator
    std::atomic<float>* paramWgEmbouchure = nullptr;  // 0..1
    std::atomic<float>* paramWgReedStiff  = nullptr;  // 0..1
    std::atomic<float>* paramWgAperture   = nullptr;  // 0..1
    std::atomic<float>* paramWgDamping    = nullptr;  // 0..1
    std::atomic<float>* paramWgBell       = nullptr;  // 0..1
    std::atomic<float>* paramWgConical    = nullptr;  // 0..1 even-harmonic (sax) body
    std::atomic<float>* paramWgNoise      = nullptr;  // 0..1 breath noise
    std::atomic<float>* paramWgGrowl      = nullptr;  // 0..1 flutter growl

    // Transient APVTS parameter pointers — set via setTransientParams().
    std::atomic<float>* paramTransientGain      = nullptr; // 0..2, default 0 (off)
    std::atomic<float>* paramTransientDecay     = nullptr; // 10..2000 ms
    std::atomic<float>* paramTransientChoice    = nullptr; // index into TransientLibrary
    std::atomic<float>* paramTransientTrigger   = nullptr; // 0=Always, 1=Non-legato only
    std::atomic<float>* paramTransientVariation = nullptr; // 0..1 per-trigger round-robin amount
    std::atomic<float>* paramTransientFilter    = nullptr; // 0/1 route transient through voice filter
    std::atomic<float>* paramTransientDynamics  = nullptr; // 0..1 how much transient follows breath VCA
    std::atomic<float>* paramTransientResonate  = nullptr; // 0..1 pitch-resonator depth (comb feedback)
    std::atomic<float>* paramTransientDamping    = nullptr; // 0..1 resonator damping (bright→dark)
    std::atomic<float>* paramTransientMorph      = nullptr; // 0..50 ms note-body fade-in under transient
};
