# Vane — Roadmap

Design north star: a breath-savvy expressive instrument where intensity has multiple independent dimensions. A loud sine with a little noise can feel as present as a sawtooth through chorus. The goal is never a single linear loudness axis but a rich, performable space of timbral states — shaped by continuous gesture (breath, pressure, slide, bend) rather than triggered by discrete note events. Breath is central, but any continuous controller producing curves and phrases is at home here.

---

## Phase 1 — Expression curves & sweet spots

### SVF response shaping
- Map CC74 (slide) across the full audible range: near-silence to fully open, not just a narrow sweep around a base cutoff
- Resonance/cutoff coupling: simultaneous increase produces the classic squelchy wind-controller character — find and lock in good default curves
- Pressure → brightness interaction on top of CC74: pressure adds a second, per-note brightness dimension without fighting slide
- Explore non-linear (exponential, S-curve) mapping for each route in the ModMatrix
- Velocity contribution to initial timbre, separate from VCA floor

### Portamento curves
- Interval-proportional glide time: wider interval = longer ramp, giving a more "organic" feel than fixed-time glide
- Curve shape options (linear, exponential, logarithmic)

### Sweet-spot bookmarks
- Named preset snapshots: store the full parameter + ModMatrix route state
- Quick-recall in the GUI — a small bank of labelled slots
- Foundation for a future patch format (distinct from DAW automation)

---

## Phase 2 — GUI & parameter access

### Source macros — abstract MIDI bindings
The current code hardcodes `CC + 2` as "breath" and `CC + 11` as "expression". This breaks the
moment a user switches controller (Zefiro uses Aftertouch for breath; an EWI user may have
remapped to CC11; a keyboard player routes expression pedal differently). The fix is a two-layer
architecture:

**Layer 1 — MIDI binding (macro definitions):**
Named abstract sources, each with a concrete MIDI binding stored in the preset:
```
Breath     → CC2           (default; editable to: Aftertouch | CCn | MPE Z | …)
Expression → CC11          (default; editable)
Pressure   → MPE Z         (channel pressure, per-voice)
Slide      → CC74 / MPE Y  (editable)
Pitchbend  → PB, range ±48 st  (range editable: ±2 / ±12 / ±24 / ±48 / custom)
Velocity   → note-on vel   (fixed; not rebindable)
```
The binding table is serialised with the preset, so a preset carries its own source definitions.
Loading a preset on a different controller = change one binding field, re-save.

**Layer 2 — routes (the existing ModMatrix):**
Routes reference macro names, not raw CC numbers. `Breath → VCA Level × 1.0` is meaningful
regardless of which CC number "Breath" currently resolves to.

**Pitchbend range normalisation:**
Devices disagree wildly: Sylphyo default = ±48 st; standard MIDI keyboard = ±2 st; many are
configurable. The raw pitchbend JUCE value is always ±1, so the normalization constant lives in
the macro binding: `PB_semitones = rawPB × rangeSetting`. Routes see a ±1 value already scaled
to the declared range, so a route "PB → pitch fine, ×1.0" always means ±1 declared semitone
regardless of the physical device range.

**Implementation notes:**
- `ModSourceID` gains a `Macro` base (e.g. 512..519) alongside the existing `CC` and `MPE_*` IDs
- processBlock resolves each macro to its bound raw value before passing to evaluate()
- The macro binding table is a small struct array serialised inside the preset XML
- Existing default routes are migrated automatically (CC + 2 → Macro::Breath at load time)

### ModMatrix visual editor
A full-screen (or full-panel) patching surface for building and editing route sets. This is
the primary preset-editing UI — what lets a user create a new sweet spot without opening a DAW.

**Design direction — Stack + patch points (hybrid):**
- Left column: macro source nodes (Breath, Expression, Pressure, Slide, PB, Velocity)
  each showing a live meter bar and its current MIDI binding (editable inline)
- Right column: destination nodes (VCA Level, Cutoff, Reso, Pitch Fine, Glide, …)
- Centre: the routes — each rendered as a horizontal strip:
  `[● Breath] ──────── [▶ Cutoff] × [0.50 ▓▓▓░░] curve:[Exp▾] slew:[5/80ms]`
- "+ Add route" at the bottom inserts a new strip with source and dest pickers
- Delete button (×) on each strip removes it live
- Amount is a draggable slider; curve and slew open a small popover
- Live modulation value shown as a moving fill on each route's amount bar

**Why not a pure flow/node graph:**
Flow graphs are visually expressive but hard to interact with on a small plugin window or on
an iPad. The strip layout is scannable, touch-friendly, and maps well to the existing
ModMatrix data structure. A flow visualisation can be added as a secondary "view" toggle.

**Source macro binding panel:**
Accessible from a small "Edit sources…" button or from tapping a source node label:
- For each macro: a CC picker (0–127), or toggle to Aftertouch / MPE Z / MPE Y / fixed
- Pitchbend macro: additional range field (semitones, ±2..±96, free entry)
- Changes take effect immediately; routes are live-updated since they reference the macro

### Parameter panel
- Expose the full parameter set in the standalone window — at minimum a compact one-page view
- Target: everything addressable without a DAW (Bitwig exposes params cleanly; Logic requires plugin-side UI)
- Parameters are synthesis-engine values (cutoff base, reso base, wave, glide time)
  — distinct from ModMatrix amounts, which are per-route and live in the patch layer above

---

## Phase 3 — Timbral depth (subtractive engine extensions)

### Waveform modulation
- PWM: modulate pulse width via breath/CC/pressure — highly effective with slow modulation
- Wavemorphing: cross-fade between adjacent waveforms (sine → triangle → saw → square) as a continuous parameter
- Wavefolding: saturate/fold the oscillator output before the SVF, expressively modulated

### Noise & inharmonicity
- Blend oscillator with band-limited or coloured noise (separate noise amount route in ModMatrix)
- Inharmonic partials: slight frequency offset of harmonics for a slightly "alive" quality even on sine

### Spatial expression
- Stereo width as an expressible dimension
- Frequency-dependent width: low fundamentals stay narrow (mono compatible), high-pitched sounds afford wide spreading
- Width modulatable via breath or CC

### Mute/sordino effect
- Formant-style comb or resonant shelf that simulates acoustic muting
- Distinct from a simple low-pass cut — the Sordina character comes from the spectral shape, not just rolloff
- Expressively modulatable via CC (not a static insert effect)
- Note: Librewave Sordina (GPLv3) is a reference; a clean-room implementation is needed for compatibility with CC0

### Chorus / flange (expressive)
- Not a static send effect — modulation depth and rate driven by breath/CC/pressure
- Mono-in, stereo-out: contributes to spatial dimension above
- Could double as ensemble detune layer for WT (see Phase 4)

---

## Phase 4 — Wavetable engine

### Core WT synthesis
- Single-cycle wavetable playback with anti-aliased interpolation
- Table index as primary timbral parameter (replaces/supplements waveform selector)
- Index modulatable via all ModMatrix sources — CC74 → index sweep is an alternative to CC74 → filter cutoff

### Multi-voice WT ensemble
- Several voices per note, each with a slightly offset table index
- Index spread produces a rich ensemble quality distinct from chorus (it's timbral, not pitch-based)
- Spread itself expressively modulatable

### Expression mapping notes
- Index modulation curves may need different shapes than SVF cutoff curves — experimentation needed
- WT index can approximate lowpass behaviour (timbre opens as index rises) but with a different harmonic character

---

## Phase 5 — Polyphonic intelligence

### Polyphonic legato
- Each MPE voice maintains its own phase/filter state across note changes
- "True polyphonic legato": simultaneous notes each get seamless transitions rather than restarts
- Note: CC breath, CC11, and global aftertouch are single-channel (paraphonic) — all
  simultaneous notes share one breath envelope. Per-note pressure (MPE Z) is truly
  polyphonic. Profiles document which sources are paraphonic and which are per-note.

### Harmonic generation
- Second-voice contrary motion: generate a line moving opposite the melody, at least an octave away, with independent legato
- Rotating/expanding chords (Kilgore / Brecker Xpander "Harmonic Expansionism" model): chord voicings shift across a sequence, yielding oblique and contrary motion rather than a fixed block chord
- Each generated voice is a full synth voice (own channel, own legato lane) — not a transposition effect
- Fluid-Chords-style adaptive voicing: generated notes find available intervals rather than always using the same chord shape

---

## Phase 6 — Advanced engines

### FM synthesis
- Operator network (2–4 ops), ratio and index modulatable via ModMatrix
- Index as primary expressive dimension (analogous to filter cutoff in subtractive)

### Additive synthesis
- Partial bank with individual amplitude/frequency envelopes
- Spectral morphing between stored partial frames

---

## Phase 7 — Integration & tooling

### Controller profiles

Once source macros exist (Phase 2), a controller profile is simply a named macro-binding
preset. Profiles are separate from sound presets and mix freely: load profile "Sylphyo" +
sound "Reedlike Lyrical", or load profile "Exquis" + the same sound without changing any routes.

**Controller categories:**

Controllers differ substantially in what MIDI data they produce; profiles are organised by
category rather than as a flat list:

- **Windcontrollers** — monophonic, send note data alongside breath (via CC, aftertouch, or
  PB) and typically bite or glide as pitchbend. The phrase and legato is the core gesture.
  *Profiles: Sylphyo (breath→CC2+CC11+AT simultaneously, slide→CC74, PB ±48 st); WX-11
  (breath→CC2, bite→PB up-only ±12 st); EWI-USB (breath→CC2, slide→CC74); Artinoise
  re.corder. Special case: Eigenharp Pico — uniquely combines windcontroller gesture
  with high-resolution breath and MPE-style per-note dimensions.*

- **Breath controllers** — no note data; send only breath, bite, and/or accelerometer
  signals. Always paired with a separate note source. The Sylphyo can send breath to three
  CC routes simultaneously; the Zefiro lets the player shape the breath curve on the device.
  *Profiles: Yamaha BC-1 (breath→CC2); Artinoise Zefiro (breath→AT, curve configurable).*

- **MPE instruments** — per-note pressure (MPE Z), slide (CC74 / MPE Y), and pitchbend
  on member channels 2–16. Full polyphonic expression, each note independent.
  *Profiles: Exquis (pressure→MPE Z, slide→CC74, PB ±48 st); generic MPE.*

- **Polyphonic aftertouch controllers** — velocity + polyphonic aftertouch; typically
  no pitchbend or only coarse master-channel PB. Aftertouch maps naturally to MPE Pressure.
  *Profiles: Novation Launchpad X; Launchpad Pro (adds programmable CC and PB).*

- **Standard keyboard / default** — velocity-only or with mono aftertouch; PB ±2 st.
  *Profile: keyboard default (velocityMix = 1.0, breath macro unbound).*

Built-in profiles are a starting point; users can edit and save their own. A **compound
profile** (e.g. Zefiro providing breath + Exquis providing notes and slide) is possible
once the macro system can resolve breath and note sources independently.

**Paraphonic breath in poly mode:**
CC breath, CC11, and global aftertouch are inherently single-channel signals — one value
shared across all voices. In poly mode this produces a *paraphonic* texture: simultaneous
notes share a common breath envelope. This is musically distinct from full MPE (where each
note has independent pressure) but is the natural and often desirable behaviour with
windcontrollers and breath controllers. Profiles should document whether each macro source
is global (paraphonic) or per-note (truly polyphonic).

- User-editable profiles stored alongside presets in the app data directory
- Profile switching in the editor is a single tap / menu pick; routes are unaffected

**MIDI learn for macro bindings:**
In the macro binding panel, a "Learn" button per macro puts that macro in listen mode: the
next continuous MIDI message arriving (CC, Aftertouch, MPE Z/Y) is adopted as the binding.
A calibration blow or slider wiggle is enough. The learned CC number + range are stored in
the profile.

**Bidirectional (Exquis):**
The Exquis USB-MIDI sysex API exposes scale, pad layout, and LED color. A future integration
could write a layout matching the current preset's scale (via MTS-ESP) or read the active
Exquis config to pre-populate tuning. This is exploratory — the sysex protocol is
documented but the integration is non-trivial.

### DrawnQurve profile
- Vane-specific curve set: map DrawnQurve's curve outputs to Vane's ModMatrix routes
- Target the sweet-spot bookmarks as named curve destinations

### Dynamic tuning
- Hermode-style just-intonation: adjust MTS-ESP root or generate JI chord offsets on the fly
- Chord-aware intonation: when generating voices (Phase 5), tune them to pure intervals relative to the root
- MTS-ESP master integration: Vane as a tuning source, not just a client

### Modulation visualisation
- Display incoming CC curves in real time (CC2, CC11, CC74, pressure, pitchbend)
- Intensity indicator: composite "brightness/energy" readout across active modulation dimensions
- Useful both for performance feedback and for debugging expression maps

---

## Field notes — 2026-07-19 (Alex, queued; not scheduled)

### "Breathless" mode
A mode for velocity-only input (keyboards, pads — no breath/CC2 stream):
synthesize simple envelopes from note-on velocity so Vane is playable
without a wind controller. Today, no breath = silence by design (factory
routes put VCA on breath/expression/pressure; VelVCA defaults to 0).
Breathless mode would flip that stance per-patch: velocity shapes an
internally generated envelope (attack/decay derived from velocity, not a
generic ADSR panel) feeding the same slewed mod-source path breath uses —
the existing amplitude model stays the single truth, breathless just
feeds it a synthetic curve.

### Waveguide PhysMod — lab experiments
The nonlinear param interactions need a place to be explored
systematically rather than by slider luck. Known repro: certain **bore
damping** settings kill the sound entirely, and it only returns after a
significant rest (state in the waveguide loop decays past re-excitation?
needs instrumentation). Wanted: a lab/probe harness (the
`Tools/wasm/regression-test.mjs` pattern — fresh instance per case,
scripted param sweeps, rendered-output assertions) that can map stable vs
dead regions of the param space and pin the bore-damping kill as a
regression test.

### PhysMod param modulation (very small ranges, event-driven curves)
Experience so far: a tiny slider movement goes a long way, so modulation
must operate in **very narrow ranges** around the patch value. Idea:
route existing mod sources (note-on, breath/pressure/expression) through
**curves** into selected PhysMod params, with per-route depth measured in
fractions of the usable range (the ModMatrix + Slewer machinery is the
natural carrier — same slew/curve discipline as VCA/cutoff routes).
Start with one or two params after the lab work identifies which are
stable enough to move under modulation.

## Deferred / research

- Spectral / granular processing
- Convolution reverb that is excluded from the "no onboard effects" rule (debatable)
- CLAP format via `clap-juce-extensions`
- Microtonal chord generation (generated voices tuned to JI ratios of the played note)

---

## Notes on scope

Not every item above needs a button. The priority is that anything performatively meaningful is reachable from the standalone without opening a DAW. Preset recall, ModMatrix amounts, and the key timbral parameters (cutoff curve, resonance coupling, width) are the minimum viable panel. Everything else can live behind a tabbed or collapsible view.

The sweet-spot bookmark system is the connective tissue: it makes experimentation (finding good curves) directly usable in performance without manually recreating settings.
