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

    // ── Reserved — declared but not yet wired into SynthVoice::renderNextBlock ──
    // Adding a new destination requires:
    //   1. Reading mods[OscWaveshape] / mods[GlideTime] in renderNextBlock.
    //   2. Adding a route in VaneProcessor's constructor.
    //   3. Adding an APVTS parameter for the amount (optional but expected).
    static constexpr int OscWaveshape  = 4;   // intended: wavetable position / morph
    static constexpr int GlideTime     = 5;   // intended: scale on base glide time

    static constexpr int NumDests = 6;
};
