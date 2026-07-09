# Articulation Testing

## Why articulation first

A breath-controlled physical model succeeds or fails through gesture response. Static tone is secondary. The first test suite should stress attacks, slurs, pressure changes, pitch stability, and register behaviour.

## Starter tests

### 1. Long tone crescendo

Purpose:

- breath-to-amplitude response
- breath-to-brightness response
- sustain stability

Gesture:

```text
C4, 4 seconds
breath: 0.15 -> 0.85 -> 0.20
```

### 2. Soft onset

Purpose:

- low-pressure speaking threshold
- breathy pre-tone noise
- attack delay

Gesture:

```text
D4, 3 seconds
breath: 0.00 -> 0.25 slowly, then hold
```

### 3. Tongued repeated notes

Purpose:

- attack transient repeatability
- note reset behaviour
- transient noise

Gesture:

```text
G4 repeated 8 times
short breath pulses
```

### 4. Slurred whole step

Purpose:

- transition without full silence
- pitch interpolation
- bore/reed continuity

Gesture:

```text
F4 -> G4 -> F4
breath continuous
```

### 5. Octave leap

Purpose:

- register stability
- delay-length retuning artifacts
- transient chirp

Gesture:

```text
C4 -> C5 -> C4
breath continuous
```

### 6. Growl held note

Purpose:

- low-frequency modulation response
- sideband/noise behaviour

Gesture:

```text
Bb3, 3 seconds
growl: 0 -> 0.7 -> 0
```

### 7. Vibrato held note

Purpose:

- vibrato pitch depth
- vibrato air depth
- modulation interaction

Gesture:

```text
A4, 3 seconds
vibrato pitch and air enabled after 1 second
```

## Descriptor targets

For each test, record:

- peak amplitude
- RMS envelope
- attack time
- attack noise estimate
- spectral centroid
- pitch estimate
- pitch stability
- sustain amplitude variation
- clipping
- silence

## Reference samples

Reference samples should be short, normalized, clearly named, and licensed for project use.

Suggested naming:

```text
references/tenor_real/tongued_G4_mf_001.wav
references/tenor_real/slur_F4_G4_001.wav
references/stk_saxofony/longtone_C4_crescendo_001.wav
references/silverwood/longtone_C4_mf_001.wav
```

Use references as descriptor anchors, not literal targets.
