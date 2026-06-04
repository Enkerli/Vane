# Vane

An **MPE / MTS-ESP / CC-first**, mono-capable synthesizer plugin built with JUCE 8,
for macOS (AU · VST3 · Standalone) and iPadOS (AUv3).

Designed around **continuous gestural expression** rather than discrete note events.
Wind controllers (Sylphyo, EWI, WX-11), dedicated breath controllers (BC-1, Zefiro),
MPE instruments (Exquis, Seaboard), and poly-aftertouch controllers all find a home
here — anything that expresses through curves and phrases rather than velocity and
key-release. The default VCA is **breath-first** (velocity is opt-in), and the whole
signal path is built so a legato wind line stays smooth and click-free.

The UI is a single self-contained WebView surface (Stage · Patch · Matrix · Presets),
which reflows to one pane at a time on a narrow iPad/AUv3 window.

---

## Synthesis

- **Wavetable morph oscillator** — load an arbitrary `.wav` (Serum/Vital `clm`
  frame-size detection, AKWF tables), or use the built-in harmonic stack.
  Mip-mapped + FFT band-limited so no partial ever exceeds Nyquist. Normalised
  `Morph` sweep across frames with a filmstrip, live frame readout, snap-to-frame,
  and an opt-in **phase-aware morph** so interpolation never cancels.
- **Wavetable library browser** — a bundled, CC0 factory set (AKWF, sorted
  dark→bright) plus your own imports, deduped by content hash; the iOS-friendly
  load path (no document picker needed).
- **Pulse-width** (phase distortion), **inharmonicity** (√2-ratio FM), **wavefold**,
  and **noise blend** (white / pink / brown), all modulatable.
- **Sync-transpose** — wavetable hard-sync for a formant peak at `f0 × ratio`;
  non-integer ratios give inharmonicity-via-transposition.
- **Stereo unison** — up to 6 detuned voices spread across the field, each channel
  filtered independently for a true stereo image (Vane is not dual-mono). Level
  stays constant as you engage it.
  - **Rotating chords** (Kilgore/Brecker) — a *chord* voice mode where each harmony
    voice follows its own looping interval sequence, advancing one step per played
    note (resetting on a new phrase). A monophonic legato line rotates the chord
    internally — no extra keys.
- **Transients** — a per-note attack layer: a CC0 sample (tonal instrument
  attacks *or* synthesised inharmonic noises — tongue, click, chiff, knock…) with
  per-trigger **variation** (round-robin), **filter coupling** (the attack sits in
  the note's spectral space), breath **dynamics** (so it never overpowers a soft
  attack), a tuned **pitch resonator** (Karplus-Strong — the noise acquires the
  note's pitch), and a **noise→tone morph**.
- **Cytomic TPT state-variable filter** — LP / BP / HP, per-sample-smoothed cutoff.

## Expression & control

- **MPE** — full per-note pressure, slide (CC74), and pitchbend.
- **MTS-ESP** — live microtonality via the ODDSound master/client protocol;
  MTS-silenced pitches are quantised to the nearest sounding pitch (no dropouts).
  When no master is connected, a library of **internal tunings** (12-EDO, just,
  Pythagorean, meantone, Werckmeister, 7-note diatonic with holes, 19-EDO,
  Bohlen-Pierce) takes over, with a dedicated **Tuning** stage view (system name,
  deviation map, holes, `MTS ▸ Internal ▸ Bypass` precedence).
- **Mono legato** — sample-exact cross-voice handoff: oscillator phase, FM
  modulator phase, SVF filter state, the unison stack's phases *and* the right-
  channel filter all carry across the boundary, so nothing clicks.
- **Glide / portamento** — legato-only, with four curve modes: Linear, Exponential,
  RC, and **Bézier** (an editable time→pitch trajectory you draw).
- **Rigs / controller profiles** — compose multiple controllers with per-instance
  Notes/Mod role masks and channel/zone routing.

## Modulation matrix

A generic **24-slot** matrix (source-pool, migration-safe). Each route is
`source → destination × amount`, with:

- **Sources** — Breath, Expression, MPE Pressure / Slide / Pitchbend, Velocity,
  **Keytrack** (note pitch, bipolar around C4), and 8 configurable aux CCs.
- **Destinations** — VCA, Cutoff, Reso, Pitch, Morph, PW, Fold, Noise, Inharm,
  Sync, Transient, Uni Detune.
- **Editable response curves** — a monotone cubic-spline curve per route
  (drag anchors, 1–3 points for bends and S-curves; bipolar sources mirror).
- **Mod-of-mod** — a route's amount can be **scaled by another source**
  (e.g. Keytrack × Breath→Cutoff so low notes get less breath-to-cutoff).

---

## Architecture

```
 MIDI (MPE channels, CC, MTS-ESP)
        │
   ModMatrix ──── 24 slots: source → dest × amount, editable curve, ×scale-source
        │           (per-voice MPE dims + global CC/macro/aux + keytrack)
        ▼
 ┌──────────────────────────────────────────────┐
 │ SynthVoice × 15  (one per MPE member channel) │
 │   Oscillator (+ kMaxUnison-1 unison oscs)     │   wavetable morph · PD · FM ·
 │   SVFilter  (+ filterR for stereo unison)     │   sync · noise · fold
 │   SamplePlayer + CombResonator (transient)    │
 │   smoothedVCA / smoothedHz / smoothedCutoff   │
 └──────────────────────────────────────────────┘
        │
   stereo AudioBuffer
```

### Legato voice handoff (mono)

Mono legato allocates a *new* voice and hands off through shared atomics, published
by the outgoing voice each block and restored by the incoming one:

| Published | Restored as |
|--|--|
| `lastOscPhase`, `lastPmPhase` | `osc.reset()` / `setPmPhase()` — the centre oscillator continues |
| `lastFilterS1/S2`, `lastCutoffHz` | SVF integrators + cutoff primed before `setState()` |
| `lastVCALevel` (`smoothedVCA × tailLevel`) | the *actual* amplitude — distinguishes true legato from a tail-off |
| `lastUnisonPhase[]`, `lastFilterRS1/S2` | the detuned unison stack + right filter continue too |

Publishing `smoothedVCA × tailLevel` (not the raw smoothed level) is the key
invariant separating true legato (breath continuously on) from an attack following
a tail-off.

---

## Building

Requirements: Xcode, CMake ≥ 3.22, JUCE 8 cloned alongside the project.

```sh
git clone https://github.com/Enkerli/Vane.git
cd Vane
git clone --depth 1 --branch 8.0.6 https://github.com/juce-framework/JUCE.git JUCE
cmake -S . -B build-mac          # configures AU · VST3 · Standalone (+ AUv3, MTS if present)
cmake --build build-mac --target Vane_Standalone Vane_AU Vane_VST3
```

A headless **render probe** (`Tools/RenderProbe`, excluded from the default build)
instantiates the processor and measures the engine without a host — used to verify
DSP changes (pitch, aliasing, stereo width, legato continuity, mod-of-mod, …):

```sh
cmake --build build-mac --target VaneRenderProbe
```

Debug builds run the in-process unit tests (tuning, mod-matrix curves, oscillator,
wavetable, transients) on launch.

## Licence

[The Unlicense](LICENSE) — public domain. Bundled factory samples/wavetables are
CC0 (provenance in `Assets/transients/SOURCES.md`).
