# Vane — playing-test plan (whole experience)

These are **playing tests**, not UI checks. The question isn't "does the button
work" — it's *how does it feel to play, and how do the features combine* across
the controllers Vane is for. UX ≠ UI: a feature can be correct in isolation and
still ruin a phrase when it interacts badly (the unison-legato click was exactly
that). Run these as short musical sessions, not checklists.

Companion test presets: `Tools/PresetGen` writes a set into
`~/Library/Vane/Presets/` (names prefixed `Test - `). Each one
isolates an interaction below.

---

## 1. Controllers under test

Each controller offers a different *expression vocabulary*; the same patch should
feel idiomatic on each, and the features should map to what the controller
actually sends.

| Controller | Drives amplitude | Other live axes | Velocity | Articulation gesture |
|---|---|---|---|---|
| **Sylphyo** (primary) | breath (CC2) | accelerometer/tilt → CC, slide; MPE | **fixed** | breath onset / tonguing |
| **EWI** | breath | bite (→ glide/vibrato), key rollers | none | breath + key change |
| **Breath controller + keyboard** | breath (CC2) | keyboard velocity (if any) | from keys | key attack |
| **MPE keyboard** (Seaboard/Exquis) | pressure (or breath if routed) | slide (CC74), per-note bend | yes | key strike + press |
| **Poly-AT pad** (Launchpad X) | aftertouch | — | yes | pad strike |

Setup pass per controller (do once):
- Confirm the **identity floor** — the right rig/profile, channel/zone, and that
  notes + the breath/pressure axis arrive. (AUv3: direct-open the named source.)
- Confirm the **VCA source**: breath-first patches need CC2/pressure mapped to VCA,
  not velocity. On the Sylphyo, **velocity is fixed** — so anything velocity-driven
  must instead ride breath (this is the whole reason for transient **Dynamics** and
  for routing **× Breath** in the matrix).

---

## 2. Interaction tests (the core)

For each: *setup → play → listen/feel for → fail signs*.

### A. Legato × unison × rotating chords  ·  preset: `Test - Rotating Stack`
- **Play:** sustained breath, slur a scale/arpeggio, mono. Then 6-voice detune.
- **Feel for:** the line stays one continuous sound; the chord rotates note-to-note
  with no click on *either* channel; stopping/restarting breath resets the rotation.
- **Fail:** a tick on the right channel or on a detuned voice at note changes
  (the cross-voice handoff regressing); the chord jumping discontinuously.

### B. Breath dynamics × transients  ·  preset: `Test - Breath Lead`
- **Play:** soft entries vs hard/fast breath onsets; legato vs re-tongued notes.
- **Feel for:** a soft attack gives a soft (or no) chiff; a hard attack pops the
  transient; **Dynamics** keeps the transient from ever overpowering a quiet note;
  **Non-legato** trigger only fires the chiff when you re-articulate (breath dip).
- **Fail:** the transient identical at all dynamics (Dynamics not biting), or
  louder than the note on a gentle entry; chiff firing mid-slur in Non-legato.

### C. Inharmonic attack × pitch resonator  ·  preset: `Test - Inharmonic Attack`
- **Play:** the same line up and down the range.
- **Feel for:** the noise attack reads as *part of* the note (Resonate pulls it to
  the note's pitch), not a pasted click; it tracks pitch across the keyboard.
- **Fail:** the attack sounds like a separate event ("superimposed"); a fixed-pitch
  click that ignores the note (Resonate off / not tracking).

### D. Keytrack × curves × mod-of-mod  ·  preset: `Test - Keytrack Filter`
- **Play:** low register vs high register, same breath.
- **Feel for:** high notes open brighter (Keytrack→Cutoff); the response **curve**
  shapes where that kicks in; **× Breath** on the route means low notes get *less*
  breath-to-cutoff, so the bottom doesn't get muddy under hard breath.
- **Fail:** brightness flat across the range; low notes over-bright under breath
  (mod-of-mod not scaling); the curve having no audible effect.

### E. Wavetable morph × sync × inharmonicity  ·  preset: `Test - Morph Sweep`
- **Play:** breath/expression swept while sustaining; vary the morph route depth.
- **Feel for:** simple→complex timbral travel as breath rises; sync-transpose adds
  a formant; non-integer sync = inharmonic shimmer; all under continuous control.
- **Fail:** morph stepping/zippering; the morph fighting the note's pitch (a sign
  of a non-phase-aware table — try Phase-align); harsh aliasing on extreme sync.

### F. Tuning × glide  ·  preset: `Test - Microtonal Glide`
- **Play:** with an MTS master (Entonal etc.) *and* with none (internal tuning).
- **Feel for:** pitches land on the scale; MTS "holes" quantise to the nearest
  sounding pitch rather than dropping out; glide (esp. **Bézier**) travels through
  the tuning smoothly; switching MTS master ↔ internal is graceful.
- **Fail:** notes dropping to silence on holes; the pill/system name wrong; a glide
  that audibly steps between scale degrees.

---

## 3. Whole-experience / workflow

- **Preset switching mid-performance** — change presets while breath is on; the
  note should continue, not retrigger or click; the right preset name shows; the
  wavetable/curves/sequences come with it.
- **iPad / AUv3 reflow** — narrow window: the focus bar swaps Stage·Patch·Matrix·
  Presets; single-tap typed entry works; no accidental text selection on a drag.
- **Rig / profile** — switch controllers; the profile banner offers to apply a
  patch's intended profile; sources re-bind.
- **Recovery** — Panic clears stuck notes; the Tuning view recovers if the MTS
  master disappears.
- **Two-hands reality** — can you reach the gesture you need *while playing*?
  (e.g. is Morph better on breath than on a knob you can't reach mid-phrase.)

---

## 4. Cross-cutting things to watch (failure modes we've actually hit)

- **Fixed velocity** (Sylphyo) silently disabling a velocity-driven feature → use
  breath/Dynamics/× Breath instead.
- **Legato continuity** breaking when a new subsystem isn't in the handoff (osc,
  filter, *unison phases*, *filterR* — all now covered; new ones must join).
- **Tuning init** — with no MTS master, internal tuning must be 12-EDO by default,
  not silence (the all-notes-8 Hz bug).
- **Mod amount maxing out** — full breath at amount 1 can peg cutoff; use the curve
  or a smaller amount so there's travel left.

---

## 5. Test presets (what each exercises)

| Preset | Exercises | Key settings |
|---|---|---|
| `Test - Breath Lead` | core breath play, transient dynamics, legato | mono, breath→VCA+cutoff, tonal transient, Dynamics ~0.8, glide RC |
| `Test - Rotating Stack` | rotating chords, unison legato | mono, 4 voices, Chord mode, varied-length sequences |
| `Test - Stereo Unison Pad` | stereo unison, morph, width | 6 voices detune, width 1, morph on breath |
| `Test - Inharmonic Attack` | transient fusion (resonator, filter coupling) | inharmonic sample, Resonate ~0.5, Filter route on |
| `Test - Keytrack Filter` | keytrack, response curve, mod-of-mod | Keytrack→Cutoff w/ curve, × Breath scaling |
| `Test - Microtonal Glide` | tuning (internal/MTS), Bézier glide | internal just/19-EDO, Bézier glide, mono |
| `Test - Morph Sweep` | wavetable morph, sync, inharmonicity | breath→Morph, mild sync, inharm route |

> The point of a preset here is to make the interaction *audible in one note* —
> load it, play, and the thing under test is front and centre.
