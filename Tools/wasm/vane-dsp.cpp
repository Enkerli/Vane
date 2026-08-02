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
#include "Synth/FormantFilter.h"   // real global vowel/wah formant stage (juce_core-light)
#include "MPE/TuningClient.h"   // the REAL tuning engine — MTS code compile-gated off
#include "MiniSaxVoice.h"       // the REAL waveguide reed/bore engine (MiniSax — std-lib only)
#include "Synth/BreathEnvelope.h" // synthetic breath for note-only input (shared header, std-lib only)
#include "Synth/SamplePlayer.h" // one-shot PCM transient player (pure std, no JUCE)
#include "Synth/CombResonator.h"// Karplus-Strong pitch resonator (pure std)
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

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

// Per-MPE-channel pressure (1..16), updated by vane_set_expr. In MONO the VCA is
// driven by the MAX pressure across the currently-HELD notes (not just the
// sounding voice's own channel). On an MPE keyboard (Exquis) each note has its
// own channel pressure; a legato phrase presses the next note (pressure ramping
// up from ~0) while releasing the previous (pressure falling), so a mono voice
// that followed only the newest channel would DIP to that note's momentarily-low
// pressure at every transition. Taking the max keeps the volume continuous
// across the crossfade — the "works with anything driving volume" intent, the
// per-note equivalent of a wind controller's single continuous breath. Single
// notes are unaffected (max of one = itself).
float gChPress[17] = {};

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
int   pFilterMode = 0;  // 0 LP / 1 BP / 2 HP — SVFilter::Mode          [id 16]
float pFold   = 0.0f;   // 0..1 wavefold amount (pre-filter drive)      [id 17]
// Global vowel/formant stage (post-mix, one instance — matches the plugin's
// global topology so mono legato stays seamless). Enable OFF by default → the
// filter is a pass-through (amount forced 0). Real Patch-tab ids/units.
FormantFilter gFormantL, gFormantR;   // one per channel — plugin's formantL/formantR
bool  gVowelEn   = false;  // [id 18]  Off/On
int   gVowelMode = 0;      // [id 19]  0 Vowel / 1 Wah
float pVowelPos  = 0.0f;   // [id 20]  open (close→open, F1) / Wah sweep
float pVowFront  = 0.0f;   // [id 21]  back→front (F2)
float pVowRound  = 0.0f;   // [id 22]  lip rounding (lowers F2/F3)
float pVowAmt    = 1.0f;   // [id 23]  dry→formant mix
float pVowBite   = 0.5f;   // [id 24]  reso / Q (talkbox bite)
float pVowMove   = 0.0f;   // [id 25]  slow drift depth
float gVowelPosMod = 0.0f; // VowelPos mod-dest (12), captured from the sounding voice

// Noise blend (pre-filter, mixed with the oscillator — SynthVoice's noise path):
float pNoise     = 0.0f;   // [id 26] 0..1 blend (0 = pure wave, 1 = pure noise)
int   pNoiseType = 0;      // [id 27] 0 White / 1 Pink / 2 Brown
float pDetune    = 0.0f;   // [id 28] cents, ±100 — oscillator detune (added to the pitch multiplier)
float pMasterTune = 0.0f;  // [id 29] cents, ±100 — global tune (same multiplier path)

// Waveguide (MiniSax reed/bore) mode — parity with the plugin's Phase 6 + the
// direct-breath rework: breath (macro/pressure/velocity-mix) blows the reed
// directly, the model's output level IS the dynamics (VCA bypassed for the wg
// signal; blended noise stays VCA-gated), and tone params take the Wg mod-
// matrix destinations (13..20) additively.
bool  gWgOn = false;       // [id 30]
float pWgEmbouchure = 0.5f, pWgReedStiff = 0.5f, pWgAperture = 0.5f;   // [31..33]
float pWgDamping = 0.2f, pWgBell = 0.7f, pWgConical = 0.62f;           // [34..36]
float pWgNoise = 0.05f, pWgGrowl = 0.0f;                               // [37..38]

// Synthetic breath [55..59] — the stand-in wind source for note-only input.
// Mirrors SynthVoice; the envelope itself is the SHARED header, not a second
// copy, because this file already reimplements enough of the voice glue.
int   pSbMode = 1;                                                     // [55] 0 Off, 1 Auto, 2 Always
float pSbAtk = 35.0f, pSbDec = 120.0f, pSbSus = 0.80f, pSbRel = 180.0f;// [56..59]
// Latched once a real breath/expression/pressure message arrives, never
// cleared: a controller resting at zero is a player choosing silence.
bool  gSawRealExpression = false;
constexpr float kWgMakeupGain = 2.5f;   // engine peaks ~0.4 at full breath — same constant as SynthVoice

// ── Stereo unison / rotating chords (SynthVoice parity) ──────────────────────
// Up to kMaxUnison detuned (or chord-interval) sub-voices per note, spread
// across the stereo field with the plugin's exact equal-power pan + power
// normalisation. Chord mode: sub-voice j-1 plays chordInterval[j-1] semitones
// above the melody, captured per note-on from a shared rotation counter that
// wraps at LCM(1..kChordSteps) — the plugin's "deterministic but non-repeating"
// rotating-chord scheme, verbatim.
constexpr int kMaxUnison  = 6;
constexpr int kChordSteps = 16;
constexpr int kChordRotWrap = 720720;   // LCM(1..16) — every seq length divides it
int   pUniVoxChoice = 0;    // [39] choice index → {1,2,3,4,6}
float pUniDet       = 14.0f;// [40] detune spread, cents (0..50)
float pUniWid       = 0.7f; // [41] stereo width (0..1)
bool  gChordMode    = false;// [42] 0 = Detune, 1 = Chord
float gChordSeq[kMaxUnison - 1][kChordSteps] = {};
int   gChordLen[kMaxUnison - 1] = {};
int   gChordRot = 0;

// ── Transient layer (SynthVoice parity) ──────────────────────────────────────
// Factory samples arrive from the host (fetched + WAV-decoded in JS, pushed
// through the staging buffer) instead of BinaryData; index 0 = "None".
struct TrEntry { std::vector<float> data; float srcRate = 48000.0f, nativeHz = 440.0f; bool pitched = true; };
std::vector<TrEntry> gTransients;   // [0] = None sentinel
int   pTrChoice  = 0;      // [43] sample index (0 = None)
float pTrGain    = 0.0f;   // [44] 0..2
float pTrDecay   = 200.0f; // [45] ms
int   pTrTrigger = 1;      // [46] 0 = Always, 1 = Non-legato only
float pTrVar     = 0.3f;   // [47] per-trigger variation 0..1
bool  pTrFilt    = true;   // [48] route through the voice filter
float pTrDyn     = 0.75f;  // [49] 0 = fixed level, 1 = fully VCA-gated
float pTrReso    = 0.3f;   // [50] pitch-resonator depth
float pTrDamp    = 0.5f;   // [51] resonator damping (bright→dark)
float pTrMorph   = 12.0f;  // [52] ms — noise→tone duck ramp
float gTrScratch[kMaxBlock];

// ── Glide modes / curves (SynthVoice parity) ─────────────────────────────────
int  pGlideMode  = 0;      // [53] 0 = Fixed Time, 1 = Fixed Rate (ms per semitone)
int  pGlideCurve = 0;      // [54] 0 Lin / 1 Exp / 2 RC / 3 Bezier
constexpr int kGlideLUT = 65;
float gGlideLUT[kGlideLUT];
bool  gGlideLUTActive = false;

// ── Host staging buffer — wavetable frames / transient PCM / glide anchors ──
// The host grows it via vane_staging(n), writes floats straight into WASM
// memory, then calls the matching commit entry. Loads are user-initiated and
// rare; the brief allocation is acceptable (single-threaded worklet).
std::vector<float> gStaging;

// User-loaded morph wavetable (Library table or .wav import). All oscillators
// point here when active; Built-in reverts to Wavetable::builtInDefault().
Wavetable gUserTable;
bool      gUserTableActive = false;

// ── Monotone-cubic curve LUT — ModMatrix::buildCurveLUT verbatim ─────────────
void monotoneTangents (const std::vector<float>& xs, const std::vector<float>& ys,
                       std::vector<float>& m) {
    const int n = (int) xs.size();
    std::vector<float> d ((size_t) (n - 1));
    for (int i = 0; i < n - 1; ++i) d[(size_t) i] = (ys[(size_t)(i+1)] - ys[(size_t) i]) / (xs[(size_t)(i+1)] - xs[(size_t) i]);
    m.assign ((size_t) n, 0.0f);
    m[0] = d[0]; m[(size_t)(n-1)] = d[(size_t)(n-2)];
    for (int i = 1; i < n - 1; ++i)
        m[(size_t) i] = (d[(size_t)(i-1)] * d[(size_t) i] <= 0.0f) ? 0.0f
                                                                   : (d[(size_t)(i-1)] + d[(size_t) i]) * 0.5f;
    for (int i = 0; i < n - 1; ++i) {
        if (d[(size_t) i] == 0.0f) { m[(size_t) i] = 0.0f; m[(size_t)(i+1)] = 0.0f; continue; }
        const float a = m[(size_t) i] / d[(size_t) i], b = m[(size_t)(i+1)] / d[(size_t) i];
        const float h = std::hypot (a, b);
        if (h > 3.0f) { const float t = 3.0f / h; m[(size_t) i] = t*a*d[(size_t) i]; m[(size_t)(i+1)] = t*b*d[(size_t) i]; }
    }
}
void buildGlideLUT (const std::vector<std::pair<float, float>>& anchors) {
    std::vector<std::pair<float, float>> a;
    for (const auto& pr : anchors)
        a.emplace_back (std::clamp (pr.first, 0.012f, 0.988f), std::clamp (pr.second, 0.0f, 1.0f));
    if (a.empty()) { gGlideLUTActive = false; return; }
    std::sort (a.begin(), a.end(), [](const auto& p1, const auto& q){ return p1.first < q.first; });
    std::vector<float> xs, ys;
    xs.push_back (0.0f); ys.push_back (0.0f);
    for (const auto& pr : a) { xs.push_back (pr.first); ys.push_back (pr.second); }
    xs.push_back (1.0f); ys.push_back (1.0f);
    std::vector<float> m; monotoneTangents (xs, ys, m);
    const int n = (int) xs.size();
    for (int k = 0; k < kGlideLUT; ++k) {
        const float x = (float) k / (float) (kGlideLUT - 1);
        int i = 0; while (i < n - 2 && x > xs[(size_t)(i+1)]) ++i;
        const float x0 = xs[(size_t) i], x1 = xs[(size_t)(i+1)], y0 = ys[(size_t) i], y1 = ys[(size_t)(i+1)];
        const float h = x1 - x0;
        float y;
        if (h <= 0.0f) y = y0;
        else {
            const float t = (x - x0) / h, t2 = t*t, t3 = t2*t;
            y = (2*t3 - 3*t2 + 1)*y0 + (t3 - 2*t2 + t)*h*m[(size_t) i]
              + (-2*t3 + 3*t2)*y1 + (t3 - t2)*h*m[(size_t)(i+1)];
        }
        gGlideLUT[k] = std::clamp (y, 0.0f, 1.0f);
    }
    gGlideLUTActive = true;
}

// One-pole per-sample smoothing coefficient (~3 ms), shared by the oscillator-
// character smoothers (morph/PW/inharm/sync). Set from the sample rate in
// vane_init; matches the real engine's smoothedX.reset(sr, 0.003).
float gParamSmooth = 0.00693f;

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
// 13 original dests + the 8 Waveguide tone dests (13..20) — index-aligned with
// the UI's DESTS list and the plugin's ModSlots::kDestNames. Dest 7 (Noise) and
// 13..20 (Wg) are now implemented here; Transient (10) / UniDetune (11) still
// accumulate unused (those features aren't in the standalone voice yet).
constexpr int kNumDests = 21;
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
    // Mono legato-hold: on the LAST note-off in mono, instead of releasing at
    // once we FREEZE the amplitude for a short window and wait for a possible
    // next note. If one arrives (a slur/detached-but-connected phrase) the voice
    // reconnects with no dip; if the window expires, the normal tail-off runs.
    // This is what makes mono smooth "with anything driving the volume" — with
    // per-note pressure, key-release drops that note's pressure, which would
    // otherwise notch the level between notes; freezing bridges the gap. (An
    // addition beyond the JUCE engine's immediate tail-off, at the user's
    // explicit request — the JUCE version stays seamless via continuous breath /
    // overlapping notes, which a keyboard's detached playing doesn't provide.)
    bool  holding    = false;
    int   holdSamples = 0;
    float holdVca    = 0.0f;
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
    // Oscillator-character params, per-sample smoothed toward their per-block
    // target — same reason as cutoff/pitch. Holding morph/PW/inharm/sync constant
    // for a whole 128-sample render quantum means they STEP at every block
    // boundary when modulated (e.g. Pressure→Morph); each step is a small
    // waveform discontinuity that repeats at the block rate (SR/128 ≈ 375 Hz),
    // producing sidebands at harmonic±375 Hz — audible as a rough, "gritty"
    // buzz that reads as intermodulation, especially with two notes. The real
    // engine per-sample-smooths PW/Inharm/Sync (smoothedPW/Inharm/Sync); morph
    // it block-steps, but smoothing it too only helps and costs nothing here.
    float morphSmoothed  = 0.0f;
    float pwSmoothed     = 0.5f;
    float inharmSmoothed = 0.0f;
    float syncSmoothed   = 1.0f;
    float foldDriveSmoothed = 1.0f;   // wavefold drive (1 = transparent); per-sample smoothed like the real smoothedFoldDrive
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

    // Noise blend state (per-voice, ported verbatim from SynthVoice: 64-bit LCG
    // white source, Paul Kellett 7-state pink approximation, brown integrator).
    uint64_t noiseWhiteState = 0x9E3779B97F4A7C15ULL;
    float    noisePinkB[7] = {};
    float    noiseBrownAcc = 0.0f;

    // Synthetic breath — per voice, handed across a mono legato transition
    // exactly like the bore below (see startNote).
    BreathEnvelope synthBreath;

    // Waveguide (MiniSax) engine — one bore per voice. Reset on non-legato
    // attacks only; mono legato REUSES this voice slot, so the bore keeps
    // ringing across a slur for free (the plugin needs an explicit cross-voice
    // handoff for the same effect — here the state simply stays put).
    minisax::MiniSaxVoice wg;
    uint32_t              wgSeed = 0;

    // Stereo unison: extra sub-voices (osc or bore per slot, mirrors the
    // plugin's unisonOscs/waveguideVoices), a right-channel filter for true
    // stereo width, and this note's captured chord intervals.
    Oscillator            uniOscs[kMaxUnison - 1];
    minisax::MiniSaxVoice wgUni[kMaxUnison - 1];
    uint32_t              wgUniSeeds[kMaxUnison - 1] = {};
    SVFilter              filtR;
    float                 chordInterval[kMaxUnison - 1] = {};

    // Transient layer (SynthVoice's per-voice state, same names).
    SamplePlayer  trPlayer;
    CombResonator trReso;
    SVFilter      trFilt;
    float trEnvLevel = 0.0f, trGainMul = 1.0f;
    bool  trResoActive = false;
    int   trResoSilent = 0;
    float oscMorphRamp = 1.0f, oscMorphInc = 0.0f;
    bool  oscMorphArmed = false;

    // Glide-curve state (Exp / RC / Bezier — Linear uses currentHz/glideCoeff).
    float glideTargetLog = 8.78f;    // log2(targetHz), cached like the plugin
    float glideExpLogHz  = 8.78f, glideExpCoeff = 0.0f;
    float glideRcHz      = 440.0f, glideRcCoeff = 0.0f;
    float glideBezStartLog = 8.78f;
    int   glideBezSamples = 0, glideBezElapsed = 0;
};

Voice   voices[kMaxVoices];
int     monoVoiceIdx = -1;
float   monoLastHz   = 0.0f;
float   monoLastVCA  = 0.0f;
// Stereo render: vane_buffer() stays the LEFT channel (every existing consumer
// — the CLI's `enkerli render`, older worklets — keeps working as mono);
// vane_buffer_r() adds the right channel for the stereo worklet.
float   renderBuf[kMaxBlock];
float   renderBufR[kMaxBlock];

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
// Poly headroom: gently pull the level down as voices stack so the limiter
// engages LESS on chords (the excess intermodulation only appears once the
// limiter pins the peak at the ceiling — below that it's inaudible). 1-2 notes
// are left at full level (min(1, 2/N)); 3+ get progressively more headroom. This
// is standalone-only compensation for having no DAW headroom; it slightly lowers
// chord level vs the plugin but removes the limiter-induced roughness.
float gPolyGain  = 1.0f;
float gPolyCoeff = 0.0f;  // ~15 ms one-pole (set from SR), smooths voice-count steps
// Stereo: ONE envelope from the louder channel and ONE gain applied to both —
// independent per-channel limiters would shift the stereo image whenever one
// side clips (the image "leans away" from peaks); a linked limiter preserves it.
inline void masterLimit (float& l, float& r) {
    const float ax = std::max (std::fabs (l), std::fabs (r));
    if (ax > limEnv) limEnv = ax;                          // instant peak catch
    else             limEnv = ax + (limEnv - ax) * limRelEnv;   // slow release
    constexpr float thr = 0.95f;
    const float tgt = limEnv > thr ? thr / limEnv : 1.0f;
    if (tgt < limGain) limGain += (tgt - limGain) * limGainAtk;   // reduce fast
    else               limGain += (tgt - limGain) * limGainRel;   // recover slow
    l *= limGain; r *= limGain;
    if (l > 1.0f) l = 1.0f; else if (l < -1.0f) l = -1.0f;        // hard safety
    if (r > 1.0f) r = 1.0f; else if (r < -1.0f) r = -1.0f;
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
    for (int c = 0; c < 17; ++c) gChPress[c] = 0.0f;
    gFormantL.prepare (sampleRate); gFormantL.reset();
    gFormantR.prepare (sampleRate); gFormantR.reset(); gVowelPosMod = 0.0f;
    breathSlewer.prepare (sampleRate); breathSlewer.setRates (5.0f, 80.0f); breathSlewer.reset();
    exprSlewer.prepare (sampleRate);   exprSlewer.setRates (5.0f, 80.0f); exprSlewer.reset();
    int vi = 0;
    for (auto& v : voices) {
        v.osc.prepare (sampleRate);   // also wires Wavetable::builtInDefault() (Harmonic Stack)
        v.filt.prepare (sampleRate);
        v.pressureSlewer.prepare (sampleRate); v.pressureSlewer.setRates (3.0f, 50.0f);
        v.slideSlewer.prepare (sampleRate);    v.slideSlewer.setRates (2.0f, 20.0f);
        v.velSlewer.prepare (sampleRate);      v.velSlewer.setRates (20.0f, 0.0f);
        v.pbModSlewer.prepare (sampleRate);    v.pbModSlewer.setRates (2.0f, 20.0f);
        v.active = false; v.tailLevel = 0.0f; v.vca = 0.0f;
        // Waveguide engine: deterministic per-voice noise seed (decorrelated
        // breath noise across voices — same intent as SynthVoice's seeds).
        v.wgSeed = minisax::NoiseGenerator::defaultSeed ^ (uint32_t) (vi * 0x9E3779B1u);
        v.wg.prepare (sampleRate, v.wgSeed);
        v.synthBreath.prepare (sampleRate);
        v.noiseWhiteState = 0x9E3779B97F4A7C15ULL ^ (uint64_t) vi;
        for (auto& b : v.noisePinkB) b = 0.0f;
        v.noiseBrownAcc = 0.0f;
        // Stereo-unison sub-voices: extra oscillators + one bore per slot (each
        // with its own decorrelated seed), and the right-channel filter.
        v.filtR.prepare (sampleRate);
        // Transient layer.
        v.trReso.prepare (sampleRate);
        v.trFilt.prepare (sampleRate);
        v.trPlayer.stop(); v.trEnvLevel = 0.0f; v.trResoActive = false;
        v.oscMorphArmed = false; v.oscMorphRamp = 1.0f;
        for (int j = 0; j < kMaxUnison - 1; ++j) {
            v.uniOscs[j].prepare (sampleRate);
            v.wgUniSeeds[j] = v.wgSeed ^ (uint32_t) ((j + 1) * 0x85EBCA6Bu);
            v.wgUni[j].prepare (sampleRate, v.wgUniSeeds[j]);
            v.chordInterval[j] = 0.0f;
        }
        ++vi;
    }
    // Rotating-chord defaults — the suite's factory sequences (index.html
    // state.chordSeqs '3,4;7,5;10,9;12,7;5,3'); the host overwrites them via
    // vane_set_chord/vane_set_chord_len when the user edits.
    {
        const float defSeq[kMaxUnison - 1][2] = { {3,4}, {7,5}, {10,9}, {12,7}, {5,3} };
        for (int j = 0; j < kMaxUnison - 1; ++j) {
            gChordLen[j] = 2;
            for (int k = 0; k < kChordSteps; ++k)
                gChordSeq[j][k] = (k < 2) ? defSeq[j][k] : 0.0f;
        }
        gChordRot = 0;
    }
    // Transient store: keep host-loaded samples across re-inits (the host loads
    // once per page); just ensure the None sentinel exists.
    if (gTransients.empty()) gTransients.emplace_back();
    monoVoiceIdx = -1; monoLastHz = 0.0f; monoLastVCA = 0.0f; heldCount = 0;
    // Master limiter coefficients (see masterLimit): 50 ms env release, 1 ms
    // gain attack, 150 ms gain recovery — the slow recovery is what keeps it
    // from pumping at a musical beat rate.
    const float sr = (float) sampleRate;
    limEnv = 0.0f; limGain = 1.0f;
    limRelEnv  = std::exp (-1.0f / (0.050f * sr));
    limGainAtk = 1.0f - std::exp (-1.0f / (0.001f * sr));
    limGainRel = 1.0f - std::exp (-1.0f / (0.150f * sr));
    gPolyGain  = 1.0f;
    gPolyCoeff = 1.0f - std::exp (-1.0f / (0.015f * sr));     // 15 ms one-pole (poly-headroom step smoothing)
    gParamSmooth = 1.0f - std::exp (-1.0f / (0.003f * sr));   // 3 ms one-pole (morph/PW/inharm/sync)
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
    // Breath and expression only — deliberately NOT CC74, which streams from
    // any MPE keyboard and would make Auto stand down for a player who has no
    // wind controller at all. Plugin parity.
    if ((cc == 2 || cc == 11) && v01 > 0.0f) gSawRealExpression = true;
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
    // Synthetic breath takes the SAME legato decision as the bore and the VCA
    // below — that is what puts several notes inside one breath (melisma).
    // monoLastVCA is the resume level: mono here reuses the voice slot so the
    // envelope is already running, but the plugin allocates a fresh voice, and
    // passing it keeps the two implementations honest.
    v.synthBreath.noteOn (v.vel, legato && gMono, monoLastVCA);
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
        // Waveguide: clear the bores on fresh attacks only — a legato note keeps
        // the ringing bores (this voice slot is reused in mono, so continuity is
        // inherent) and re-entrains them at the new delay length: a physical slur.
        v.wg.reset (v.wgSeed);
        for (int j = 0; j < kMaxUnison - 1; ++j) v.wgUni[j].reset (v.wgUniSeeds[j]);
        v.pitchMulSmoothed = 1.0f;             // matches smoothedPitchMult.setCurrentAndTargetValue(1.0f) at prepare
        v.cutoffSmoothed   = pCutoff;          // matches smoothedCutoff.setCurrentAndTargetValue(initCutoff) at note-on
        v.morphSmoothed = pMorph; v.pwSmoothed = pPW; v.inharmSmoothed = pInharm; v.syncSmoothed = pSync;  // snap osc params on a fresh attack
        v.foldDriveSmoothed = Oscillator::foldDrive (clamp01 (pFold));
    }
    v.tailLevel = 1.0f; v.releasing = false;
    v.holding = false; v.holdSamples = 0;    // a new note cancels any pending legato-hold (seamless reconnect)

    // Rotating-chord capture — SynthVoice::noteStarted verbatim: EVERY note-on
    // in chord mode advances the shared counter (legato included), and this
    // note keeps the intervals it captured for its whole life. The counter
    // wraps at LCM(1..kChordSteps), so each sequence's (idx % len) phase is
    // continuous across the wrap.
    if (gChordMode) {
        const int idx = gChordRot;
        gChordRot = (idx + 1) % kChordRotWrap;
        for (int j = 0; j < kMaxUnison - 1; ++j)
            v.chordInterval[j] = (gChordLen[j] > 0)
                ? gChordSeq[j][idx % gChordLen[j]]
                : 0.0f;
    }

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
    // Fixed Rate (mode 1): glideMs is the cost PER SEMITONE — larger intervals
    // take proportionally longer, capped at 5 s (SynthVoice verbatim).
    float nominalGlideMs = pGlideMs;
    if (pGlideMode == 1 && legato && prevHz > 0.0f && pGlideMs > 0.0f) {
        const float semis = std::fabs (12.0f * std::log2 (targetHz / prevHz));
        nominalGlideMs = std::min (pGlideMs * semis, 5000.0f);
    }
    const float minGlideMs = (legato && prevHz > 0.0f) ? (1000.0f / targetHz) : 0.0f;
    const float effGlideMs = legato ? (nominalGlideMs > minGlideMs ? nominalGlideMs : minGlideMs) : 0.0f;

    // Curve state — SynthVoice::noteStarted's four branches. All start at
    // prevHz on legato so the handoff is slope-continuous; non-legato snaps.
    // Coefficient formula (Exp/RC): c = 1 - exp(-4.6/N) -> 99% arrival in N.
    v.glideTargetLog = std::log2 (targetHz > 1.0f ? targetHz : 1.0f);
    const bool glideLegato = legato && effGlideMs > 0.0f && prevHz > 0.0f;
    if (pGlideCurve == 1) {          // Exponential — 1-pole IIR in log2(Hz)
        if (glideLegato) {
            v.glideExpLogHz = std::log2 (prevHz);
            const float N   = effGlideMs * 0.001f * (float) gSampleRate;
            v.glideExpCoeff = (N > 0.0f) ? (1.0f - std::exp (-4.6f / N)) : 1.0f;
        } else { v.glideExpLogHz = v.glideTargetLog; v.glideExpCoeff = 1.0f; }
        v.currentHz = glideLegato ? prevHz : targetHz; v.glideCoeff = 1.0f;
    } else if (pGlideCurve == 2) {   // RC — 1-pole IIR in linear Hz
        if (glideLegato) {
            v.glideRcHz  = prevHz;
            const float N = effGlideMs * 0.001f * (float) gSampleRate;
            v.glideRcCoeff = (N > 0.0f) ? (1.0f - std::exp (-4.6f / N)) : 1.0f;
        } else { v.glideRcHz = targetHz; v.glideRcCoeff = 1.0f; }
        v.currentHz = glideLegato ? prevHz : targetHz; v.glideCoeff = 1.0f;
    } else if (pGlideCurve == 3) {   // Bezier — time-driven trajectory (LUT)
        if (glideLegato) {
            v.glideBezStartLog = std::log2 (prevHz);
            v.glideBezSamples  = (int) (effGlideMs * 0.001f * (float) gSampleRate);
            v.glideBezElapsed  = 0;
        } else { v.glideBezSamples = 0; }
        v.currentHz = glideLegato ? prevHz : targetHz; v.glideCoeff = 1.0f;
    } else {                          // Linear in semitones — multiplicative
        if (glideLegato) {
            v.currentHz = prevHz;
            const float nSamples = effGlideMs * 0.001f * (float) gSampleRate;
            v.glideCoeff = std::pow (targetHz / prevHz, 1.0f / (nSamples > 1.0f ? nSamples : 1.0f));
        } else {
            v.currentHz = targetHz; v.glideCoeff = 1.0f;
        }
    }
    v.osc.setFrequency (v.currentHz);

    // ── Transient trigger — SynthVoice::noteStarted verbatim ─────────────────
    // "Always" (0) fires on every note-on; "Non-legato" (1) only on a fresh
    // attack. Pitched samples track the note (speed = hz/nativeHz, clamped
    // 0.125..8); inharmonic ones play at fixed speed. Per-trigger variation
    // jitters gain (±2.5 dB), micro-pitch (±5%), and — noise only — the start
    // offset (up to ~4 ms) so repeated hits stop sounding like a loop.
    if (pTrChoice > 0 && pTrChoice < (int) gTransients.size()) {
        const bool shouldTrigger = (pTrTrigger == 0) || ! legato;
        if (shouldTrigger) {
            TrEntry& ts = gTransients[(size_t) pTrChoice];
            if (ts.data.size() >= 2) {
                const float pitchTrack = ts.pitched
                    ? std::clamp (targetHz / ts.nativeHz, 0.125f, 8.0f) : 1.0f;
                float speedRatio = pitchTrack * (float) (ts.srcRate / gSampleRate);
                auto rnd = [&v]() {
                    v.noiseWhiteState = v.noiseWhiteState * 6364136223846793005ULL
                                                          + 1442695040888963407ULL;
                    return (float) ((int32_t) (v.noiseWhiteState >> 33)) / 2147483648.0f;
                };
                v.trGainMul  = 1.0f + pTrVar * 0.30f * rnd();
                speedRatio  *= 1.0f + pTrVar * 0.05f * rnd();
                int startOff = 0;
                if (! ts.pitched) {
                    const float u = 0.5f * (rnd() + 1.0f);
                    startOff = (int) (pTrVar * u * 0.004f * ts.srcRate);
                }
                v.trPlayer.setSample (ts.data.data(), (int) ts.data.size());
                v.trEnvLevel = 1.0f;
                v.trPlayer.trigger (speedRatio, startOff);
                v.trReso.reset();
                v.trReso.setTuning (targetHz > 0.0f ? targetHz : 110.0f);
                v.trResoActive = true; v.trResoSilent = 0;
                // Noise→tone morph: duck the note body so the oscillator
                // emerges from under the transient (TrMorph ms ramp).
                if (pTrMorph > 0.5f) {
                    v.oscMorphRamp  = 0.0f;
                    v.oscMorphInc   = 1.0f / (pTrMorph * 0.001f * (float) gSampleRate);
                    v.oscMorphArmed = true;
                } else { v.oscMorphRamp = 1.0f; v.oscMorphArmed = false; }
            }
        }
    } else { v.oscMorphArmed = false; v.oscMorphRamp = 1.0f; }
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
        if (v.active && v.note == note && (channel < 0 || v.channel == channel)) {
            if (gMono) {                             // legato-hold: freeze, wait for a next note, then release
                v.holding = true; v.holdVca = v.vca;
                v.holdSamples = (int) (0.100f * (float) gSampleRate);   // 100 ms bridge window
            } else {
                v.releasing = true;                  // poly: each note releases at once (matches per-note behaviour)
            }
            // The breath dies either way. If a next note arrives inside the mono
            // bridge window it re-aims through the legato path above, so a trill
            // does not get chopped by this.
            v.synthBreath.noteOff();
        }
}

// Per-MPE-channel expression update (applies to the sounding voice on that channel).
void vane_set_expr (int channel, float bend, float slide, float pressure) {
    if (channel >= 1 && channel <= 16) gChPress[channel] = pressure;   // for the mono max-pressure driver
    if (pressure > 0.0f) gSawRealExpression = true;
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
        case 16: pFilterMode = (int) (val + 0.5f); if (pFilterMode < 0) pFilterMode = 0; else if (pFilterMode > 2) pFilterMode = 2; break;
        case 17: pFold   = val; break;
        case 18: gVowelEn   = (val > 0.5f); break;
        case 19: gVowelMode = (val > 0.5f) ? 1 : 0; break;
        case 20: pVowelPos  = val; break;
        case 21: pVowFront  = val; break;
        case 22: pVowRound  = val; break;
        case 23: pVowAmt    = val; break;
        case 24: pVowBite   = val; break;
        case 25: pVowMove   = val; break;
        case 26: pNoise     = val; break;
        case 27: pNoiseType = (int) (val + 0.5f); if (pNoiseType < 0) pNoiseType = 0; else if (pNoiseType > 2) pNoiseType = 2; break;
        case 28: pDetune     = val; break;   // cents
        case 29: pMasterTune = val; break;   // cents
        case 30: gWgOn = (val > 0.5f); break;
        case 31: pWgEmbouchure = val; break;
        case 32: pWgReedStiff  = val; break;
        case 33: pWgAperture   = val; break;
        case 34: pWgDamping    = val; break;
        case 35: pWgBell       = val; break;
        case 36: pWgConical    = val; break;
        case 37: pWgNoise      = val; break;
        case 38: pWgGrowl      = val; break;
        case 39: pUniVoxChoice = (int) (val + 0.5f); if (pUniVoxChoice < 0) pUniVoxChoice = 0; else if (pUniVoxChoice > 4) pUniVoxChoice = 4; break;
        case 40: pUniDet       = val; break;   // cents 0..50
        case 41: pUniWid       = val; break;   // 0..1
        case 42: gChordMode    = (val > 0.5f); break;
        case 43: pTrChoice  = (int) (val + 0.5f); break;
        case 44: pTrGain    = val; break;
        case 45: pTrDecay   = val; break;
        case 46: pTrTrigger = (int) (val + 0.5f); break;
        case 47: pTrVar     = val; break;
        case 48: pTrFilt    = (val > 0.5f); break;
        case 49: pTrDyn     = val; break;
        case 50: pTrReso    = val; break;
        case 51: pTrDamp    = val; break;
        case 52: pTrMorph   = val; break;
        case 53: pGlideMode  = (int) (val + 0.5f); break;
        case 54: pGlideCurve = (int) (val + 0.5f); break;
        case 55: pSbMode = (int) (val + 0.5f); if (pSbMode < 0) pSbMode = 0; else if (pSbMode > 2) pSbMode = 2; break;
        case 56: pSbAtk  = val; break;   // ms
        case 57: pSbDec  = val; break;   // ms
        case 58: pSbSus  = val; break;   // 0..1
        case 59: pSbRel  = val; break;   // ms
        default: break;
    }
}

// ── Host staging + load entries (wavetables / transients / glide anchors) ────
float* vane_staging (int nFloats) {
    if (nFloats < 1) nFloats = 1;
    if ((int) gStaging.size() < nFloats) gStaging.resize ((size_t) nFloats);
    return gStaging.data();
}

// Build the user morph wavetable from staged raw frames: total/frameSize
// frames, each linear-resampled to Wavetable::kTableSize — mirroring
// loadFromMemory's slicing (which is compiled out under VANE_WASM because it
// depends on JUCE's WAV reader; the HOST decodes the WAV instead).
// Returns the frame count (0 = rejected). All oscillators re-point here.
int vane_load_wavetable (int totalSamples, int frameSize, int phaseAlign) {
    if (frameSize < 8 || totalSamples < frameSize
        || totalSamples > (int) gStaging.size()) return 0;
    const int numFrames = totalSamples / frameSize;
    std::vector<std::vector<float>> frames ((size_t) numFrames);
    for (int f = 0; f < numFrames; ++f) {
        auto& out = frames[(size_t) f];
        out.resize ((size_t) Wavetable::kTableSize);
        const float* in = gStaging.data() + (size_t) f * (size_t) frameSize;
        for (int i = 0; i < Wavetable::kTableSize; ++i) {
            const float pos = (float) i * (float) frameSize / (float) Wavetable::kTableSize;
            const int   i0  = (int) pos;
            const float fr  = pos - (float) i0;
            const int   i1  = (i0 + 1 < frameSize) ? i0 + 1 : 0;   // wrap: single cycle
            out[(size_t) i] = in[i0] + fr * (in[i1] - in[i0]);
        }
    }
    Wavetable built;
    if (! built.build (frames, phaseAlign != 0)) return 0;
    gUserTable = std::move (built);
    gUserTableActive = true;
    for (auto& v : voices) {
        v.osc.setWavetable (&gUserTable);
        for (auto& o : v.uniOscs) o.setWavetable (&gUserTable);
    }
    return numFrames;
}

void vane_use_builtin_wavetable () {
    gUserTableActive = false;
    for (auto& v : voices) {
        v.osc.setWavetable (&Wavetable::builtInDefault());
        for (auto& o : v.uniOscs) o.setWavetable (&Wavetable::builtInDefault());
    }
}

// Append one transient sample from staging. Returns its 1-based index
// (matching the page's TrChoice ordering; 0 stays "None").
int vane_add_transient (int totalSamples, float srcRate, float nativeHz, int pitched) {
    if (totalSamples < 2 || totalSamples > (int) gStaging.size()) return 0;
    if (gTransients.empty()) gTransients.emplace_back();   // None sentinel
    TrEntry e;
    e.data.assign (gStaging.begin(), gStaging.begin() + totalSamples);
    e.srcRate  = srcRate  > 0.0f ? srcRate  : 48000.0f;
    e.nativeHz = nativeHz > 0.0f ? nativeHz : 440.0f;
    e.pitched  = pitched != 0;
    gTransients.push_back (std::move (e));
    return (int) gTransients.size() - 1;
}

// Bezier glide trajectory: staging holds nPairs (x,y) anchor pairs.
void vane_set_glide_anchors (int nPairs) {
    std::vector<std::pair<float, float>> anchors;
    for (int i = 0; i < nPairs && (i * 2 + 1) < (int) gStaging.size(); ++i)
        anchors.emplace_back (gStaging[(size_t)(i * 2)], gStaging[(size_t)(i * 2 + 1)]);
    buildGlideLUT (anchors);
}

// Rotating-chord sequences (the UI's chordSeqsEdit): per harmony voice j
// (0..kMaxUnison-2), a length + up to kChordSteps fractional-semitone steps.
// Fractional so just ratios keep their precision (the host parses "3/2" into
// 12·log2(3/2) before sending) — same contract as the plugin's setChordSeqs.
void vane_set_chord_len (int j, int len) {
    if (j < 0 || j >= kMaxUnison - 1) return;
    gChordLen[j] = len < 0 ? 0 : (len > kChordSteps ? kChordSteps : len);
}
void vane_set_chord (int j, int k, float semis) {
    if (j < 0 || j >= kMaxUnison - 1 || k < 0 || k >= kChordSteps) return;
    gChordSeq[j][k] = semis;
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

float* vane_buffer   () { return renderBuf;  }   // LEFT (and mono-compat) channel
float* vane_buffer_r () { return renderBufR; }   // RIGHT channel (stereo worklet)

void vane_render (int n) {
    if (n > kMaxBlock) n = kMaxBlock;
    for (int s = 0; s < n; ++s) { renderBuf[s] = 0.0f; renderBufR[s] = 0.0f; }

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
                // Non-linear curves read their own targets — retune those too
                // (15 ms arrival, same as the linear path above).
                v.glideTargetLog = std::log2 (newHz);
                const float c = 1.0f - std::exp (-4.6f / nSamples);
                v.glideExpCoeff = c; v.glideRcCoeff = c;
                v.glideBezStartLog = std::log2 (v.currentHz > 1.0f ? v.currentHz : 1.0f);
                v.glideBezSamples  = (int) nSamples; v.glideBezElapsed = 0;
            }
        }

        v.pressureSlewer.setStep (n);
        v.slideSlewer.setStep (n);
        v.velSlewer.setStep (n);
        v.pbModSlewer.setStep (n);

        // ── Per-voice slewed mod sources ────────────────────────────────────────
        // During a legato-hold, FREEZE the per-voice slewers (read their held
        // value, don't advance them toward the released note's fading expression)
        // so the whole per-note modulation state is bridged across the gap, not
        // just the VCA — otherwise pressure/slide would decay during the hold and
        // notch the reconnect. Global breath/expression are shared and keep moving.
        const float slideBp = (v.slide - 0.5f) * 2.0f;             // neutral(0.5) -> 0, matches SynthVoice.cpp
        // MONO VCA driver = max pressure across the held notes (continuous across
        // a legato channel-switch); poly / single note uses the voice's own.
        float pressInput = v.pressure;
        if (gMono && heldCount > 0) {
            pressInput = 0.0f;
            for (int i = 0; i < heldCount; ++i) {
                const int ch = heldStack[i].channel;
                if (ch >= 1 && ch <= 16 && gChPress[ch] > pressInput) pressInput = gChPress[ch];
            }
        }
        const float pressS  = v.holding ? v.pressureSlewer.value() : v.pressureSlewer.process (pressInput);
        const float slideS  = v.holding ? v.slideSlewer.value()    : v.slideSlewer.process (slideBp);
        const float velS    = v.holding ? v.velSlewer.value()      : v.velSlewer.process (v.vel);
        const float pbS     = v.holding ? v.pbModSlewer.value()    : v.pbModSlewer.process (v.bend);   // Pitchbend as a MOD source

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
        // The global formant stage's Vowel-open is modulated by dest 12 from the
        // SOUNDING voice (last active wins, like the plugin's modOut[VowelPos]).
        gVowelPosMod = mods[12];

        // MPE pitchbend (X): a per-channel multiplier on top of the glided base
        // pitch (currentHz), NOT folded into the portamento target — matches the
        // real engine's separate smoothedHz x smoothedPitchMult. mods[3] (Pitch)
        // adds mod-matrix fine-tune semitones on top, like OscPitchFine. This is
        // the RAW per-block target; v.pitchMulSmoothed ramps to it per-sample
        // below (mirrors smoothedPitchMult exactly — see the Voice struct note).
        // Detune + Master Tune ride the same multiplier (cents → semitones),
        // exactly like SynthVoice's totalSemitones (+ detuneCents / 100).
        const float bendMulTarget = std::pow (2.0f,
            (v.bend * pBendRange + mods[3] + (pDetune + pMasterTune) * 0.01f) / 12.0f);

        // Legato-hold countdown: while holding, the VCA is FROZEN at the level it
        // had at note-off (bridging the gap to a possible next note); when the
        // window expires with no new note, fall through to the normal tail-off.
        if (v.holding) {
            v.holdSamples -= n;
            if (v.holdSamples <= 0) { v.holding = false; v.releasing = true; }
        }

        // Dest application — mirrors SynthVoice::renderNextBlock:
        const float vcaTarget      = v.holding ? v.holdVca
                                               : clamp01 (pVelVCA * std::sqrt (v.vel) + mods[0]);
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
        // Wavefold: amount + OscFold mod (dest 6) → exponential drive once per
        // block (std::pow too costly per sample), ramped per-sample below. Applied
        // to the oscillator output BEFORE the filter, exactly like the real engine.
        const float foldDriveTarget = Oscillator::foldDrive (clamp01 (pFold + mods[6]));
        const SVFilter::Mode filtMode = (SVFilter::Mode) pFilterMode;   // 0 LP / 1 BP / 2 HP

        // Noise blend amount (dest 7 = OscNoiseMix), block-rate like the plugin.
        const float noiseMix = clamp01 (pNoise + mods[7]);

        // ── Waveguide (MiniSax) block-rate snapshot — mirrors SynthVoice ───────
        // Tone params take the Wg mod destinations (13..20) additively; breath
        // blows the reed DIRECTLY (breath/expression macro, MPE pressure, or the
        // velocity mix as keyboard fallback — max of them), no floor, no VCA
        // routing: dynamics/subtone/rearticulation come from the model. The
        // engine smooths breath internally (20 ms), so block-rate is fine.
        minisax::VoiceInputs wgIn;
        if (gWgOn) {
            wgIn.params.embouchure     = clamp01 (pWgEmbouchure + mods[13]);
            wgIn.params.reedStiffness  = clamp01 (pWgReedStiff  + mods[14]);
            wgIn.params.reedAperture   = clamp01 (pWgAperture   + mods[15]);
            wgIn.params.boreDamping    = clamp01 (pWgDamping    + mods[16]);
            wgIn.params.bellBrightness = clamp01 (pWgBell       + mods[17]);
            wgIn.params.conicalAmount  = clamp01 (pWgConical    + mods[18]);
            wgIn.params.noiseAmount    = clamp01 (pWgNoise      + mods[19]);
            wgIn.params.growlAmount    = clamp01 (pWgGrowl      + mods[20]);
            // Vibrato comes from Vane's own modulation, not the engine LFO.
            wgIn.params.vibratoAirAmount   = 0.0f;
            wgIn.params.vibratoPitchAmount = 0.0f;
            wgIn.params.outputGain         = 1.0f;
            // Synthetic breath joins the SAME max(), so it can only add a
            // floor-with-a-shape and never overrides a player who is blowing.
            // Auto stands down once a real expression message has been seen —
            // the LATCH, not the current value.
            float synth = 0.0f;
            if (pSbMode == 2 || (pSbMode == 1 && ! gSawRealExpression)) {
                BreathEnvelope::Params bp;
                bp.attackMs = pSbAtk; bp.decayMs = pSbDec;
                bp.sustain  = pSbSus; bp.releaseMs = pSbRel;
                synth = v.synthBreath.advance (n, bp);
            } else {
                v.synthBreath.reset();
            }
            wgIn.params.breath = clamp01 (std::max (std::max (breathS, exprS),
                                                    std::max (std::max (pressS, synth),
                                                              pVelVCA * std::sqrt (v.vel))));
        }

        // ── Stereo unison — SynthVoice's block-rate computation verbatim ────────
        // uN sub-voices spread across the field with equal-power pans, power-
        // normalised so engaging unison doesn't change the level; Detune mode
        // spreads ±uDetune cents, Chord mode plays this note's captured
        // intervals. UnisonDetune mod (dest 11) sweeps the spread live.
        static constexpr int kUnisonChoice[] = { 1, 2, 3, 4, 6 };
        const int  uN       = kUnisonChoice[pUniVoxChoice];
        const bool unisonOn = uN > 1;
        const float uDetune = clampf (pUniDet + mods[11] * 50.0f, 0.0f, 50.0f);
        float uDetMul[kMaxUnison], uPanL[kMaxUnison], uPanR[kMaxUnison];
        {
            float pwr = 0.0f;
            for (int j = 0; j < uN; ++j) {
                const float spread = (uN > 1) ? ((float) j / (float) (uN - 1)) * 2.0f - 1.0f : 0.0f;
                uDetMul[j] = gChordMode
                    ? ((j == 0) ? 1.0f : std::pow (2.0f, v.chordInterval[j - 1] / 12.0f))
                    : std::pow (2.0f, spread * uDetune / 1200.0f);
                const float ang = (spread * pUniWid + 1.0f) * 0.25f * 3.14159265358979f;
                uPanL[j] = std::cos (ang);
                uPanR[j] = std::sin (ang);
                pwr += uPanL[j] * uPanL[j] + uPanR[j] * uPanR[j];
            }
            const float norm = (pwr > 0.0f) ? std::sqrt (2.0f / pwr) : 1.0f;
            for (int j = 0; j < uN; ++j) { uPanL[j] *= norm; uPanR[j] *= norm; }
        }

        // Resonance (k) is block-rate, matching the real engine — only cutoff
        // (g/a1-a3) needs per-sample updates to stay coefficient-continuous.
        v.filt.setResonance (resonance);
        if (unisonOn) v.filtR.setResonance (resonance);

        // ── Transient: pre-render this block into the shared scratch ─────────
        // (env × gain × per-trigger jitter, NOT tailLevel and NOT the filter —
        // both are applied per-sample below, mirroring SynthVoice.) mods[10]
        // is the TransientLevel matrix destination (e.g. Velocity→Transient).
        int   transientN = 0;
        bool  transientRouteFilter = false;
        if (v.trPlayer.isPlaying() && pTrChoice > 0) {
            const float gain0 = std::clamp (pTrGain + mods[10], 0.0f, 2.0f) * v.trGainMul;
            if (gain0 > 1.0e-6f) {
                const float decayCoeff = pTrDecay > 0.0f
                    ? std::exp (-1.0f / (pTrDecay * 0.001f * (float) gSampleRate)) : 0.0f;
                transientN = n;
                for (int i = 0; i < n; ++i) gTrScratch[i] = 0.0f;
                v.trPlayer.renderAdding (gTrScratch, n, v.trEnvLevel, decayCoeff, gain0);
                transientRouteFilter = pTrFilt;
                if (transientRouteFilter) v.trFilt.setResonance (resonance);
            }
        }
        const float resoFb   = 0.90f * pTrReso;
        const float resoDamp = 1.0f - 0.95f * pTrDamp;
        const bool  resoOn   = pTrReso > 1.0e-4f && v.trResoActive;
        const bool  transientOn = (transientN > 0) || resoOn;

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

            // Oscillator character: one-pole 3 ms smoothing toward the per-block
            // target so morph/PW/inharm/sync never STEP at a block boundary (which
            // otherwise buzzes at SR/128 when they're modulated — see the Voice
            // struct note). Mirrors the real engine's smoothedPW/Inharm/Sync.
            v.morphSmoothed  += (vMorph  - v.morphSmoothed)  * gParamSmooth;
            v.pwSmoothed     += (vPW     - v.pwSmoothed)     * gParamSmooth;
            v.inharmSmoothed += (vInharm - v.inharmSmoothed) * gParamSmooth;
            v.syncSmoothed   += (vSync   - v.syncSmoothed)   * gParamSmooth;
            v.foldDriveSmoothed += (foldDriveTarget - v.foldDriveSmoothed) * gParamSmooth;

            if (v.releasing) {
                v.tailLevel *= 0.9995f;
                if (v.tailLevel < 0.0001f) { v.tailLevel = 0.0f; v.active = false; v.releasing = false; }
            }

            // Glide — SynthVoice's four curves. Linear advances currentHz
            // multiplicatively (constant semitone rate); Exp is a 1-pole IIR in
            // log2(Hz); RC the same in linear Hz (analog-circuit feel); Bezier
            // interpolates log-pitch along the editable LUT trajectory.
            // currentHz mirrors the resolved pitch in every mode so the mono
            // legato handoff (monoLastHz → prevHz) works across curve changes.
            if (pGlideCurve == 1) {
                v.glideExpLogHz += (v.glideTargetLog - v.glideExpLogHz) * v.glideExpCoeff;
                if (std::fabs (v.glideExpLogHz - v.glideTargetLog) < 8.33e-6f)   // 0.01 cent
                    v.glideExpLogHz = v.glideTargetLog;
                v.currentHz = std::exp2 (v.glideExpLogHz);
            } else if (pGlideCurve == 2) {
                v.glideRcHz += (v.targetHz - v.glideRcHz) * v.glideRcCoeff;
                if (std::fabs (v.glideRcHz - v.targetHz) < 0.01f)                // ~0.04 cent at A4
                    v.glideRcHz = v.targetHz;
                v.currentHz = v.glideRcHz;
            } else if (pGlideCurve == 3) {
                if (v.glideBezSamples <= 0 || v.glideBezElapsed >= v.glideBezSamples) {
                    v.currentHz = v.targetHz;
                } else {
                    const float t = (float) v.glideBezElapsed / (float) v.glideBezSamples;
                    float prog = t;
                    if (gGlideLUTActive) {                        // 65-point LUT, linear interp
                        const float f = t * (float) (kGlideLUT - 1);
                        const int   i0 = (int) f;
                        prog = (i0 >= kGlideLUT - 1) ? gGlideLUT[kGlideLUT - 1]
                             : gGlideLUT[i0] + (f - (float) i0) * (gGlideLUT[i0 + 1] - gGlideLUT[i0]);
                    }
                    v.currentHz = std::exp2 (v.glideBezStartLog
                                             + prog * (v.glideTargetLog - v.glideBezStartLog));
                    ++v.glideBezElapsed;
                }
            } else {
                v.currentHz *= v.glideCoeff;
                const bool overshot = v.glideCoeff > 1.0f ? (v.currentHz > v.targetHz) : (v.currentHz < v.targetHz);
                if (v.glideCoeff != 1.0f && overshot) { v.currentHz = v.targetHz; v.glideCoeff = 1.0f; }
            }
            const float hzNow = v.currentHz * v.pitchMulSmoothed;
            v.osc.setFrequency (hzNow * uDetMul[0]);
            for (int j = 1; j < uN; ++j) v.uniOscs[j - 1].setFrequency (hzNow * uDetMul[j]);

            // Sound source: the real morph engine (16-frame Harmonic Stack +
            // PD PW + √2-FM inharmonicity + hard-sync via Oscillator::
            // nextMorphed), or the MiniSax reed/bore when Waveguide mode is on
            // — identical DSP to the plugin either way. uN sub-voices (osc or
            // bore each) pitched by uDetMul and panned by uPanL/R, exactly the
            // plugin's stereo-unison path.
            float srcL, srcR;
            if (gWgOn) {
                wgIn.gate = v.releasing ? 0.0f : 1.0f;   // holding = still bridging → keep blowing
                if (! unisonOn) {
                    wgIn.pitchHz = hzNow;
                    srcL = srcR = v.wg.processSample (wgIn) * kWgMakeupGain;
                } else {
                    srcL = srcR = 0.0f;
                    for (int j = 0; j < uN; ++j) {
                        wgIn.pitchHz = hzNow * uDetMul[j];
                        minisax::MiniSaxVoice& eng = (j == 0) ? v.wg : v.wgUni[j - 1];
                        const float w = eng.processSample (wgIn) * kWgMakeupGain;
                        srcL += w * uPanL[j]; srcR += w * uPanR[j];
                    }
                }
            } else if (! unisonOn) {
                srcL = srcR = v.osc.nextMorphed (v.morphSmoothed, v.pwSmoothed, v.inharmSmoothed, v.syncSmoothed);
            } else {
                const float w0 = v.osc.nextMorphed (v.morphSmoothed, v.pwSmoothed, v.inharmSmoothed, v.syncSmoothed);
                srcL = w0 * uPanL[0]; srcR = w0 * uPanR[0];
                for (int j = 1; j < uN; ++j) {
                    const float w = v.uniOscs[j - 1].nextMorphed (v.morphSmoothed, v.pwSmoothed, v.inharmSmoothed, v.syncSmoothed);
                    srcL += w * uPanL[j]; srcR += w * uPanR[j];
                }
            }

            // Noise blend (pre-filter, so noise shares the SVF's tonal character)
            // — SynthVoice's generation verbatim: 64-bit LCG white, Paul Kellett
            // pink, leaky-integrator brown. In waveguide mode the blended noise
            // stays VCA-gated (the model is self-dynamic; bare noise is not).
            if (noiseMix > 0.0005f) {
                v.noiseWhiteState = v.noiseWhiteState * 6364136223846793005ULL
                                                      + 1442695040888963407ULL;
                const float white = (float) ((int32_t) (v.noiseWhiteState >> 33)) / 2147483648.0f;
                float noiseOut;
                if (pNoiseType == 1) {
                    v.noisePinkB[0] = 0.99886f * v.noisePinkB[0] + white * 0.0555179f;
                    v.noisePinkB[1] = 0.99332f * v.noisePinkB[1] + white * 0.0750759f;
                    v.noisePinkB[2] = 0.96900f * v.noisePinkB[2] + white * 0.1538520f;
                    v.noisePinkB[3] = 0.86650f * v.noisePinkB[3] + white * 0.3104856f;
                    v.noisePinkB[4] = 0.55000f * v.noisePinkB[4] + white * 0.5329522f;
                    v.noisePinkB[5] = -0.7616f * v.noisePinkB[5] - white * 0.0168980f;
                    noiseOut = (v.noisePinkB[0] + v.noisePinkB[1] + v.noisePinkB[2]
                               + v.noisePinkB[3] + v.noisePinkB[4] + v.noisePinkB[5]
                               + v.noisePinkB[6] + white * 0.5362f) * (1.0f / 9.0f);
                    v.noisePinkB[6] = white * 0.115926f;
                } else if (pNoiseType == 2) {
                    v.noiseBrownAcc = 0.999f * v.noiseBrownAcc + 0.025f * white;
                    noiseOut = v.noiseBrownAcc * 3.0f;
                } else {
                    noiseOut = white;
                }
                float wet = noiseOut * noiseMix;
                if (gWgOn) wet *= v.vca;
                srcL = srcL * (1.0f - noiseMix) + wet;   // noise is mono/centred —
                srcR = srcR * (1.0f - noiseMix) + wet;   // added equally, like the plugin
            }

            // Waveguide mode bypasses the VCA multiply — the model's output
            // level IS the dynamics (breath drove the reed); tailLevel stays
            // as the voice-death envelope. Mirrors SynthVoice's vgain exactly.
            const float vgain = (gWgOn ? 1.0f : v.vca) * v.tailLevel;
            srcL = Oscillator::wavefold (srcL, v.foldDriveSmoothed);        // pre-filter, like SynthVoice
            const float outL = v.filt.process (srcL, filtMode) * vgain;     // LP / BP / HP per the Mode param
            float outR;
            if (unisonOn) {
                // Right channel gets its own filter so the detune becomes a
                // true stereo image (plugin's filterR), sharing coefficients.
                v.filtR.setCutoff (v.cutoffSmoothed);
                srcR = Oscillator::wavefold (srcR, v.foldDriveSmoothed);
                outR = v.filtR.process (srcR, filtMode) * vgain;
            } else {
                outR = outL;
            }
            // Noise→tone morph: duck the note BODY so the oscillator emerges
            // from under the transient; then mix the transient (post-filter
            // unless routed through trFilt, which shares the voice cutoff so
            // the attack sits in the note's spectral space). Dynamics gates the
            // transient by the VCA envelope so it never exceeds the sustained
            // sound on breath instruments. Mono transient — added to both sides.
            float bodyL = outL, bodyR = outR;
            if (v.oscMorphArmed && v.oscMorphRamp < 1.0f) {
                bodyL *= v.oscMorphRamp; bodyR *= v.oscMorphRamp;
                v.oscMorphRamp += v.oscMorphInc;
                if (v.oscMorphRamp > 1.0f) v.oscMorphRamp = 1.0f;
            }
            if (transientOn) {
                float t = (s < transientN) ? gTrScratch[s] * v.tailLevel : 0.0f;
                if (resoOn) {
                    t = v.trReso.process (t, resoFb, resoDamp);
                    if (std::fabs (t) < 1.0e-4f) {
                        if (++v.trResoSilent > (int) (gSampleRate * 0.05)) v.trResoActive = false;
                    } else v.trResoSilent = 0;
                }
                if (transientRouteFilter) {
                    v.trFilt.setCutoff (v.cutoffSmoothed);
                    t = v.trFilt.process (t, filtMode);
                }
                t *= (1.0f - pTrDyn) + pTrDyn * v.vca;
                bodyL += t; bodyR += t;
            }
            renderBuf[s]  += bodyL;
            renderBufR[s] += bodyR;

            if (! v.active) break;
        }

        if (gMono && monoVoiceIdx >= 0 && &v == &voices[monoVoiceIdx]) {
            monoLastHz = v.currentHz;
            // Waveguide mode: the sounding amplitude is model-driven, so the
            // legato proxy considers the breath excitation too — a slur must be
            // recognized even when no Breath→VCA route exists (plugin parity).
            monoLastVCA = (gWgOn ? std::max (v.vca, wgIn.params.breath) : v.vca) * v.tailLevel;
        }
    }

    // Poly headroom: min(1, 2/N) over the sounding voices — 1-2 notes full, 3+
    // progressively quieter so the master limiter engages less (and its
    // chord-intermod doesn't appear). Smoothed per-sample (15 ms) so the level
    // doesn't step when a note is added/released. Mono counts as 1 (one sounding
    // voice), so it's never touched.
    int nActive = 0;
    for (auto& v : voices) if (v.active) ++nActive;
    const float polyTarget = nActive <= 2 ? 1.0f : std::sqrt (2.0f / (float) nActive);

    // Global vowel/formant stage (post-mix, after master gain, before the
    // standalone limiter) — mirrors PluginProcessor's post-mix formant. Params
    // once per block; process() per sample (pass-through when disabled → amount 0).
    // vOpen = base + the sounding voice's VowelPos mod (dest 12), so Breath→Vowel
    // gives the talkbox sweep.
    const float vOpen = clamp01 (pVowelPos + gVowelPosMod);
    for (auto* f : { &gFormantL, &gFormantR }) {
        f->setMode (gVowelMode ? FormantFilter::Mode::Wah : FormantFilter::Mode::Vowel);
        f->setParams (vOpen, pVowFront, pVowRound, gVowelEn ? pVowAmt : 0.0f, pVowBite, pVowMove);
    }

    for (int s = 0; s < n; ++s) {
        gPolyGain += (polyTarget - gPolyGain) * gPolyCoeff;
        float xl = renderBuf[s]  * pOutput * gPolyGain;
        float xr = renderBufR[s] * pOutput * gPolyGain;
        xl = gFormantL.process (xl);
        xr = gFormantR.process (xr);
        masterLimit (xl, xr);
        renderBuf[s] = xl; renderBufR[s] = xr;
    }
}

} // extern "C"
