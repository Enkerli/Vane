# Suggested First GitHub Issues

## Issue 1: Create offline render CLI skeleton

Build `minisax-render` that loads a preset JSON and articulation suite JSON, then writes one WAV file per test. For this issue, placeholder audio is acceptable.

Acceptance criteria:

- CLI accepts `--preset`, `--suite`, and `--out`.
- Creates output directory.
- Writes one WAV per test case.
- Names files after test ids.
- Does not require a GUI or DAW.

## Issue 2: Implement parameter loading and validation

Load all required MiniSax parameters from JSON and clamp normalized values.

Acceptance criteria:

- Missing required fields produce clear errors.
- Unknown fields are preserved or ignored explicitly.
- Parameter hash is generated and included in render metadata.

## Issue 3: Implement minimal FractionalDelay

Create a linear-interpolated fractional delay line with unit tests.

Acceptance criteria:

- Supports dynamic delay length.
- No out-of-bounds access.
- Deterministic output.
- Tests cover integer and fractional delay values.

## Issue 4: Implement basic reed nonlinearity

Create a stable pressure-controlled nonlinear valve.

Acceptance criteria:

- Inputs: mouth pressure, bore pressure, aperture, stiffness, embouchure.
- Output: flow.
- No NaN/Inf across normalized parameter ranges.
- Unit test sweeps parameter grid.

## Issue 5: Implement MiniSaxVoice v0.1

Connect breath pressure, reed, bore delay, loss filter, bell filter, and output.

Acceptance criteria:

- Can render non-silent tones for starter suite.
- No clipping by default preset.
- No NaN/Inf.
- Deterministic with fixed random seed.

## Issue 6: Add analysis pipeline

Wire `tools/analyze_renders.py` into docs and CI/local scripts.

Acceptance criteria:

- Produces JSON per WAV.
- Produces summary CSV.
- Reports silence and clipping warnings.

## Issue 7: Add experiment workflow docs

Document how to create a preset variant, render it, analyze it, and record notes.

Acceptance criteria:

- Includes exact commands.
- Uses `experiments/EXPERIMENT_TEMPLATE.md`.
- Explains preset genealogy.
