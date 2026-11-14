// WindCore: a reed woodwind core built from published acoustics.
//
// Exciter x resonator x mouth, each replaceable:
//   - Reed: one damped mode driving a Bernoulli flow through the reed channel
//     (Wilson & Beavers 1974; Guillemain, Kergomard & Voinier, JASA 118, 2005).
//   - Bore: a truncated cone as a lossy cylinder of the bore's length in parallel with
//     the inertance of the missing cone tip (Dalmont, Gilbert & Kergomard 2000;
//     Kergomard et al., Acta Acustica 105, 2019), plus a mouthpiece chamber volume.
//     With the missing cone made very long, the bore becomes a cylinder (clarinet).
//   - Tone-hole lattice: waves above the cutoff leave the bore instead of reflecting
//     (Benade 1976); the radiated sound is the part that isn't reflected.
//   - Mouth: the player's vocal tract as resonances in series with the bore, upstream
//     of the reed (Scavone 2003; Chen, Smith & Wolfe 2011; Li et al. 2015).
//   - Keys: a note change crossfades between two bore lengths, as a tone hole that
//     opens over a few milliseconds.
//
// Pressures are in units of the reed's closing pressure, flows in units of that
// pressure over the bore's characteristic impedance. No dependencies; header only.
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <array>

namespace windcore
{

inline constexpr double kPi = 3.14159265358979323846;   // M_PI is not standard (MSVC leaves it undefined)

struct Params
{
    // --- Mouth (vocal tract), fixed in Hz
    float mouthFreq   = 1000.0f;  // Hz, first tract resonance
    float mouthDepth  = 0.5f;     // peak impedance, in bore impedances (0 = no tract). The horn's curves were fitted with it at 0.5.
    float mouthQ      = 5.0f;
    float mouth2Ratio = 2.3f;     // second resonance = mouthFreq * ratio
    float mouth2Level = 0.5f;     // its depth relative to the first

    // --- Reed and embouchure
    float reedFreq    = 2600.0f;  // Hz, reed resonance (stiffness with the lip on it)
    float reedLift    = 2.25f;    // the reed resonance never sits below this multiple of the note (lip moves up the reed); at 2.5 and up the top octave gets a second, flat regime
    float reedDamp    = 0.9f;     // lip damping (q): low = edgy and squeal-prone, high = dull
    float aperture    = 1.3f;     // zeta: tip opening / embouchure looseness
    float lay         = 0.06f;    // how gradually the reed closes on the mouthpiece lay
    float air         = 0.02f;    // turbulence: noise proportional to the flow past the reed
    float airTone     = 5000.0f;  // Hz, lowpass on that noise
    float airTilt     = -0.45f;   // dB per semitone above C4 (negative: noisier low, cleaner high, as measured on saxophones)

    // --- Horn
    float coneBeta    = 0.30f;    // missing cone as a share of the full cone (reed-closed share of the cycle)
    float topNote     = 99.0f;    // MIDI note: notes above it are silent (slurring down from them can leave G6 and up a semitone flat)
    float hornTop     = 98.0f;    // MIDI note: higher notes play on the horn shaped for this one (it was fitted up to here)
    float coneMax     = 0.45f;    // m, largest missing-cone length (the low horn's size)
    float chamber     = 1.3f;     // mouthpiece chamber volume / missing-cone volume (1 = in tune with itself)
    float baffle      = 3.0f;     // above this (in units of c / missing-cone length) the chamber stops absorbing: higher = darker
    float cutoffK     = 200.0f;   // Hz*m: tone-hole cutoff = cutoffK / missing-cone length
    float cutoffMin   = 450.0f;   // Hz
    float boreLoss    = 0.99f;    // reflection gain at low frequency
    float lossHz      = 16000.0f; // wall-loss lowpass, upper limit
    float lossRatio   = 8.0f;     // wall-loss lowpass as a multiple of the note (long tubes lose more)
    float bell        = 8.0f;     // dB, shelf above 2 kHz on the radiated sound

    // --- Playing
    float breathFloor = 0.45f;    // mouth pressure / reed closing pressure at the softest breath
    float breathTop   = 0.60f;    // the same at full breath
    float breathCurve = 1.0f;     // breath to loudness curve
    float embMin      = 0.04f;    // embouchure opening at the softest breath (full breath = 1)
    float embTrack    = 0.25f;    // how much the aperture follows the embouchure (0.5 = physical)
    float loudOpen    = 0.0f;     // the reed opens this much more at full breath (soft notes unchanged): the tone brightens as you blow harder
    float embFloor    = 0.0f;     // share of the aperture that stays whatever the embouchure (keeps soft notes speaking)
    float breathKnee  = 0.03f;    // below this breath the pressure falls to nothing
    float breathAttack  = 0.012f; // s
    float breathRelease = 0.012f; // s
    float keyTime     = 0.010f;   // s, a key opening or closing in a slur
    float tongueTime  = 0.005f;   // s, the tongue leaving the reed
    float tongueStop  = 0.010f;   // s, the tongue returning
    float reedComp    = 1.0f;     // how much of the reed's lag is tuned out

    // --- Body: peaks and dips that stay at fixed frequencies while the notes move (they scale with the horn's size)
    float body        = 0.0f;     // depth of the pattern: 0 = none, 1 = as measured on tenor and baritone sources
    float bodySize    = 1.0f;     // all its frequencies times this (below 1 = a larger body)

    // --- Mouth as heard: two vowel resonances on the radiated sound, fixed in Hz whatever the note or the horn's size.
    // (The tract upstream of the reed, mouthFreq and mouthDepth above, is the physical version; it makes no formant.)
    float vowel       = 0.5f;     // 0 = oo, 0.5 = ah, 1 = ee
    float vowelDepth  = 0.0f;     // dB at the upper resonance (the lower one gets three quarters of it); 0 = off
    float vowelQ      = 4.5f;
    float vowelF1     = 0.0f;     // Hz: with vowelF2, the two resonances given directly (for a tracked voice) in place of vowel; 0 = use vowel
    float vowelF2     = 0.0f;

    // --- Life: what a player's body does to a held note (all 0 = a perfectly still player)
    float wobble      = 0.0f;     // dB, slow random movement of the breath (level)
    float shimmer     = 0.0f;     // share, slow random movement of the embouchure (brightness, apart from level)
    float drift       = 0.0f;     // cents, slow random movement of pitch
    float vibDepth    = 0.0f;     // cents, vibrato depth (each way)
    float vibRate     = 5.2f;     // Hz
    float vibDelay    = 0.25f;    // s into a note before vibrato starts to grow
    float slurDip     = 0.0f;     // share of the breath given up for a moment at a slurred note change
    float slurTime    = 0.035f;   // s, length of that dip

    // --- Onset: turbulent air at the start of a note, injected into the reed's flow so the horn pitches it
    // (Scavone 1996: releasing the tongue gives a rush of pressure and turbulent flow). 0 = none.
    float chiff       = 0.0f;     // at a tongued start or after silence
    float chiffTime   = 0.015f;   // s, the burst's rise; it lasts about six times this (0.015 gives saxophones' 8-16 ms unpitched start)
    float keyNoise    = 0.0f;     // a smaller burst when the fingers move in a slur
    float chiffDirect = 0.8f;     // share of the burst heard directly (unpitched) rather than through the horn, for an air chiff
    float bloom       = 0.0f;     // s: how long brightness takes to follow breath (90% of a change); needs respond > 0
    float weight      = 0.0f;     // s added to the breath attack and release: how heavy the air column feels
    float respond     = 0.0f;     // 0..1: how much the tone darkens below full breath (0 = same colour at every level). Brightness lags breath by `bloom`.
    float scoop       = 0.0f;     // cents: pitch offset at a tongued start, gone in about 60 ms (negative = starts flat, as reeds do)
    float chiffColor  = 1.0f;     // 0 = reed: dark noise, mostly through the horn (saxophone, clarinet); 1 = air: bright hiss, mostly heard directly (flute)
    float glide       = 0.006f;   // s, smoothing of horn and embouchure settings

    // --- Voice in the mouth: a signal given with setMouthInput (a sung or spoken voice, about +-1).
    // Both 0 = none, and the engine is exactly as without it.
    float voiceSing   = 0.0f;     // the signal as a share of the mouth pressure: singing into the horn (the reed's flow carries it, the horn pitches it)
    float voiceAir    = 0.0f;     // the signal as the turbulence the flow carries, in place of noise: a whisper becomes the breath noise
    float outGain     = 1.0f;
};

// Per-note corrections: tuning as a curve over the embouchure opening (soft to loud), and level. An engine reads them through a pointer, so several engines
// can share one set and a control layer can rewrite it between blocks.
// The horn across the range: a multiplier per note for each of these settings, and a number of
// dB per note added to the bell. They stand where a saxophone family's different sizes would:
// one continuous instrument whose proportions change from the bottom to the top.
enum Curve { kCutoff, kLossRatio, kConeBeta, kChamber, kBaffle, kAperture, kLay, kEmbTrack, kBoreLoss, kNumCurves };

struct Tables
{
    float tuneCents[128] = {};
    float tuneLoud[128] = {};
    float tuneBend[128] = {};      // cents = tuneCents + tuneLoud * e + tuneBend * e * e, e = square root of the embouchure opening
    float gainDb[128] = {};
    float curve[kNumCurves][128];
    float bellAdd[128] = {};
    float floorAdd[128] = {};      // added to embFloor
    Tables() { for (auto& c : curve) for (auto& v : c) v = 1.0f; }
};

class Engine
{
public:
    void prepare (double hostRate)
    {
        fsHost = hostRate;
        fs = hostRate * kOS;
        nzLoA = (float) (1.0 - std::exp (-2 * kPi * 700.0 / fs)); nzRA = (float) (1.0 - std::exp (-2 * kPi * 1800.0 / fs)); nzRloA = (float) (1.0 - std::exp (-2 * kPi * 150.0 / fs));
        T = 1.0 / fs;
        designDecimator();
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        w = 0; uM = 0; pPrev = 0; pC = 0; rx = 0; rv = 0; gamma = 0; pM = p.embMin; pMTarget = p.embMin; tongue = 0; tongueTarget = 0;
        lp1 = 0; bz1 = bz2 = 0; nz = 0; uPrev = 0;
        for (auto& m : mouth) m = {};
        for (auto& t : taps) t = {};
        std::memset (dec, 0, sizeof dec); decPos = 0;
        sh1 = 0; tick = 0; held = 0; curNote = -1; curTap = -1; x1 = -1; rng = seed; bendSemis = 0;
        zetaS = -1; first = true; breath = 0; zetaE = 0; noteS = -1; above = false; q = p; burstT = 0; burstAmp = 0; tongueWasLow = true; nzLo = nzR = nzRlo = 0; bl1 = bl2 = 0; respS = 0; respA = 1; respMix = 1; scoopNow = 0; respTick = 0; for (auto& pk : peaks) pk = {}; bodyScale = bodyDone = bodyAmt = -1; for (auto& pk : vow) pk = {}; vowS = -1; vowDepthS = 0; vowDone = vowDepthDone = -9; vowF1S = vowF2S = vowF1Done = vowF2Done = 0; voiceIn = voicePrev = voiceNow = 0; for (auto& v : ln) v = 0; lifeBreath = lifeEmb = 1; lifeCents = 0; vibPhase = 0; noteAge = 0; slurPhase = 1; lifeRng = seed * 2654435761u + 0x9E3779B1u;
    }

    Params p;                      // the settings as set; the horn's curves act on top of them
    uint32_t seed = 0x12345u;      // air noise; give stacked engines different ones
    Tables own;                    // used unless shared tables are set
    const Tables* shared = nullptr;
    const Tables& tables() const { return shared != nullptr ? *shared : own; }

    /** Pitch-driven use (a host voice): the note as a fractional MIDI number, without bend.
        A jump of more than half a semitone is a key change; smaller moves retune the horn. */
    void setPitch (float note, float bendSemitones)
    {
        bendSemis = bendSemitones;
        note = std::clamp (note, 0.0f, 127.0f);
        above = note > p.topNote + 0.5f;
        if (above) { tongueTarget = 0; return; }      // out of range: silent, and the horn keeps its last note
        if (curTap >= 0 && std::abs (note - taps[curTap].note) <= 0.5f) { taps[curTap].note = note; return; }
        keyTo (note);
    }
    void setGate (bool on) { tongueTarget = on && ! above ? 1.0f : 0.0f; }

    void setBreath (float b)   { breath = std::clamp (b, 0.0f, 1.0f); }
    void setBend (float semis) { bendSemis = semis; }

    /** One host-rate sample of a signal in the player's mouth (see voiceSing, voiceAir). Call before tickHost. */
    void setMouthInput (float v) { voicePrev = voiceIn; voiceIn = v; }
    /** A tongue stroke on a sounding note: the reed is stopped and released, with the onset burst and scoop. */
    void tongueStroke() { tongue = 0; tongueWasLow = true; }

    void noteOn (int note)
    {
        note = std::clamp (note, 0, 127);
        ++held;
        above = (float) note > p.topNote;
        if (above) { tongueTarget = 0; return; }      // out of range: silent, and the horn keeps its last note
        keyTo ((float) note);
        tongueTarget = 1;
    }

    void noteOff (int note)
    {
        if (held > 0) --held;
        if (held == 0 && note == curNote) tongueTarget = 0;
        else if (held == 0) tongueTarget = 0;
    }

    // One host-rate sample.
    float tickHost()
    {
        float y = 0;
        for (int k = 0; k < kOS; ++k)
        {
            voiceNow = voicePrev + (voiceIn - voicePrev) * (float) (k + 1) / (float) kOS;
            const float v = tickInternal();
            dec[decPos] = v; dec[decPos + kDecN] = v;
            decPos = (decPos + 1) % kDecN;
        }
        const float* d = dec + decPos;
        for (int i = 0; i < kDecN; ++i) y += d[i] * decH[i];
        // response: the tone opens with breath. A two-pole lowpass whose cutoff rises from just above the note
        // to fully open as breath goes from nothing to full; breath is followed with a lag (bloom), so the upper
        // harmonics arrive after the fundamental at a start and trail a crescendo, as on a saxophone.
        if (p.respond > 0.001f)
        {
            if ((respTick++ & 15) == 0)
            {
                const float tau = std::max (0.002f, p.bloom / 2.3f);
                respS += (1.0f - std::exp (-16.0f / ((float) fsHost * tau))) * (breath - respS);
                const float w = std::clamp (1.0f - p.respond * (1.0f - respS), 0.0f, 1.0f);
                const float f0 = (float) noteHz (curNote < 0 ? 60 : curNote) * 1.2f;
                const float fc = std::min (0.45f * (float) fsHost, f0 * std::pow (std::max (1.0f, 12000.0f / f0), w * std::sqrt (w)));
                respA = 1.0f - std::exp (-2.0f * (float) kPi * fc / (float) fsHost); respMix = w * w * w;
            }
            bl1 += respA * (y - bl1); bl2 += respA * (bl1 - bl2);
            y = bl2 + respMix * (y - bl2);
        }
        else { bl1 = bl2 = y; respS = breath; }
        // bell shelf (first-order high shelf at 2 kHz)
        const float lpv = sh1 + shA * (y - sh1); sh1 = lpv;
        y = lpv + shG * (y - lpv);
        if (bodyAmt > 0)
            for (auto& k : peaks)
            {
                const float o = k.b0 * y + k.z1;
                k.z1 = k.b1 * y - k.a1 * o + k.z2; k.z2 = k.b2 * y - k.a2 * o; y = o;
            }
        if (vowDepthDone > 0.01f)
            for (auto& k : vow)
            {
                const float o = k.b0 * y + k.z1;
                k.z1 = k.b1 * y - k.a1 * o + k.z2; k.z2 = k.b2 * y - k.a2 * o; y = o;
            }
        return y * outG;
    }

    // exposed for the bench
    float lastPressure = 0, lastFlow = 0, lastOpen = 0;

private:
    static constexpr int kOS = 2, kTaps = 4, kBuf = 8192, kDecN = 48, kCtl = 16;
    static constexpr double kC = 343.0;

    struct Tap { float note = -1, g = 0, target = 0; double d = -1, dInc = 0; };
    struct Mode { float s1 = 0, s2 = 0, g = 0, k = 0, den = 0, A = 0; };

    double fsHost = 48000, fs = 96000, T = 1.0 / 96000;
    std::array<float, kBuf> buf {}; int w = 0, curTap = -1;
    Tap taps[kTaps];
    Mode mouth[2];
    float uM = 0, pPrev = 0, rx = 0, rv = 0, gamma = 0, tongue = 0, tongueTarget = 0, breath = 0;
    float lp1 = 0, lpA = 1, bz1 = 0, bz2 = 0, b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float nz = 0, nzA = 1, uPrev = 0;
    float aIn = 0, bCh = 0, rCh = 0, gCh = 0, pC = 0, zetaS = -1, layS = 0.06f, wr = 0, qr = 1, airS = 0;
    float gAtk = 0, gRel = 0, tUp = 0, tDown = 0, gTarget = 0, keyRate = 0, pMTarget = 1, pM = 1, zetaE = 0;
    float dec[2 * kDecN] = {}, decH[kDecN] = {}; int decPos = 0;
    float sh1 = 0, shA = 0, shG = 1, outG = 1;
    Params q;                      // settings in force at the note being played
    float noteS = -1;
    float burstT = 0, burstAmp = 0, burstTau = 0.008f; bool tongueWasLow = true;   // onset burst
    float bl1 = 0, bl2 = 0, respS = 0, respA = 1, respMix = 1, scoopNow = 0; unsigned respTick = 0;   // response and scoop state
    float nzLo = 0, nzLoA = 0.045f, nzR = 0, nzRA = 0.11f, nzRlo = 0, nzRloA = 0.0098f;   // fixed for 96 kHz internal rate
    bool above = false;            // the current note is above topNote
    // body: five peaking filters after the horn, at the host rate
    struct Peak { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; };
    Peak peaks[5]; float bodyScale = -1, bodyDone = -1, bodyAmt = -1;
    Peak vow[2]; float vowS = -1, vowDepthS = 0, vowDone = -9, vowDepthDone = -9;
    float vowF1S = 0, vowF2S = 0, vowF1Done = 0, vowF2Done = 0;
    float voiceIn = 0, voicePrev = 0, voiceNow = 0;                  // the signal in the mouth
    // life: four slow noises (two for breath, one embouchure, one pitch), vibrato phase, note age, slur dip
    float ln[4] = {}, lifeBreath = 1, lifeEmb = 1, lifeCents = 0, vibPhase = 0, noteAge = 0, slurPhase = 1;
    uint32_t lifeRng = 0x9E3779B1u;
    int tick = 0, held = 0, curNote = -1; uint32_t rng = 0x12345u;
    double x1 = -1; float bendSemis = 0; bool first = true;

    void designDecimator()
    {
        double sum = 0;
        for (int i = 0; i < kDecN; ++i)
        {
            const double n = i - (kDecN - 1) / 2.0, fc = 0.225;
            const double s = std::abs (n) < 1e-9 ? 2 * fc : std::sin (2 * kPi * fc * n) / (kPi * n);
            const double win = 0.42 - 0.5 * std::cos (2 * kPi * i / (kDecN - 1)) + 0.08 * std::cos (4 * kPi * i / (kDecN - 1));
            decH[i] = (float) (s * win); sum += decH[i];
        }
        for (auto& h : decH) h = (float) (h / sum);
    }

    static double noteHz (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); }

    // Phase lag of the reflection filter at w (rad/sample).
    double filterLag (double wn) const
    {
        const std::complex<double> z1 = std::polar (1.0, -wn), z2 = z1 * z1;
        const std::complex<double> h = ((double) b0 + (double) b1 * z1 + (double) b2 * z2) / (1.0 + (double) a1 * z1 + (double) a2 * z2)
                                       * ((double) lpA / (1.0 - (1.0 - (double) lpA) * z1));
        return -std::arg (h);
    }

    // Round-trip delay (samples) that puts the bore's first resonance at f.
    double delayFor (double f) const
    {
        const double wn = 2 * kPi * f * T;
        const double wc = wn * bCh, X = 0.5 * aIn / std::tan (0.5 * wn) - wc / (1.0 + wc * rCh * wc * rCh);
        const double theta = X > 1e-9 ? kPi - std::atan (1.0 / X) : 0.5 * kPi;
        // the reed answers late; that lag would flatten the note
        const double rr = wn / std::max (1e-6, (double) wr), reedLag = std::atan2 (qr * rr, 1.0 - rr * rr);
        return std::max (2.0, (2 * theta - filterLag (wn) - p.reedComp * reedLag) / wn);
    }

    void control()
    {
        const float dt = (float) (kCtl * T);
        const float sm = 1.0f - std::exp (-dt / std::max (1e-4f, p.glide));
        const float key = 1.0f - std::exp (-dt / std::max (1e-4f, p.keyTime * 0.4f));

        // Life: slow random movement of breath, embouchure and pitch, vibrato, and the dip at a slur.
        {
            static constexpr float kHz[4] = { 4.5f, 0.8f, 3.5f, 2.0f };
            for (int i = 0; i < 4; ++i)
            {
                lifeRng = lifeRng * 1664525u + 1013904223u;
                const float white = (float) (int32_t) lifeRng * (1.0f / 2147483648.0f);
                const float a = 1.0f - std::exp (-6.2831853f * kHz[i] * dt);
                ln[i] += a * (white * std::sqrt (3.0f * (2.0f - a) / a) - ln[i]);     // unit spread whatever the rate
            }
            noteAge += dt;
            slurPhase = std::min (1.0f, slurPhase + dt / std::max (0.005f, p.slurTime));
            const float hump = std::sin (3.14159265f * slurPhase);
            lifeBreath = std::pow (10.0f, p.wobble * 2.0f * std::tanh (0.5f * (0.5f * ln[0] + 0.85f * ln[1])) / 20.0f)   // mostly slow, never more than two spreads
                        * (1.0f - std::clamp (p.slurDip, 0.0f, 0.95f) * hump * hump);
            lifeEmb = 1.0f + p.shimmer * 2.0f * std::tanh (0.5f * ln[2]);
            const float grow = std::clamp ((noteAge - p.vibDelay) / 0.3f, 0.0f, 1.0f);
            vibPhase += 6.2831853f * p.vibRate * dt; if (vibPhase > 6.2831853f) vibPhase -= 6.2831853f;
            lifeCents = p.drift * 2.0f * std::tanh (0.5f * ln[3]) + p.vibDepth * grow * grow * (3.0f - 2.0f * grow) * std::sin (vibPhase);
        }
        // The horn's proportions at this note (gliding with the keys in a slur).
        {
            const Tables& tc = tables();
            const float nt = std::min ((float) std::clamp (curNote < 0 ? 60 : curNote, 0, 127), p.hornTop);
            noteS = noteS < 0 ? nt : noteS + key * (nt - noteS);
            const int n0 = std::clamp ((int) noteS, 0, 126); const float fr = noteS - (float) n0;
            auto at = [&] (const float* c) { return c[n0] + fr * (c[n0 + 1] - c[n0]); };
            q = p;
            q.cutoffK   = p.cutoffK * at (tc.curve[kCutoff]);
            q.lossRatio = p.lossRatio * at (tc.curve[kLossRatio]);
            q.coneBeta  = std::clamp (p.coneBeta * at (tc.curve[kConeBeta]), 0.115f, 0.42f);
            // a control and the curve multiply; together they stay inside the range the horn was fitted over
            q.chamber   = std::clamp (p.chamber * at (tc.curve[kChamber]), 0.9f, 2.2f);
            q.baffle    = std::clamp (p.baffle * at (tc.curve[kBaffle]), 1.0f, 12.0f);
            q.aperture  = std::clamp (p.aperture * at (tc.curve[kAperture]), 0.5f, 2.6f);
            q.lay       = std::clamp (p.lay * at (tc.curve[kLay]), 0.004f, 0.35f);
            q.embTrack  = std::clamp (p.embTrack * at (tc.curve[kEmbTrack]), 0.0f, 1.0f);   // above 1, pitch bends with breath around B3-C4
            q.boreLoss  = std::min (0.998f, p.boreLoss * at (tc.curve[kBoreLoss]));
            q.bell      = p.bell + at (tc.bellAdd);
            q.embFloor  = std::clamp (p.embFloor + at (tc.floorAdd), 0.0f, 0.95f);
        }
        // Mouth as heard: the two resonances glide when the vowel is played (about 20 ms), so it can sit on a joystick.
        {
            const float gl = 1.0f - std::exp (-dt / 0.02f);
            vowS = vowS < 0 ? p.vowel : vowS + gl * (std::clamp (p.vowel, 0.0f, 1.0f) - vowS);
            vowDepthS += gl * (std::max (0.0f, p.vowelDepth) - vowDepthS);
            const bool direct = p.vowelF1 > 0 && p.vowelF2 > 0;      // the two resonances given in Hz (a tracked voice)
            if (direct)
            {
                const float a = std::clamp (p.vowelF1, 150.0f, 1500.0f), b = std::clamp (p.vowelF2, 400.0f, 4000.0f);
                vowF1S = vowF1S > 0 ? vowF1S * std::pow (a / vowF1S, gl) : a; vowF2S = vowF2S > 0 ? vowF2S * std::pow (b / vowF2S, gl) : b;
            }
            else vowF1S = vowF2S = 0;
            const bool moved = direct && std::abs (vowF1S - vowF1Done) + std::abs (vowF2S - vowF2Done) > 0.003f * vowF1S;
            if (moved || (! direct && vowF1Done > 0) || std::abs (vowS - vowDone) > 0.002f || std::abs (vowDepthS - vowDepthDone) > 0.02f)
            {
                // oo (320, 900 Hz), ah (700, 1200), ee (300, 2300): straight lines between them in log frequency
                static constexpr float kF1[3] = { 320.0f, 700.0f, 300.0f }, kF2[3] = { 900.0f, 1200.0f, 2300.0f };
                const float u = vowS * 2.0f; const int i = u < 1.0f ? 0 : 1; const float t = u - (float) i;
                float f[2] = { kF1[i] * std::pow (kF1[i + 1] / kF1[i], t), kF2[i] * std::pow (kF2[i + 1] / kF2[i], t) };
                if (vowF1S > 0) { f[0] = vowF1S; f[1] = vowF2S; }
                const float g[2] = { 0.75f * vowDepthS, vowDepthS };
                for (int k = 0; k < 2; ++k)
                {
                    const float A = std::pow (10.0f, g[k] / 40.0f), w0 = 6.2831853f * std::min (0.45f * (float) fsHost, f[k]) / (float) fsHost;
                    const float al = std::sin (w0) / (2 * std::max (0.5f, p.vowelQ)), cs = std::cos (w0), a0 = 1 + al / A;
                    vow[k].b0 = (1 + al * A) / a0; vow[k].b1 = -2 * cs / a0; vow[k].b2 = (1 - al * A) / a0;
                    vow[k].a1 = -2 * cs / a0; vow[k].a2 = (1 - al / A) / a0;
                }
                vowDone = vowS; vowDepthDone = vowDepthS; vowF1Done = vowF1S; vowF2Done = vowF2S;
            }
        }
        // Body: the pattern's frequencies follow the size of the horn for this part of the range (a baritone's
        // sit lower than a tenor's, an alto's and a soprano's higher), moving slowly so a slur does not sweep them.
        {
            static constexpr float kNote[8]  = { 0, 45, 52, 64, 72, 78, 86, 127 };
            static constexpr float kScale[8] = { 0.73f, 0.73f, 1.0f, 1.0f, 1.12f, 1.3f, 1.75f, 1.75f };
            int i = 0; while (i < 6 && noteS > kNote[i + 1]) ++i;
            const float t = std::clamp ((noteS - kNote[i]) / (kNote[i + 1] - kNote[i]), 0.0f, 1.0f);
            const float target = (kScale[i] + t * (kScale[i + 1] - kScale[i])) * p.bodySize;
            bodyScale = bodyScale < 0 ? target : bodyScale + 0.003f * (target - bodyScale);
            if (std::abs (bodyScale - bodyDone) > 0.002f * bodyScale || std::abs (p.body - bodyAmt) > 1e-4f)
            {
                static constexpr float kHz[5] = { 450.0f, 800.0f, 1650.0f, 2800.0f, 5600.0f };
                static constexpr float kDb[5] = { 3.0f, -4.0f, 5.0f, 3.5f, -5.0f };
                static constexpr float kQ[5]  = { 1.4f, 2.0f, 1.6f, 1.5f, 0.9f };
                for (int k = 0; k < 5; ++k)
                {
                    const float A = std::pow (10.0f, p.body * kDb[k] / 40.0f);
                    const float w0 = 6.2831853f * std::min (0.45f * (float) fsHost, kHz[k] * bodyScale) / (float) fsHost;
                    const float al = std::sin (w0) / (2 * kQ[k]), cs = std::cos (w0), a0 = 1 + al / A;
                    peaks[k].b0 = (1 + al * A) / a0; peaks[k].b1 = -2 * cs / a0; peaks[k].b2 = (1 - al * A) / a0;
                    peaks[k].a1 = -2 * cs / a0; peaks[k].a2 = (1 - al / A) / a0;
                }
                bodyDone = bodyScale; bodyAmt = p.body;
            }
        }
        // The horn follows the note: missing cone = a share of the full cone, up to coneMax.
        const double fNow = noteHz (std::min ((float) (curNote < 0 ? 60 : curNote), p.hornTop));
        const double x1T = std::min ((double) p.coneMax, q.coneBeta * kC / (2 * fNow));
        x1 = (x1 < 0) ? x1T : x1 + key * (x1T - x1);
        aIn = (float) (kC / (x1 * fs));
        bCh = (float) (fs * q.chamber * x1 / (3.0 * kC));
        rCh = 3.0f / std::max (0.05f, q.chamber * q.baffle);   // the chamber is a volume behind a resistance
        gCh = 1.0f / (1.0f / std::max (1e-6f, bCh) + rCh);

        // Tone-hole cutoff: 2nd-order lowpass, gain boreLoss.
        {
            const double fc = std::clamp ((double) q.cutoffK / x1, (double) p.cutoffMin, 0.35 * fs);
            const double K = std::tan (kPi * fc * T), qf = 0.7071, n = 1.0 / (1 + K / qf + K * K);
            b0 = (float) (K * K * n * q.boreLoss); b1 = 2 * b0; b2 = b0;
            a1 = (float) (2 * (K * K - 1) * n); a2 = (float) ((1 - K / qf + K * K) * n);
            const double fl = std::min ({ (double) p.lossHz, 0.45 * fs, q.lossRatio * q.coneBeta * kC / (2 * x1) / 0.3 });
            lpA = (float) (1.0 - std::exp (-2 * kPi * fl * T));
        }

        if (zetaS < 0) { zetaS = q.aperture; layS = q.lay; airS = p.air; }
        zetaS += sm * (q.aperture * lifeEmb - zetaS); layS += sm * (q.lay - layS); airS += sm * (p.air * std::pow (10.0f, p.airTilt * (noteS - 60.0f) / 20.0f) - airS);
        wr = (float) (2 * kPi * std::min (0.2 * fs, std::max ((double) p.reedFreq, p.reedLift * q.coneBeta * kC / (2 * x1))) * T); qr = p.reedDamp;
        nzA = (float) (1.0 - std::exp (-2 * kPi * p.airTone * T));

        // Mouth resonances (state-variable band-passes with a known instantaneous part).
        for (int i = 0; i < 2; ++i)
        {
            auto& m = mouth[i];
            const double f = std::min (0.4 * fs, (double) p.mouthFreq * (i == 0 ? 1.0 : p.mouth2Ratio));
            const float g = (float) std::tan (kPi * f * T), k = 1.0f / std::max (0.5f, p.mouthQ);
            const float A = p.mouthDepth * (i == 0 ? 1.0f : p.mouth2Level);
            m.g = g; m.k = k; m.den = 1.0f / (1 + k * g + g * g);
            m.A += sm * (A - m.A);
        }

        // Breath to mouth pressure.
        // The player opens the embouchure as they blow harder: the reed's closing pressure pM
        // rises with breath, and the mouth pressure stays a playable share of it.
        {
            const float bb = std::pow (breath, p.breathCurve);
            pMTarget = p.embMin + (1.0f - p.embMin) * bb;
            const float share = p.breathFloor * std::min (1.0f, breath / std::max (1e-4f, p.breathKnee))
                              + (p.breathTop - p.breathFloor) * bb;
            pMTarget *= lifeBreath;
            gTarget = share * pMTarget;
        }
        scoopNow *= (float) std::exp (-kCtl * T / 0.045);
        gAtk = (float) (1.0 - std::exp (-T / std::max (1e-4f, p.breathAttack + p.weight)));
        gRel = (float) (1.0 - std::exp (-T / std::max (1e-4f, p.breathRelease + 0.5f * p.weight)));
        tUp = (float) (T / std::max (1e-4f, p.tongueTime)); tDown = (float) (T / std::max (1e-4f, p.tongueStop));
        keyRate = (float) (T / std::max (1e-4f, p.keyTime));

        // Tap delays (pitch, bend, tuning table).
        const Tables& tb = tables();
        const float embS = std::sqrt (pM);
        for (auto& t : taps)
        {
            if (t.note < 0) continue;
            const int ni = std::clamp ((int) std::lround (t.note), 0, 127);
            const double f = noteHz (t.note + bendSemis + 0.01f * (lifeCents + scoopNow) + (tb.tuneCents[ni] + (tb.tuneLoud[ni] + tb.tuneBend[ni] * embS) * embS) * 0.01);
            const double D = std::min ((double) kBuf - 8, delayFor (f));
            if (t.d < 0) { t.d = D; t.dInc = 0; } else t.dInc = (D - t.d) / kCtl;
        }

        const int ni = std::clamp (curNote, 0, 127);
        const float og = p.outGain * std::pow (10.0f, tb.gainDb[ni] / 20.0f);
        outG = first ? og : outG + key * (og - outG);
        shA = (float) (1.0 - std::exp (-2 * kPi * 2000.0 / fsHost)); shG = std::pow (10.0f, q.bell / 20.0f);
        first = false;
    }

    void keyTo (float note)
    {
        curNote = (int) std::lround (note);
        float gmax = 0;
        for (auto& t : taps) gmax = std::max (gmax, t.g);
        int best = 0;
        if (gmax <= 0)
        {
            // first note: nothing is sounding, so the horn can simply be set
            taps[0].g = 1;
            x1 = -1;
        }
        else
        {
            // every other note change is a key moving, tongued or not: the bore keeps its sound
            for (int i = 1; i < kTaps; ++i) if (taps[i].g < taps[best].g) best = i;
            for (auto& t : taps) t.target = 0;
            if (tongue > 0.5f) slurPhase = 0;      // a slur: the breath gives for a moment
            if (tongue > 0.5f && p.keyNoise > 0) { burstT = 0; burstAmp = p.keyNoise; burstTau = 0.6f * p.chiffTime; }
        }
        taps[best].note = note; taps[best].target = 1; taps[best].d = -1;
        curTap = best; noteAge = 0;
    }

    float readTap (double d) const
    {
        const int di = (int) d; const float fr = (float) (d - di);
        const int i = (w - di) & (kBuf - 1);
        const float* b = buf.data();
        const float y0 = b[(i + 1) & (kBuf - 1)], y1 = b[i], y2 = b[(i - 1) & (kBuf - 1)], y3 = b[(i - 2) & (kBuf - 1)];
        // 3rd-order Lagrange, fractional position fr between y1 (delay di) and y2 (delay di+1)
        const float c0 = y1, c1 = y2 - y0 * (1.0f / 3) - y1 * 0.5f - y3 * (1.0f / 6);
        const float c2 = 0.5f * (y0 + y2) - y1, c3 = (1.0f / 6) * (y3 - y0) + 0.5f * (y1 - y2);
        return ((c3 * fr + c2) * fr + c1) * fr + c0;
    }

    float tickInternal()
    {
        if (tick == 0) control();
        tick = (tick + 1) % kCtl;

        // --- bore: what comes back from the open end
        float pin = 0, gsum = 0;
        for (auto& t : taps)
        {
            if (t.target > t.g) t.g = std::min (t.target, t.g + keyRate);
            else if (t.target < t.g) t.g = std::max (t.target, t.g - keyRate);
            if (t.note >= 0) t.d += t.dInc;
            gsum += t.g;
        }
        if (gsum > 0)
        {
            float ssum = 0;
            for (auto& t : taps)
                if (t.g > 0)
                {
                    const float s = t.g / gsum, sm = s * s * (3 - 2 * s); // smooth share
                    pin += sm * readTap (t.d);
                    ssum += sm;
                }
            pin /= ssum;
        }
        lp1 += lpA * (pin - lp1);
        const float refl = b0 * lp1 + bz1;
        bz1 = b1 * lp1 - a1 * refl + bz2;
        bz2 = b2 * lp1 - a2 * refl;
        const float pMinus = -refl;
        const float radiated = pin - refl;

        // --- breath and tongue
        gamma += (gTarget > gamma ? gAtk : gRel) * (gTarget - gamma);
        pM += (pMTarget > pM ? gAtk : gRel) * (pMTarget - pM);
        zetaE = zetaS * std::sqrt (pM) * (q.embFloor + (1.0f - q.embFloor) * std::pow (pM, q.embTrack)) * (1.0f + p.loudOpen * pM * pM);
        if (tongueTarget > tongue) tongue = std::min (tongueTarget, tongue + tUp);
        else if (tongueTarget < tongue) tongue = std::max (tongueTarget, tongue - tDown);
        // onset: the tongue leaving the reed (or the first air after silence) starts a burst
        if (tongue < 0.02f) tongueWasLow = true;
        else if (tongueWasLow && tongue > 0.05f)
        {
            tongueWasLow = false;
            if (p.chiff > 0) { burstT = 0; burstAmp = p.chiff; burstTau = std::max (0.001f, p.chiffTime); }
            scoopNow = p.scoop;
        }

        // --- mouth: pressure at the reed = gamma - (tract impedance * flow)
        float Sm = 0, Bm = 0;
        for (auto& m : mouth)
        {
            if (m.A <= 0) continue;
            Sm += m.A * m.k * (m.s1 - m.g * m.s2) * m.den;
            Bm += m.A * m.k * m.g * m.den;
        }

        // --- bore seen from the reed: p = V + W u
        const float W = 1.0f / (1.0f + 0.5f * aIn + gCh);
        const float V = W * (2 * pMinus - uM - 0.5f * aIn * pPrev + gCh * pC);

        // --- reed channel flow, solved exactly with the linear parts
        const float o = 1.0f + rx;
        const float open = 0.5f * (o + std::sqrt (o * o + layS * layS));
        const float kz = zetaE * open * tongue * tongue * (3 - 2 * tongue);
        const float A = gamma * (1.0f + p.voiceSing * voiceNow) - Sm - V, B = W + Bm, Bk = B * kz;
        float u;
        if (A >= 0) u =  kz * 0.5f * (-Bk + std::sqrt (Bk * Bk + 4 * A));
        else        u = -kz * 0.5f * (-Bk + std::sqrt (Bk * Bk - 4 * A));
        const float dp = A - B * u;

        // turbulence: noise carried by the flow
        rng = rng * 1664525u + 1013904223u;
        const float white = (float) (int32_t) rng * (1.0f / 2147483648.0f);
        nz += nzA * (white - nz);
        float un = airS * std::abs (u) * nz * 8.0f + p.voiceAir * std::abs (u) * voiceNow;
        float direct = 0;
        if (burstAmp > 0)
        {
            burstT += (float) T;
            const float xb = burstT / burstTau;
            const float e = burstAmp * xb * std::exp (1.0f - xb) * std::sqrt (std::max (0.0f, gamma));   // grows with breath
            const float c = std::clamp (p.chiffColor, 0.0f, 1.0f);
            const float share = 0.25f + c * (p.chiffDirect - 0.25f);          // reed: a quarter heard directly; air: chiffDirect
            nzLo += nzLoA * (nz - nzLo);
            nzR += nzRA * (white - nzR); nzRlo += nzRloA * (nzR - nzRlo);
            un += (1.0f - share) * e * (c * nz + (1.0f - c) * 2.0f * nzR) * 1.5f;   // through the horn: pitched (darker for a reed)
            const float air = (nz - nzLo) * 0.35f;                           // air: noise from about 700 Hz to 5 kHz
            const float reed = (nzR - nzRlo) * 0.9f;                         // reed: rougher, darker noise, about 150 Hz to 1.8 kHz
            direct = share * e * (c * air + (1.0f - c) * reed);
            if (xb > 10.0f) burstAmp = 0;
        }

        const float pr = V + W * (u + un);

        // --- state updates
        uM += 0.5f * aIn * (pr + pPrev);
        uM *= 0.99995f;
        pPrev = pr;
        pC += gCh * (pr - pC) / std::max (1e-6f, bCh);
        buf.data()[w] = pr - pMinus;     // outgoing wave
        w = (w + 1) & (kBuf - 1);

        for (auto& m : mouth)
        {
            if (m.A <= 0) { m.s1 = m.s2 = 0; continue; }
            const float bp = (m.g * (u - m.s2) + m.s1) * m.den;   // band-pass state, unit drive
            const float v1 = bp - m.s1;
            m.s1 = bp + v1;
            const float lp = m.s2 + m.g * bp;
            m.s2 = lp + m.g * bp;
        }

        rv += wr * (wr * (-dp / pM - rx) - qr * rv);   // rv is velocity * T
        rx += rv;
        if (rx < -1.5f) { rx = -1.5f; rv = 0; }

        lastPressure = pr; lastFlow = u; lastOpen = open;
        return radiated + direct;
    }
};

} // namespace windcore
