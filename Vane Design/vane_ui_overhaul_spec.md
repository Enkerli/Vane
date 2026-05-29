# Vane — UI Overhaul Spec (v1)

Brief for design generation **and** implementation. Decisions locked:
**WebView rendering · generic 24-slot mod matrix · whole-app design.**

Visual source of truth: the artboards in this folder
(`artboards/*.jsx`, `wireframe-styles.css`, `Vane Wireframes.html`).
This doc consolidates them into one app and pins the data model, interaction,
and cross-platform rules a design generator needs.

---

## 1. Goals & platforms

- One coherent single-page performance UI replacing the current native editor
  (`PluginEditor` + `PatchPanel` + `ModMatrixEditor`) and the clipboard-based
  preset "workaround".
- Targets: **macOS** (AU / VST3 / Standalone) and **iPadOS** (AUv3). Same web
  app on both; layout is responsive.
- Headline features: a **free mod matrix** (any source → any dest) with
  **precise + touch-friendly** controls, and a real **preset browser**.

## 2. Rendering & architecture

- **JUCE 8 `WebBrowserComponent`** with native integration, mirroring
  DrawnQurve's `Source/WebUI/WebCurveEditor`:
  - Resource provider serves a bundled web app (esbuild → `BinaryData`).
  - C++→JS: `emitEventIfBrowserIsVisible(id, var)`. JS→C++:
    `Options::withEventListener(id, …)`.
  - AUv3: navigate on `parentHierarchyChanged` (host attaches view late).
  - Fonts bundled via `juce_add_binary_data` (Inter, JetBrains Mono).
- **Mod-matrix backend = fixed pool of 24 generic slots**, each an APVTS
  param group so automation / preset persistence / host modulation all work:
  - `slotN_enable` (bool), `slotN_source` (choice), `slotN_dest` (choice),
    `slotN_amount` (float −1..+1), `slotN_curve` (choice: Lin/Exp/S),
    `slotN_atk` (ms), `slotN_rel` (ms). N = 0..23.
  - Default patch pre-fills slots to match today's defaults (breath/expr→VCA,
    slide/breath/expr/pressure→cutoff, etc.). Empty slots = disabled.

### Bridge contract (initial)
- C→JS events: `paramChanged{id,value}`, `meters{breath,expr,pressure,slide,bend,vel}`
  (~30 Hz), `presetList`, `presetChanged`, `tuningStatus`, `controllerLabel`.
- JS→C events: `uiReady`, `setParam{id,value}`, `slotEdit{slot,field,value}`,
  `presetSave{name}`, `presetLoad{id}`, `presetDelete{id}`, `panic`, `reconnectMts`.

## 3. Design system

Use `wireframe-styles.css` tokens verbatim. Light (default) + dark variants.

- Source colors (consistent everywhere — dots, meters, route accents):
  Breath `#4b86c7` · Expression `#2d9d8a` · Pressure `#d28330` ·
  Slide `#a35bbf` · Pitchbend `#c39529` · Velocity `#7a4cff`.
- Type: Inter (UI), JetBrains Mono (numeric readouts).
- Surfaces: warm `--vn-bg` / `--vn-panel`; rounded panels; thin `--vn-line`.

## 4. App layout (single page)

```
┌ Header ─────────────────────────────────────────────────────────────┐
│ preset ‹ name ›  |  controller chip · tuning chip  |  mono/poly  panic │
├──────────────────────────────────┬──────────────────────────────────┤
│ STAGE (left, ~1.5fr)             │ INSPECTOR (right, ~1fr)           │
│  • Visualiser (scrolling per-    │  Tabbed: [Patch] [Matrix] [Presets]│
│    source history)               │                                    │
│  • Live source meters            │  Patch  → osc/filter/perf sliders  │
│                                  │  Matrix → 24-slot free route list  │
│                                  │  Presets→ browser w/ filters       │
└──────────────────────────────────┴──────────────────────────────────┘
```

Reference artboards: `main-view.jsx` (MainViewA), `visualiser.jsx` (VisA),
`mod-matrix.jsx` (**MMStack = the chosen 02-B**), `presets.jsx` (PresetA),
`controllers.jsx`.

## 5. Data model

**Sources** (matrix + meters): Breath (CC2), Expression (CC11),
Pressure (MPE Z), Slide (CC74), Pitchbend (PB), Velocity.

**Destinations** (matrix): VCA Level, Filter Cutoff, Filter Reso, Pitch (fine),
Osc Morph, Osc PW, Osc Fold, Osc Noise, Osc Inharm. (Future: Unison Detune,
Glide Time.)

**Patch params** (Patch tab), with ranges/units for typed entry:
- Osc: Morph 0–3 (Sine→Tri→Sqr→Saw), PW 0.5–0.999, Fold 0–1, Inharm 0–1,
  Noise 0–1, Noise Type {White,Pink,Brown}, Detune ±100 ¢.
- Filter: Cutoff 20–20000 Hz, Reso 0–1, Mode {LP,BP,HP}.
- Perf: Output 0–1, Vel→VCA 0–1, Glide 0–2000 ms, Glide Mode {Fixed Time,
  Fixed Rate}, Glide Curve {Linear,Exp,RC}, Mono on/off, Master Tune ±100 ¢,
  PB Range MPE 1–96 st, PB Range non-MPE 1–96 st.
- Macro bindings: Breath src {CC,AT,MPE Z} + CC#, Expr src {CC,AT} + CC#.

## 6. Components & interaction

**Every continuous control (patch sliders + route amounts) must support:**
1. **Coarse drag**.
2. **Fine drag** (slow-drag, or modifier on desktop) for sub-step precision.
3. **Typed entry**: double-tap / double-click reveals an inline numeric field
   with units; Enter commits, Esc cancels. *(Directly addresses the "precision
   / fine tuning" pain point — the Bitwig-like win.)*
4. **Stepper** affordance (±) where space allows.
- **Touch targets ≥ 44 pt** on all interactive elements (the second pain point).

**Mod-matrix route row** (MMStack): source dot+label+CC (picker) → arrow →
dest (picker) → amount slider −1..+1 with mono readout → curve segmented
{lin · exp · S} → atk/rel mono readouts → live source meter → enable + delete.
Footer "+ New route" picks source/dest into the next free slot. Empty state:
dashed "add a route" affordance. Filters: All / Active / By source.

**Preset browser** (replaces clipboard copy/paste): searchable list with
sidebar filters (controller, tag, scale, favorites), per-item tags + tiny
breath-curve glyph, Save current / rename / delete / favorite. Keep an
import/export-text escape hatch for portability.

## 7. Responsive / cross-platform

- **Desktop window** (resizable, ~900×600+): two-column as above.
- **iPad AUv3** (portrait & landscape): single column stack — Header, Stage,
  then the active Inspector tab full-width; larger controls; bottom tab bar.
- Breakpoint on available width; the same React tree, CSS-driven.
- Respect safe areas; no hover-only affordances (touch has no hover).

## 8. States to design

- Empty mod matrix (no active slots) · matrix near full (scrolling).
- Meters idle vs active · MTS-ESP connected vs disconnected.
- Preset: unsaved-changes, naming/rename, empty library.
- Light & dark themes.

## 9. Validation (how we test design + UI)

- React app runs in a **plain browser with a mock bridge** (simulated
  param/meter/preset streams) → fast iteration, no plugin rebuild.
- Visual diff against the existing HTML wireframes (source of truth);
  component snapshots; devtools iPad sizes for the responsive layout.
- Manual: macOS AU/VST3/Standalone + **iPad AUv3** (untested since the revert).

## 10. Build phasing

1. WebView shell (bridge, resource provider, CMake/esbuild, fonts); port the
   Patch + meters screen to prove the pipeline on macOS & iPad.
2. 24-slot generic mod-slot backend (DSP/state) with default-patch parity.
3. Free mod-matrix UI on the new backend (headline).
4. Preset browser replacing the clipboard workaround.
5. Polish: visualiser, theming, accessibility, AUv3 hardening.
