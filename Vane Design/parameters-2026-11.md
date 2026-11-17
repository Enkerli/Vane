# Vane parameters (generated 2026-11-17)

Every host-visible parameter, from `VanePerfRender --params` on `main`: id, display name, default, range,
steps (0 = continuous). 303 in all.

## Performance (9)

| id | name | default | range | steps |
|---|---|---|---|---|
| `outputLevel` | Output Level | 0.8000000 | 0..1 | 2147483647 |
| `glideTime` | Glide Time | 0.0000000 | 0..2000 | 2147483647 |
| `glideMode` | Glide Mode | Fixed Time | 0..1 | 2 |
| `glideCurve` | Glide Curve | Linear | 0..3 | 4 |
| `masterTune` | Master Tune (cents) | 0.0000000 | -100..100 | 2147483647 |
| `velocityMix` | Velocity to VCA | 0.0000000 | 0..1 | 2147483647 |
| `monoMode` | Mono | Off | 0..1 | 2 |
| `pitchbendRange` | MPE Pitchbend Range (st) | 48 | 1..96 | 2147483647 |
| `nonMPEPBRange` | Pitchbend Range (st) | 2 | 1..96 | 2147483647 |

## Room (3)

| id | name | default | range | steps |
|---|---|---|---|---|
| `roomMix` | Room Mix | 0.0000000 | 0..1 | 2147483647 |
| `roomSize` | Room Size | 0.3500000 | 0..1 | 2147483647 |
| `roomTone` | Room Tone | 0.5000000 | 0..1 | 2147483647 |

## Filter (3)

| id | name | default | range | steps |
|---|---|---|---|---|
| `filterCutoff` | Filter Cutoff | 1200.0002441 | 20..20000 | 2147483647 |
| `filterRes` | Filter Resonance | 0.3000000 | 0..1 | 2147483647 |
| `filterMode` | Filter Mode | LP | 0..2 | 3 |

## Vowel filter (8)

| id | name | default | range | steps |
|---|---|---|---|---|
| `vowelEnable` | Vowel Enable | Off | 0..1 | 2 |
| `vowelPos` | Vowel Open | 0.5000000 | 0..1 | 2147483647 |
| `vowelFront` | Vowel Front | 0.5000000 | 0..1 | 2147483647 |
| `vowelRound` | Vowel Round | 0.0000000 | 0..1 | 2147483647 |
| `vowelAmount` | Vowel Amount | 1.0000000 | 0..1 | 2147483647 |
| `vowelReso` | Vowel Bite | 0.5000000 | 0..1 | 2147483647 |
| `vowelMove` | Vowel Move | 0.0000000 | 0..1 | 2147483647 |
| `vowelMode` | Formant Mode | Vowel | 0..1 | 2 |

## Oscillator (8)

| id | name | default | range | steps |
|---|---|---|---|---|
| `oscDetune` | Osc Detune | 0.0000000 | -100..100 | 2147483647 |
| `oscMorphPos` | Osc Morph | 0.0000000 | 0..1 | 2147483647 |
| `noiseBlend` | Noise Blend | 0.0000000 | 0..1 | 2147483647 |
| `noiseType` | Noise Type | White | 0..2 | 3 |
| `oscFold` | Wavefold | 0.0000000 | 0..1 | 2147483647 |
| `oscInharm` | Inharmonicity | 0.0000000 | 0..1 | 2147483647 |
| `oscSync` | Osc Sync | 1.0000000 | 1..8 | 2147483647 |
| `oscPW` | Osc Pulse Width | 0.5000000 | 0.5..0.999 | 2147483647 |

## Unison and chords (4)

| id | name | default | range | steps |
|---|---|---|---|---|
| `unisonVoices` | Unison Voices | 1 | 0..4 | 5 |
| `unisonMode` | Unison Mode | Detune | 0..1 | 2 |
| `unisonDetune` | Unison Detune | 14.0000000 | 0..50 | 2147483647 |
| `unisonWidth` | Unison Width | 0.7000000 | 0..1 | 2147483647 |

## Transient layer (10)

| id | name | default | range | steps |
|---|---|---|---|---|
| `transientChoice` | Transient Sample | None | 0..15 | 16 |
| `transientGain` | Transient Gain | 0.0000000 | 0..2 | 2147483647 |
| `transientDecay` | Transient Decay (ms) | 199.9999695 | 10..2000 | 2147483647 |
| `transientTrigger` | Transient Trigger | Non-legato | 0..1 | 2 |
| `transientVariation` | Transient Variation | 0.3000000 | 0..1 | 2147483647 |
| `transientFilter` | Transient Filter Route | On | 0..1 | 2 |
| `transientDynamics` | Transient Dynamics | 0.7500000 | 0..1 | 2147483647 |
| `transientResonate` | Transient Resonate | 0.3000000 | 0..1 | 2147483647 |
| `transientDamping` | Transient Damping | 0.5000000 | 0..1 | 2147483647 |
| `transientMorph` | Transient Morph (ms) | 12.0000000 | 0..50 | 2147483647 |

## Older fixed routes (only read to translate old presets) (50)

| id | name | default | range | steps |
|---|---|---|---|---|
| `breathVCAamt` | Breath to VCA | 1.0000000 | 0..1 | 2147483647 |
| `exprVCAamt` | Expression to VCA | 1.0000000 | 0..1 | 2147483647 |
| `pressVCAamt` | Pressure to VCA | 0.5000000 | 0..1 | 2147483647 |
| `cc74CutoffAmt` | CC74 to Cutoff | 0.9000000 | 0..1 | 2147483647 |
| `breathCutoffAmt` | Breath to Cutoff | 0.2500000 | 0..0.5 | 2147483647 |
| `pressCutoffAmt` | Pressure to Cutoff | 0.2000000 | 0..0.5 | 2147483647 |
| `veloCutoffAmt` | Velocity to Cutoff | 0.1500000 | 0..0.5 | 2147483647 |
| `breathResAmt` | Breath to Resonance | 0.1500000 | 0..0.5 | 2147483647 |
| `cc74ResAmt` | CC74 to Resonance | 0.0000000 | 0..0.5 | 2147483647 |
| `slideMorphAmt` | Slide to Morph | 0.0000000 | 0..1 | 2147483647 |
| `pressMorphAmt` | Pressure to Morph | 0.0000000 | 0..1 | 2147483647 |
| `slidePWAmt` | Slide to PW | 0.0000000 | 0..0.5 | 2147483647 |
| `pressPWAmt` | Pressure to PW | 0.0000000 | 0..0.5 | 2147483647 |
| `slideNoiseAmt` | Slide to Noise | 0.0000000 | 0..1 | 2147483647 |
| `pressNoiseAmt` | Pressure to Noise | 0.0000000 | 0..1 | 2147483647 |
| `slideFoldAmt` | Slide to Fold | 0.0000000 | 0..1 | 2147483647 |
| `pressFoldAmt` | Pressure to Fold | 0.0000000 | 0..1 | 2147483647 |
| `slideInharmAmt` | Slide to Inharm | 0.0000000 | 0..1 | 2147483647 |
| `pressInharmAmt` | Pressure to Inharm | 0.0000000 | 0..1 | 2147483647 |
| `breathMorphAmt` | Breath to Morph | 0.0000000 | 0..1 | 2147483647 |
| `breathPWAmt` | Breath to PW | 0.0000000 | 0..0.5 | 2147483647 |
| `breathNoiseAmt` | Breath to Noise | 0.0000000 | 0..1 | 2147483647 |
| `breathFoldAmt` | Breath to Fold | 0.0000000 | 0..1 | 2147483647 |
| `breathInharmAmt` | Breath to Inharm | 0.0000000 | 0..1 | 2147483647 |
| `breathVCACurve` | Breath to VCA Curve | 0 | 0..2 | 2147483647 |
| `exprVCACurve` | Expr to VCA Curve | 0 | 0..2 | 2147483647 |
| `pressVCACurve` | Pressure to VCA Curve | 0 | 0..2 | 2147483647 |
| `cc74CutoffCurve` | CC74 to Cutoff Curve | 0 | 0..2 | 2147483647 |
| `breathCutoffCurve` | Breath to Cutoff Curve | 1 | 0..2 | 2147483647 |
| `exprCutoffCurve` | Expr to Cutoff Curve | 1 | 0..2 | 2147483647 |
| `pressCutoffCurve` | Pressure to Cutoff Curve | 1 | 0..2 | 2147483647 |
| `breathResCurve` | Breath to Reso Curve | 1 | 0..2 | 2147483647 |
| `exprResCurve` | Expr to Reso Curve | 1 | 0..2 | 2147483647 |
| `veloCutoffCurve` | Velocity to Cutoff Curve | 0 | 0..2 | 2147483647 |
| `cc74ResCurve` | CC74 to Reso Curve | 0 | 0..2 | 2147483647 |
| `slideMorphCurve` | Slide to Morph Curve | 0 | 0..2 | 2147483647 |
| `pressMorphCurve` | Pressure to Morph Curve | 0 | 0..2 | 2147483647 |
| `slidePWCurve` | Slide to PW Curve | 0 | 0..2 | 2147483647 |
| `pressPWCurve` | Pressure to PW Curve | 0 | 0..2 | 2147483647 |
| `slideNoiseCurve` | Slide to Noise Curve | 0 | 0..2 | 2147483647 |
| `pressNoiseCurve` | Pressure to Noise Curve | 0 | 0..2 | 2147483647 |
| `slideFoldCurve` | Slide to Fold Curve | 0 | 0..2 | 2147483647 |
| `pressFoldCurve` | Pressure to Fold Curve | 0 | 0..2 | 2147483647 |
| `slideInharmCurve` | Slide to Inharm Curve | 0 | 0..2 | 2147483647 |
| `pressInharmCurve` | Pressure to Inharm Curve | 0 | 0..2 | 2147483647 |
| `breathMorphCurve` | Breath to Morph Curve | 0 | 0..2 | 2147483647 |
| `breathPWCurve` | Breath to PW Curve | 0 | 0..2 | 2147483647 |
| `breathNoiseCurve` | Breath to Noise Curve | 0 | 0..2 | 2147483647 |
| `breathFoldCurve` | Breath to Fold Curve | 0 | 0..2 | 2147483647 |
| `breathInharmCurve` | Breath to Inharm Curve | 0 | 0..2 | 2147483647 |

## Mod matrix slots (144)

24 slots, each `modSlotN_src`, `_dst`, `_amt`, `_curve`, `_scale`, `_en`: source, destination, amount -1..1,
curve, a second source scaling the amount, enabled.

## Controllers (macros, aux CCs) (12)

| id | name | default | range | steps |
|---|---|---|---|---|
| `aux0_cc` | Aux 1 CC | 0 | 0..127 | 128 |
| `aux1_cc` | Aux 2 CC | 0 | 0..127 | 128 |
| `aux2_cc` | Aux 3 CC | 0 | 0..127 | 128 |
| `aux3_cc` | Aux 4 CC | 0 | 0..127 | 128 |
| `aux4_cc` | Aux 5 CC | 0 | 0..127 | 128 |
| `aux5_cc` | Aux 6 CC | 0 | 0..127 | 128 |
| `aux6_cc` | Aux 7 CC | 0 | 0..127 | 128 |
| `aux7_cc` | Aux 8 CC | 0 | 0..127 | 128 |
| `macroBreathSrc` | Breath Source | CC | 0..2 | 3 |
| `macroBreathCC` | Breath CC Number | 2 | 0..127 | 2147483647 |
| `macroExprSrc` | Expression Source | CC | 0..1 | 2 |
| `macroExprCC` | Expression CC Number | 11 | 0..127 | 2147483647 |

## Waveguide (model, MiniSax) (10)

| id | name | default | range | steps |
|---|---|---|---|---|
| `waveguideOn` | Waveguide Mode | Off | 0..1 | 2 |
| `waveguideModel` | Waveguide Model | WindCore | 0..1 | 2 |
| `waveguideEmbouchure` | Waveguide Embouchure | 0.5000000 | 0..1 | 2147483647 |
| `waveguideReedStiffness` | Waveguide Reed Stiffness | 0.5000000 | 0..1 | 2147483647 |
| `waveguideReedAperture` | Waveguide Reed Aperture | 0.5000000 | 0..1 | 2147483647 |
| `waveguideBoreDamping` | Waveguide Bore Damping | 0.2000000 | 0..1 | 2147483647 |
| `waveguideBellBrightness` | Waveguide Bell Brightness | 0.7000000 | 0..1 | 2147483647 |
| `waveguideConical` | Waveguide Conical Amount | 0.6200000 | 0..1 | 2147483647 |
| `waveguideBreathNoise` | Waveguide Breath Noise | 0.0500000 | 0..1 | 2147483647 |
| `waveguideGrowl` | Waveguide Growl | 0.0000000 | 0..1 | 2147483647 |

## Voice input (10)

| id | name | default | range | steps |
|---|---|---|---|---|
| `voiceTongue` | Voice Tongue | Off | 0..1 | 2 |
| `voicePitch` | Voice Pitch | Off | 0..2 | 3 |
| `voicePitchSlew` | Voice Pitch Slew | 40.0000000 | 1..300 | 2147483647 |
| `voiceTranspose` | Voice Transpose | 0 | -36..36 | 73 |
| `voicePitchBend` | Voice Pitch Bend | 0.4000000 | 0..1 | 2147483647 |
| `voiceMidiOut` | Voice MIDI Out | Off | 0..1 | 2 |
| `voiceChiff` | Voice Onset Noise | 0.0000000 | 0..3 | 2147483647 |
| `voiceVowel` | Voice Vowel Strength | 0.0000000 | 0..1 | 2147483647 |
| `voiceUpper` | Voice Upper Formants | 0.0000000 | 0..1 | 2147483647 |
| `voiceBreath` | Voice Breath Follow | 0.0000000 | 0..1 | 2147483647 |

## WindCore character (27)

| id | name | default | range | steps |
|---|---|---|---|---|
| `windMouth` | Wind Mouth shape | 0.5000000 | 0..1 | 2147483647 |
| `windMouthDepth` | Wind Mouth strength | 0.3000000 | 0..1 | 2147483647 |
| `windBite` | Wind Embouchure | 0.4000000 | 0..1 | 2147483647 |
| `windResponse` | Wind Response | 0.5714286 | 0..1 | 2147483647 |
| `windWeight` | Wind Air weight | 0.2500000 | 0..1 | 2147483647 |
| `windAir` | Wind Air | 0.2000000 | 0..1 | 2147483647 |
| `windReed` | Wind Reed | 0.4731975 | 0..1 | 2147483647 |
| `windChamber` | Wind Chamber | 0.6250001 | 0..1 | 2147483647 |
| `windBaffle` | Wind Baffle | 0.5859277 | 0..1 | 2147483647 |
| `windCone` | Wind Cone | 0.5000001 | 0..1 | 2147483647 |
| `windHoles` | Wind Tone holes | 0.4314554 | 0..1 | 2147483647 |
| `windBell` | Wind Bell | 0.5714286 | 0..1 | 2147483647 |
| `windBody` | Wind Body | 0.5000000 | 0..1 | 2147483647 |
| `windBodySize` | Wind Body size | 0.4679914 | 0..1 | 2147483647 |
| `windFingers` | Wind Fingers | 0.5228788 | 0..1 | 2147483647 |
| `windTongue` | Wind Tongue | 0.3979400 | 0..1 | 2147483647 |
| `windChiff` | Wind Chiff | 0.1000000 | 0..1 | 2147483647 |
| `windChiffColor` | Wind Chiff colour | 0.0000000 | 0..1 | 2147483647 |
| `windBloom` | Wind Bloom | 0.4500000 | 0..1 | 2147483647 |
| `windScoop` | Wind Scoop | 0.3888889 | 0..1 | 2147483647 |
| `windKeyNoise` | Wind Key noise | 0.2000000 | 0..1 | 2147483647 |
| `windWobble` | Wind Wobble | 0.1666667 | 0..1 | 2147483647 |
| `windShimmer` | Wind Shimmer | 0.1600000 | 0..1 | 2147483647 |
| `windDrift` | Wind Pitch drift | 0.2000000 | 0..1 | 2147483647 |
| `windVibrato` | Wind Vibrato | 0.0000000 | 0..1 | 2147483647 |
| `windVibRate` | Wind Vibrato rate | 0.4250000 | 0..1 | 2147483647 |
| `windSlur` | Wind Slur | 0.3571429 | 0..1 | 2147483647 |

## Synthetic breath (5)

| id | name | default | range | steps |
|---|---|---|---|---|
| `synthBreathMode` | Synthetic Breath | Auto | 0..2 | 3 |
| `synthBreathAttack` | Synth Breath Attack | 35.0000038 | 1..500 | 2147483647 |
| `synthBreathDecay` | Synth Breath Decay | 120.0000076 | 1..1000 | 2147483647 |
| `synthBreathSustain` | Synth Breath Sustain | 0.8000000 | 0..1 | 2147483647 |
| `synthBreathRelease` | Synth Breath Release | 180.0000000 | 1..2000 | 2147483647 |

