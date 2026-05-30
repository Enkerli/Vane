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
1. **Port identity on iPadOS.** AUv3 may not expose input *port* names the way
   the desktop standalone does. Fallback: match on channel only, or a
   user-labelled "source slot." Affects how robust the sequencer+wind split is
   on iPad.
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
