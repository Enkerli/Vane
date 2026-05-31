# Vane — Roadmap (reassessed)

Replaces the original UI-overhaul plan, which is largely delivered and partly
stale. This reflects where Vane actually is and where it's going.

---

## What Vane is now

A JUCE 8 **MPE + MTS-ESP + CC-first**, mono-capable synthesizer built around
continuous gestural expression. AU / VST3 / Standalone (+ iPadOS AUv3).

Delivered:
- **WebView UI** (WebBrowserComponent), whole-app.
- **Generic 24-slot mod matrix** (source-pool, slot-based, migration-safe).
- **Rigs / controller profiles** — multi-controller composition with per-instance
  Notes/Mod role masks + channel/zone routing (Stage 2a/2b). Grounded in the
  MIDI-probe findings: channel/zone is the cross-platform identity floor;
  device-name enumeration works (incl. AUv3) for pairing *suggestions*; Vane
  can't publish virtual ports as an AUv3, but can direct-open named sources.
- **Wavetable engine** — load arbitrary `.wav` (Serum/Vital `clm` frame-size
  detection, FFT band-limiting), normalized morph with filmstrip + live frame +
  snap-to-frame, **phase-aware morph** (opt-in), **sync-transpose inharmonicity**,
  per-frame vs global normalization (global kept on branch `wt-global-norm`),
  table embedded in state for portability.
- **Real FFT spectrum** + **MPE-aware active-modulation** display.
- Smooth legato (mono/wind), per-voice expression plumbed to the UI.

The wavetable work became the new center of gravity — which is why we're
**dropping modulatable chorus/flange**: the timbral motion now comes from the
table, not a modulated delay. Their effect wasn't interesting enough to keep.

---

## Wavetable storage (in progress)

Embedding the table in every preset is portable but redundant; the same table in
N presets is N copies. The fix is a **content-addressed library + reference**,
which also gives the public-domain-vs-copyright distinction.

**Phase 1 (now): library + hash dedup + reference**
- A library folder (`~/Library/Vane/Wavetables/<md5>.wav`). Loading a table
  stores it once, keyed by content hash → the same table across presets is one
  file.
- **Presets** store a *reference* (`wavetableHash` + `wavetableName`), not the
  bytes — lean, deduped.
- **DAW projects** stay self-contained: `getStateInformation` keeps the embedded
  bytes (`wavetableData`), since a project is the portable unit.
- On load: embedded bytes → else resolve hash from the library → else built-in.

**Phase 2 (later): sharing + license**
- Per-table license/source metadata (public-domain / owned / copyrighted).
- "Export preset with embedded table" gated by it: PD/owned embed freely;
  copyrighted → reference-only ("recipient needs the table"). Yields shareable
  vs local-only presets naturally.

---

## Prioritized backlog

**Tier 1 — workflow + identity (backend/DSP; survives a UX redesign)**
1. **WT library** — P1 dedup (in progress), then P2 license/export.
2. **MTS-ESP completeness** — tuning-name display, internal tunings, and the
   keymap-"holes" abrupt-note-end fix. Flagged at the very start, never done;
   the most under-delivered part of Vane's MTS-first identity.

**Tier 2 — after the Claude Design UX pass (UI-heavy → shape with Design input)**
3. **Bézier mod-curves** — editable per-route modulation curves (replaces the
   discrete Lin/Exp/S choices). High expressive payoff; natural in the WebView.
4. **Preset browser / UX rework** — informed by Design.

**Tier 3 — self-contained, whenever**
5. **BLEP-clean sync** — anti-alias the hardsync reset edge (the one rough edge
   of sync-transpose).
6. **Stereo-unison** — make Vane stereo for the first time (per-voice detuned
   bank + pan spread). The road-not-taken when wavetables took over.
7. **Keytracking** (mod-by-pitch, e.g. PWM-by-range) — flagged early, low priority.

**Meta**
- **Stabilization / state-versioning** — versioned, recallable presets (a preset
  recalled with a version number reproduces the same result). The surface
  (tables + rigs + profiles + presets) is interaction-heavy; a consolidation
  pass will pay off.

---

## Sequencing principle

Don't build UI-heavy features right before a UX redesign. Tier 1 (WT library,
MTS-ESP) is backend/DSP and survives whatever Design changes; do it now. Hold
Tier 2 (Bézier, preset browser) until Design's input lands.
