// vane-dsp.cpp — C-ABI Web Audio voice engine for the Vane *standalone* webapp.
//
// Reuses the plugin's real DSP — Oscillator (built-in band-limited waveforms) +
// SVFilter — compiled to WASM. JUCE is stubbed (Tools/wasm/juce-stub) down to
// jlimit/MathConstants, so no JUCE module is linked. The morph wavetable
// (Oscillator::nextMorphed + Wavetable FFT loading) is a later stage; this
// voice uses the built-in waveforms, so Wavetable.cpp is NOT compiled here.
//
// AMPLITUDE MODEL — reproduces Vane's real one, not a generic synth's. Vane has
// NO flat Attack/Decay/Sustain/Release knob: the audible envelope is whatever
// the mod matrix routes to VCA, and the FACTORY routing (PluginProcessor.cpp
// kFactory[]) is:
//   Breath → VCA  1.00 lin   Expression → VCA  1.00 lin   Pressure → VCA  0.50 lin
//   Slide → Cutoff 0.90 lin  Breath/Expr/Pressure → Cutoff 0.25/0.25/0.20 exp
//   Breath/Expr → Reso 0.15/0.15 exp     Velocity → Cutoff 0.15 lin
// vcaLevel = clamp(VelVCA·√velocity + that sum, 0, 1) — VelVCA defaults to 0, so
// out of the box velocity contributes NOTHING to loudness; breath/expression/
// pressure are the real envelope (SynthVoice.cpp ~line 668). Each mod source is
// independently slewed (one-pole, asymmetric attack/release — ModSlots::
// slewRates) before being summed, exactly like the real engine's Slewer.h
// (reproduced verbatim below — it has no JUCE dependency).
//
// LEGATO — mono mode only (matches SynthVoice::noteStarted). A new note is
// "legato" iff the previous mono voice's VCA was still above 0.02 (breath was
// still flowing): pitch portamento-glides instead of jumping, and VCA/filter
// continue instead of re-attacking. Implements the DEFAULT glide curve (Linear-
// semitone, Fixed-Time) plus the always-on minimum 1-period anti-click ramp;
// the Exponential/RC/Bézier curve variants are not yet ported (deferred, like
// the rest of the mod-matrix UI/aux sources/macros).
//
// tailLevel is the real engine's *separate*, fixed (non-editable) note-off
// safety ramp — ×0.9995 per sample — so a MIDI note-off is never abrupt even
// though breath (not note-off) is the real dynamic control.
//
// The AudioWorklet drives this: vane_init(sr) once, then per MIDI/CC event
// vane_note_on/off + vane_set_expr + vane_set_cc, per knob vane_set_param, and
// per render quantum vane_render(n) → read vane_buffer()[0..n) (mono) out of
// WASM memory.
#include "Synth/Oscillator.h"
#include "Synth/SVFilter.h"
#include "Synth/Wavetable.h"
#include "MPE/TuningClient.h"   // the REAL tuning engine — MTS code compile-gated off
#include <cmath>

// The real Wavetable.cpp is compiled in (with the stub juce_dsp FFT and the WAV
// interchange compiled out via VANE_WASM), so Oscillator::prepare() wires the
// genuine 16-frame Harmonic Stack via Wavetable::builtInDefault() and the voice
// renders through Oscillator::nextMorphed — the same morph/PD/FM/sync engine as
// the plugin, not an analytic-waveform approximation.

namespace {

// The REAL TuningClient, compiled without VANE_HAS_MTS: FollowMTS has no master
// and reads the internal cents table (default edo12 = transparent ET), Internal
// uses the selected table, Bypass forces 12-EDO. All the subtle behavior —
// hole-snapping noteToHz for wind controllers, A4-anchored linear EDO mapping,
// the Bohlen-Pierce tritave period, live-retune epochs — is the engine's own.
// MTS-ESP itself can't work in web code (it dlopens a system dylib), which is
// exactly why the standalone defaults to Internal (set from the host at boot).
TuningClient gTuning;

// Internal tuning ids in the UI's TUN_ORDER (index.html) — the host sends an
// index across the C ABI instead of a string.
const char* const kTuningIds[] = { "edo12", "just", "pyth", "meanqc", "werck3", "diat7", "edo19", "bp" };
constexpr int kNumTuningIds = 8;

// ── Slewer — verbatim port of Source/Modulation/Slewer.h (zero JUCE deps) ────
// One-pole lag, asymmetric attack/release, block-rate (process() called once
// per vane_render(n), mirroring the real engine's once-per-buffer call rate).
class Slewer {
public:
    void prepare (double sampleRate, int samplesPerStep = 1) {
        sr = (float) sampleRate; step = samplesPerStep < 1 ? 1 : samplesPerStep;
        recompute();
    }
    void setRates (float atkMs, float relMs) { attackMs = atkMs; releaseMs = relMs; recompute(); }
    // process() is called once per vane_render(n) call (block-rate), and n varies
    // by render quantum — recompute the coefficients for the ACTUAL block size each
    // call (cheap), or the slew is calibrated to the wrong call rate (the exact trap
    // Slewer.h's real-engine comment warns about: step=1 assumed but called every
    // n samples makes the release n× slower than the configured ms).
    void setStep (int samplesPerStep) { step = samplesPerStep < 1 ? 1 : samplesPerStep; recompute(); }
    void reset (float v = 0.0f) { current = v; }
    float process (float target) {
        float coeff = (target > current) ? attackCoeff : releaseCoeff;
        current += (1.0f - coeff) * (target - current);
        return current;
    }
    float value() const { return current; }
private:
    void recompute() { attackCoeff = coeff (attackMs); releaseCoeff = coeff (releaseMs); }
    float coeff (float ms) const {
        if (ms <= 0.0f) return 0.0f;
        return std::exp (-(float) step / (sr * ms * 0.001f));
    }
    float sr = 48000.0f; int step = 1;
    float attackMs = 5.0f, releaseMs = 30.0f, attackCoeff = 0.0f, releaseCoeff = 0.0f, current = 0.0f;
};

constexpr int kMaxVoices = 16;
constexpr int kMaxBlock  = 2048;
double gSampleRate = 48000.0;

// Global (patch) params, real Vane ids (index.html RANGE table) where noted.
float pCutoff   = 1128.0f;  // Hz                              [id 1]
float pReso     = 0.1f;     // 0..1                            [id 2]
float pOutput   = 0.8f;     // 0..1 master gain (separate from VCA) [id 8]
float pVelVCA   = 0.0f;     // 0..1 — "Velocity to VCA" amount; 0 = velocity contributes nothing [id 9]
float pBendRange = 48.0f;   // MPE member-channel pitch-bend range, semitones [id 7]
float pGlideMs  = 0.0f;     // ms — portamento time (Fixed Time mode)        [id 10]
bool  gMono     = false;
// Oscillator (morph wavetable) params — real Patch-tab ids/units (RANGE table):
float pMorph  = 0.0f;   // 0..1 across the table's frames               [id 12]
float pPW     = 0.5f;   // 0.5..0.999 phase-distortion pulse width      [id 13]
float pInharm = 0.0f;   // 0..1 FM-inharmonicity index                  [id 14]
float pSync   = 1.0f;   // 1..8 wavetable hard-sync / transpose ratio   [id 15]

// ── Mod matrix (generic slots — mirrors ModMatrix/ModSlots) ─────────────────
// 24 configurable slots, evaluated PER VOICE each block so per-note MPE sources
// (Pressure, Slide, Pitchbend, Velocity, Keytrack) modulate each note
// independently — this is what makes e.g. Slide→Morph genuinely per-note.
// Source choices (ModSlots::kSourceNames): 0 Off · 1 Breath · 2 Expression ·
//   3 Pressure · 4 Slide · 5 Pitchbend · 6 Velocity · 7-14 Aux (unsupported
//   standalone: evaluate to 0) · 15 Keytrack.
// Dest choices (kDestNames): 0 VCA · 1 Cutoff · 2 Reso · 3 Pitch · 4 Morph ·
//   5 PW · 6 Fold · 7 Noise · 8 Inharm · 9 Sync · 10 Transient · 11 UniDetune ·
//   12 Vowel. Dests this voice doesn't implement yet (Fold/Noise/Transient/
//   UniDetune/Vowel) accumulate but go unused.
// Slew rates are derived from the SOURCE (ModSlots::slewRates), not per slot —
// matching the real engine; the UI's per-slot atk/rel are accepted but ignored
// for now (same as the real header's "future revision" note).
constexpr int kNumSlots = 24;
constexpr int kNumDests = 13;
struct Slot { int src = 0, dst = 0, curve = 0; float amt = 0.0f; bool on = true; };
Slot gSlots[kNumSlots];

// The factory routing — PluginProcessor.cpp kFactory[], verbatim.
void resetSlotsToFactory() {
    static const Slot kFactory[] = {
        { 1, 0, 0, 1.00f },  // Breath     → VCA     lin
        { 2, 0, 0, 1.00f },  // Expression → VCA     lin
        { 3, 0, 0, 0.50f },  // Pressure   → VCA     lin
        { 4, 1, 0, 0.90f },  // Slide      → Cutoff  lin
        { 1, 1, 1, 0.25f },  // Breath     → Cutoff  exp
        { 2, 1, 1, 0.25f },  // Expression → Cutoff  exp
        { 3, 1, 1, 0.20f },  // Pressure   → Cutoff  exp
        { 1, 2, 1, 0.15f },  // Breath     → Reso    exp
        { 2, 2, 1, 0.15f },  // Expression → Reso    exp
        { 6, 1, 0, 0.15f },  // Velocity   → Cutoff  lin
    };
    for (int i = 0; i < kNumSlots; ++i) gSlots[i] = Slot{};
    for (int i = 0; i < 10; ++i) gSlots[i] = kFactory[i];
    // Velocity→Cutoff (slot 9) defaults OFF here, unlike the plugin: a wind
    // controller sending fixed/high note-on velocity gets a brightness kick on
    // every attack (measured ~1.84×) that the other Vane versions don't exhibit
    // in practice. The standalone "Vel→brightness" checkbox (param id 11)
    // toggles exactly this slot's enable.
    gSlots[9].on = false;
}

// Per-route curve shaping — ModRoute::CurveShape semantics.
inline float applyCurve (float x, int curve) {
    if (curve == 1) return x * std::fabs (x);                       // Exponential
    if (curve == 2) {                                                // SCurve: smoothstep on |x|, sign kept
        const float a = std::fabs (x) > 1.0f ? 1.0f : std::fabs (x);
        const float s = a * a * (3.0f - 2.0f * a);
        return x < 0.0f ? -s : s;
    }
    return x;                                                        // Linear
}

// Breath (CC2) / Expression (CC11) are GLOBAL sources (shared route.slewer in
// the real engine — all voices hear the same breath), unlike per-voice MPE.
float ccBreathRaw = 0.0f, ccExprRaw = 0.0f;
Slewer breathSlewer, exprSlewer;   // atk 5ms / rel 80ms — ModSlots::slewRates case 1/2

struct Voice {
    Oscillator osc;
    SVFilter   filt;
    bool  active    = false;
    int   note      = -1;
    int   channel   = -1;
    float vel       = 0.0f;   // 0..1, captured at note-on (constant for the note, like the real engine)
    float bend      = 0.0f;   // -1..1  (MPE X)
    float slide     = 0.0f;   // 0..1   (MPE Y / CC74), bipolarised before use
    float pressure  = 0.0f;   // 0..1   (MPE Z)
    float tailLevel = 0.0f;   // 1 while held; ×0.9995/sample after note-off (fixed safety ramp, not a user knob)
    bool  releasing = false;
    float vca       = 0.0f;   // smoothedVCA equivalent — linearly rate-limited toward the block target
    Slewer pressureSlewer;    // atk 3ms / rel 50ms  — case 3
    Slewer slideSlewer;       // atk 2ms / rel 20ms  — case 4 (operates on the bipolar value)
    Slewer velSlewer;         // atk 20ms / rel 0ms  — case 6
    Slewer pbModSlewer;       // atk 2ms / rel 20ms — Pitchbend as a MOD SOURCE (case 5), separate from the pitch multiplier
    // cutoffSmoothed/pitchMulSmoothed: per-SAMPLE 3ms linear ramps toward the
    // block's raw (unslewed) target — mirrors the real engine's smoothedCutoff/
    // smoothedPitchMult (JUCE SmoothedValue, reset(sr,0.003), getNextValue()
    // every sample). The mod-matrix sources feeding the target are already
    // block-rate slewed (matches ModMatrix's own per-buffer evaluation); what
    // was missing was smoothing the DESTINATION across the block itself — the
    // real engine's own comment calls this out: holding cutoff/pitch constant
    // for a whole render quantum makes SVF coefficient jumps at block
    // boundaries audible as "crunchiness" during breath sweeps or vibrato.
    float cutoffSmoothed    = 4000.0f;
    float pitchMulSmoothed  = 1.0f;
    float  keytrack = 0.0f;   // note pitch bipolar around C4 (±48 st → ±1) — mod source 15, per-note constant

    // Portamento (Linear-semitone / Fixed-Time curve only — the real engine's
    // default). currentHz approaches targetHz multiplicatively each sample —
    // this is the UNBENT note pitch; live MPE pitchbend (bendSlewer, above) is
    // applied as a separate multiplier on top each block, exactly like the real
    // engine's smoothedHz × smoothedPitchMult split (SynthVoice.cpp ~line 673).
    float currentHz  = 440.0f;
    float targetHz   = 440.0f;
    float glideCoeff = 1.0f;   // per-sample multiplicative step exponent base; 1 = snap
    uint32_t tuningEpoch = 0;  // gTuning.tuningEpoch() at last pitch resolve — live retune
};

Voice   voices[kMaxVoices];
int     monoVoiceIdx = -1;
float   monoLastHz   = 0.0f;
float   monoLastVCA  = 0.0f;
float   renderBuf[kMaxBlock];

// ── Mono held-note stack (see vane_set_mono / vane_note_on / vane_note_off) ──
struct HeldNote { int note, channel, vel; };
HeldNote heldStack[kMaxVoices];
int      heldCount = 0;

inline float clamp01 (float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// ── Standalone-only master limiter ───────────────────────────────────────────
// NOT in the real engine (PluginProcessor.cpp's masterGain comment admits the
// 0.8 default only leaves "~2 dB headroom before clipping into downstream
// effects" — fine inside a DAW's float bus, which has no hard ceiling until a
// later stage; NOT fine writing straight to the browser's hardware output,
// which hard-clips at ±1.0). Two full-VCA voices sum to ~1.6 and MUST be tamed.
//
// The earlier attempt was a per-SAMPLE tanh waveshaper — which was the wrong
// tool and the actual cause of the "roughness/distortion" reported with two
// notes: waveshaping the SUM of two tones generates intermodulation products
// (difference tones + sidebands) = new frequencies = audible distortion, not a
// clean beat. A proper limiter instead computes a smooth GAIN and scales the
// whole signal linearly (no new frequencies): a peak envelope with instant
// attack + slow release drives a gain that is reduced quickly but recovered
// slowly. Because the release (150 ms) is far slower than any musical beat
// period, the gain does NOT pump at the beat rate — the natural acoustic
// beating of two summed tones is preserved, only the overall level rides down
// when the mix would clip. Single notes (< threshold) pass at unity untouched.
float limEnv = 0.0f;      // peak envelope (instant attack, slow release)
float limGain = 1.0f;     // applied gain (fast down, slow up)
float limRelEnv = 0.0f;   // env release coeff  (set from SR in vane_init)
float limGainAtk = 0.0f;  // gain attack coeff  (fast — reduce)
float limGainRel = 0.0f;  // gain release coeff (slow — recover)
inline float masterLimit (float x) {
    const float ax = std::fabs (x);
    if (ax > limEnv) limEnv = ax;                          // instant peak catch
    else             limEnv = ax + (limEnv - ax) * limRelEnv;   // slow release
    constexpr float thr = 0.95f;
    const float tgt = limEnv > thr ? thr / limEnv : 1.0f;
    if (tgt < limGain) limGain += (tgt - limGain) * limGainAtk;   // reduce fast
    else               limGain += (tgt - limGain) * limGainRel;   // recover slow
    float y = x * limGain;
    if (y > 1.0f) y = 1.0f; else if (y < -1.0f) y = -1.0f;        // hard safety
    return y;
}

void pushHeld (int note, int channel, int vel) {
    for (int i = 0; i < heldCount; ++i)
        if (heldStack[i].note == note && heldStack[i].channel == channel) { heldStack[i].vel = vel; return; }
    if (heldCount < kMaxVoices) heldStack[heldCount++] = { note, channel, vel };
}
// Removes (note, channel) from the stack if present. wasTop reports whether the
// removed entry was the sounding (top) note. Returns false if not found.
bool popHeld (int note, int channel, bool& wasTop) {
    for (int i = 0; i < heldCount; ++i) {
        if (heldStack[i].note == note && heldStack[i].channel == channel) {
            wasTop = (i == heldCount - 1);
            for (int j = i; j < heldCount - 1; ++j) heldStack[j] = heldStack[j + 1];
            --heldCount;
            return true;
        }
    }
    wasTop = false;
    return false;
}

} // namespace

extern "C" {

void vane_init (double sampleRate) {
    gSampleRate = sampleRate;
    ccBreathRaw = 0.0f; ccExprRaw = 0.0f;   // full reset — a re-init shouldn't inherit stale CC state
    breathSlewer.prepare (sampleRate); breathSlewer.setRates (5.0f, 80.0f); breathSlewer.reset();
    exprSlewer.prepare (sampleRate);   exprSlewer.setRates (5.0f, 80.0f); exprSlewer.reset();
    for (auto& v : voices) {
        v.osc.prepare (sampleRate);   // also wires Wavetable::builtInDefault() (Harmonic Stack)
        v.filt.prepare (sampleRate);
        v.pressureSlewer.prepare (sampleRate); v.pressureSlewer.setRates (3.0f, 50.0f);
        v.slideSlewer.prepare (sampleRate);    v.slideSlewer.setRates (2.0f, 20.0f);
        v.velSlewer.prepare (sampleRate);      v.velSlewer.setRates (20.0f, 0.0f);
        v.pbModSlewer.prepare (sampleRate);    v.pbModSlewer.setRates (2.0f, 20.0f);
        v.active = false; v.tailLevel = 0.0f; v.vca = 0.0f;
    }
    monoVoiceIdx = -1; monoLastHz = 0.0f; monoLastVCA = 0.0f; heldCount = 0;
    // Master limiter coefficients (see masterLimit): 50 ms env release, 1 ms
    // gain attack, 150 ms gain recovery — the slow recovery is what keeps it
    // from pumping at a musical beat rate.
    const float sr = (float) sampleRate;
    limEnv = 0.0f; limGain = 1.0f;
    limRelEnv  = std::exp (-1.0f / (0.050f * sr));
    limGainAtk = 1.0f - std::exp (-1.0f / (0.001f * sr));
    limGainRel = 1.0f - std::exp (-1.0f / (0.150f * sr));
    resetSlotsToFactory();
}

// Mono held-note stack (pushHeld/popHeld, namespace-scope above): classic
// monophonic last-note-priority. While mono, every physical note-on is pushed;
// the TOP of the stack is what sounds. Releasing a note that is NOT on top has
// no audible effect (it was already "underneath"). Releasing the TOP note
// reveals whatever's now on top — re-sounding it via the same legato path as
// any other mono note-change, so it glides in rather than clicking — the
// "trill" effect of holding one note and tapping a second. Only when the stack
// empties does the voice actually release. This isn't literally how the plugin
// does it under the hood (no such stack exists in PluginProcessor.cpp/
// SynthVoice.cpp — mono there is a generation-counter voice-kill scheme riding
// on JUCE's own MPE note tracking, which this standalone voice doesn't have);
// it reproduces the SPECIFIED behavior directly.
void vane_set_mono (int isMono) {
    gMono = isMono != 0;
    if (! gMono) monoVoiceIdx = -1;
    heldCount = 0;   // a mode switch starts the held-note stack fresh either way
}

// Generic CC input; only Breath (2) and Expression (11) are wired (Vane's
// default macro bindings — state.cc in index.html). Other CCs are accepted and
// ignored for now (no-op), rather than guessing a mapping that doesn't exist yet.
void vane_set_cc (int cc, float v01) {
    if (cc == 2)  ccBreathRaw = v01;
    else if (cc == 11) ccExprRaw = v01;
}

// Triggers (or, in mono, retargets) the sounding voice for `note`. Shared by a
// fresh physical note-on AND a mono stack reattach (releasing the top note
// reveals the next one down) — both go through the same legato/glide decision.
void startNote (int note, int vel, int channel) {
    int idx;
    bool legato = false;
    if (gMono) {
        idx = (monoVoiceIdx >= 0 && voices[monoVoiceIdx].active) ? monoVoiceIdx : -1;
        if (idx < 0) for (int i = 0; i < kMaxVoices; ++i) if (! voices[i].active) { idx = i; break; }
        if (idx < 0) idx = 0;                       // degrade gracefully if somehow all busy
        legato = monoLastVCA > 0.02f;                // breath was still flowing → glide, don't re-attack
        monoVoiceIdx = idx;
    } else {
        idx = -1;
        for (int i = 0; i < kMaxVoices; ++i) if (! voices[i].active) { idx = i; break; }
        if (idx < 0) {                               // steal the quietest voice
            float lowest = 2.0f;
            for (int i = 0; i < kMaxVoices; ++i) if (voices[i].vca < lowest) { lowest = voices[i].vca; idx = i; }
            if (idx < 0) idx = 0;
        }
    }

    Voice& v = voices[idx];
    const float prevHz = legato ? v.currentHz : 0.0f;
    v.active = true; v.note = note; v.channel = channel; v.vel = vel / 127.0f;
    v.keytrack = ((float) note - 60.0f) / 48.0f;              // bipolar around C4, ±4 oct → ±1
    if (v.keytrack > 1.0f) v.keytrack = 1.0f; else if (v.keytrack < -1.0f) v.keytrack = -1.0f;
    // VCA/tail and expression: legato CONTINUES the current state so a phrase
    // sounds like one gesture, not a string of re-triggers. A fresh attack
    // resets everything and lets the breath-driven VCA open the voice.
    //   - VCA/slewers/smoothers reset only on a fresh note (no re-attack click).
    //   - Expression (bend/slide/pressure) is preserved through legato: on a
    //     wind controller the breath/slide/pressure are CONTINUOUS across a
    //     slur, so re-zeroing them at every note-on made the timbre BLIP at each
    //     note — e.g. the factory Slide→Cutoff route (0.90) snapped the filter
    //     back toward base cutoff until the controller's next CC74 arrived,
    //     which reads as "legato isn't smooth" no matter how long the pitch
    //     glide is. A truly fresh note has no prior channel state, so it starts
    //     at the MPE-neutral defaults (slide 0.5 CENTRED — reading note.timbre's
    //     default, NOT 0, which through Slide→Cutoff would drag the filter ~4.5
    //     octaves down and make the high range near-inaudible).
    if (! legato) {
        v.bend = 0.0f; v.slide = 0.5f; v.pressure = 0.0f;
        v.vca = 0.0f; v.pressureSlewer.reset(); v.slideSlewer.reset(); v.velSlewer.reset(); v.pbModSlewer.reset();
        v.pitchMulSmoothed = 1.0f;             // matches smoothedPitchMult.setCurrentAndTargetValue(1.0f) at prepare
        v.cutoffSmoothed   = pCutoff;          // matches smoothedCutoff.setCurrentAndTargetValue(initCutoff) at note-on
    }
    v.tailLevel = 1.0f; v.releasing = false;

    // Portamento: glide from the previous pitch when legato, with the real
    // engine's always-on minimum (one period at the target, so even glideMs=0
    // crosses a slope-continuity boundary instead of snapping instantaneously).
    // Pitch comes from the REAL tuning engine (internal tunings, hole-snapping);
    // MTS-filtered/unresolvable notes return 0 — suppress the voice like the
    // real engine does, so a 0 Hz oscillator never poisons the SVF states.
    const float targetHz = gTuning.noteToHz (note, channel);
    if (targetHz <= 0.0f) { v.active = false; v.tailLevel = 0.0f; return; }
    v.targetHz = targetHz;
    v.tuningEpoch = gTuning.tuningEpoch();
    const float minGlideMs = (legato && prevHz > 0.0f) ? (1000.0f / targetHz) : 0.0f;
    const float effGlideMs = legato ? (pGlideMs > minGlideMs ? pGlideMs : minGlideMs) : 0.0f;
    if (legato && effGlideMs > 0.0f && prevHz > 0.0f) {
        v.currentHz = prevHz;
        const float nSamples = effGlideMs * 0.001f * (float) gSampleRate;
        // Multiplicative per-sample step so the ratio glide is constant in
        // semitones (matches juce::ValueSmoothingTypes::Multiplicative).
        v.glideCoeff = std::pow (targetHz / prevHz, 1.0f / (nSamples > 1.0f ? nSamples : 1.0f));
    } else {
        v.currentHz = targetHz; v.glideCoeff = 1.0f;
    }
    v.osc.setFrequency (v.currentHz);
}

void vane_note_on (int note, int vel, int channel) {
    if (gMono) pushHeld (note, channel, vel);
    startNote (note, vel, channel);
}

void vane_note_off (int note, int channel) {
    if (gMono) {
        bool wasTop = false;
        if (popHeld (note, channel, wasTop)) {
            if (! wasTop) return;                     // released a note that wasn't sounding — silent no-op
            if (heldCount > 0) {                       // trill: reveal the next-held note, gliding (legato) into it
                const HeldNote top = heldStack[heldCount - 1];
                startNote (top.note, top.vel, top.channel);
                return;
            }
            // stack now empty — fall through to the normal release below
        }
        // not found in the stack (shouldn't normally happen) — fall through too
    }
    for (auto& v : voices)
        if (v.active && v.note == note && (channel < 0 || v.channel == channel))
            v.releasing = true;                      // start the fixed tail-off ramp
}

// Per-MPE-channel expression update (applies to the sounding voice on that channel).
void vane_set_expr (int channel, float bend, float slide, float pressure) {
    for (auto& v : voices)
        if (v.active && v.channel == channel) { v.bend = bend; v.slide = slide; v.pressure = pressure; }
}

void vane_set_param (int id, float val) {
    switch (id) {
        case 1: pCutoff    = val; break;
        case 2: pReso      = val; break;
        case 7: pBendRange = val; break;
        case 8: pOutput    = val; break;
        case 9: pVelVCA    = val; break;
        case 10: pGlideMs  = val; break;
        case 11: gSlots[9].on = (val > 0.5f); break;   // Vel→brightness = the factory Velocity→Cutoff slot's enable
        case 12: pMorph  = val; break;
        case 13: pPW     = val; break;
        case 14: pInharm = val; break;
        case 15: pSync   = val; break;
        default: break;
    }
}

// Configure one mod-matrix slot (the Matrix tab's slotEdit). src/dst/curve are
// the UI's choice indices (ModSlots order); amt is -1..1; on gates the slot
// without clearing it. Per-slot atk/rel from the UI are ignored — slew rates
// derive from the source, matching the real engine.
void vane_set_slot (int n, int src, int dst, float amt, int curve, int on) {
    if (n < 0 || n >= kNumSlots) return;
    gSlots[n].src = src; gSlots[n].dst = dst; gSlots[n].amt = amt;
    gSlots[n].curve = curve; gSlots[n].on = on != 0;
}

// Tuning source: 0 = FollowMTS (no master in web → internal-table fallback),
// 1 = Internal, 2 = Bypass (12-EDO). Matches TuningSource's enum order.
void vane_set_tuning_source (int s) {
    gTuning.setTuningSource (s == 1 ? TuningSource::Internal
                            : s == 2 ? TuningSource::Bypass
                                     : TuningSource::FollowMTS);
}

// Internal tuning by index into the UI's TUN_ORDER (see kTuningIds).
void vane_set_internal_tuning (int idx) {
    if (idx >= 0 && idx < kNumTuningIds)
        gTuning.setInternalTuning (kTuningIds[idx]);
}

float* vane_buffer () { return renderBuf; }

void vane_render (int n) {
    if (n > kMaxBlock) n = kMaxBlock;
    for (int s = 0; s < n; ++s) renderBuf[s] = 0.0f;

    // Recalibrate every slewer to THIS call's actual block size (see Slewer::
    // setStep) before processing — n can vary between calls.
    breathSlewer.setStep (n);
    exprSlewer.setStep (n);

    // Global mod sources, slewed once per block (matches the real engine's
    // once-per-buffer Slewer::process() call rate). Curves are applied per-slot
    // in the matrix evaluation, not here.
    const float breathS = breathSlewer.process (ccBreathRaw);
    const float exprS   = exprSlewer.process (ccExprRaw);

    for (auto& v : voices) {
        if (! v.active) continue;

        // Live retune: if the tuning changed while this note is held (switching
        // scale/source mid-breath — the core wind-controller gesture), re-query
        // the pitch and glide to it over ~15 ms instead of waiting for the next
        // note-on. Mirrors SynthVoice::renderNextBlock's tuningEpoch check.
        if (gTuning.tuningEpoch() != v.tuningEpoch) {
            v.tuningEpoch = gTuning.tuningEpoch();
            const float newHz = gTuning.noteToHz (v.note, v.channel);
            if (newHz > 0.0f && newHz != v.targetHz) {
                v.targetHz = newHz;
                const float nSamples = 0.015f * (float) gSampleRate;
                v.glideCoeff = std::pow (newHz / v.currentHz, 1.0f / nSamples);
            }
        }

        v.pressureSlewer.setStep (n);
        v.slideSlewer.setStep (n);
        v.velSlewer.setStep (n);
        v.pbModSlewer.setStep (n);

        // ── Per-voice slewed mod sources ────────────────────────────────────────
        const float pressS  = v.pressureSlewer.process (v.pressure);
        const float slideBp = (v.slide - 0.5f) * 2.0f;             // neutral(0.5) -> 0, matches SynthVoice.cpp
        const float slideS  = v.slideSlewer.process (slideBp);
        const float velS    = v.velSlewer.process (v.vel);
        const float pbS     = v.pbModSlewer.process (v.bend);      // Pitchbend as a MOD source

        // ── Mod matrix: evaluate every slot for THIS voice (per-note MPE) ──────
        float mods[kNumDests] = {};
        for (int i = 0; i < kNumSlots; ++i) {
            const Slot& sl = gSlots[i];
            if (! sl.on || sl.src == 0 || sl.dst < 0 || sl.dst >= kNumDests) continue;
            float srcVal;
            switch (sl.src) {
                case 1:  srcVal = breathS;    break;   // Breath (global CC2)
                case 2:  srcVal = exprS;      break;   // Expression (global CC11)
                case 3:  srcVal = pressS;     break;   // Pressure (per-voice, MPE Z)
                case 4:  srcVal = slideS;     break;   // Slide (per-voice bipolar, MPE Y)
                case 5:  srcVal = pbS;        break;   // Pitchbend (per-voice, MPE X)
                case 6:  srcVal = velS;       break;   // Velocity (per-voice)
                case 15: srcVal = v.keytrack; break;   // Keytrack (per-note constant)
                default: srcVal = 0.0f;       break;   // Aux 1-8 — unsupported standalone
            }
            mods[sl.dst] += applyCurve (srcVal, sl.curve) * sl.amt;
        }

        // MPE pitchbend (X): a per-channel multiplier on top of the glided base
        // pitch (currentHz), NOT folded into the portamento target — matches the
        // real engine's separate smoothedHz x smoothedPitchMult. mods[3] (Pitch)
        // adds mod-matrix fine-tune semitones on top, like OscPitchFine. This is
        // the RAW per-block target; v.pitchMulSmoothed ramps to it per-sample
        // below (mirrors smoothedPitchMult exactly — see the Voice struct note).
        const float bendMulTarget = std::pow (2.0f, (v.bend * pBendRange + mods[3]) / 12.0f);

        // Dest application — mirrors SynthVoice::renderNextBlock:
        const float vcaTarget      = clamp01 (pVelVCA * std::sqrt (v.vel) + mods[0]);
        const float targetCutoffHz = pCutoff * std::pow (2.0f, mods[1] * 5.0f);   // ±5 octaves
        const float resonance      = clamp01 (pReso + mods[2]);
        // Per-voice oscillator character — THIS is what makes morph per-note:
        // each voice's own slide/pressure (via routes like Slide→Morph) offsets
        // the shared base params independently.
        const float vMorph  = clamp01 (pMorph + mods[4]);
        const float vPW     = clamp01 (pPW + mods[5]);
        const float vInharm = clamp01 (pInharm + mods[8]);
        float vSync = pSync + mods[9] * 7.0f;                       // kSyncMax-1 = 7
        if (vSync < 1.0f) vSync = 1.0f; else if (vSync > 8.0f) vSync = 8.0f;

        // Resonance (k) is block-rate, matching the real engine — only cutoff
        // (g/a1-a3) needs per-sample updates to stay coefficient-continuous.
        v.filt.setResonance (resonance);

        for (int s = 0; s < n; ++s) {
            // VCA: 3 ms linear rate-limited approach to the block target (mirrors
            // smoothedVCA.reset(sr, 0.003) — eliminates block-boundary amplitude
            // steps without implying any musical attack/release shape).
            const float maxStep = 1.0f / (0.003f * (float) gSampleRate);
            if (v.vca < vcaTarget) v.vca = (v.vca + maxStep < vcaTarget) ? v.vca + maxStep : vcaTarget;
            else                    v.vca = (v.vca - maxStep > vcaTarget) ? v.vca - maxStep : vcaTarget;

            // Cutoff/pitch-mult: same 3 ms linear approach as VCA, applied
            // directly in Hz / multiplier space — mirrors the real engine's
            // smoothedCutoff / smoothedPitchMult (JUCE SmoothedValue, 3 ms ramp,
            // getNextValue() every sample). Without this, the SVF coefficients
            // and pitch multiplier were flat for the whole render quantum and
            // stepped at block boundaries — audible as "crunchiness" during
            // breath sweeps or vibrato, exactly the real engine's own comment
            // on why it smooths cutoff per-sample in the first place.
            const float cTarget = clampf (targetCutoffHz, 20.0f, 20000.0f);
            if (v.cutoffSmoothed < cTarget) v.cutoffSmoothed = (v.cutoffSmoothed + maxStep * 20000.0f < cTarget) ? v.cutoffSmoothed + maxStep * 20000.0f : cTarget;
            else                             v.cutoffSmoothed = (v.cutoffSmoothed - maxStep * 20000.0f > cTarget) ? v.cutoffSmoothed - maxStep * 20000.0f : cTarget;
            v.filt.setCutoff (v.cutoffSmoothed);

            if (v.pitchMulSmoothed < bendMulTarget) v.pitchMulSmoothed = (v.pitchMulSmoothed + maxStep < bendMulTarget) ? v.pitchMulSmoothed + maxStep : bendMulTarget;
            else                                     v.pitchMulSmoothed = (v.pitchMulSmoothed - maxStep > bendMulTarget) ? v.pitchMulSmoothed - maxStep : bendMulTarget;

            if (v.releasing) {
                v.tailLevel *= 0.9995f;
                if (v.tailLevel < 0.0001f) { v.tailLevel = 0.0f; v.active = false; v.releasing = false; }
            }

            v.currentHz *= v.glideCoeff;
            const bool overshot = v.glideCoeff > 1.0f ? (v.currentHz > v.targetHz) : (v.currentHz < v.targetHz);
            if (v.glideCoeff != 1.0f && overshot) { v.currentHz = v.targetHz; v.glideCoeff = 1.0f; }
            v.osc.setFrequency (v.currentHz * v.pitchMulSmoothed);

            // The real morph engine: 16-frame Harmonic Stack (sine → rich saw)
            // + phase-distortion PW + √2-FM inharmonicity + hard-sync, all from
            // Oscillator::nextMorphed — identical DSP to the plugin, with this
            // voice's OWN mod-matrix-offset values (per-note morph et al.).
            const float oscOut  = v.osc.nextMorphed (vMorph, vPW, vInharm, vSync);
            const float filtOut = v.filt.process (oscOut, SVFilter::Mode::LP);
            renderBuf[s] += filtOut * v.vca * v.tailLevel;

            if (! v.active) break;
        }

        if (gMono && monoVoiceIdx >= 0 && &v == &voices[monoVoiceIdx]) {
            monoLastHz = v.currentHz; monoLastVCA = v.vca * v.tailLevel;
        }
    }

    for (int s = 0; s < n; ++s) renderBuf[s] = masterLimit (renderBuf[s] * pOutput);
}

} // extern "C"
