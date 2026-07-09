# MiniSax — waveguide reed instrument lab (v0.1)

A minimal, deterministic, offline-renderable physical-modelling reed instrument
inspired by STK `Saxofony` and the Silverwood Tenor Sax Reaktor ensemble.
The target is not literal tenor sax emulation: it is an expressive,
breath-playable instrument lab with rigorous experiment traceability.
See `docs/` for the full design brief (architecture, implementation plan,
articulation testing, traceability model).

This is a **standalone subproject** — it builds without JUCE and without
anything from the Vane plugin. Phase 6 (plugin integration) comes only after
the offline pipeline has proven itself.

## Build

```bash
cmake -S MiniSax -B MiniSax/build -DCMAKE_BUILD_TYPE=Release   # from the repo root
cmake --build MiniSax/build -j
ctest --test-dir MiniSax/build          # unit tests
```

Requires CMake ≥ 3.22 and a C++17 compiler. The only third-party dependency
is the vendored single-header [nlohmann/json](https://github.com/nlohmann/json)
(`third_party/nlohmann/json.hpp`).

## Render and analyze

```bash
cd MiniSax
./build/minisax-render \
  --preset presets/breathy_001.json \
  --suite  tests/articulation_suite_001.json \
  --out    renders/breathy_001/

python3 tools/analyze_renders.py \
  --input renders/breathy_001/ \
  --out   analysis/breathy_001/
```

`minisax-render` writes one mono 16-bit WAV per test case (named after the
test id) plus `render_report.json` — preset id, model version, engine git
commit, suite id, per-test noise seed, parameters hash, peak, and warnings
(silence / clipping / non-finite samples). Optional flags: `--seed <uint32>`
(base noise seed, default fixed and recorded) and `--git-commit <hash>`
(otherwise `MINISAX_GIT_COMMIT` or `git rev-parse HEAD` is used).

`analyze_renders.py` writes one descriptor JSON per WAV and a `summary.csv`.
Descriptors: peak, RMS, attack time (10→90% of envelope peak), spectral
centroid, zero-crossing rate, autocorrelation pitch estimate, pitch stability
(cents std-dev), sustain amplitude variation, silence/clipping detection.
Provenance from `render_report.json` is merged into every report. With numpy
(+ optionally scipy) all descriptors are computed; with only the standard
library the spectral/pitch descriptors degrade to null and a warning is
recorded.

Renders and analysis output are gitignored; presets and suites are versioned.

## Signal path

```text
breath -> compressive pressure map (+ noise, growl, vibrato-air)
       -> reed reflection table (STK-style, clamped)
       -> mouthpiece junction
       -> fractional-delay bore (half-period; quarter-wave resonator)  <- feedback
       -> one-pole loss filter
       -> DC blocker -> bell high-shelf biquad -> output gain
```

Everything is deterministic: the per-test noise seed is derived from the base
seed and the test id (so reordering tests in a suite never changes audio) and
recorded in the render report.

## Articulation event semantics

| Event | Meaning |
|---|---|
| `noteOn` | Gate steps to 1; pitch steps to `note` (MIDI) or `pitchHz`. |
| `noteOff` | Gate steps to 0 (voice release smoothing ≈ 20 ms). |
| `control` | Breakpoint on the named `target` parameter. The value is **reached at** `time`; `curve:"linear"` ramps from the previous breakpoint of that target, otherwise it steps. Before the first breakpoint the preset value holds. |
| `pitch` | Retune **starting at** `time`. `curve:"linear"` glides over 60 ms (override per event with `glideSeconds`); no curve = instant retune (exposes delay-retuning artifacts, deliberately). |

The asymmetry is intentional: control ramps describe gestures spanning their
whole segment (a crescendo), while pitch changes are slur-like and local.

## v0.1 bring-up notes (found via the descriptor pipeline)

- **Octave bug**: the naive `delay = sr/f` mapping sounded an octave low —
  the reed end is a closed termination, so the loop needs **half** the period
  (as in STK Clarinet). Caught by the analyzer's pitch descriptor.
- **Speaking threshold / choke window**: with a linear breath→pressure map the
  reed table's usable window is inherently narrow — it speaks above
  `(1/R − offset)/(2|slope|)` and chokes near `(1 − offset)/|slope|`, a ratio
  of only ~1.7. v0.1 therefore drives the reed through a compressive map
  (`pressure = 1.30 · breath^0.38`), placing the speaking threshold near
  breath ≈ 0.22 with no choke up to full breath at default settings.
- Pitch runs ~15–25 cents sharp (loop-filter group delay); the planned pitch
  correction table (docs/ARCHITECTURE.md) is the proper fix.
- The spectrum is odd-harmonic (clarinet-like), as expected for a quarter-wave
  bore. Moving toward conical/sax behaviour (e.g. Saxofony's asymmetric
  two-segment delay) is future work.

## Layout

```text
Source/        DSP engine + JSON IO + renderer (small plain C++ classes)
cli/           minisax-render main
unit_tests/    assert-based tests (delay, reed, determinism, envelope, hash)
presets/       versioned presets with provenance (see schemas/)
tests/         articulation test suites
tools/         analyze_renders.py
schemas/       JSON schemas for presets and suites
docs/          design brief: architecture, plan, testing, traceability
experiments/   experiment logs (EXPERIMENT_TEMPLATE.md)
renders/       WAV output (gitignored)
analysis/      descriptor output (gitignored)
references/    reference samples (gitignored; see docs/ARTICULATION_TESTING.md)
```
