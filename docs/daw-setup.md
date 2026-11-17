# Notes and audio into Vane, per host

Vane needs notes (MIDI) and, for the voice input or a control voltage, audio. A host gives an instrument notes
and an effect audio, so one of the two has to be brought across. There are three builds to choose from:

| Build | Gets by itself | Has to be brought in |
|---|---|---|
| **Vane** (instrument) | notes | audio, on its "Voice" side chain |
| **Vane FX** (effect, macOS) | audio, on its stereo input | notes |
| **Vane** or **Vane FX** app (standalone) | both: the audio device's input and the MIDI devices | nothing |

What the audio does once it arrives is in [voice-input.md](voice-input.md): Voice Tongue, Voice Vowel, Voice Breath
Follow and the Voice sources read a voice; **Input 1** and **Input 2** read each channel as a control signal, DC
included. Choose the WindCore model for the voice controls (the "WindCore - Voice" preset has them set).

Status of each route below: **works** means someone played it; **untested** means it follows from how the host
is documented to work and nobody has tried it here yet.

## Bitwig Studio
- **Vane FX on an audio track, notes by Note Receiver (works).** Put Vane FX on the audio track that carries the
  voice. Add a **Note Receiver** device before it and choose the track or controller the notes come from.
- **On an instrument track, audio by Audio Receiver (works).** The track already gets the notes. Add an **Audio
  Receiver** device before the plugin and choose the track or input the voice is on. This suits playing from a
  controller: the instrument track is the one you arm.
- **Vane's side chain (not offered).** Bitwig showed no side-chain source for the instrument, so the Audio
  Receiver is the way in.
- **Audio as modulation (untested).** An Audio Sidechain modulator on any Vane parameter needs nothing from the
  plugin, but it bypasses the voice tracker: level only, no vowels or consonants.

- **Voice Pitch and notes from the host.** With Voice Pitch on, the voice plays the notes and Vane ignores notes
  from the host, so a Note Receiver left on the track cannot add a second set of notes to the one mono voice.
- **Bitwig sets the pitch-bend range of channel 1 to 48** (as for any MPE plugin). Vane used to assume 2 there,
  so every bend, Voice Pitch's included, came out 24 times too large: the sound jumped about, sat an octave or so
  high and clipped. Vane now reads the range the host set.

## Logic Pro
- **Vane FX as a MIDI-controlled effect (untested).** Create a *Software Instrument* track. In its Instrument slot
  choose **AU MIDI-controlled Effects > Enkerli > Vane FX**. In the plugin window's header, set **Side Chain** to
  the audio track or input that carries the voice. The track gives it notes; the side chain gives it audio.
- **Vane FX in an audio track's Audio FX slot** gets the audio but no notes. It only makes sense there once Vane
  can play from the voice's own pitch.
- **Vane, the instrument, with its side chain (untested).** Load Vane on a Software Instrument track and look for
  the **Side Chain** menu in the plugin window's header.
- A blank Vane FX window means a stray AUv3 is registered; see "No AUv3 for Vane FX on macOS yet" in
  [voice-input.md](voice-input.md).

## Standalone
Choose the input device in the app's audio settings and untick **Mute audio input** (it starts muted, with a
feedback warning: use headphones). MIDI devices are chosen in the same window.

## A control voltage
Breath on one channel and bite on the other, into a **DC-coupled** input (most interfaces block DC on their
inputs). Route **Input 1** and **Input 2** in the modulation matrix; the "WindCore - CV Mouth and Air" preset is a
starting point. They are not yet a choice for the breath that blows WindCore.
