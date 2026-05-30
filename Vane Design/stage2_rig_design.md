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
   - **AUv3 (iOS, AUM) — MEASURED (corrected).** Ran the probe in AUM:
     - Path A: channels come through cleanly — playing a Sylphyo (MPE) shows
       ch 1–14 (manager + members), ~130–165 events/s. **Channel/zone
       discrimination is reliable.**
     - Path B: `getAvailableDevices()` **does** list real hardware by name —
       `"Exquis"`, `"Sylphyo"` appeared alongside `"AUM"`. (An earlier run showed
       only `"AUM"` because the routed sources were *virtual/in-host*
       — DrawnQurve + AUM's keyboard — which collapse to the host name.) So:
       **hardware MIDI endpoints ARE enumerable by name from inside the AUv3.**
       In-host/virtual sources appear as the host.
     - **But enumeration ≠ attribution.** Seeing `"Exquis"` in the list doesn't
       tag the *host's merged stream* — those messages still arrive channel-only.
       Per-message device identity on iOS needs either (a) opening the named
       source directly via CoreMIDI, or (b) virtual ports (below).
     - **Virtual ports — tested, FAILED under AUv3.** Vane tried to publish
       "Vane Notes" / "Vane Mod" via `MidiInput::createNewDevice`; both came back
       **`NOT created`** (createNewDevice returned null) inside the AUv3 extension
       in AUM. So an AUv3 extension can *enumerate* sources but **cannot create
       CoreMIDI virtual endpoints** here (sandbox). The "multi-port" AUv3 plugins
       likely declare AU MIDI *outputs* (`MIDIOutputNames`) or create ports from
       their container app, not extension-created virtual inputs. Standalone /
       desktop is expected to succeed (createNewDevice works there) — untested
       control. The port==role *model* is still attractive; the *mechanism*
       isn't available to us as an AUv3.
     - **Direct-open — WORKS (measured).** `MidiInput::openDevice` from inside the
       AUv3 opened the Sylphyo and counted `direct 1655` while playing. So an AUv3
       **can open named CoreMIDI sources directly and read them, tagged.**
       Per-device identity on iPad is real — for *CoreMIDI endpoints*. (AUM,
       Network, other apps' virtual ports opened too but read 0 — nothing was
       flowing through them, not a failure.)
     - **The CoreMIDI-endpoint vs AU-port distinction (the crux).** Vane's source
       list sees hardware (Sylphyo/Zefiro), Network, and other apps' *virtual*
       ports — but NOT AU-declared MIDI ports (Progressions' Bass/Block/Arp/Strum
       Out = Port 1–5; PolyPipe A..MS; Polythemus 1–8). Those exist only inside
       the host's routing graph and reach us via the merged AU-input stream.
       How those plugins expose them: multiple MIDI *outputs* via the AUv3
       `midiOutputNames` property + `MIDIOutputEventBlock` (cable-tagged) — NOT
       CoreMIDI virtual endpoints, and **not surfaced by JUCE** (wrapper-level
       work). Multiple named *inputs* are typically container-app CoreMIDI
       virtual destinations (not the extension) or UMP groups.
   - **The clean long-term mechanism: MIDI-CI Discovery (MIDI 2.0).** A device
     announces manufacturer/family/model IDs; that maps directly to a Manifold
     entry → true semi-automated pairing, cross-platform. Aligns with Manifold's
     `bidirectional` / MIDI-CI roadmap. Far horizon, but the *right* answer.

   **Matching ladder for 2b** — settled, evidence-backed:
   - **Channel / MPE zone** — the cross-platform floor; works everywhere incl.
     AUv3/AUM (measured). What 2a's instance `match` is built on.
   - **Device name via enumeration/direct-open** — works on desktop AND AUv3 for
     CoreMIDI endpoints (hardware). Measured: Sylphyo opened, `direct 1655`.
     Use for **identity / pairing suggestions, NOT the audio path** — consuming
     the direct stream double-delivers (the host also routes it). So enumeration
     says "Sylphyo is here → suggest the Sylphyo rig"; the notes still arrive via
     the host stream on their channel. No double notes.
   - **MIDI-CI model id** — the future, cross-platform, true auto-pairing.
   - **NOT available to us:** Vane-published virtual ports (AUv3 `createNewDevice`
     fails); receiving AU-declared ports like "Progressions Bass Out" directly
     (not CoreMIDI endpoints — only the host's merged stream + channel).

   **Net for 2a:** build instance `match` on **channel / zone** (universal). A
   **device-identity layer** (enumeration/direct-open; desktop + iPad hardware)
   drives *pairing suggestions* on top — never the note path. Host-routed AU
   generators are distinguished by channel. On iPad the sequencer+wind split
   means distinct channels (wind on its MPE zone, sequencer outside it) — how
   MPE rigs are wired anyway. This fully specifies 2a; nothing here blocks it.

   **Flagged (deeper, optional):** letting a user route "Progressions Bass Out →
   Vane's Bass input" by intent needs multiple named *input* ports — no clean
   JUCE/AUv3 path (container-app virtual ports, or AUv3 wrapper work to expose
   `midiOutputNames`-style multi-port). Park it; channel/zone covers it today.
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
