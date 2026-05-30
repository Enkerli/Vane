# Notes for Manifold — from Vane's side

Working notes captured while wiring Vane's controller profiles to Manifold
(`github.com/Enkerli/manifold`). Vane is the first consumer; this is feedback
from actually trying to *use* the data, plus a few things Vane can contribute
back. Manifold is built in a separate chat — carry these over there.

---

## What Vane brings to the gig

Manifold documents what controllers *send*. Vane is the runtime that *consumes*
it and, crucially, can *measure* it:

1. **Ground-truth CCs via MIDI-learn.** Several entries have `cc: null` /
   "TODO: research" for the interesting sources — Zefiro Pro accelerometer X/Y/Z,
   Exquis push-encoders. Vane already captures CCs by learning. We can feed the
   captured numbers (and observed `update_rate`) straight back into Manifold.
   Vane → Manifold is a measurement loop, not just a read.
2. **`update_rate` / `resolution` in the wild.** The schema has fields for these
   and most are `null`. Vane sees the real message stream; it can report
   measured rates per source (e.g. Sylphyo breath ≈ N msg/s over USB vs BLE).
3. **A reason for the `mappings.<synth>` namespace to exist.** Vane is the proof
   that the shared factual core + a per-synth mapping layer is the right shape.
   `mappings.vane` round-trips into a real profile today.
4. **`prescriptive` / `vane-native` validation.** Controllers developed in-house
   get a profile that says what they *should* send; Vane is where that gets
   exercised.

---

## The "rig" concept (lives in Vane, not Manifold)

A clean division of labor fell out of the integration:

- **Manifold owns** per-controller capability facts: the *option space*. One
  file per device, with `modes` and `configurable` flags enumerating what it
  *can* do.
- **Vane owns** the **rig** (we've been calling it a *profile*): a frozen
  *selection* over that option space, possibly spanning **several** controllers,
  with per-source **roles**.

A rig = an ordered list of controller-instances, each:
`{ manifold_ref: slug@version, mode: <mode id>, channel/zone, role_mask }`

This is what makes the three things the user cares about expressible:

- **More than one profile per controller.** Sylphyo-breath-on-CC2 and
  Sylphyo-breath-on-aftertouch are two *rigs* pointing at the same `sylphyo.yaml`,
  differing only in selected `mode` + which `expression.breath.source` variant
  is live. Manifold already models this correctly as `modes` + `configurable`
  rather than separate files — good. The downstream rig is where a *choice*
  gets frozen. Worth a one-line note in SCHEMA.md: "a host 'profile' = controller
  × mode × config-choice; one file backs many."
- **More than one controller in a setup.** Zefiro + Exquis = a rig listing both
  files. Manifold shouldn't grow a "rig"/"setup" object — keep it per-device.
- **MIDI-FX as a controller, with role split.** Sequencer (notes only) + wind
  controller (modulation only) is two instances with complementary role masks.
  Manifold's `software` category already covers the sequencer side
  (`keystep`, `touchosc`); the role/arbitration logic is purely Vane's.

---

## Schema / content suggestions

### 1. Drop "expression" as if it were universal; name the real common one
Almost no controller has a dedicated "Expression" control. What *is* nearly
universal on keyboard-style controllers is the **mod wheel (CC1)** — original
MIDI-standard, present on Keystep, Alesis V-mini, basically every synth keyboard.
Suggest a small **named-source registry** (conventional source → default CC) so
consumers can seed sensibly:

| Named source | Conventional CC | Where it shows up |
|---|---|---|
| Mod wheel | CC1 | Keyboards, synth controllers |
| Breath | CC2 | Wind controllers (CC11 as the mirror/alt) |
| Expression pedal | CC11 (or CC4) | Pedals — see FCB-1010 / FCB-01 |
| Sustain | CC64 | Keyboards |
| Slide / timbre | CC74 | MPE |

`expression` as an expressive *dimension* in the schema is fine; the point is
it's rarely a *dedicated control* — don't surface it as a default row.

### 2. Overlays for surface controllers (Sensel Morph)
Sensel overlays are swappable physical layouts (Piano, Drum, **Buchla Thunder**,
Innovator's). An overlay redefines what the surface sends — it's effectively a
**sub-controller / layout** of the same device. And it gets nicely complicated:
some overlays (Thunder) have an **on-overlay button that switches sub-layouts**,
so one overlay carries several send-maps. Still not *complex* — just nested.

Suggested shape: add an optional `overlays:` list to surface controllers, each
overlay being a mini-profile (its own `sends`/`expression`), and optionally
`layouts:` within an overlay for the on-device switch. Downstream, each
overlay (or layout) maps to one Vane rig.

### 3. Keyboards & synths as controllers
Keyboard synths are also controllers and deserve entries — the **Arturia
MicroFreak** is a great example because it *sends and receives* more than most
non-MPE synths (aftertouch + mod strip out; lots in). Good `bidirectional: true`
candidate. Others: Alesis V-mini, Keystep family (already in).

### 4. Document the `null` CCs (with Vane's help)
`exquis` encoders and `zefiro` Pro accelerometer are the most *interesting*
sources and currently undocumented. These are config-dependent, so the honest
value may be "learnable, varies by config" rather than a fixed number — but a
typical/default value + a note is more useful than `null`. Vane's MIDI-learn
can supply observed defaults.

### 5. Pedals (for when we get to the FCB-01)
An `expression pedal` is a real, common continuous source (CC11/CC4), and pedal
controllers (FCB-1010, the FCB-01) are worth a `cc-only` / `expressive` entry.
Flag for later — will be fun.

---

## Status of the Vane side (Stage 1, done)
- A hand-seeded **controller catalog** in the Vane UI mirrors Manifold facts
  (Sylphyo std/mpe, EWI, Zefiro Pro, Exquis, Sensel Morph + Thunder, Keystep,
  MicroFreak, Generic MPE). Picking a controller seeds the Breath CC + Aux
  labels/CCs and shows the controller's notes.
- Honest CCs only: documented numbers are seeded (breath CC2, mod wheel CC1);
  config-dependent ones (encoders, accel) seed a label + 0 with a "Learn" prompt.
- Next: a real **Manifold→Vane exporter** replaces the hand-seeded catalog
  (YAML → the JSON the UI reads), then Stage 2 (rig = composition over N
  controllers) reshapes the profile store.
