# MiniSax Architecture

## Philosophy

MiniSax is an instrument-design laboratory. The core musical target is a breath-controlled reed/waveguide voice that can move through tenor sax, reed organ, blown bottle, clarinet-like, and digital waveguide territory. The architecture should support reproducible exploration rather than opaque patch tweaking.

## System components

```text
presets/*.json
  -> minisax-render
    -> MiniSaxVoice DSP
    -> renders/<preset>/<test>.wav
      -> tools/analyze_renders.py
        -> analysis/<preset>/<test>.json
        -> analysis/<preset>/summary.csv
```

## DSP block diagram

```text
Note/Pitch + Gesture Inputs
        |
        v
Breath Smoothing + Modulators
        |
        v
Mouth Pressure + Breath Noise
        |
        v
Reed Nonlinearity / Valve
        |
        v
Mouthpiece Junction
        |
        v
Fractional Delay Bore <---- feedback path
        |
        v
Loss Filter / Damping / Dispersion Approximation
        |
        v
Bell / Body Filter
        |
        v
Output
```

## Minimal C++ classes

### `MiniSaxVoice`

Owns the voice state. Accepts sample-by-sample or block-level control input and produces mono audio.

Responsibilities:

- parameter smoothing
- pitch to delay-length conversion
- pressure/noise/growl/vibrato modulation
- calling reed and bore components
- output gain and safety limiting

### `ReedNonlinearity`

Models a nonlinear pressure-controlled valve.

Suggested first approximation:

```cpp
float deltaP = mouthPressure - borePressure;
float opening = aperture - reedStiffness * deltaP + embouchureOffset;
opening = clamp(opening, 0.0f, 1.0f);
float flow = opening * nonlinearFlow(deltaP);
```

`nonlinearFlow` can initially be a signed square-root or tanh-shaped curve:

```cpp
float nonlinearFlow(float x) {
    return std::tanh(flowGain * x);
}
```

This is not acoustically complete, but it is stable and tunable.

### `FractionalDelay`

Needed because pitch frequency rarely maps to an integer delay length. Use linear interpolation in v0.1. Later: allpass interpolation or higher-order interpolation.

### `BoreModel`

Contains the feedback delay and loss filtering. Initially one delay line plus a one-pole lowpass is enough.

### `BellFilter`

A simple biquad, shelf, or one-pole highpass/lowpass combination. The goal is to make brightness controllable and to avoid raw delay-line tone.

## Pitch model

Initial delay length:

```text
delaySamples = 0.5 * sampleRate / frequencyHz
```

The half factor is because the reed end acts as a closed termination, making
the bore a quarter-wave resonator (same convention as STK Clarinet). The
first draft of this document used the full period, which sounds an octave low.

This is intentionally crude. Later versions should add pitch correction tables because effective acoustic length changes with reed, bell, embouchure, register, and damping.

## Control model

Inputs should support both synthetic test gestures and live mappings later.

Initial normalized controls:

```text
breath: 0..1
embouchure: 0..1
reedStiffness: 0..1
reedAperture: 0..1
boreDamping: 0..1
bellBrightness: 0..1
noiseAmount: 0..1
growlAmount: 0..1
vibratoAirAmount: 0..1
vibratoPitchAmount: 0..1
outputGain: linear or dB
```

## Stability requirements

- No NaN or Inf output.
- Output should not exceed +/-1.0 without warning.
- Silence detection should warn if test is unexpectedly silent.
- Clipping detection should warn if peak exceeds threshold.
- Deterministic noise seed per render.
