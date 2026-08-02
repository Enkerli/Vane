# Vane from a sequencer — the waveguide was silent, and why

*Measured 2026-08-02. From the suite plan's track E: "Vane probably does accept
performance from controllers which don't produce breath or MPE controls; it
might make sense to make that more prominent, as using Vane from sequencers
wouldn't require other messaging."*

It did not. **The MiniSax waveguide produced nothing at all from a plain note.**
Fixed 2026-08-02 with a synthetic breath envelope — the decision and the
measurements are below; the rest of this page is kept as the record of why.

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

### Decided 2026-08-02: a synthetic breath ENVELOPE, not a flat floor

Alex: *"apply the equivalent of an ADSR envelope (or at least an AD one) when
the incoming MIDI messages only send notes with velocity."*

That is a better answer than any of A/B/C, and it dissolves the objection to B.
A flat velocity floor could not tell "no controller" from "breath deliberately
down" — but an envelope is not a floor. It has a shape, a beginning and an end,
so it behaves like a source rather than a bias, and a patch built around real
breath is unaffected because the envelope only ever contributes through the
same `max()` the other sources already go through.

`Source/Synth/BreathEnvelope.h` — deliberately NOT a generic ADSR:

- **Attack is slow (35 ms default).** A reed does not snap; an instant attack is
  what makes synthetic wind sound synthetic.
- **Velocity scales the peak AND shortens the attack.** Blowing harder is both
  louder and sooner — one gesture, two consequences.
- **Legato does not retrigger.** The melisma case: several notes inside one
  breath. A legato note re-aims the target and leaves the level alone, so a
  slurred line keeps its shape. It mirrors what the voice already does with the
  bore and the VCA across a mono legato transition, and it is why the class
  takes `legato` rather than inferring it.

Ten checks in `BreathEnvelope.test.cpp` (standalone, no CMake target), including
both halves of the melisma contrast: a slur holds its level, a re-articulation
drops below the sustain it was holding.

**Toward qurves.** Alex wants these shapes recorded as DrawnQurve "qurves"
eventually. `levelFor` is written as a pure function of a phase and the segment
times precisely so a curve lookup can replace the ramp arithmetic without moving
anything else around it.

### Wired 2026-08-02 — and measured

Both engines. `SynthVoice` (plugin) and `vane-dsp.cpp` (the webapp's wasm voice)
each carry the envelope, and both include the SAME `BreathEnvelope.h` rather
than a second copy — this file already reimplements enough of the voice glue,
and INTENT L5 has four incidents on the board.

Five parameters after `waveguideOn`: mode (Off / **Auto** / Always), attack,
decay, sustain, release. Wasm ids 55–59. Auto is the default.

**The same measurement that found the silence, re-run:**

```
  Off      peak 0.00000   rms 0.00000     ← the old behaviour, still reachable
  Auto     peak 1.72275   rms 0.43746
  Always   peak 1.72409   rms 0.43710
```

For scale, a real breath controller at CC2=110 through the existing probe reads
`rms 0.4359` — so the envelope lands where a player would, not hotter. (Peaks
above 1.0 are the waveguide's existing gain staging; real breath peaks 1.99 the
same way. Not introduced here, but worth a look sometime.)

**Auto yields rather than stacks.** With a real breath CC present, Off / Auto /
Always all render `rms 0.4403` — identical to within noise. The test is the
LATCH, not the current value: a controller resting at zero is a player choosing
silence, and the reed's own speaking threshold should give it. Breath (CC2) and
expression (CC11) set the latch; CC74 deliberately does not, since it streams
from any MPE keyboard whose player may have no wind controller at all.

**Melisma works across the voice handoff.** Mono legato allocates a *fresh*
voice in the plugin, so a per-voice envelope would start Idle and re-attack on
every slur. `noteOn` takes a `resumeLevel` — the same shared value the bore and
the VCA hand off on — and the voice passes `initVCA`. That value already
included `wgBreath` in waveguide mode (`SynthVoice.cpp:1211`), so the synthetic
breath becomes its own legato proxy with no extra plumbing.

```
  slurred          trough 102% / 120% of the level held
  detached (gap)   trough   5% /   3%
```

**Verification.** 12 checks in `BreathEnvelope.test.cpp`; 6 in the wasm
regression suite (60 total); `SYNTHBREATH=1 [MODE=0|1|2] [REAL=1] [MELISMA=1
[DETACHED=1]]` in `VaneRenderProbe`; `VaneSelfTest` 70289 pass.

Both melisma checks ship with a **detached negative control**, because the first
version of each did not bite: the probe released the note in the same block as
the next note-on, and the wasm one used an 80 ms gap — inside the 100 ms mono
legato-hold bridge. Both dutifully reported no difference between a slur and a
re-articulation. A melisma check that cannot see a re-attack is measuring the
ringing bore, not the envelope.

**UI.** In the Waveguide group, under the reed controls, shown only when the
waveguide is on — a three-way Off/Auto/Always plus the four times. Off collapses
the sliders. One shared `index.html` drives the webapp and the plugin WebUI, so
the plugin needed five relay entries in `WebVaneEditor.cpp`; values cross that
bridge in real units.

---

## The original three options, kept as the record

**Recommendation was: C plus A.** C is unambiguously right — the current behaviour
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
