# Pitch from the voice: where it stands, and the map for whoever continues

Goal: sing a line and Vane plays it, with no MIDI in (Vane FX on an audio track), and send MIDI out carrying the
voice's expression. State on 2026-11-15: the offline core works on synthetic voices; nothing is in the plugin yet.

## What exists (branch `voice-pitch`)
- `WindCore/VoiceTracker.h`: pitch by YIN's normalised difference on the last 40 ms at 12 kHz, 50 to 600 Hz, with a
  parabola for sub-sample periods. Reports `pitchHz`, `pitchNote` (MIDI with fraction) and `periodicity`.
- `WindCore/VoiceNotes.h`: pitch frames to events (note on, note off, bend). A note is a key; the pitch's distance
  from it is a continuous bend, so scoops, slides and vibrato survive. Two modes: notes, and `glide` (one note per
  phrase, the bend carries the melody).
- `windcore-render --voice v.wav v.notes=1|2 v.transpose=<st>`: the voice's pitch plays the engine offline.
- `tools/voicein_notes_check.py`: sung test melodies with known notes (`voicein_synth.py` now takes pitch items).

## Measured
Synthetic, C2 to C5: pitch within about 8 cents (the test voices wobble by about that), no octave errors, every
note of legato and tongued scales and of leaps found with none invented (72 of 72). Notes start 40 to 55 ms after a
consonant and 70 to 105 ms after a legato change. Melodies that change vowel while sliding invent 1 to 3 passing
notes. A slow slide becomes steps in notes mode (use glide).
Real voice (one bass-baritone, four recordings): a vowel held "on one note" near 70 Hz wanders across C2..D2 and
gives 15 notes in notes mode, 1 in glide mode. A whisper still starts 2 false notes. Not judged by ear.

## Next steps, in order
1. **Plugin.** Parameters Voice Pitch (off / notes / glide) and Voice Transpose. In `processBlock`, at each tracker
   frame run `VoiceNotes` and add its events to the block's MIDI at that sample, on channel 1: note on (velocity
   from `voiceInput.follow`), note off, pitch bend scaled by the `nonMPEPBRange` parameter. Voices then need no change.
2. **MIDI out.** `producesMidi()` true and `NEEDS_MIDI_OUTPUT TRUE`, the same events out, plus the expression the
   tracker already has: level as CC2 (breath), the back-to-front vowel as CC74, onset brightness as CC71. Check
   each host again afterwards (pluginval, and the routes in `docs/daw-setup.md`).
3. **Steadier notes on real voices.** Ideas not tried: hysteresis that widens with the voice's own recent wobble;
   quantising through the tuning (below); starting a note at once on a consonant at the last pitch and letting the
   bend correct it (the wait for a stable pitch is most of the latency).
4. **Chords**: extra engines at intervals from the tracked note.

## Connections worth knowing (the wider map)
- **Tuning.** `VoiceNotes::quantise` is a hook: Vane already has tuning systems (`Source/MPE/TuningClient`,
  MTS-ESP, an internal "just" tuning) and rotating chords in just ratios (`setChordSeqs`). A sung pitch should snap
  to the degrees of the active tuning, not to 12 equal steps, and chords should stack from it in ratios.
- **Expression is already tracked.** Level, loudness follow, vowel (F1 to F4 and a back-to-front axis), onset
  brightness and consonant onsets are in `modMatrix.voiceInput`. Pitch is the last missing dimension of a voice
  as a controller; MIDI out makes Vane a voice-to-MPE-style converter for any other instrument.
- **The input is also a control voltage** (Input 1, Input 2, DC kept): a breath CV could be the breath itself.
- **The panel lives in another repository**: Enkerli/music-suite, `apps/vane/index.html` (sources list, WindCore
  panel, Voice section). Every new parameter needs a key in `Source/WebUI/WebVaneEditor.cpp` and a control there.
- **Builds**: Vane (instrument), Vane FX (effect, input on), the standalone apps. AUv3 only builds with
  `cmake -G Xcode`; a macOS AUv3 registers itself on build and hides the AU (see `docs/voice-input.md`).
- **Ears decide.** Offline numbers here were wrong about what mattered more than once; live playing is the test.
