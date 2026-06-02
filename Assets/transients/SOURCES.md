# Transient Sample Sources

All files in this directory are released under **CC0 1.0 Universal (Public Domain)**.
No attribution is legally required, but the sources are documented here for reference.

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

The flute was re-trimmed (2026-06) to remove a ~80 ms breathy pre-attack so its
loud onset lands ~10 ms in, matching the other samples; a 2 ms fade-in avoids a
click at the new start.  This makes it usable as a punchy transient layer
(before, the loud part arrived after the decay envelope had faded it out).
