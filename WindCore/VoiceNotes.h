// VoiceNotes: the notes a voice is singing, from the pitch VoiceTracker reports every 5 ms.
//
// A sung pitch is never still: it scoops into a note, wobbles, slides to the next one. This keeps
// the two things apart that a synthesiser needs apart: which NOTE is meant (a key going down), and
// where the pitch is around it (a bend, kept continuous, so scoops, slides and vibrato survive).
//   - A note starts once the pitch has held still for `stableFrames` (15 ms by default).
//   - While it sounds, the pitch's distance from the note is the bend.
//   - A new note starts only when the pitch has moved `changeSemis` away AND settled there; a
//     jump of about an octave has to last longer, since that is what a pitch error looks like.
//   - Consonants and short breaths do not end the note: it is held `holdSeconds` without pitch.
// One call per frame, no allocation, no dependencies. Notes are MIDI numbers; `transpose` moves
// them (a low voice playing a tenor's range), and `quantise` can snap them to a scale or a tuning.
#pragma once
#include <algorithm>
#include <cmath>

namespace windcore
{

class VoiceNotes
{
public:
    // --- settings
    float transpose     = 0.0f;    // semitones added to the voice's pitch
    int   stableFrames  = 3;       // frames the pitch must hold still before a note starts
    float stableSemis   = 0.6f;    // "still": within this spread
    float changeSemis   = 0.7f;    // this far from the note, and settled, is a new note
    float settledSemis  = 0.3f;    // "settled": the pitch has stopped moving (a tighter spread than for the first note, so a slide's passing pitches are not notes)
    int   changeFrames  = 5;       // for this many frames (25 ms)
    int   semitoneFrames = 9;      // a move to the next semitone must last this long (45 ms): a voice wavers that far on one note
    float minPeriodicity = 0.8f;   // below this (VoiceTracker::periodicity) the pitch is not trusted: whispers, creak, noise
    int   octaveFrames  = 12;      // a move of about an octave or a twelfth must last this long (60 ms)
    float holdSeconds   = 0.15f;   // a note outlasts this much silence or noise (a consonant, a breath)
    // Between consonants a sung note wanders (about +-80 cents on one real voice): a new note there needs a move of
    // `legatoSemis`, held `legatoFrames`. After a consonant (process()'s `onset`) the next steady pitch picks the
    // note at once, as a singer's syllable does.
    float legatoSemis   = 0.75f;   // chosen against one real voice and the synthetic melodies (docs/pitch-tracking.md)
    int   legatoFrames  = 10;      // 50 ms
    float bendAmount    = 0.4f;    // share of the pitch's wander around the note that goes out as bend (0 = locked to the note)
    float bendSmooth    = 0.04f;   // s, smoothing of that bend
    bool  glide         = false;   // true: one note per phrase, and the bend carries the whole melody
    float glideRange    = 24.0f;   // semitones the bend may reach in glide mode
    /** Optional: the note a pitch belongs to (a scale, a tuning's degrees). Default: the nearest semitone. */
    int (*quantise) (float pitchNote, void* context) = nullptr;
    void* quantiseContext = nullptr;

    struct Event { enum Type { kNone, kNoteOn, kNoteOff, kBend } type = kNone; int note = -1; float bend = 0; };

    // --- state, readable
    int   note = -1;               // the note sounding, -1 if none
    float bend = 0;                // semitones from it

    void reset() { note = -1; bend = 0; bendS = 0; n = 0; away = 0; silent = 0; sinceOnset = 1e9f; }

    /** One frame. `pitchNote` and `periodicity` are VoiceTracker's (-1 if unvoiced), `open` its mouth state, `dt` the
        frame's length in seconds. Writes up to three events to `out` (a new note comes before the old one's
        off, so a monophonic instrument slurs) and returns how many. */
    int process (float pitchNote, float periodicity, bool open, float dt, Event out[3], bool onset = false)
    {
        int count = 0;
        if (onset) sinceOnset = 0.0f; else sinceOnset += dt;
        lastDt = dt;
        const bool valid = pitchNote > 0.0f && periodicity >= minPeriodicity;
        if (! valid)
        {
            n = 0; away = 0;
            if (note >= 0)
            {
                silent += dt;
                if (silent > (open ? 2.0f * holdSeconds : holdSeconds)) { out[count++] = { Event::kNoteOff, note, 0 }; note = -1; bend = 0; }
            }
            return count;
        }
        silent = 0;
        const float p = pitchNote + transpose;
        if (n < kHist) hist[n++] = p; else { for (int i = 1; i < kHist; ++i) hist[i - 1] = hist[i]; hist[kHist - 1] = p; }
        const int k = std::min (n, std::max (2, stableFrames));
        float lo = 1e9f, hi = -1e9f, sum = 0;
        for (int i = n - k; i < n; ++i) { lo = std::min (lo, hist[i]); hi = std::max (hi, hist[i]); sum += hist[i]; }
        const bool still = n >= stableFrames && hi - lo <= stableSemis;
        const float centre = sum / (float) k;

        if (note < 0)
        {
            if (! still) return 0;
            note = pick (centre); bend = (glide ? 1.0f : bendAmount) * (centre - (float) note); bendS = bend; away = 0;
            out[count++] = { Event::kNoteOn, note, bend };
            out[count++] = { Event::kBend, note, bend };
            return count;
        }
        const float dev = p - (float) note;
        if (glide)
        {
            setBend (std::clamp (dev, -glideRange, glideRange), out, count);
            return count;
        }
        const bool afterConsonant = sinceOnset < 0.25f;      // a new syllable: the note may change readily
        const float threshold = afterConsonant ? changeSemis : legatoSemis;
        if (std::abs (dev) > threshold)
        {
            ++away;
            const float a = std::abs (dev);
            const bool octaveLike = std::abs (a - 12.0f) < 0.6f || std::abs (a - 19.0f) < 0.6f || std::abs (a - 24.0f) < 0.6f;
            const int need = octaveLike ? octaveFrames : ! afterConsonant ? legatoFrames : a < 1.5f ? semitoneFrames : changeFrames;
            if (still && hi - lo <= settledSemis && away >= need && pick (centre) != note)
            {
                const int old = note;
                note = pick (centre); bend = bendAmount * (centre - (float) note); bendS = bend; away = 0;
                out[count++] = { Event::kNoteOn, note, bend };
                out[count++] = { Event::kNoteOff, old, 0 };
                out[count++] = { Event::kBend, note, bend };
                return count;
            }
            // on the way somewhere: follow it a little past the threshold, so a slide is heard as a slide
            if (! octaveLike) setBend (std::clamp (dev, -threshold - 0.5f, threshold + 0.5f), out, count);
        }
        else { away = 0; setBend (dev, out, count); }
        return count;
    }

private:
    static constexpr int kHist = 16;
    float hist[kHist] = {}; int n = 0, away = 0; float silent = 0, sinceOnset = 1e9f, lastDt = 0.005f, bendS = 0;

    int pick (float pitch) const { return quantise != nullptr ? quantise (pitch, quantiseContext) : (int) std::lround (pitch); }
    void setBend (float b, Event* out, int& count)
    {
        // only part of the wander goes out (glide mode keeps it all: the bend is the melody there), smoothed
        const float target = glide ? b : bendAmount * b;
        bendS += (1.0f - std::exp (-lastDt / std::max (0.001f, bendSmooth))) * (target - bendS);
        if (std::abs (bendS - bend) < 0.02f) return;
        bend = bendS; out[count++] = { Event::kBend, note, bend };
    }
};

} // namespace windcore
