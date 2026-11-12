# WindCore: a reed, cone and mouth model with character controls

WindCore is a physical model of a single-reed woodwind with a conical bore, written from the published
acoustics literature, with controls for the character of the sound. In Vane it is the second choice of
**Waveguide Model**, next to MiniSax.

It is a work in progress. It plays in tune across a wide range, responds to breath, and changes character
in ways you can hear, but it does not yet sound like a saxophone: listeners so far describe it as a
generic woodwind. What is still missing is an open question (see the end).

## The model
One header, `WindCore.h`, with no dependencies. Its parts:

| Part | Model | Source |
|---|---|---|
| Reed | One damped mode; Bernoulli flow through the reed channel, solved exactly each sample with the bore and mouth | Wilson and Beavers 1974; Guillemain, Kergomard and Voinier 2005 |
| Bore | A cone with its tip cut off: a lossy cylinder in parallel with the inertance of the missing tip, plus the mouthpiece chamber | Dalmont, Gilbert and Kergomard 2000; Kergomard et al. 2019 |
| Tone holes | Above a cutoff, sound leaves the bore instead of reflecting; the output is the part not reflected | Benade 1976 |
| Vocal tract | Resonances in series with the bore, upstream of the reed | Scavone 2003; Chen, Smith and Wolfe 2011 |
| Keys | A note change crossfades between two bore lengths, like a hole opening over a few milliseconds | |
| Embouchure | Breath opens the embouchure, so loudness is continuous from nothing | |
| Body | Peaks and dips fixed in Hz after the horn, sliding with the horn's size across the range | |
| Mouth (as heard) | Two vowel resonances fixed in Hz on the radiated sound | Li, Chen, Smith and Wolfe 2015 |
| Life | Slow random movement of breath, embouchure and pitch; a vibrato that grows in; a breath dip at slurs | |
| Onset | A burst of turbulent air at a tongued start (and a smaller one at a slur), from reed (dark, mostly through the horn) to air (a bright hiss heard directly), then a bloom: the sound opens from dark to full over about 90 ms | Scavone 1996; the collected saxophones' attacks are no brighter than their settled notes |
| Breath noise | Turbulence on the flow, stronger low down and cleaner up high (0.45 dB less per semitone), as measured on the instruments the horn was fitted to | |

There are no separate instruments or zones. The horn's proportions follow the note along smooth curves
(`curves.txt`): one continuous instrument from the bottom of the range to the top.

## How it was tuned
- **The horn's curves** were fitted, note by note at three dynamics, to the median harmonic profile of about
  twenty saxophone instruments (sampled and modelled), measured from recordings of them all playing the same
  phrases. How the profile changes from soft to loud comes from the modelled instruments only (sampled ones change
  colour little with dynamics), and the fit requires the tone to brighten with breath as theirs does: in the tenor
  range WindCore now brightens about 1.25 times per doubling of breath, where it was 1.12 (the modelled
  saxophones measure 1.3 to 1.5). Opening the reed more at loud breath, or less at soft breath, broke notes beyond
  that, so the brightness range is built into the horn's curves rather than added on top. The lowest four harmonics count three times, every note must speak at very soft breath, and pitch
  must move smoothly with breath (`tools/fit.py`, `tools/curves.py`). The target itself is not included.
- **The Body pattern** is the shape those instruments share, measured the same way: a hump near 450 Hz, a dip
  near 800 Hz, humps from 1.6 to 3 kHz, less above 5 kHz.
- **Tuning and level** are calibrated per note over three breath levels (`tools/calibrate.py`, `cal.txt`).
- **Moving a character control** detunes the horn; per-note corrections for each control's two ends are in
  `trims.json` (`tools/trims.py`).

## Controls
Defined in `character.json`: label, the name of each end, the model setting it moves, its range, a plain
description and its source. Vane's parameters are `windMouth`, `windMouthDepth` and so on (one per control), each 0 to 1. Playing and patching: [docs/windcore.md](../docs/windcore.md).

| Group | Controls |
|---|---|
| Player | Mouth shape, Mouth strength, Embouchure, Air |
| Mouthpiece and reed | Reed, Chamber, Baffle |
| Horn | Cone, Tone holes, Bell, Body, Body size |
| Fingers and tongue | Fingers, Tongue, Chiff, Chiff colour, Bloom, Key noise |
| Life | Wobble, Shimmer, Pitch drift, Vibrato, Vibrato rate, Slur |

## What is known, and what is not
- **Measured:** every note from F#1 to Eb7 speaks in its first register at ten breath levels from very soft to
  full, in tune to within 6 cents for 95% of them (with breath noise and the onset burst on). Slurs and tongued re-attacks into the top octave land on the
  right note (488 cases tested, `tools/top_regimes.py`). All notes of the test phrases are at pitch, with no
  level spikes at note changes. With the character controls set at random over 60% of their ranges, tuning
  stays within about 12 cents for 95% of notes.
- **Low octave:** from F#1 to E2 and around G2, how a note starts (with or without the onset burst, fast or slow
  breath) can move its pitch by up to about 20 cents: two ways the reed can settle. Tuning is calibrated on the
  default start (`calibrate_settings.json`).
- **Range:** notes above Eb7 are silent. Slurring down from them could leave the top octave a semitone or two
  flat (a second way the horn can play there). For the same reason the reed's resonance sits at 2.25 times the
  note in the top octave; from 2.5 times up, that second regime appears.
- **Heard:** a generic woodwind, more malleable than MiniSax but in the same category. The Life controls can
  sound like trying too hard. Played with an attack transient and a convolution reverb, it comes closer to an
  acoustic instrument, with something cello-like at the bottom and oboe-like at the top.
- **Not found yet:** what makes a model sound like a saxophone rather than a woodwind. Matching steady
  harmonic levels to within a few dB was not enough. Candidates are the attack (how harmonics arrive and how
  the waveform keeps developing in the first few hundred milliseconds) and the space the instrument sounds in.

## Files
- `WindCore.h`: the engine. `WindCharacter.h`: the control layer used by the plugin. `WindCoreData.h`:
  generated by `tools/embed.py` from `character.json`, `cal.txt`, `curves.txt` and `trims.json`.
- `cli/windcore-render.cpp`: events in, WAV out.
- `bench/playing/`: short wind-controller phrases (MIDI with breath) used for testing.
- `tools/`: calibration, fitting, measurement and test scripts. They need Python 3 with numpy, scipy and mido,
  and build the renderer with clang++ on first use.

```bash
cd WindCore/tools
python3 calibrate.py 4         # retune the default settings
python3 trims.py               # remeasure the control corrections
python3 embed.py               # regenerate WindCoreData.h for the plugin
python3 check.py               # render the test phrases and screen them
```

`fit.py` and `curves.py` refit the horn's curves; they need a target file (`WIND_TARGET`, a measured
harmonic profile per note at three dynamics) that is not part of this repository.

## References
- A. H. Benade, *Fundamentals of Musical Acoustics*, 1976.
- J.-M. Chen, J. Smith, J. Wolfe, "Saxophonists tune vocal tract resonances in advanced performance techniques", *JASA* 129, 2011.
- J.-P. Dalmont, J. Gilbert, J. Kergomard, "Reed instruments, from small to large amplitude periodic oscillations and the Helmholtz motion analogy", *Acta Acustica* 86, 2000.
- Ph. Guillemain, J. Kergomard, T. Voinier, "Real-time synthesis of clarinet-like instruments using digital impedance models", *JASA* 118, 2005.
- J. Kergomard et al., "Role of the resonator geometry on the pressure spectrum of reed conical instruments", *Acta Acustica* 105, 2019.
- W. Li, J.-M. Chen, J. Smith, J. Wolfe, "Effect of vocal tract resonances on the sound spectrum of the saxophone", *Acta Acustica* 101, 2015.
- G. Scavone, "Modeling vocal-tract influence in reed wind instruments", SMAC 2003.
- T. A. Wilson, G. S. Beavers, "Operating modes of the clarinet", *JASA* 56, 1974.

Check volume and page details before citing.
