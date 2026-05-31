# Vane — Design questions for upcoming work

For a design pass ahead of the UI-heavy roadmap items. Context first, then the
questions by area. See `Vane Design/roadmap.md` for priorities.

---

## Context (read me first)

**What Vane is.** A JUCE 8 **MPE + MTS-ESP + CC-first**, mono-capable synthesizer
built around continuous gestural expression (breath/pressure/slide controllers
like the Sylphyo, MPE surfaces, microtuning). Not a marketplace product — a
personal instrument. AU / VST3 / Standalone on macOS, **AUv3 on iPadOS**.

**The UI.** One self-contained file, `Source/WebUI/index.html` (~1300 lines:
HTML + CSS + vanilla JS + inline SVG), compiled into the binary and rendered in a
JUCE `WebBrowserComponent` (WKWebView on Apple). **No framework, no build step.**
The C++↔JS bridge is in `Source/WebUI/WebVaneEditor.cpp` (event names map to JS
`Bridge.on(...)` / `Bridge.send(...)`).

**Current structure.**
- **Header:** brand · preset nav (`‹ name ▾ ›`) · rig/controller pill · MTS-ESP
  chip · Mono/Poly · panic · theme toggle.
- **Stage (left):** view segments **Sources / Spectrum / Pitch** + a meters
  legend. Sources = per-note MPE traces + global CC streams; Spectrum = real FFT
  of the output; Pitch = pitchbend/slide.
- **Inspector (right):** tabs **Patch / Matrix / Presets**.
- **Modals:** Rig/Controller setup (instances + roles + channel + aux +
  diagnostics + profile bar), preset save/rename, import/export.
- **Visual language:** per-source colour coding (breath/expr/pressure/slide/
  bend/vel), light + dark themes.

**Two hard constraints.**
1. **Same UI on desktop (mouse) and iPadOS AUv3 (touch, smaller/shorter window,
   hosts like AUM).** We've already hit button-row overflow; modals can exceed
   the AUv3 view height.
2. **C++ strings must stay ASCII** (non-ASCII through `juce::String` traps on
   iOS). HTML/JS may use Unicode freely; C++-sourced labels may not.

What we want from design: **approaches and interaction models**, expressed so they
can be built in vanilla JS/SVG — not pixel comps.

---

## 1. Bézier mod-curves (Matrix) — UPCOMING

**Context.** The **Matrix** tab is a list of up to 24 modulation slots; each row
is `source → destination · amount · curve · atk/rel`. Today `curve` is a discrete
choice (Lin / Exp / S). We want to replace it with an **editable response curve**
per route.

**Questions.**
- How to present an editable per-route curve without overwhelming a 24-row list?
  (inline mini-curve thumbnail that expands · a popover on the row · a dedicated
  editor for the *selected* route?)
- Curve model + control: cubic Bézier with draggable handles? multi-segment with
  tension? How many control points keeps it expressive but simple?
- One interaction that works for **both** mouse and touch (no hover, no
  modifiers) for dragging control points.
- Should curves be **bipolar-aware** (pitchbend, ±1) vs unipolar (breath, 0..1)?
- Showing the curve *live* — the modulation value riding along it as you play.

Relevant: `renderMatrix()` and the slot model in `index.html`; `slotEdit` bridge.

## 2. Preset / rig / profile / wavetable management — UPCOMING

**Context.** Four related-but-distinct artifact types now exist: **presets**
(sound), **rigs/profiles** (controllers), **wavetables** (a content-hashed local
library), and the **preset↔profile association** (a preset can suggest its rig).
The Presets tab is a flat list with partly-stubbed tags/favourites.

**Questions.**
- IA: one unified browser, or separate surfaces per type? How do they relate
  visually (a preset *references* a rig + a wavetable)?
- Minimal-but-effective organisation for a *personal* instrument: tags? search?
  favourites? folders? (Avoid marketplace-scale complexity.)
- How to surface "this preset references a rig/table you don't have."
- Where does the rig/profile management live relative to presets?

Relevant: `renderPresets()`, the Rig modal (`renderControllers()`), the
preset↔profile banner, in `index.html`.

## 3. Wavetable storage, provenance & sharing (P2) — UPCOMING

**Context.** Tables are deduped in a local library (`~/Library/Vane/Wavetables/`,
content-hash keyed); presets reference them; DAW projects embed them. **P2** adds
per-table **license/source metadata** (public-domain / owned / copyrighted) and
**export-with-embed** for sharing, gated by license (PD/owned embed freely;
copyrighted → reference-only, "recipient needs the table").

**Questions.**
- How to capture a table's provenance/license with **minimal friction** (the user
  shouldn't have to tag every table; sensible defaults + a quick override)?
- Make the export choice clear and *safe*: "shareable" vs "local-only," without
  accidentally embedding a copyrighted table.
- Is a **library view** worth it, and what would it show (tables, which presets
  use each, license, size, a thumbnail/filmstrip)?

## 4. MTS-ESP / tuning surface — UPCOMING (Tier 1)

**Context.** Vane is **MTS-ESP-first**, but tuning is currently just a header chip
(`MTS-ESP · on` / `⚠ off · click to retry`). We want to add: the **active tuning's
name**, **internal/built-in tunings** (selectable when no MTS master is present),
and handling of keymap **"holes"** (pitches the tuning silences).

**Questions.**
- Where should tuning live, given it's core to the instrument's identity — an
  expanded header chip, a dedicated panel, a modal? How prominent?
- Presenting **MTS-ESP master vs internal tuning** (precedence, fallback,
  switching) without confusion.
- Communicating microtonal state to the *player* visually — a note bent off
  12-ET, or **silenced by a hole** — ideally in the Stage/Pitch view.

## 5. Wavetable section density — practical

**Context.** The Patch tab's Oscillator group has a dense **Wavetable** block:
name + live frame readout, a button row (**Built-in / Snap / Phase-align /
Load**), a live waveform SVG, a **filmstrip** overview SVG (click to scrub), and
the **Morph** slider (labelled "frame N / total"). It's a lot in one column.

**Questions.**
- Should the table *visualisation* (waveform + filmstrip) move to the **Stage**
  as a dedicated view, leaving controls in Patch? Or keep it together?
- Is the **Morph ↔ frame ↔ snap ↔ filmstrip-scrub** relationship discoverable?
- Could the WT display and the real **Spectrum** view be combined (see the frame
  *and* its spectrum together)?

## 6. iPad / touch / AUv3 — cross-cutting

**Context.** Identical UI on macOS (mouse) and iPadOS AUv3 (touch, smaller/shorter
window, in hosts like AUM). Sliders currently use Shift/Alt for fine control
(no modifiers on touch); modals can exceed the AUv3 height.

**Questions.**
- Touch-target sizing and slider interaction (fine control, typed entry) without
  hover or modifier keys.
- Layout reflow for **narrow/short AUv3** windows — priority order of what to
  show; should the Stage and Inspector stack?
- AUv3-specific UX: the host already provides transport/preset chrome — should
  Vane's header duplicate it or defer?

---

### Lower priority / optional
- The **Stage / Inspector** split and the three Stage views — still the right
  top-level structure as the feature set grew?
- Does the per-source **colour system** scale to the newer sources (aux 1–8,
  sync) and stay legible in both themes?
