# Transient Sample Sources

All files in this directory are released under **CC0 1.0 Universal (Public Domain)**.
No attribution is legally required, but the sources are documented here for reference.

## Inharmonic transients (gen_*.wav)

Synthesised from filtered noise + fast envelopes by `gen_inharmonic.py` (seeded,
reproducible).  These have no harmonic series (autocorrelation periodicity < 0.27
vs ~0.98 for a tonal note), so they layer cleanly under any oscillator pitch and
are loaded with `pitched = false` (no pitch-tracking — the attack is identical
across the keyboard, with no smearing under transposition).

  - `gen_tongue.wav`  Tongue     — bandpass noise, wind articulation "tut"
  - `gen_click.wav`   Key Click  — broadband, ultra-short mechanical click
  - `gen_chiff.wav`   Air Chiff  — highpassed breath noise
  - `gen_knock.wav`   Wood Knock — lowpass resonant noise, woody thunk
  - `gen_pick.wav`    Pick Noise — bright bandpass burst, string friction
  - `gen_buzz.wav`    Reed Buzz  — gritty soft-clipped mid noise

## Flute, Clarinet, Cello, Trumpet

**Versilian Studios Chamber Orchestra 2 — Community Edition (VSCO-2-CE)**
- Repository: https://github.com/sgossner/VSCO-2-CE
- License: CC0-1.0
- Original files:
  - `Woodwinds/Flute/stac/LDFlute_stac_A4_v1_rr1.wav` → `flute_stac_A4.wav`
  - `Woodwinds/Clarinet/stac/DCClar_stac_D4_v1_rr1_sum.wav` → `clarinet_stac_D4.wav`
  - `Strings/Cello Section/spic/spic_C3_v1_RR1.wav` → `cello_spic_C3.wav`
  - `Brass/Trumpet/stac/Sum_SHTrumpet_stac_A4_v1_rr1.wav` → `trumpet_stac_A4.wav`

## Baroque Alto Recorder

**Versilian Community Sample Library (VCSL)**
- Repository: https://github.com/sgossner/VCSL
- License: CC0-1.0
- Original file:
  - `Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Staccato/AltRecorder_Stac_D4_rr1_Main.wav` → `recorder_stac_D4.wav`

## Processing

Each sample was:
1. Trimmed to the attack onset only (280–380 ms depending on instrument)
2. Mixed down from stereo to mono
3. Normalised to 0.85 peak
4. A 40 ms linear fade-out applied at the tail to prevent clicks
5. Written as 16-bit PCM WAV (recorder: 48 kHz native, others: 44.1 kHz)

The flute, trumpet and clarinet were re-trimmed (2026-06) to remove slow
breathy/brassy pre-attacks so their loud onset lands ~2-10 ms in, matching the
recorder and cello; a 2 ms fade-in avoids a click at each new start.  This makes
all five usable as punchy transient layers with consistent onset timing (before,
the loud part of these three arrived tens of ms late — under the decay envelope
it was inaudible).  Trim points: flute 80 ms, trumpet 25 ms, clarinet 20 ms.
