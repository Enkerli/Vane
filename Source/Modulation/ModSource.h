#pragma once

// Integer IDs for modulation sources and destinations.
//
// Layout:
//   0..3   — per-voice MPE dimensions (one value per active voice)
//   128..  — global CC values (CC + ccNumber, e.g. CC + 2 for breath)
//   256..  — abstract macro slots (Macro + n, n = 0..NumMacros-1)
//
// Routes should reference Macro IDs rather than raw CC numbers so that
// the concrete MIDI binding (which CC, or Aftertouch, or MPE dim) can be
// changed at runtime without rebuilding the route table.

struct ModSourceID {
    // ── Per-voice MPE dimensions ──────────────────────────────────────────────
    static constexpr int MPE_Pressure  = 0;   // channel pressure, 0..1
    static constexpr int MPE_Slide     = 1;   // CC74 / Y axis, 0..1
    static constexpr int MPE_Pitchbend = 2;   // signed, -1..1
    static constexpr int Velocity      = 3;   // note-on velocity, 0..1

    static constexpr int NumVoiceSources = 4;

    // ── Global CC values ──────────────────────────────────────────────────────
    // Index = CC + ccNumber, e.g. CC + 2 for breath controller
    static constexpr int CC = 128;            // base offset; 128..255

    // ── Abstract macro slots ──────────────────────────────────────────────────
    // Each macro maps to one concrete MIDI source (CC, Aftertouch, MPE dim).
    // The binding is stored in APVTS parameters so it is preset-saveable and
    // live-editable without touching route definitions.
    static constexpr int Macro         = 256; // base offset; 256..260
    static constexpr int MacroBreath   = 256; // typically CC2 or CC11 or Aftertouch
    static constexpr int MacroExpr     = 257; // typically CC11
    static constexpr int MacroPressure = 258; // typically MPE_Pressure (per-voice)
    static constexpr int MacroSlide    = 259; // typically MPE_Slide (per-voice)
    static constexpr int MacroPitchbend = 260;// typically MPE_Pitchbend (per-voice)

    static constexpr int NumMacros = 5;

    static constexpr int NumTotal  = 261;
};

struct ModDestID {
    static constexpr int FilterCutoff  = 0;   // additive offset to base cutoff
    static constexpr int FilterRes     = 1;   // additive offset to base resonance
    static constexpr int VCALevel      = 2;   // additive, clamped 0..1 in SynthVoice
    static constexpr int OscPitchFine  = 3;   // semitones, additive to pitchbend total

    // ── Oscillator modulation destinations ───────────────────────────────────────
    // OscWaveshape: additive offset to oscMorphPos (0..3).  Applied as
    //   activeMorphPos = clamp(morphPos + mods[OscWaveshape] × 3, 0, 3)
    //   so a ±1 mod sweeps the full Sine→Tri→Sqr→Saw spectrum.
    // OscPulseWidth: additive offset to oscPW (0..1, 0.5 = identity warp).
    //   activePW = clamp(basePW + mods[OscPulseWidth], 0, 1)
    static constexpr int OscWaveshape  = 4;   // morph position sweep
    static constexpr int GlideTime     = 5;   // intended: scale on base glide time (unwired)
    static constexpr int OscPulseWidth = 6;   // phase-distortion pulse width
    // OscNoiseMix: additive offset to noiseBlend (0..1).
    //   activeNoiseMix = clamp(noiseBlend + mods[OscNoiseMix], 0, 1)
    //   so a +1 route at amount 1.0 sweeps from off to full noise.
    static constexpr int OscNoiseMix   = 7;   // oscillator noise blend
    // OscFold: additive offset to foldAmt (0..1).
    //   activeFold = clamp(oscFold + mods[OscFold], 0, 1)
    //   Drives the pre-filter wavefolder; 0 = transparent, 1 = heavy folding.
    static constexpr int OscFold       = 8;   // wavefold depth
    // OscInharm: additive offset to inharmonicity (0..1).
    //   activeInharm = clamp(oscInharm + mods[OscInharm], 0, 1)
    //   Drives the FM-approximation inharmonicity (FM index); 0 = harmonic.
    static constexpr int OscInharm     = 9;   // inharmonicity / FM index

    static constexpr int NumDests = 10;
};

// ── Generic mod-slot model ──────────────────────────────────────────────────────
//
// The fixed-route table is being replaced by a pool of NumSlots identical slots,
// each configured at runtime through APVTS params: source (choice), dest (choice),
// amount (float -1..1) and curve (choice).  This namespace holds the single source
// of truth for the choice lists and the choice→internal-ID mappings, shared by the
// processor (param layout + defaults), the ModMatrix (evaluate) and the editor.
namespace ModSlots {
    static constexpr int NumSlots = 24;

    // Source choice list.  Index 0 = "Off" (slot inactive).  The rest map to the
    // abstract macro IDs (so the concrete CC/AT/MPE binding stays runtime-swappable)
    // except Velocity which is a direct voice source.
    inline const char* const kSourceNames[] = {
        "Off", "Breath", "Expression", "Pressure", "Slide", "Pitchbend", "Velocity"
    };
    static constexpr int NumSourceChoices = 7;

    // Destination choice list.  Maps to ModDestID values.
    inline const char* const kDestNames[] = {
        "VCA", "Cutoff", "Reso", "Pitch", "Morph", "PW", "Fold", "Noise", "Inharm"
    };
    static constexpr int NumDestChoices = 9;

    // Curve choice list (matches ModRoute::CurveShape integer order).
    inline const char* const kCurveNames[] = { "Lin", "Exp", "S" };
    static constexpr int NumCurveChoices = 3;

    // choice index → ModSourceID.  Returns -1 for "Off"/out-of-range.
    inline int sourceId(int choice) {
        switch (choice) {
            case 1: return ModSourceID::MacroBreath;
            case 2: return ModSourceID::MacroExpr;
            case 3: return ModSourceID::MacroPressure;
            case 4: return ModSourceID::MacroSlide;
            case 5: return ModSourceID::MacroPitchbend;
            case 6: return ModSourceID::Velocity;
            default: return -1;   // Off
        }
    }

    // choice index → ModDestID.  Returns -1 for out-of-range.
    inline int destId(int choice) {
        switch (choice) {
            case 0: return ModDestID::VCALevel;
            case 1: return ModDestID::FilterCutoff;
            case 2: return ModDestID::FilterRes;
            case 3: return ModDestID::OscPitchFine;
            case 4: return ModDestID::OscWaveshape;
            case 5: return ModDestID::OscPulseWidth;
            case 6: return ModDestID::OscFold;
            case 7: return ModDestID::OscNoiseMix;
            case 8: return ModDestID::OscInharm;
            default: return -1;
        }
    }

    // Slew attack/release (ms) derived from the SOURCE's physical behaviour rather
    // than stored per slot: breath/expression/pressure are smooth and slow; slide
    // and pitchbend track fast; velocity is effectively instantaneous.  This keeps
    // the per-slot param count down while matching the old fixed-route feel.
    // (A future revision can expose per-slot atk/rel once the WebView UI is ready.)
    inline void slewRates(int sourceChoice, float& atkMs, float& relMs) {
        switch (sourceChoice) {
            case 1: case 2: atkMs = 5.0f; relMs = 80.0f; break;  // Breath / Expression
            case 3:         atkMs = 3.0f; relMs = 50.0f; break;  // Pressure
            case 4: case 5: atkMs = 2.0f; relMs = 20.0f; break;  // Slide / Pitchbend
            case 6:         atkMs = 20.0f; relMs = 0.0f; break;  // Velocity
            default:        atkMs = 5.0f; relMs = 30.0f; break;
        }
    }
}
