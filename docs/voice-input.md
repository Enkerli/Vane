# Voice into WindCore

A sung or spoken voice playing the wind model: vowels shape it, "t", "k" and "p" articulate its notes. Pitch
still comes from MIDI. This is a first prototype, offline only: the analysis, a renderer that takes a voice
recording beside the notes, test voices with known answers, and a plan for the live version.

**Status: nothing here has been judged by ear yet, and no real voice has been through it.** The numbers below
are from synthetic voices. They show the tracker works on clean input; they do not show that any of this
sounds good.

## What "the vowel in the model" turned out to mean
WindCore has two mouths (see the comments in `WindCore.h`):

- **Mouth shape / Mouth strength** (`vowel`, `vowelDepth`), the playable one, with the Wind Mouth and Wind
  Mouth Strength destinations. It is two resonances on the radiated sound: a filter after the horn.
- **The tract upstream of the reed** (`mouthFreq`, `mouthDepth`), the physical one. It makes no formant.

So driving `vowel` from a voice is the filter version, not the in-model one. Three ways of getting a vowel
inside the model were tried on held notes:

| Tried | Result |
|---|---|
| The voice's formants on the tract upstream of the reed (`mouthFreq`, `mouth2Ratio`, with `mouthDepth` up to 5) | No vowel pattern in the sound. Notes go 30 to 150 cents flat and brighten or thin unpredictably. This agrees with the acoustics: a player's tract does not put formants on a reed instrument's sound. |
| Vowel resonances inside the horn's loop (on the returning wave) | Notes die or jump to another register as soon as the resonances are strong enough to hear. Removed. |
| **The voice itself in the mouth** | Works: the horn keeps its note. Kept, as two settings (below). |

The last one is what a player does when singing or growling into the instrument, and it needs no vowel
tracking. The engine takes one sample of voice per sample of sound (`Engine::setMouthInput`):

- `voiceSing`: the voice as a share of the mouth pressure that blows the reed. The reed's flow carries it and
  the horn resonates it.
- `voiceAir`: the voice riding on the flow through the reed, where breath noise is made. A whisper becomes the
  instrument's breath noise. Usable from 0 to about 3; it is limited to 4, above which the horn stops playing
  its note.

Both carry the voice's own pitch into the sound beside the note (unless it is whispered). Whether that is
the "impossible instrument" worth having, or a mess, is for listening to decide.

## The tracker
`WindCore/VoiceTracker.h`: one sample in at a time, fixed memory, no dependencies, so it can move into the
plugin as it is.

- **Level, every sample.** Power over 12 ms gives the level; the mouth is open above 32 dB below the loudest
  recent level and closed below 38 dB. An **onset** is the level rising 10 dB above its lowest of the last
  50 ms, where that lowest was at least 18 dB below the loudest (so a swell or a glide between vowels is not
  one). A consonant and the vowel after it count as one onset. Each onset reports how bright it is (the share
  of its power above 3 kHz) and how loud.
- **Vowel, every 5 ms.** The last 25 ms, brought down to about 12 kHz: linear prediction of order 12, its
  roots giving the first two formants; voicing and the voice's pitch by autocorrelation. The formants are
  placed on the engine's vowel line (oo 320/900 Hz, ah 700/1200, ee 300/2300). They hold through closures
  and bursts.
- **States:** closed, burst, voiced, noise.

### Measured on synthetic voices
`tools/voicein_synth.py` builds voices from a pulse or noise source through four formant resonators, with
closures and noise bursts shaped as "t" (above 4.5 kHz), "k" (1.4 to 2.6 kHz) and "p" (below 900 Hz).
`tools/voicein_check.py` scores the tracker against what they were built with.

| Test | Result |
|---|---|
| Formants, voiced, pitch 90 to 300 Hz | F1 within 22 to 95 cents (median), F2 within 12 to 38 cents |
| Formants, whispered | F1 within 57 cents, F2 within 22 |
| Vowel position on the 0..1 line | Median error 0.00 to 0.02; 95% within 0.07 |
| Consonant onsets, clean (7 runs, 58 consonants) | All found, none false. Late by 0.3 to 0.5 ms median, 1.3 ms at most |
| Vowels starting without a consonant | All found; late by 1 to 9 ms |
| Consonants with noise 45 dB below the voice | All found, 3.3 ms late at most; one false onset per file, at the start, when the noise begins |
| Swells and vowel glides with no consonant | No false onsets |
| Brightness reported at the onset | t 0.6, k 0.2 to 0.27, p 0.03 to 0.09 |

The vowel answer trails the voice by about 40 ms (half the window plus smoothing), and the engine glides its
resonances over another 20 ms.

These voices are clean and their consonants are ideal. Expect real speech to be harder: voiced stops
("b", "d", "g") dip less than 18 dB, room sound fills closures, formants of high voices are pulled toward
the harmonics, and a voice of a different size sits off the vowel line (`v.size` scales for that).

## The renderer
`windcore-render` takes `--voice voice.wav` beside the events, which still give the notes and the breath.

| Setting | What the voice does |
|---|---|
| `v.vowel=<dB>` | Its place on the vowel line moves the vowel resonances on the sound (the filter version) |
| `v.formants=<dB>` | Its two formants set those resonances directly (`vowelF1`, `vowelF2`) |
| `voiceSing=<0..1>`, `voiceAir=<0..3>` | It goes into the mouth (the in-model versions) |
| `v.tongue=1` | Closures stop the reed; onsets tongue it (`Engine::tongueStroke`), with the scoop |
| `v.chiff=<0..3>` | Onset noise at those onsets, as loud as the consonant, coloured from reed (dark, "p") to air (bright, "t") |
| `v.breath=<0..1>` | Its level drives the breath. Breath is held behind the tongue through a closure |
| `--voice-log f.txt` | Writes what was tracked |

With `v.tongue` and breath from the events, a note speaks at the consonant: on the test voices the sound
dips 13 to 28 dB at each one and is back within 6 dB of full in 10 to 25 ms, in tune. With `v.breath` the
note follows the voice's own level instead, so it speaks when the vowel does, a few tens of milliseconds
after the consonant.

Renders with no voice are bit-identical to before these changes.

```bash
cd WindCore/tools
python3 voicein_check.py                         # the tracker against the test voices
python3 voicein_render.py out/                   # a listening folder from the test voices
python3 voicein_render.py out/ my-voice.wav      # every variant, driven by a recording
```

## Pitch from the voice (later)
The tracker already reports the voice's pitch (autocorrelation over 25 ms, 70 to 500 Hz), unused. Playing
from it would need: protection against octave errors, a decision per moment between a new note and a bend
(the engine's `setPitch` treats a move of more than half a semitone as a key change), holding the pitch
through consonants and breaths, and an answer for latency: two periods of a low voice are 25 ms before any
smoothing. Chording would then be extra engines at intervals from the tracked note.

## Making it live in Vane
Not built yet. The offline results have not been heard, and the first step changes the plugin's bus layout
in every format, which needs testing in hosts.

**What Vane declares now.** A synth (`IS_SYNTH TRUE`, MIDI in) with one stereo output and no input
(`BusesProperties().withOutput(...)` in `PluginProcessor.cpp`). Formats: AU, AUv3, VST3 and Standalone on
macOS; AUv3 and Standalone on iOS; VST3 and Standalone on Windows; LV2, VST3 and Standalone on Linux; CLAP on the desktop
platforms. No
microphone permission is requested.

**1. A voice input.** Add an optional input bus ("Voice", mono or stereo, off by default) and accept it in
`isBusesLayoutSupported`. As an instrument with a side-chain input this reaches VST3 and CLAP hosts, and AU
hosts that offer a side chain on instruments. Standalone opens the input device once the bus exists; it
needs `MICROPHONE_PERMISSION_ENABLED` (macOS and iOS). AUv3 on iOS is the weak point: hosts rarely route
audio into an instrument. That needs a second build of the same code as a MIDI-controlled effect
(`IS_SYNTH FALSE`, AU type music effect), which is also the "audio effect version" of the roadmap. To check
per host before promising anything: how each one presents an instrument's input bus.

**2. Tracking.** One `VoiceTracker` in the processor, fed in `processBlock` from the input bus (copied to a
mono scratch buffer before the output is cleared). All on the audio thread; it allocates nothing. It adds no
plugin latency: onsets are known 1 to 9 ms after they happen, plus the block; the vowel trails by about 40 ms.

**3. Vowel and level as modulation sources.** New global sources beside the macros in `ModSource.h`:
**Voice Vowel** (0 = oo, 1 = ee), **Voice Level**, and **Voice Brightness** (of the last onset). Routed to
the existing Wind Mouth and Wind Mouth Strength destinations they give the filter version with no new engine
code; routed to cutoff or anything else they work for the rest of Vane too. `NumTotal` grows, and every list
of sources (native and web panel, the latter in the music-suite repository) gains three entries.

**4. Tongue.** A switch, "Voice Tongue". On an onset the sounding WindCore voices get
`tongueStroke()`, with chiff and chiff colour from the onset; while the mouth is closed their gate is off.
Block-accurate is enough at small blocks (64 samples is 1.3 ms); otherwise split the block at the onset.

**5. The voice in the mouth.** Two more WindCore controls, "Voice sing" and "Voice air" (`voiceSing`,
`voiceAir`, entries in `character.json` with no tuning trim). `SynthVoice` gets a pointer to the block's
voice samples and calls `setMouthInput` per sample.

**Risks.** A microphone that hears the instrument will track the instrument: headphones or a close
microphone. Thresholds (`rise`, `dip`, `openDb`) will need to be reachable while this is being tuned on real
voices.

Suggested order: 1 to 3 together (smallest, and it puts a real voice on the Mouth controls), then 4, then 5,
then the effect build, then pitch.
