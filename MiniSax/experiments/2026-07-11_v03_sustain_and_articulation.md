# Experiment 2026-07-11 (second session) — v0.3: sustained tone + smoother articulations

## Goal

Get the sustained sound closer to the Silverwood snippet (the H5..H8
plateau was several dB shy in v0.2) and smooth out articulations (step
breath events and note gates were audibly thumpy).

## Starting point

v0.2: conical ring-mod shaper, H2 matched (−0.1 dB) but H5..H8 followed
the loop's 1/k rolloff; gate attack ≈ 2.6 ms, breath smoothing ≈ 8 ms,
default pitch glide 60 ms.

## Changes tried

1. **Fixed body formant** (peaking biquad in the output chain).  Swept
   frequency/gain/Q against the reference profile with H2..H8 fully
   weighted: 1.4 kHz, +10 dB, Q 1.1.
2. **Conical tap ratio** re-swept jointly with the formant: the ring-mod
   product is a 2·f0 rectangle with duty = 2·tap, harmonic m carrying
   sin(pi·m·duty)/m, so the tap sets the even-harmonic mix.  0.30 beats
   the naive 0.25 (profile error 42 vs 205 with formant).
3. **Smoothing pass**: breath smoothing 8 -> 20 ms, gate attack 2.6 -> 11 ms,
   gate release 20 -> 32 ms, default pitch glide 60 -> 80 ms.  The suites'
   own breath envelopes still differentiate hard vs soft attacks on top of
   these floors (hard_tongue's 25 ms ramp vs soft_tongue's 180 ms).
4. **Output makeup** re-measured with the formant: outputScale 0.22 -> 0.12
   (worst peak 0.96 at outputGain 1 across breath/conical/pitch extremes).
5. **tenor_sax_009 re-voiced** for the v0.3 chain: conicalAmount 0.62,
   bellBrightness 0.70, plus gentle vibrato (air 0.20 / pitch 0.15) to
   approximate the reference's sustain animation (sustainAmplitudeVariation
   0.15 there vs 0.02 for a dead-straight render).  Deeper vibrato smears
   the measured harmonic peaks (each harmonic wobbles ±k·depth), so depth
   was set with the descriptor watching.

## Results

C4 long tone (tenor_sax_009) vs reference, H2..H8 dB rel H1:

```text
tenor_sax_009  -0.1  -7.3  -1.2  -8.1  -11.4  -16.3  -20.2   e/o +7.0  centroid 1729
SilvSnip       -0.2  -7.8  -6.4  -7.8   -5.8   -7.8   -5.8   e/o +5.4  centroid 1471
```

H2/H3/H5 within 0.5 dB.  H4 runs hot (the duty-rectangle's m=2 line;
reads as reedy bite).  H6..H8 sit below the reference plateau once vibrato
smearing is in play — the static-tone fit (no vibrato) had H6 −9.4/H7
−13.6/H8 −16.5.  Full matrix (11 runs): no silence, no clipping, no
register flips; attacks now measure 45–60 ms instead of near-instant.

## Interesting presets saved

- `tenor_sax_009` (re-voiced; now carries the reference-chasing sustain).

## Problems

- H4 ~5 dB hot vs reference — pitch-tracked notching would fix it but the
  fixed-EQ budget of v0.3 can't.
- H7/H8 still shy; the reference's plateau extends further than one
  formant peak covers.  A second presence peak (~2.5–3 kHz) is the next
  candidate.
- Vibrato depth trades sustain life against measured harmonic sharpness;
  the analyzer could measure harmonics on a vibrato-tracked grid instead.

## Next steps

- Second presence formant; possibly pitch-tracked H4 trim.
- Vibrato-aware harmonic measurement in the analyzer.
- Dedicated articulation events (tongue/slap/flutter) as DSP features.
