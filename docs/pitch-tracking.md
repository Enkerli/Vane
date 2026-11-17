# Pitch from the voice: where it stands, and the map for whoever continues

Goal: sing a line and Vane plays it, with no MIDI in (Vane FX on an audio track), and send MIDI out carrying the
voice's expression. State on 2026-11-16: in the plugin (Voice Pitch, Voice Transpose, Voice MIDI Out), checked
through the processor on synthetic voices; not yet played live.

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

## In the plugin (done)
- **Voice Pitch** (Off / Notes / Glide) and **Voice Transpose** (-36..36 st). In `processBlock`, at each tracker
  frame, `VoiceNotes` events become MIDI on channel 1 at that sample (note on with velocity from the loudness
  follower, note off, pitch bend scaled by the non-MPE Pitchbend Range) and are merged into the block's MIDI, so
  the synth plays them like any incoming notes. Switching off or losing the input sends a note off.
- **Voice MIDI Out**: the plugin now declares MIDI output (`producesMidi`, `NEEDS_MIDI_OUTPUT`). Out goes only what
  the voice made (incoming MIDI is not passed through): notes and bends, loudness as CC2, the back-to-front vowel
  as CC74, each consonant's brightness as CC71. About 120 CC messages a second while the voice moves; thin them
  if a host or device complains.
- Checked: sung melodies with no MIDI notes into `VanePerfRender` (`VANE_VOICE`, and `VANE_MIDI_OUT=file` to log
  what comes out): 9 of 9, 9 of 9 and 6 of 6 notes out, instrument in tune with the voice within about 10 cents.

## Against a real voice (2026-11-16)
A singing-voice editor's transcription of one real recording (20 notes, D2 to B2) as the reference
(`tools/voicein_vs_reference.py voice.wav reference.mid`). The voice wanders about +-80 cents within a note, centred
on it, and the first version made 47 notes (29 extra: flipping between neighbours) and passed all the wander as
bend, which sounded like searching. Now: an octave check in the tracker (a voice can repeat as well at two periods
as at one); after a consonant the next steady pitch picks the note at once, and in between a change needs 0.75
semitone held 50 ms; Voice Pitch Bend (0..1, 0.4 by default) sets how much of the wander is heard. Result: 18 extra,
10 of 20 matching the reference's pitch (12 before; 4 differ, some where the voice itself sits between notes).
Synthetic melodies: 90 of 96 notes, notes about 25 ms later than before. A tighter setting (0.9 semitone, 70 ms)
leaves 11 extra at about 125 ms. Listening examples: WindBenchy 15-voice-pitch (not in the repository).

## Next steps, in order
1. **Play it live**, Vane FX on an audio track with nothing else: the whole point, and nothing above replaces it.
2. **Hosts.** MIDI out from an instrument or an effect is routed differently in each host; extend
   `docs/daw-setup.md` once tried.
3. **Steadier notes on real voices.** Ideas not tried: hysteresis that widens with the voice's own recent wobble;
   quantising through the tuning (below); starting a note at once on a consonant at the last pitch and letting the
   bend correct it (the wait for a stable pitch is most of the latency).
4. **Chords**: extra engines at intervals from the tracked note.

## The wider map (other repositories)
Paths are siblings of this checkout on the maintainer's machine; all on github.com/Enkerli.
- **enkerli-juce**: the suite's JUCE settings, each with the reason it exists, and a test ladder (`TESTING.md`,
  `tools/validate.sh`, `tools/suite-build`). Read it before touching CMake or a plugin format. Two of its lessons
  bear on open problems here: a blank WKWebView must never be silent (inject an error overlay; run
  `tools/webview-smoke.swift` on the page), which is the first thing to try on the blank macOS AUv3 panel; and
  plugin codes are forever (Vane FX's `VAfx` must not change).
- **enkerli-swift**: the Swift AUv3 foundation (chord theory, pattern format, kernel) behind MelGen, ProgGenie,
  SwiftPitchFold, SwiftSerpe and SwiftDrawnQurve; iOS and macOS only.
- **ProgGenie** (`aumi PgGn`): chord progressions from corpus transition statistics, with per-change curation. A
  sung root note from Voice MIDI Out could steer it, or it could feed Vane chords that the voice then inflects.
- **DrawnQurve** (JUCE, `aumi Dqau`) and **SwiftDrawnQurve**: a drawn line looped into CC, pressure, bend or notes.
  Its lanes are natural sources for Vane's Wind destinations; Vane's voice expression (CC2, CC74, CC71) is the same
  kind of stream it emits.
- **music-suite** (monorepo, apps in `apps/`): Vane's panel (`apps/vane`), **Twirl** (a gesture toy with Web MIDI
  out), pitchfold, chord-dictionary, progression-studio, MIDIcurator and others. Twirl's loops and Vane's voice are
  both continuous gesture-to-MIDI sources; a shared vocabulary of expression CCs would let them stand in for each
  other.
- **Controllers**: Vane already has controller profiles (Sylphyo, Exquis and others). A new device (the M-VAVE
  FM-1 is on the desk) would start as a profile; its custom firmwares are a separate project that has not begun.

## Connections inside Vane
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
