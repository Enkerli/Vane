# WindCore: playing and patching

WindCore is the second **Waveguide Model** in Vane, next to MiniSax: a reed, cone and mouth model with
character controls. This page is about playing it and shaping it. How it works and how it was tuned are in
[WindCore/README.md](../WindCore/README.md).

## Getting a sound
1. In **Waveguide**, turn the Reed / Bore Engine **On** and choose **WindCore**.
2. Play with breath (CC2 by default). Breath blows the reed directly: loudness and brightness come from the
   model, so no VCA routing is needed. From a sequencer or keyboard, **Synthetic Breath** stands in.
3. Set the filter fully open and leave fold and noise blend off to hear the model on its own.

The range is F#1 to Eb7, one continuous instrument; notes above Eb7 are silent.

## A saxophone-like patch
WindCore's steady tone matches the harmonic levels of a set of saxophone instruments to within a few dB, yet
on its own it can sound like a generic woodwind. What brings it closer, in our listening so far, is how notes
start and the space they sound in:

| Where | Setting | Why |
|---|---|---|
| Room | **Mix** about 25%, **Size** small to medium, **Tone** natural | Acoustic instruments are heard in a space. A little room does more than any tone control. |
| WindCore, Fingers and tongue | **Chiff** at its default, **Chiff colour** toward reed, **Bloom** about 90 ms | A saxophone note starts no brighter than it settles: some rough, dark air, then the upper harmonics arrive after the fundamental. Chiff colour toward air gives a flute's bright hiss instead. |
| WindCore, Player | **Air** at its default | Breath noise that is stronger low down and cleaner up high, as on recordings. |
| WindCore, Horn | **Body** around 1; **Body size** below 1 for a larger, darker instrument | Peaks and dips fixed in Hz, as saxophones share. |
| WindCore, Player | **Mouth shape** and **Mouth strength** to taste | A vowel coloration, fixed in Hz. It does not change tuning, so it can be played. |
| Transients (optional) | An attack sample with the pitch resonator, at low gain | Another way to colour the start. The recorder and flute chiffs sound flute-like here. |

Keep **Life** low at first. Its slow movement of breath and pitch can sound like trying too hard; a player on a
wind controller already brings their own.

## The controls
Each slider in the panel names its two ends and says what it does. In short:

| Group | Control | Ends | What it moves |
|---|---|---|---|
| Player | Mouth shape | oo / ee | Two vowel resonances on the sound, fixed in Hz |
| | Mouth strength | open throat / strong vowel | How strongly that vowel colours the sound (up to 10 dB) |
| | Embouchure | tight / loose | How open the reed tip is held: smaller and darker, or louder and brighter |
| | Air | clean / breathy | Turbulence in the air stream, riding on the flow |
| Mouthpiece and reed | Reed | soft / hard | How abruptly the reed closes on the mouthpiece |
| | Chamber | large / small | The mouthpiece volume: darker and spread, or tighter and focused |
| | Baffle | low / high | How far up the spectrum the chamber keeps absorbing: more buzz and cut when high |
| Horn | Cone | slender / stubby | How much of the cone is missing: which harmonics are hollowed out |
| | Tone holes | covered / open | Where sound starts leaving through the holes: more ringing upper harmonics when open |
| | Bell | dark / bright | A plain tilt of the radiated sound |
| | Body, Body size | plain / full, large / small | The fixed peaks and dips, and where they sit |
| Fingers and tongue | Fingers | snappy / lazy | How long a key takes to move in a slur |
| | Tongue | hard / soft | How fast the tongue leaves the reed |
| | Chiff | clean / breathy | Air at a tongued start or after a rest |
| | Chiff colour | reed / air | Dark, rough noise mostly through the horn (reed instruments), or a bright hiss heard directly (flutes) |
| | Bloom | instant / slow | How long the sound takes to open from dark to full after a tongued start |
| | Key noise | silent / noisy | A smaller rush of air at a slurred note change |
| Life | Wobble, Shimmer, Pitch drift | steady / restless, fixed / shifting, locked / loose | Slow random movement of breath, embouchure and pitch |
| | Vibrato, Vibrato rate | none / wide, slow / fast | A vibrato that grows in after a quarter of a second |
| | Slur | seamless / articulated | A moment's dip of breath at a slurred note change |

Moving a tone control detunes the horn a little; WindCore corrects that per note, to within about 12 cents for
most settings. Body, Mouth, Bell, Chiff, Bloom, Key noise and Life don't affect tuning. When two tone controls are both far from their defaults (for example a large Chamber with a stubby Cone), their corrections don't add up exactly, and a few notes can land 40 cents or more off.

## Room
**Room** is a Vane stage of its own, after everything else, so it works with any sound: early reflections and
a short diffuse tail, as a close microphone hears an instrument in a room. **Mix** 0 turns it off completely
(the default). **Size** goes from a small booth to a larger room, **Tone** from dark to bright.

## Known limits
- By ear, WindCore is still closer to a generic woodwind than to a saxophone.
- In the lowest octave (F#1 to E2) and on a few notes around G2, how a note starts can move its pitch by up to
  about 20 cents: the reed can settle in two ways there.
- The character controls are not modulation-matrix targets yet, so the mouth can't be played from a joystick.
- The browser version plays MiniSax.
