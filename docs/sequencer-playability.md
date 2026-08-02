# Vane from a sequencer — the waveguide is silent, and why

*Measured 2026-08-02. From the suite plan's track E: "Vane probably does accept
performance from controllers which don't produce breath or MPE controls; it
might make sense to make that more prominent, as using Vane from sequencers
wouldn't require other messaging."*

It does not, currently. **The MiniSax waveguide produces nothing at all from a
plain note.**

## The measurement

`msuite render`, driving the committed `vane-dsp.wasm` — the same engine the
plugin compiles:

```
  waveguide on, breath 0 (a sequencer's plain note)   peak 0.00000   rms 0.00000
  waveguide on, breath 0.7                           peak 0.99997   rms 0.26574
```

Not quiet. Silent.

## Why

`SynthVoice.cpp` feeds the reed from whichever expression source is loudest:

```cpp
wgBreath = clamp01(std::max({ breathM, exprM, pressure,
                              veloMix * std::sqrt(velocity) }));
```

Three of those four need a controller that Serpe, `msuite play`, a DAW piano
roll and most keyboards do not send. The fourth is the keyboard fallback — and
`velocityMix` ("Velocity to VCA") **defaults to 0.0**
(`PluginProcessor.cpp:1079`). So every term is zero, breath is zero, and the
reed never speaks.

This is not a bug in the model. A reed with no breath makes no sound, and the
absence of a floor is deliberate — it is what gives the subtone and
rearticulation the PhysMod doctrine was written around. The problem is that a
whole class of input silently produces nothing, with no signal saying why.

The suite's own rule for a comparable case: *absence is expected, a dead-end is
a bug* (the WebMIDI-in-Safari rule, TESTING_NOTES §2).

## Three ways out — this is a product call

**A. Raise the `velocityMix` default above 0.**
One line, and a fresh instance responds to plain notes.
*Cost:* `velocityMix` also scales the ordinary VCA (`SynthVoice.cpp:697`), so
this changes the dynamics of **every** patch on new instances, not only the
waveguide. Saved sessions keep their stored value, so nothing existing moves.

**B. Fall back to velocity inside the waveguide branch only.**
Leaves the wavetable path untouched. *Cost:* it has to distinguish "no
controller is present" from "breath is deliberately down", and those look
identical at this point in the signal chain. Getting it wrong removes the
subtone on purpose-built patches.

**C. Say something.** Leave the audio alone; when the waveguide is on and no
breath source has been seen for N seconds, surface it in the UI — one line, not
a modal. *Cost:* does not make Vane sequencer-playable by itself, but it turns
an undiagnosable silence into a fixable one, and it composes with A or B.

**Recommendation: C plus A.** C is unambiguously right — the current behaviour
is unexplainable from inside the app, and that is worth fixing on its own terms.
A is the smaller of the two audio changes and only affects new instances, but it
does change the instrument's out-of-box dynamics, which is Alex's call and not
mine.

## Also worth knowing

The nine `MiniSax/presets/*.json` carry **waveguide model parameters only** —
none of them touches `velocityMix`, which lives in the plugin's APVTS. So "ship
a sequencer-friendly factory preset" does not work as stated: the preset format
cannot express the parameter that matters. Fixing that would mean widening the
preset schema, which is a larger change than any of A/B/C.
