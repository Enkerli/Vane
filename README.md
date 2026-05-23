# Vane

An MPE/MTS-ESP/CC-first monophonic synthesizer plugin for macOS, built with JUCE 8.

Designed for breath controllers (Sylphyo, EWI, WX-11) and other expressive MPE instruments. Every design decision prioritises continuous breath expression over velocity-centric keyboard paradigms.

## Features

- **MPE** — full per-note pressure, slide (CC74), and pitchbend expression
- **MTS-ESP** — live microtonality via the ODDSound MTS-ESP master/client protocol; auto-filters MTS-silenced pitches
- **CC modulation** — CC2 (breath) and CC11 (expression) routed to VCA and filter cutoff through a slew-limited mod matrix
- **Mono legato** — sample-exact oscillator phase and SVF filter state continuity across note transitions; no click, no glitch
- **Poly mode** — full MPE polyphony for chords and keyboard playing
- **Portamento** — smoothed pitch glide between legato notes only; attacks always snap to pitch
- **Subtractive synthesis** — PolyBLEP oscillator (sine, triangle, saw, square, noise) through a Cytomic TPT state-variable filter (LP/BP/HP)
- **Wind-first VCA** — velocity contribution is a mix parameter (0 = pure breath, 1 = pure velocity)

## Architecture

```
CC2 / CC11 / MPE pressure / MPE slide
        │
   ModMatrix (slew-limited routes)
        │
  ┌─────┴──────┐
  │ SynthVoice │ × 15  (one per MPE member channel)
  │  Oscillator│
  │  SVFilter  │
  │  smoothedVCA / smoothedHz / smoothedCutoff
  └────────────┘
        │
  AudioBuffer (stereo)
```

### Legato voice handoff

On each note transition in mono mode the outgoing voice publishes:

| Atomic | Contains |
|--------|----------|
| `lastOscPhase` | oscillator phase for the *next* sample — new voice calls `osc.reset()` directly, no advance needed |
| `lastFilterS1/S2` | SVF integrator states — new voice primes coefficients then calls `filter.setState()` |
| `lastCutoffHz` | smoothed cutoff Hz — new voice snaps `smoothedCutoff` here to avoid a coefficient mismatch |
| `lastVCALevel` | `smoothedVCA × tailLevel` — the *actual* output amplitude, not the raw smoothed level |

Publishing `smoothedVCA × tailLevel` rather than `smoothedVCA` alone is the key invariant that distinguishes true legato (breath continuously on, `initVCA > 0.02`) from an attack following a tail-off (breath was off, `initVCA ≈ 0`).

## Building

Requirements: Xcode, CMake ≥ 3.22, JUCE 8 cloned alongside the project.

```sh
git clone https://github.com/your-username/Vane.git
cd Vane
# Clone JUCE 8 as JUCE/
git clone --depth 1 --branch 8.0.6 https://github.com/juce-framework/JUCE.git JUCE
cmake -B build-mac -G Xcode
xcodebuild -project build-mac/Vane.xcodeproj -scheme Vane_AU -configuration Release build
```

The AU component is installed automatically to `~/Library/Audio/Plug-Ins/Components/`.

## Modulation matrix (defaults)

| Source | Destination | Amount | Attack | Release |
|--------|-------------|--------|--------|---------|
| CC2 (breath) | VCA Level | 1.0 | 5 ms | 80 ms |
| CC11 (expression) | VCA Level | 1.0 | 5 ms | 80 ms |
| CC2 | Filter Cutoff | 0.5 | 5 ms | 80 ms |
| CC11 | Filter Cutoff | 0.5 | 5 ms | 80 ms |
| MPE Pressure | VCA Level | 0.5 | 3 ms | 50 ms |
| MPE Slide (CC74) | Filter Cutoff | 0.5 | 2 ms | 20 ms |

Slide is converted to bipolar (±1) before routing so the filter sweeps symmetrically around the base cutoff.

## Parameters

| ID | Name | Range | Default |
|----|------|-------|---------|
| `oscWave` | Waveform | Sine / Triangle / Saw / Square / Noise | Saw |
| `oscDetune` | Detune | ±100 cents | 0 |
| `filterCutoff` | Filter Cutoff | 20–20 000 Hz | 8 000 Hz |
| `filterRes` | Resonance | 0–1 | 0.3 |
| `filterMode` | Filter Mode | LP / BP / HP | LP |
| `glideTime` | Glide Time | 0–2 000 ms | 0 |
| `velocityMix` | Velocity → VCA | 0–1 | 0 (wind mode) |
| `masterTune` | Master Tune | ±100 cents | 0 |
| `monoMode` | Mono / Poly | boolean | Poly |

## Licence

MIT
