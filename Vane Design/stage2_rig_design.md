# Stage 2 — Rig as composition (design sketch)

Decision locked: **per-instance role mask**. Open decision: how a rig relates to
the catalog + existing flat profiles (options A/B/C at the end — pick one).

This is a sketch to agree the shape *before* reshaping `ProfileManager`. No code
yet.

---

## The model

A **rig** is a named, ordered list of **controller instances** plus rig-level
settings. (Today's "profile" = a one-instance rig.)

```
Rig {
  name:        "Zefiro + Exquis"
  instances:   [ Instance, … ]
  mono:        bool?            // rig-level default (winds → true)
  // (later) tuning/scale ref?
}

Instance {
  ref:      "Sylphyo"          // catalog/Manifold identity (see A/B/C)
  mode:     "mpe" | "standard" // one of the controller's Manifold `modes`
  match: {                     // how we recognise THIS instance's MIDI
    port:     "Sylphyo Link"   // input device name; null = any
    channels: "2-16" | "1" | "omni"
  }
  roles: {                     // ── the per-instance role mask ──
    notes:  true|false         // contributes sounding pitches (note-on/off)?
    mods:   { Breath, Pressure, Slide, Pitchbend, Velocity, Aux0..Aux7 }  // subset
  }
  bindings: {                  // this instance's global-source CCs
    breathCC: 2,  exprCC: 11,  aux: [{label,cc}, …]
  }
}
```

### The cases this expresses
- **Multiple profiles per controller** — two rigs reference `Sylphyo` with
  different `mode` / `bindings` (CC2-breath vs aftertouch-breath). Same catalog
  entry, different frozen choice.
- **Zefiro + Exquis** — one rig, two instances; both `notes:true`, mods merged.
- **Sequencer notes + wind mods** — Instance A `{ref:Keystep, roles:{notes:true,
  mods:{}}}`, Instance B `{ref:Sylphyo, roles:{notes:false,
  mods:{Breath,Pressure,Pitchbend}}}`. The wind's note-ons are dropped; its
  breath/pressure modulate the sequencer's notes.

---

## How it maps onto what exists (key insight)

We do **not** rip out today's flat binding params (`macroBreathCC`, `aux*_cc`,
`auxLabel*`). The rig is an **authoring/storage layer that compiles down** to
them:

- **Single-instance rig** → just populates the existing flat params on load.
  Engine + routing unchanged. (This is literally today's behaviour, re-housed.)
- **Multi-instance rig** → a new **MIDI router** sits in front of the synth:
  matches each message to an instance by `(port, channel)`, applies the role
  mask (accept note only if `roles.notes`; route a CC to its mod source only if
  that dim ∈ `roles.mods`), then feeds the *same* mod sources the engine already
  reads. The engine downstream doesn't change.

### Storage
APVTS params are flat and fixed-count — wrong tool for a variable list of
instances. The rig lives in the **state ValueTree** as a `VaneRig` subtree with
child `Instance` nodes (this is how presets already carry non-automatable state).
`ProfileManager` → `RigManager`: reads/writes that subtree as XML under
`~/Library/Vane/Profiles/` (keep `.vaneprofile`). The active rig still *projects*
onto the flat params so nothing downstream breaks.

---

## Phasing

- **2a — schema + storage + UI** (no engine router yet). RigManager reads/writes
  nested rigs; single-instance compiles to flat params; old flat profiles
  migrate to 1-instance rigs. Controller setup becomes a small rig editor (add/
  remove instances, pick mode, edit role mask + bindings). Multi-instance merges
  *naively* (all sources summed, all notes accepted) — already delivers
  multiple-profiles-per-controller and multi-controller source combining.
- **2b — router + roles** (engine). Per-message `(port, channel)` matching
  enforces the role masks. Requires a stable per-voice id (channel/voice index)
  in the `voices` payload — **which also fixes the Sensel same-note display
  collision** flagged earlier. This is the deepest, net-new piece.

The role mask is in the schema from 2a (stored), enforced in 2b.

---

## Open questions
1. **MIDI source identity per platform** (drives instance `match` + semi-auto
   pairing). What a plugin can actually know about *which device* sent a message:
   - **Standalone (JUCE opens devices directly):** full device names via
     `MidiInput::getAvailableDevices()`, and we can keep per-device callbacks →
     reliable identity. Semi-automated pairing works here *today*: match a
     device name against Manifold slugs/aliases → suggest the rig.
   - **AU / VST3 (desktop, in a DAW):** MIDI usually arrives as one *merged*
     stream; the plugin does **not** inherently know the source device. VST3 has
     multiple event input *buses* and AUv3 can expose multiple MIDI input ports
     (this is what AUM shows / routes), but within a bus the stream is merged.
     So identity = *which port/bus* the host routed to us, not a device name —
     only as granular as the host's routing.
   - **AUv3 (iOS, AUM) — MEASURED.** Ran the probe in AUM with a Sylphyo (MPE):
     - Path A: channels came through cleanly — idle = ch 1; playing = ch 1, 5–10
       (manager + MPE member channels), ~165 events/s. **Channel/zone
       discrimination is reliable.**
     - Path B: `MidiInput::getAvailableDevices()` returned exactly **one** source,
       `"AUM"`. The host owns the hardware and re-presents itself as the single
       MIDI source — **device names are NOT visible to us inside the AUv3
       sandbox.** So on iOS, identity bottoms out at channel.
     - **Implication:** true per-source identity on iOS needs Vane to publish its
       own **named CoreMIDI virtual input ports** ("Vane Notes" / "Vane Mod");
       the user routes each source to a port in AUM and we read them tagged by
       port. That's the v2 experiment (Path-B *creation* + double-delivery check).
   - **The clean long-term mechanism: MIDI-CI Discovery (MIDI 2.0).** A device
     announces manufacturer/family/model IDs; that maps directly to a Manifold
     entry → true semi-automated pairing, cross-platform. Aligns with Manifold's
     `bidirectional` / MIDI-CI roadmap. Far horizon, but the *right* answer.

   **Matching ladder for 2b** (best available wins, manual always works), now
   evidence-backed:
   - **Channel / MPE zone** — the cross-platform floor. Works everywhere incl.
     AUv3/AUM (measured). This is what 2a's instance `match` is built on.
   - **Device name** — desktop standalone bonus (JUCE opens devices directly);
     enables semi-auto pairing *there*. Not available under AUv3.
   - **Vane-owned virtual ports** — the iOS route to per-source identity (v2).
   - **MIDI-CI model id** — the future, cross-platform, true auto-pairing.
   Store whatever matched so it's sticky. Pairing is always a *suggestion*
   ("saw 'Sylphyo Link' → apply the Sylphyo rig?"), never silent.

   **Net for 2a:** build instance `match` on **channel / zone**. On iPad, the
   sequencer+wind split means putting them on distinct channels (or the wind on
   its MPE zone, the sequencer on a single channel outside it) — which is how
   MPE rigs are wired anyway. Device-name and virtual-port matching layer on
   later without changing the schema (they just populate the same `match`).
2. **Preset ↔ rig.** Presets already store `profileName`; it becomes `rigName`.
   The suggest-banner logic carries over unchanged.
3. **Where does mono live** — rig-level default vs patch param? Current auto-mono
   is per-controller; a rig could carry it (winds → mono) but a patch can still
   override. Lean: rig sets it on *activation*, patch param wins thereafter
   (same one-directional rule we just shipped).
4. **Catalog dependency** — the A/B/C choice below.

---

## DECISION NEEDED — rig ↔ catalog relationship

**A. Rig wraps catalog refs (structured).** `instance.ref` points at the catalog/
Manifold entry; `mode` + `bindings` layered on top. Old single-controller
profiles auto-migrate to a 1-instance rig.
- ＋ Clean, Manifold-aligned, easy re-seeding and future YAML import.
- − A controller not in the catalog needs a "Custom" fallback ref.

**B. Rig as free binding set (flat).** A rig = N binding sets merged; the catalog
is only used to *seed* at creation, never referenced afterward.
- ＋ Zero catalog dependency; trivial migration (today's profile = 1 set).
- − Loses the mode/identity link; can't re-seed or update from Manifold later.

**C. Hybrid (recommended).** Each instance stores **both** a soft catalog ref
(`slug@version`, advisory — for labels/re-seeding) **and** a self-contained
binding + role snapshot (authoritative — works even if the catalog entry changes
or is missing).
- ＋ Robust *and* Manifold-aware; mirrors the "reference + override layer" idea
  from the Stage-1 discussion.
- − Slight duplication (a ref plus its resolved snapshot).

Recommendation: **C**. It's the only one that survives both a missing catalog
entry and a later Manifold update without losing the user's frozen choices.
