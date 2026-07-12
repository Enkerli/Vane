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
    static constexpr int Keytrack      = 4;   // note pitch, bipolar around C4 (±4 oct → ±1)

    static constexpr int NumVoiceSources = 5;

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
    // OscSync: additive offset to the wavetable hard-sync / transpose ratio.
    //   activeSync = clamp(oscSync + mods[OscSync]·range, 1, kSyncMax)
    //   1 = off; higher = formant peak swept up (inharmonicity via transposition).
    static constexpr int OscSync       = 10;  // wavetable sync / transpose ratio

    // TransientLevel: additive offset to the transient gain (0..1).
    //   finalGain = clamp(transientGain + mods[TransientLevel], 0, 2)
    //   Useful for velocity → transient loudness or breath → attack emphasis.
    static constexpr int TransientLevel = 11; // transient sample gain

    // UnisonDetune: additive offset (in normalised units) to the unison detune
    //   spread.  activeDetune = clamp(unisonDetune + mods[UnisonDetune]·50c, 0, 50)
    static constexpr int UnisonDetune  = 12;  // unison detune spread

    // VowelPos: additive offset to the formant filter's vowel position (0..1).
    //   activeVowel = clamp(vowelPos + mods[VowelPos], 0, 1)
    //   Breath → VowelPos gives wind-driven talkbox vowel sweeps.
    static constexpr int VowelPos      = 13;  // formant/vowel position

    // ── Waveguide (MiniSax) tone destinations ────────────────────────────────────
    // All additive, clamped 0..1 against the APVTS base value in SynthVoice —
    // same pattern as the Osc* destinations above. Lets breath/pressure/etc.
    // drive the reed physically (e.g. Breath → Bell Brightness for a wind
    // instrument's natural loud-brighter coupling), matching how a physical
    // model is normally played rather than leaving the tone static.
    static constexpr int WgEmbouchure   = 14;
    static constexpr int WgReedStiff    = 15;
    static constexpr int WgReedAperture = 16;
    static constexpr int WgBoreDamping  = 17;
    static constexpr int WgBellBright   = 18;
    static constexpr int WgConical      = 19;
    static constexpr int WgBreathNoise  = 20;
    static constexpr int WgGrowl        = 21;

    static constexpr int NumDests = 22;
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

    // ── Configurable global "aux" sources (controller-profile foundation) ──────
    // Beyond the fixed per-note MPE set + breath/expr, NumAux global CC sources let
    // a profile expose extra controls — encoders (Exquis), accelerometer CCs
    // (Sylphyo/Zefiro), a second breath, etc.  Each is bound to a CC# at runtime
    // (aux{g}_cc param); the ModMatrix resolves it as a global CC source.
    // Combining controllers = one profile listing both devices' sources.
    static constexpr int NumAux         = 8;
    static constexpr int FirstAuxChoice = 7;   // source choices 7 .. 7+NumAux-1

    // Source choice list.  Index 0 = "Off" (slot inactive).  1..6 = the fixed
    // per-note MPE set + breath/expr; 7.. = configurable global aux sources.
    // IMPORTANT: only ever APPEND — APVTS stores the actual index, so appending
    // keeps existing presets' slot sources valid (no migration).
    // Keytrack (note pitch) appended after the aux block as a per-note source.
    static constexpr int KeytrackChoice = FirstAuxChoice + NumAux;   // = 15
    inline const char* const kSourceNames[] = {
        "Off", "Breath", "Expression", "Pressure", "Slide", "Pitchbend", "Velocity",
        "Aux 1", "Aux 2", "Aux 3", "Aux 4", "Aux 5", "Aux 6", "Aux 7", "Aux 8",
        "Keytrack"
    };
    static constexpr int NumSourceChoices = 7 + NumAux + 1;

    // Destination choice list.  Maps to ModDestID values.
    // IMPORTANT: only ever APPEND — APVTS stores the choice index, so inserting
    // would shift existing presets' destination assignments.
    inline const char* const kDestNames[] = {
        "VCA", "Cutoff", "Reso", "Pitch", "Morph", "PW", "Fold", "Noise", "Inharm", "Sync",
        "Transient", "Uni Detune", "Vowel",
        "Wg Embouchure", "Wg Reed Stiff", "Wg Aperture", "Wg Damping", "Wg Bell",
        "Wg Conical", "Wg Breath Noise", "Wg Growl"
    };
    static constexpr int NumDestChoices = 21;

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
            case KeytrackChoice: return ModSourceID::Keytrack;
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
            case 9: return ModDestID::OscSync;
            case 10: return ModDestID::TransientLevel;
            case 11: return ModDestID::UnisonDetune;
            case 12: return ModDestID::VowelPos;
            case 13: return ModDestID::WgEmbouchure;
            case 14: return ModDestID::WgReedStiff;
            case 15: return ModDestID::WgReedAperture;
            case 16: return ModDestID::WgBoreDamping;
            case 17: return ModDestID::WgBellBright;
            case 18: return ModDestID::WgConical;
            case 19: return ModDestID::WgBreathNoise;
            case 20: return ModDestID::WgGrowl;
            default: return -1;
        }
    }

    // Slew attack/release (ms) derived from the SOURCE's physical behaviour rather
    // than stored per slot: breath/expression/pressure are smooth and slow; slide
    // and pitchbend track fast; velocity is effectively instantaneous.  This keeps
    // the per-slot param count down while matching the old fixed-route feel.
    // (A future revision can expose per-slot atk/rel once the WebView UI is ready.)
    inline void slewRates(int sourceChoice, float& atkMs, float& relMs) {
        if (sourceChoice == KeytrackChoice) { atkMs = 0.0f; relMs = 0.0f; return; }    // per-note constant
        if (sourceChoice >= FirstAuxChoice) { atkMs = 5.0f; relMs = 80.0f; return; }  // aux: smooth global
        switch (sourceChoice) {
            case 1: case 2: atkMs = 5.0f; relMs = 80.0f; break;  // Breath / Expression
            case 3:         atkMs = 3.0f; relMs = 50.0f; break;  // Pressure
            case 4: case 5: atkMs = 2.0f; relMs = 20.0f; break;  // Slide / Pitchbend
            case 6:         atkMs = 20.0f; relMs = 0.0f; break;  // Velocity
            default:        atkMs = 5.0f; relMs = 30.0f; break;
        }
    }
}
