#pragma once
#include <algorithm>
#include <array>
#include <vector>
#include "ModSource.h"
#include "Slewer.h"

struct ModRoute {
    // Per-route curve shaping applied after slewing and before amount scaling.
    // Linear   — raw slewed value (default, no change).
    // Exponential — x*|x|: squares the signal while preserving sign.  Gives more
    //              resolution at low source values and a steeper response near full.
    //              Natural for unipolar sources (breath, pressure, velocity).
    // SCurve   — smoothstep on |x|, sign preserved: slow start, fast middle, slow end.
    //              Useful where you want a "committed" feel at the extremes of a sweep.
    enum class CurveShape : uint8_t { Linear = 0, Exponential, SCurve };

    // ── Editable response curve (Bezier mod-curves) ───────────────────────────
    // A per-route lookup table for the unipolar response x in [0,1] -> y in [0,1],
    // built from 1..3 draggable anchors via a monotone cubic spline (see
    // ModMatrix::buildCurveLUT).  When curveLUTactive, evaluate() uses this instead
    // of the discrete CurveShape enum.  Bipolar sources index by |x| and restore
    // the sign (odd symmetry), so the curve always passes through the origin.
    static constexpr int kCurveLUT = 65;
    std::array<float, kCurveLUT> curveLUT {};   // filled by buildCurveLUT
    bool       curveLUTactive = false;

    int        source    = 0;
    int        dest      = 0;
    float      amount    = 0.0f;   // -1..1; fallback when amountParam is null
    CurveShape curve     = CurveShape::Linear;
    float      attackMs  = 5.0f;   // stored here so voices can clone matching slewers
    float      releaseMs = 30.0f;
    Slewer     slewer;             // used for CC sources; voice-source routes use per-voice slewers

    // Optional live parameter pointer.  When non-null, its value is used as the
    // route amount at evaluate() time instead of the hardcoded `amount` field.
    // Points into the APVTS — the processor owns the lifetime, so the pointer
    // is always valid while audio is running.
    std::atomic<float>* amountParam = nullptr;

    // Optional live curve override.  When non-null, evaluate() reads an int from
    // this param (0=lin, 1=exp, 2=S) instead of the static `curve` enum.
    // Allows the ModMatrix UI to change curve shape without rebuilding routes.
    std::atomic<float>* curveParam  = nullptr;

    // ── Generic-slot params ───────────────────────────────────────────────────
    // When non-null these make the route a configurable "slot": evaluate() reads
    // the live source/destination CHOICE from these params each block (mapped via
    // ModSlots::sourceId / destId) instead of using the fixed `source`/`dest`.
    // sourceChoice 0 ("Off") disables the slot.  Slew rates are derived from the
    // live source via ModSlots::slewRates.
    std::atomic<float>* sourceParam = nullptr;
    std::atomic<float>* destParam   = nullptr;

    // Mod-of-mod: when set (choice > 0), this route's amount is multiplied by the
    // value of another source (the same source-choice list).  e.g. Keytrack scaling
    // a Breath→Cutoff route so low notes get less breath-to-cutoff.  Unipolar
    // sources scale 0..1; bipolar sources (slide/pitchbend/keytrack) map to 0..1.
    std::atomic<float>* scaleParam  = nullptr;

    // Per-slot enable.  When non-null and < 0.5 the route is inactive (effSource
    // returns -1, so both evaluate() and resetVoiceSlewers() skip it) WITHOUT
    // clearing its source/dest/amount — the UI toggle disables a route while
    // preserving its settings for re-enabling.  Null (fixed routes) => always on.
    std::atomic<float>* enableParam = nullptr;
};

// Connects modulation sources (CC, MPE dimensions) to synthesis destinations.
// CC values are written per MIDI message; per-voice MPE values are passed
// at evaluate() time so each voice gets its own mod result.
//
// ── Thread model ──────────────────────────────────────────────────────────────
// Audio thread (safe): setCCValue, setAftertouch, setMacroSlot, evaluate,
//                      resetVoiceSlewers.
// Stopped only:        addRoute, clearRoutes.  These modify `routes` (a vector)
//                      which could reallocate — calling them during playback is
//                      a data race.  A future improvement would be a lock-free
//                      route-swap using a double-buffer or atomic pointer.
//
// ── Macro slot design ─────────────────────────────────────────────────────────
// Macros decouple the route table from concrete MIDI bindings.  A route targets
// MacroBreath (256) rather than CC2 directly; processBlock resolves which
// physical signal feeds MacroBreath at runtime via setMacroSlot().  This lets
// the user switch from CC2 to Aftertouch or MPE Pressure without touching routes.
//
// The `voiceBacking` field in macroVoiceBacking[] is the key:
//   >= 0  — evaluate() reads from voiceVals[voiceBacking] per-voice (MPE dims)
//   -1    — evaluate() reads from macroValues[macroIdx] (pre-resolved global)
// CC and Aftertouch macros are resolved to a scalar in processBlock, stored in
// macroValues[], and read once (globally) during evaluate().  Per-voice macros
// (pressure, slide, pitchbend) are resolved to voiceVals index so each voice
// sees its own value.
//
// ── Slewer selection ──────────────────────────────────────────────────────────
// Voice-source routes use per-voice Slewers (voiceSlewers[i]) so two simultaneous
// notes with different slide values don't bleed into each other through the shared
// route.slewer.  CC/Aftertouch routes use route.slewer (shared) because those
// values are global anyway.  isMacroVoice decides which pool to use based on
// the current backing of each macro.
class ModMatrix {
public:
    // blockSize: samples per audio block — needed so Slewer coefficients
    // are correct for the actual call rate of evaluate().
    void prepare(double sampleRate, int blockSize);

    // Called from audio thread for each CC message
    void  setCCValue(int ccNumber, float zeroToOne);
    float getCCValue(int ccNumber) const;

    // Global (non-MPE) channel pressure / Aftertouch
    void  setAftertouch(float zeroToOne) { aftertouchValue = std::clamp(zeroToOne, 0.0f, 1.0f); }
    float getAftertouch() const { return aftertouchValue; }

    // ── Macro slots ───────────────────────────────────────────────────────────
    // Called from processBlock once per block, before evaluate().
    // value        : resolved scalar for CC/Aftertouch-backed macros (ignored
    //                when voiceBacking >= 0, since the value comes from voiceVals).
    // voiceBacking : index into the voiceVals array passed to evaluate(), or -1
    //                to use `value` directly (CC/Aftertouch source).
    void  setMacroSlot(int macroIdx, float value, int voiceBacking);
    float getMacroValue(int macroIdx) const;  // last-set value (CC/AT macros)

    // Bind a configurable global "aux" source (0..NumAux-1) to a CC number.
    // Set each block by the processor from the aux{g}_cc params; the matrix then
    // resolves slot source choices 7.. as global CC sources.  See ModSlots.
    void  setAuxCC(int auxIdx, int ccNumber);

    // Evaluate all routes for one voice.
    // voiceVals:   [MPE_Pressure, MPE_Slide, MPE_Pitchbend, Velocity] — per-voice MPE state.
    // voiceSlewers: per-route slewers owned by this voice (see initVoiceSlewers).
    //              Voice-source routes use voiceSlewers to prevent cross-voice contamination;
    //              CC routes use the shared route slewer (CC values are global).
    std::array<float, ModDestID::NumDests>
    evaluate(const std::array<float, ModSourceID::NumVoiceSources>& voiceVals,
             std::vector<Slewer>& voiceSlewers);

    // Populate voiceSlewers with one Slewer per route, configured to match the
    // route's attack/release rates.  Call from SynthVoice::prepare() so each
    // voice gets its own independent slewer state for per-voice MPE sources.
    void initVoiceSlewers(std::vector<Slewer>& out, double sr, int blockSize) const;

    // Snap each per-voice slewer that handles a voice source to the corresponding
    // value in voiceVals.  Call at note-on for non-legato attacks to prevent stale
    // slewer state (from a previous note on the same voice slot) from producing a
    // brief filter-position mismatch at the start of each new note.
    // CC-source slewers are left untouched — they live in the shared route.
    void resetVoiceSlewers(std::vector<Slewer>& voiceSlewers,
                           const std::array<float, ModSourceID::NumVoiceSources>& voiceVals) const;

    void addRoute(int source, int dest, float amount,
                  float attackMs = 5.0f, float releaseMs = 30.0f,
                  ModRoute::CurveShape curve = ModRoute::CurveShape::Linear,
                  std::atomic<float>* amountParam = nullptr,
                  std::atomic<float>* curveParam  = nullptr);

    // Add a generic configurable slot.  All four params are APVTS raw-value
    // pointers: source choice, dest choice, amount (-1..1), curve choice.
    // Stopped-only (mutates the routes vector) — call before audio starts.
    void addSlot(std::atomic<float>* sourceParam, std::atomic<float>* destParam,
                 std::atomic<float>* amountParam, std::atomic<float>* curveParam,
                 std::atomic<float>* scaleParam  = nullptr,
                 std::atomic<float>* enableParam = nullptr);

    void clearRoutes();
    int  routeCount() const { return static_cast<int>(routes.size()); }

    // ── Editable response curves ──────────────────────────────────────────────
    // Build a monotone cubic-spline LUT from 1..3 interior anchors (x,y in (0,1),
    // endpoints (0,0)/(1,1) implied) into the given route, and activate it.  An
    // empty anchor list deactivates the LUT (falls back to the CurveShape enum).
    // Called on the message thread (slot edit / state load); the audio thread
    // reads the LUT lock-free (a torn read during an edit is harmless for a curve).
    void setRouteCurve(int routeIndex, const std::vector<std::pair<float, float>>& anchors);

    // Fill a 65-point LUT from anchors (static helper, also used by tests).
    static void buildCurveLUT(const std::vector<std::pair<float, float>>& anchors,
                              std::array<float, ModRoute::kCurveLUT>& out);

private:
    float getSourceValue(int sourceID,
                         const std::array<float, ModSourceID::NumVoiceSources>& voiceVals) const;

    // Resolve a route's live source ID (slot-aware: handles Off, per-note dims,
    // and configurable aux global CC sources).  -1 = inactive.
    int effSource(const ModRoute& r) const;
    int resolveChoice(int choice) const;            // source-choice index → ModSourceID
    static bool isBipolarSource(int sourceID);      // −1..1 sources (for scale-source 0..1 mapping)

    std::array<float, 128> ccValues {};   // CC 0..127, normalised 0..1
    float aftertouchValue = 0.0f;         // MIDI channel pressure (global, not MPE)
    std::array<int, ModSlots::NumAux> auxCC {};   // aux source g → CC number

    // Macro slot state — written each block by setMacroSlot(), read in evaluate().
    float macroValues   [ModSourceID::NumMacros] {};
    int   macroVoiceBacking[ModSourceID::NumMacros] = {
        -1,                       // MacroBreath   — CC-backed by default
        -1,                       // MacroExpr     — CC-backed by default
        ModSourceID::MPE_Pressure,// MacroPressure — per-voice
        ModSourceID::MPE_Slide,   // MacroSlide    — per-voice
        ModSourceID::MPE_Pitchbend// MacroPitchbend — per-voice
    };

    std::vector<ModRoute>  routes;
    double sampleRate = 44100.0;
    int    blockSize  = 512;              // updated in prepare(); used by addRoute()
};
