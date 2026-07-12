# Experiment 2026-07-11 (third session) — Phase 6: Vane plugin integration

## Goal

Integrate the MiniSax engine into Vane as a synthesis mode (Implementation
Plan Phase 6), keeping the offline lab as the place the model evolves.

## What was built

- Vane gains a **Waveguide mode**: `waveguideOn` plus eight tone parameters
  (embouchure, reed stiffness/aperture, bore damping, bell brightness,
  conical amount, breath noise, growl).  When enabled, MiniSaxVoice replaces
  the wavetable oscillator inside SynthVoice; Vane's breath (the smoothed
  VCA signal, through a 0.18 floor mapping so pp stays above the reed's
  speaking threshold) blows the reed, Vane's glide/pitchbend machinery
  drives pitch per sample, and everything downstream (noise blend, fold,
  SVF, vowel, transients, VCA) is unchanged.  Vibrato is left to Vane's
  modulation; the engine's internal vibrato LFOs are disabled in-plugin.
- The engine is reset only on non-legato attacks; a legato note re-entrains
  the still-ringing bore at the new delay length.  RenderProbe's legato
  metric reads 0.24x (boundary/steady, 1 = threshold of a click).
- `MiniSax/Source/MiniSaxVoice.cpp` is compiled into all Vane targets;
  the engine stays JSON-free in the plugin path.
- RenderProbe gains `WAVEGUIDE=1` (pitch/H2/H3/RMS probe) and `WG=1` on the
  legato test.  On Linux the tool targets now build headless like the main
  plugin (raw JUCE_WEB_BROWSER=1 misses JUCE's WebKitGTK plumbing there).

## Findings (the probe earned its keep)

1. An apparent even-harmonic collapse at A4 (H2 -28 dB in-plugin) sent us
   down two wrong paths (amplitude-normalizing the conical tap; suspecting
   linear-interpolation losses) before the real cause surfaced: **both the
   in-plugin FFT probe and the lab sweep measured harmonics in fixed bins
   around the nominal pitch**, and the voice ran progressively sharp —
   +12 cents at 180 Hz to +33 cents at 500 Hz — so every harmonic bin
   missed by k times the detune.  Measured against the *sounded* pitch,
   H2 was +0.6..+4.6 dB across the range all along.
2. The sharpness itself was a real engine bug: a near-constant **0.9
   samples of missing loop delay**.  `loopDelayCompensation` 1.5 -> 0.6
   brings the sounded pitch within a few cents across the tenor range
   (C4 long tone now renders at 261.7 Hz), closing the "runs 15-25 cents
   sharp" problem noted since v0.1.
3. Reference match after the fix (C4 long tone vs SilvSnip):
   H2 -0.3 vs -0.2, H3 -7.8 vs -7.8, H5 -7.2 vs -7.8.

## Validation

- VaneSelfTest: 70031 assertions, 0 failures (includes ASCII string-safety
  on the new parameter display names).
- RenderProbe WAVEGUIDE=1 at D3/G3/D4/A4/D5: H2 within 0..+2 dB everywhere,
  RMS scales with breath (CC 40 -> 0.19, CC 110 -> 0.44 at G3).
- MiniSax lab: all unit tests pass; matrix re-render clean (no silence,
  no clipping); engine version 0.3.1.

## Problems / next steps

- The waveguide mode's parameters are not yet ModMatrix destinations
  (block-rate APVTS reads only) — Breath -> Growl or Slide -> Embouchure
  routes would be very playable.
- Mono legato reuses the per-voice bore only when the same voice slot is
  re-chosen; a cross-voice bore handoff (like the oscillator phase handoff)
  would make waveguide legato fully seamless.  Current metric is already
  under the click threshold.
- No UI section yet: the parameters are host-automatable but absent from
  the WebUI (music-suite repo).
