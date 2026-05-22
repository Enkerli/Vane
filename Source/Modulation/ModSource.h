#pragma once

// Integer IDs for modulation sources and destinations.
// Kept as plain ints (not enum class) so CC numbers map directly:
//   source = ModSourceID::CC + ccNumber   (e.g. CC + 2 = breath)

struct ModSourceID {
    // Per-voice MPE dimensions — each SynthVoice holds these live values
    static constexpr int MPE_Pressure  = 0;   // channel pressure, 0..1
    static constexpr int MPE_Slide     = 1;   // CC74 / Y axis, 0..1
    static constexpr int MPE_Pitchbend = 2;   // signed, -1..1
    static constexpr int Velocity      = 3;   // note-on velocity, 0..1

    static constexpr int NumVoiceSources = 4;

    // Global CC values from the MIDI master channel (or non-MPE MIDI)
    // Index = CC + ccNumber, e.g. CC + 2 for breath controller
    static constexpr int CC = 128;            // base offset; 128..255

    static constexpr int NumTotal = 256;
};

struct ModDestID {
    static constexpr int FilterCutoff  = 0;   // additive offset to base cutoff
    static constexpr int FilterRes     = 1;   // additive offset to base resonance
    static constexpr int VCALevel      = 2;   // multiplicative; 0 = silence
    static constexpr int OscPitchFine  = 3;   // in semitones, additive
    static constexpr int OscWaveshape  = 4;   // reserved for WT position / shape
    static constexpr int GlideTime     = 5;   // multiplicative scale on base glide

    static constexpr int NumDests = 6;
};
