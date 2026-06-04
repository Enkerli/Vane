# Vane — Roadmap

Milestone snapshot (2026-06). The original UI-overhaul plan and the first
"reassessed" backlog are both **delivered** — what follows records where Vane is
and what's genuinely next.

---

## What Vane is now

A JUCE 8 **MPE + MTS-ESP + CC-first**, mono-capable synthesizer built around
continuous gestural expression. AU · VST3 · Standalone (+ iPadOS AUv3).
Single self-contained WebView UI (Stage · Patch · Matrix · Presets), one-pane
focus reflow on narrow/AUv3 windows.

### Delivered

**Synthesis**
- **Wavetable morph oscillator** — load arbitrary `.wav` (Serum/Vital `clm`
  detection, AKWF), mip-mapped + FFT band-limited; normalised Morph with
  filmstrip / live frame / snap; opt-in phase-aware morph; table embedded in
  state for portability + content-hash library dedup.
- **Wavetable library browser** — bundled CC0 factory set + imports, hash-deduped;
  iOS-friendly load path (no document picker).
- PD pulse-width, √2-FM inharmonicity, wavefold, noise blend (white/pink/brown),
  **sync-transpose** (formant at f0·ratio, non-integer = inharmonic).
- **Stereo unison** — up to 6 detuned voices, per-channel filter = true stereo
  image; level-consistent; cross-voice legato handoff (phases + filterR).
  - **Rotating chords** (Kilgore/Brecker) — chord voice-mode; per-harmony-voice
    interval sequences advancing one step per note (reset on new phrase);
    monophonic legato rotation, no extra keys.
- **Transients** — CC0 per-note attack layer (tonal *and* synthesised inharmonic);
  per-trigger variation, filter coupling, breath dynamics, Karplus-Strong pitch
  resonator, noise→tone morph.
- Cytomic TPT SVF (LP/BP/HP), per-sample-smoothed cutoff.

**Expression & control**
- MPE (pressure / slide / pitchbend); **MTS-ESP** with hole→nearest quantisation;
  internal-tuning library (12-EDO, just, pyth, meanqc, werck3, diat7, 19-EDO, BP);
  **Tuning** stage view (system, deviation map, holes, MTS ▸ Internal ▸ Bypass).
- **Mono legato** — sample-exact cross-voice handoff (osc/pm phase, SVF state,
  cutoff, VCA, unison phases, right filter).
- **Glide** — Linear / Exp / RC / **Bézier** (editable time→pitch trajectory).
- **Rigs / controller profiles** — multi-controller composition (Stage 2a/2b);
  channel/zone is the cross-platform identity floor (AUv3 can't publish virtual
  ports but can direct-open named sources).

**Modulation matrix (24-slot, source-pool, migration-safe)**
- Sources: breath, expression, MPE dims, velocity, **Keytrack**, 8 aux CCs.
- Destinations incl. Transient and Uni Detune.
- **Editable Bézier response curves** per route (monotone cubic spline, inline).
- **Mod-of-mod** — a route's amount scaled by another source.

**UI** — WebView whole-app; real FFT spectrum; MPE-aware active-modulation display;
AUv3 focus reflow; single-tap typed entry + global no-select touch fixes.

**Dropped** — modulatable chorus/flange (the wavetable + transient work supplies
the timbral motion that justified them).

---

## Forward backlog

**DSP polish**
- **BLEP-clean sync** — *investigated and shelved.* The mip-drop already lands
  non-integer sync at ~−45 dB alias/harmonic (vs a −71 dB table-interp floor); a
  PolyBLEP on the wavetable reset edge measured *worse*, and a correct fix needs
  minBLEP tables or 2× oversampling — real work for marginal, filter-masked gain.
  Revisit only if it sounds rough on a specific patch. See `Oscillator.h`.
- **Rotating-chord extensions** — rotation reset control (footswitch/CC),
  direction / step size, scale-quantised intervals (stay diatonic), optional
  min-note-length gate so fast grace notes don't advance the rotation.

**Storage / sharing**
- **WT library P2** — per-table license/source metadata (PD / owned / copyrighted);
  "export preset with embedded table" gated by it (PD embeds freely, copyrighted →
  reference-only). Yields shareable-vs-local presets naturally.
- **State-versioning / stabilisation** — versioned, recallable presets that
  reproduce the same result by version. The surface (tables + rigs + profiles +
  presets + curves + sequences) is interaction-heavy; a consolidation pass pays off.

**UX**
- **Preset browser / UX rework** — areas 2/3/5 of the design questions, not yet
  explored; revisit with Design.
- **Keytrack centre/threshold** — Keytrack is bipolar around C4; expose the centre
  (or a threshold) so the "less on low notes" pivot matches a player's range.

**Platform**
- **AUv3 MIDI identity** — virtual-port publishing is blocked; channel/zone is the
  floor. Lean further into direct-open of named CoreMIDI sources + pairing
  suggestions.

---

## Sequencing principle

Backend/DSP that survives a UX redesign goes first; hold UI-heavy work until
Design input lands. (The 2026-06 milestone cleared the whole prior backlog, so
the next big rock is the **preset browser / UX rework** with Design, alongside
opportunistic DSP polish.)
