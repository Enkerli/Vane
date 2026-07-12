# Implementation Plan

## Phase 0: Repository and schemas

- Create project structure.
- Add JSON schemas for presets and test suites.
- Add sample preset and articulation suite.
- Add experiment log template.

## Phase 1: Offline rendering skeleton

- Add `minisax-render` CLI.
- Parse preset JSON.
- Parse test suite JSON.
- Render placeholder sine/saw/noise output per test case.
- Write WAV files.

This validates the pipeline before the physical model exists.

## Phase 2: Minimal waveguide engine

- Implement `FractionalDelay`.
- Implement `ReedNonlinearity`.
- Implement `BoreModel` feedback loop.
- Implement simple damping and bell filter.
- Add deterministic noise.
- Replace placeholder oscillator with MiniSax engine.

## Phase 3: Articulation gestures

Support test events:

- note on/off
- pitchHz or MIDI note
- breath envelope points
- embouchure envelope points
- pitch-bend envelope points
- growl/vibrato enable windows

## Phase 4: Analysis pipeline

- Analyze WAVs into per-test JSON.
- Generate summary CSV.
- Add warnings for silence, clipping, excessive pitch drift, and unstable sustain.

## Phase 5: Comparison reports

- Compare two render directories.
- Compare render descriptors to reference descriptor profiles.
- Generate simple markdown or HTML report.

## Phase 6: Plugin integration

Only after phases 1-5 are working:

- Wrap engine in JUCE.
- Map CC2 / MPE pressure to breath.
- Map pitch bend or MPE slide to embouchure/pitch.
- Add preset browser.
- Add render/export debug command if possible.

## Definition of done for v0.1

- `minisax-render` renders at least 6 WAVs from one preset.
- `analyze_renders.py` outputs descriptors and summary CSV.
- Presets are versioned and include provenance fields.
- One parameter change is traceable through preset diff, render diff, and descriptor diff.
