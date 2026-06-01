# Vane — Wavetable library browser (plan)

An **in-app wavetable browser** — the robust, iOS-friendly answer to loading
(the AUv3 document picker is fragile; a curated factory library + an in-app list
needs no picker). Backed by the curated PD/CC0 collection at
**github.com/Enkerli/vane-wavetable-library**.

---

## What we're working with

The library repo is a serious curation project (not just files):
- **Format:** "sorted" tables are **2048-sample frames**, N frames each (violin =
  14), **no `clm` chunk** → they slice correctly under Vane's existing default
  loader *today*. Each AKWF table is single-cycles **sorted into a morph
  trajectory** (e.g. dark→bright by spectral centroid).
- **All PD / CC0** → bundlable, shareable (ties to storage P2 license tags).
- **Metadata** (`metadata/tables.schema.json` + per-table `.json` sidecars):
  `id, title, source, license, family, tags, curationStatus`
  (source/candidate/auditioned/curated/rejected), `frameCount, frameLength`,
  **`morphIntent`** (brightness_ramp / fold_growth / asymmetry_growth /
  noise_growth / formant_shift / pulse_motion / digital_decay / hybrid),
  `analysis` (centroid/rms/roughness ranges, continuity/monotonicity/uniqueness/
  vaneQuality scores), `performance` notes.
- **Buckets:** `source/` · `curated/{harmonic_growth,brightness,fold,…}` ·
  `generated/`. Tools to reorder-by-brightness, generate harmonic-growth, analyze.
- User's current Sylphyo favourites: AKWF violin / oboe / pluckalgo / stringbox /
  bw_perfectwaves (`curated`-worthy). Others "salvageable by removing a frame or
  three" (curation-side, in the repo).

## Architecture: three tiers

1. **Factory library (bundled).** A curated subset compiled into `BinaryData`
   (like `Assets/HarmonicStack.wav`) + a compiled `library.json` manifest. Always
   present, zero file access, **works in AUv3/iOS** — the whole point. This is
   the default browser content.
2. **User library (local folder).** `~/Library/Vane/Wavetables/` (already exists,
   content-hashed). Desktop users drop tables here / loaded files land here; the
   browser lists them too. On iOS, an app-Documents import folder (Files-app
   visible via `UIFileSharingEnabled`) — no document picker needed.
3. **Remote (later).** Optional fetch/sync of new curated tables from the library
   repo (network; desktop-first).

## Metadata → Vane

Align with the repo's schema; don't reinvent it. A build step compiles the
**chosen factory subset** into `library.json` (id, title, family, tags,
morphIntent, frameCount, license, + a tiny precomputed filmstrip for instant
thumbnails). Vane reads that manifest; no JSON-schema validation at runtime.
The morphIntent/family/tags drive **browser filtering**.

## The browser UI (reuses what we have)

- A list/grid of factory + user tables, grouped by **family** / filterable by
  **morphIntent** + **tags**, with **search**.
- **Filmstrip thumbnail** per table (we already render filmstrips — precompute a
  tiny one per factory table at build time; render user tables on demand).
- **Audition**: select → `loadWavetableFromData` (already the iOS-safe entry
  point) → plays immediately; the existing live-frame + filmstrip + Morph "frame
  N" show the trajectory.
- Replaces "Load .wav…" as the primary path on iOS; "Load .wav…" stays on desktop
  for arbitrary imports.
- Surfaces **license = PD/CC0** (shareable badge) — feeds storage P2.

## Loader notes

- Factory + AKWF-sorted tables are 2048/frame, no `clm` → current detection is
  correct. Keep the `clm` path for Serum/Vital imports; allow the manifest's
  `frameLength` to override for bundled tables (belt-and-suspenders).
- "Salvage by removing frames" / generation are **curation-side** (repo tools).
  A future in-app frame-trim is possible but out of scope.

## Phasing

- **P1 — Factory browser.** Compile a curated subset + manifest into BinaryData;
  build the browser list with filmstrip + filter; audition via the iOS-safe
  loader. *Delivers iOS loading without the document picker.*
- **P2 — User library + import.** Browse the local library folder; iOS
  Files-app import folder; license badges (PD) → storage P2 sharing.
- **P3 — Remote sync.** Pull new curated tables from the library repo.

## Build pipeline

A script (mirroring `Assets/gen_harmonic_stack_wav.py`) pulls the chosen tables
from the library repo (git submodule, or a pinned fetch of `curated/`), emits the
`.wav`s + `library.json` + precomputed filmstrips into `Assets/library/`, and
`juce_add_binary_data` compiles them in. The repo's `tools/` already produce the
tables; Vane just selects + compiles.

Open question for the user: **git submodule the library repo, or a pinned
fetch-and-compile?** (Submodule = reproducible + easy updates; fetch = lighter
checkout.)
