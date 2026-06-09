# Formant stage — roadmap

The vowel/formant stage (`Source/Synth/FormantFilter.h`) is a global post-mix
resonator bank in series after the synth. Current modes: **Vowel** (5-formant
A–E–I–O–U morph) and **Wah** (single swept resonant band). This file captures
the planned extensions.

## 1. Vowel v2 — articulatory space ✅ (controls done)

DONE: `vowelPos` is now the **Open** axis (F1), plus **Front** (F2) and **Round**
params; the formant freqs are computed from them (see FormantFilter
`updateCoeffsIfMoved`), spanning the IPA chart (incl. /y/, /ø/ rounded fronts).
UI has Open/Front/Round sliders + a cardinal-vowel preset row, mode-aware. Open
is the VowelPos mod destination (Breath→Open).

STILL TO DO:
- **Voice selector** (bass/tenor/alto/soprano) scaling F1..F5 + F4/F5 character.
- **Front/Round mod destinations** (only Open is modulatable today) — add
  VowelFront/VowelRound ModDests so Slide→Front etc. work.
- Ear-tune the F1/F2 ranges + rounding amounts against reference vowels.

Original design notes:

## (orig) Vowel v2 — articulatory space

The limitation of A→E→I→O→U isn't the count, it's that those vowels aren't on a
1-D line in formant space — morphing straight through them passes through
non-vowel formant combinations. Vowels live in a 2-D space (the IPA vowel
chart). Replace the single `vowelPos` with the articulatory axes:

- **Open** ↔ close → **F1** (~250 Hz close … ~850 Hz open)
- **Front** ↔ back → **F2** (~600 Hz back … ~2300 Hz front)
- **Round** ↔ unrounded → lowers F2/F3 a little + narrows (lip rounding)

Two/three continuous, independently-modulatable knobs then span the whole vowel
chart (e.g. Breath→Open = mouth opens with air; Slide→Front). F4/F5 stay fixed
as "voice character," with an optional **Voice** selector (bass/tenor/alto/
soprano) scaling the whole set.

Implementation: drive F1/F2(/F3) directly from the axes instead of table
interpolation; keep the biquad bank. A vowel-letter preset picker on top can set
the three axes to cardinal vowels. Keep the current 5-vowel table as the
"tenor preset" path for backward-compatible presets.

Ref: <https://en.wikipedia.org/wiki/IPA_vowel_chart_with_audio>

## 2. Trumpet mute (sordina / Harmon) — a FormantFilter mode

A bright, nasal, midrange-peaky coloration: a strong fixed resonant peak
(~1–1.5 kHz) + presence boost + low rolloff. Buildable as a third `Mode` reusing
the biquad bank (a couple of fixed resonances + a high-shelf tilt). The
character is subjective → needs ear-tuning on the target. `position` could map
to brightness/tightness; `Bite` to peak Q.

## 3. Glottal source (oscillator side)

Vowel formants are *fixed frequencies* (a filter) — they must NOT track pitch
(a wavetable "vowel" would chipmunk up high). So vowels stay in the filter. What
the oscillator can add is a better **glottal-style source** (a richer, buzzy
pulse for the formant filter to shape) — i.e. real source–filter voice synthesis,
which Vane's `osc → formant` chain already is. A generated glottal wavetable
(e.g. an LF-model-ish pulse train morphing open→pressed) would make the formant
stage sound markedly more vocal. Separate, optional work.

## Done
- **Vowel** mode (5-formant tenor table, global, legato-safe).
- **Wah** mode (single swept resonant band-pass; position = sweep, Bite = Q,
  Breath→Vowel = auto-wah).
