#pragma once
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

    int        source    = 0;
    int        dest      = 0;
    float      amount    = 0.0f;   // -1..1; positive = add, negative = subtract
    CurveShape curve     = CurveShape::Linear;
    float      attackMs  = 5.0f;   // stored here so voices can clone matching slewers
    float      releaseMs = 30.0f;
    Slewer     slewer;   // used for CC sources; voice-source routes use per-voice slewers
};

// Connects modulation sources (CC, MPE dimensions) to synthesis destinations.
// CC values are written per MIDI message; per-voice MPE values are passed
// at evaluate() time so each voice gets its own mod result.
//
// Thread model: setCCValue is called from the audio thread (processBlock).
//               addRoute / clearRoutes should only be called while audio is stopped
//               or behind a lock. This is intentionally simple for now.
class ModMatrix {
public:
    // blockSize: samples per audio block — needed so Slewer coefficients
    // are correct for the actual call rate of evaluate().
    void prepare(double sampleRate, int blockSize);

    // Called from audio thread for each CC message
    void  setCCValue(int ccNumber, float zeroToOne);
    float getCCValue(int ccNumber) const;

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
                  ModRoute::CurveShape curve = ModRoute::CurveShape::Linear);
    void clearRoutes();
    int  routeCount() const { return static_cast<int>(routes.size()); }

private:
    float getSourceValue(int sourceID,
                         const std::array<float, ModSourceID::NumVoiceSources>& voiceVals) const;

    std::array<float, 128> ccValues {};   // CC 0..127, normalised 0..1
    std::vector<ModRoute>  routes;
    double sampleRate = 44100.0;
    int    blockSize  = 512;              // updated in prepare(); used by addRoute()
};
