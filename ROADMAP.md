# Vane — Roadmap

Design north star: a wind-first expressive instrument where intensity has multiple independent dimensions. A loud sine with a little noise can feel as present as a sawtooth through chorus. The goal is never a single linear loudness axis but a rich, performable space of timbral states.

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

### ModMatrix editor
- View all active routes: source, destination, amount, attack/release slew
- Add / remove / reorder routes at runtime
- Visual feedback of current modulation value per route (VU-style bar or arc)

### Parameter panel
- Expose the full parameter set in the standalone window — at minimum a compact one-page view
- Target: everything addressable without a DAW (Bitwig exposes params cleanly; Logic requires plugin-side UI)
- Consider which parameters benefit from knobs vs sliders vs toggles vs menus

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

### Learnable breath routing
- The mod matrix currently hardcodes CC2 as "breath", but controllers vary:
  Zefiro uses Aftertouch for breath, some setups use CC11 exclusively, others
  use non-standard CC numbers or MIDI channel pressure
- Add a "learn breath source" mode: Vane watches incoming MIDI, identifies
  the highest-bandwidth continuous message during a calibration blow, and
  stores that source ID as the breath route in the preset
- Routes save/restore the source identity (CC number, Aftertouch, channel
  pressure) so presets are portable across the original controller; a
  re-learn step handles a different instrument

### Controller profiles
- Store per-controller MIDI mappings: which CC/dimension carries breath,
  expression, pressure, and slide for a named instrument (Sylphyo, Zefiro,
  EWI, Exquis, Morph, etc.)
- Profile bundles with the preset: switching preset automatically re-binds
  the mod routes to the correct sources for that controller
- Bidirectional where the protocol allows: Exquis (USB-MIDI sysex) exposes
  scale, pad layout, and LED color — Vane could write a layout that reflects
  the current scale/preset, or read the active Exquis config to pre-populate
  tuning and voicing defaults

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

## Deferred / research

- Spectral / granular processing
- Convolution reverb that is excluded from the "no onboard effects" rule (debatable)
- CLAP format via `clap-juce-extensions`
- Microtonal chord generation (generated voices tuned to JI ratios of the played note)

---

## Notes on scope

Not every item above needs a button. The priority is that anything performatively meaningful is reachable from the standalone without opening a DAW. Preset recall, ModMatrix amounts, and the key timbral parameters (cutoff curve, resonance coupling, width) are the minimum viable panel. Everything else can live behind a tabbed or collapsible view.

The sweet-spot bookmark system is the connective tissue: it makes experimentation (finding good curves) directly usable in performance without manually recreating settings.
