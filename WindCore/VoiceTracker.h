// VoiceTracker: what a voice is doing, for playing WindCore with it.
//
// One sample in at a time, no allocation, no dependencies. Two paths:
//   - Level (every sample): a 12 ms running power gives the level, whether the mouth is open or closed
//     (a closure is the silence before "t", "k", "p"), and onsets: the level jumping well above its
//     lowest value of the last 50 ms. An onset is reported within a few milliseconds, with how bright
//     the first sound is (a "t" is bright, a "p" dark).
//   - Vowel (every 5 ms): the last 25 ms, brought down to about 12 kHz, gives the first two formants by
//     linear prediction (Markel & Gray 1976), whether the sound is voiced, and its pitch (reported, not
//     used yet). The formants give a position on WindCore's vowel line, oo .. ah .. ee.
//
// Known limits: formants of high voices (above about 300 Hz) are pulled toward the harmonics; voiced
// stops ("b", "d", "g") dip less than voiceless ones and need a lower `rise`.
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>

namespace windcore
{

class VoiceTracker
{
public:
    // --- settings
    float openDb     = -32.0f;   // dB below the loudest recent level: above this the mouth is open
    float closeDb    = -38.0f;   // below this it is closed (a closure, or silence)
    float floorDb    = -62.0f;   // dB full scale: nothing below this counts
    float rise       = 10.0f;    // dB above the lowest level of the last 50 ms that makes an onset
    float dip        = 18.0f;    // dB below the loudest recent level that lowest level must be: a swell or a glide between vowels is no onset
    float refractory = 0.06f;    // s, least time between onsets
    float voiceSize  = 1.0f;     // formants are divided by this before the vowel line (above 1 = a smaller head than the line's)
    float smooth     = 0.025f;   // s, smoothing of the formants
    float emphasis   = 0.7f;     // high-frequency emphasis before the formant analysis (more pulls a low first formant up)

    enum State { kClosed, kBurst, kVoiced, kNoise };

    // --- results (level path: current; vowel path: as of the last 5 ms frame)
    float levelDb = -120, peakDb = -40;      // level, and the loudest recent level (falls 3 dB a second)
    bool  open = false;
    State state = kClosed;
    float onsetBright = 0;                   // 0..1 at the last onset: share of its power above 3 kHz
    float onsetLevel = 0;                    // 0..1: how loud the last onset is going to be (from the level 10 ms on, updated then)
    float f1 = 500, f2 = 1500;               // Hz, smoothed
    float vowel = 0.5f;                      // 0 = oo, 0.5 = ah, 1 = ee
    float vowelFit = 0;                      // 0..1: 1 = on the line, 0 = an octave or more from it
    float voiced = 0;                        // 0..1, periodicity
    float pitchHz = 0;                       // of the voice, 0 if unvoiced
    bool  frameReady = false;                // true for the sample on which the vowel path updated

    void prepare (double rate)
    {
        fs = rate;
        nWin = std::clamp ((int) std::lround (0.012 * fs), 8, kMaxWin);
        hp = (float) std::exp (-2.0 * 3.14159265358979 * 70.0 / fs);
        hfA = (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * 3000.0 / fs));
        msEvery = std::max (1, (int) std::lround (fs / 1000.0));
        D = std::max (1, (int) std::lround (fs / 12000.0)); fsA = fs / D;
        nTap = std::min (kMaxTap, 8 * D + 1);
        double sum = 0;
        for (int i = 0; i < nTap; ++i)
        {
            const double n = i - (nTap - 1) / 2.0, fc = 0.45 / D;
            const double s = std::abs (n) < 1e-9 ? 2 * fc : std::sin (2 * 3.14159265358979 * fc * n) / (3.14159265358979 * n);
            const double w = nTap > 1 ? 0.42 - 0.5 * std::cos (2 * 3.14159265358979 * i / (nTap - 1)) + 0.08 * std::cos (4 * 3.14159265358979 * i / (nTap - 1)) : 1.0;
            taps[i] = (float) (s * w); sum += taps[i];
        }
        for (int i = 0; i < nTap; ++i) taps[i] = (float) (taps[i] / sum);
        nFrame = std::clamp ((int) std::lround (0.025 * fsA), 64, kMaxFrame);
        nHop = std::max (1, (int) std::lround (0.005 * fsA));
        for (int i = 0; i < nFrame; ++i) window[i] = (float) (0.54 - 0.46 * std::cos (2 * 3.14159265358979 * i / (nFrame - 1)));
        reset();
    }

    void reset()
    {
        std::fill (pw, pw + kMaxWin, 0.0f); std::fill (ph, ph + kMaxWin, 0.0f); std::fill (in, in + kMaxTap, 0.0f);
        std::fill (frame, frame + kMaxFrame, 0.0f); std::fill (mins, mins + kMins, -120.0f); std::fill (med1, med1 + 3, 500.0f); std::fill (med2, med2 + 3, 1500.0f);
        wi = ii = fi = mi = 0; sumP = sumH = 0; x1 = y1 = hfLp = 0; msCount = decCount = hopCount = 0;
        sinceOnset = 1e9; levelDb = -120; peakDb = -40; open = false; state = kClosed; onsetBright = onsetLevel = 0;
        f1 = 500; f2 = 1500; vowel = 0.5f; vowelFit = 0; voiced = 0; pitchHz = 0; frameReady = false; pendingLevel = false; samples = 0;
    }

    /** One sample. Returns true on the sample where an onset is found. */
    bool process (float x)
    {
        frameReady = false; ++samples;
        // level: power over 12 ms, all of it and the part above 3 kHz
        const float y = x - x1 + hp * y1; x1 = x; y1 = y;                    // no rumble
        hfLp += hfA * (y - hfLp); const float h = y - hfLp;
        const float p2 = y * y, h2 = h * h;
        sumP += (double) p2 - (double) pw[wi]; sumH += (double) h2 - (double) ph[wi]; pw[wi] = p2; ph[wi] = h2; wi = (wi + 1) % nWin;
        if (wi == 0) { sumP = 0; sumH = 0; for (int i = 0; i < nWin; ++i) { sumP += pw[i]; sumH += ph[i]; } }   // no drift
        levelDb = 10.0f * std::log10 ((float) std::max (0.0, sumP) / (float) nWin + 1e-12f);
        peakDb = std::max ({ levelDb, peakDb - 3.0f / (float) fs, floorDb + 20.0f });
        const float rel = levelDb - peakDb;
        if (! open && rel > openDb && levelDb > floorDb) open = true;
        else if (open && (rel < closeDb || levelDb < floorDb)) open = false;

        // onset: the level well above its lowest of the last 50 ms
        bool onset = false;
        float low = mins[0]; for (int i = 1; i < kMins; ++i) low = std::min (low, mins[i]);
        low = std::max (low, floorDb);
        sinceOnset += 1.0 / fs;
        if (levelDb - low > rise && low < peakDb - dip && rel > openDb && levelDb > floorDb && sinceOnset > refractory && samples > (long) (0.05 * fs))
        {
            onset = true; sinceOnset = 0; pendingLevel = true;
            onsetBright = std::clamp ((float) (sumH / std::max (1e-12, sumP)), 0.0f, 1.0f);
            onsetLevel = std::clamp (1.0f + rel / 30.0f, 0.0f, 1.0f);
            open = true;
        }
        if (pendingLevel && sinceOnset > 0.010) { pendingLevel = false; onsetLevel = std::clamp (1.0f + rel / 30.0f, 0.0f, 1.0f); }
        if (sinceOnset < refractory) std::fill (mins, mins + kMins, levelDb);    // a consonant and its vowel are one onset: the next needs a new dip
        else if (++msCount >= msEvery) { msCount = 0; mins[mi] = levelDb; mi = (mi + 1) % kMins; }
        else mins[(mi + kMins - 1) % kMins] = std::min (mins[(mi + kMins - 1) % kMins], levelDb);

        // vowel path: decimate, and every 5 ms look at the last 25 ms
        in[ii] = x; ii = (ii + 1) % nTap;
        if (++decCount >= D)
        {
            decCount = 0; float v = 0;
            for (int i = 0; i < nTap; ++i) v += taps[i] * in[(ii + i) % nTap];
            frame[fi] = v; fi = (fi + 1) % nFrame;
            if (++hopCount >= nHop) { hopCount = 0; analyse(); frameReady = true; }
        }

        if (! open) state = kClosed;
        else if (sinceOnset < 0.03 && voiced < 0.5f) state = kBurst;
        else state = voiced >= 0.5f ? kVoiced : kNoise;
        return onset;
    }

    /** Seconds from a sound arriving to the vowel path fully reflecting it (window, decimation and smoothing). */
    double vowelLatency() const { return 0.5 * nFrame / fsA + 0.5 * nTap / fs + smooth; }

    /** Where a pair of formants sits on WindCore's vowel line: 0 = oo (320, 900 Hz), 0.5 = ah (700, 1200), 1 = ee (300, 2300).
        `distance` gets how far off the line they are, in octaves. */
    static float vowelPosition (float F1, float F2, float* distance = nullptr)
    {
        static constexpr float kF1[3] = { 320.0f, 700.0f, 300.0f }, kF2[3] = { 900.0f, 1200.0f, 2300.0f };
        const float px = std::log2 (F1), py = std::log2 (F2);
        float best = 1e9f, pos = 0.5f;
        for (int i = 0; i < 2; ++i)
        {
            const float ax = std::log2 (kF1[i]), ay = std::log2 (kF2[i]), bx = std::log2 (kF1[i + 1]) - ax, by = std::log2 (kF2[i + 1]) - ay;
            const float t = std::clamp (((px - ax) * bx + (py - ay) * by) / (bx * bx + by * by), 0.0f, 1.0f);
            const float dx = px - ax - t * bx, dy = py - ay - t * by, d = std::sqrt (dx * dx + dy * dy);
            if (d < best) { best = d; pos = 0.5f * ((float) i + t); }
        }
        if (distance != nullptr) *distance = best;
        return pos;
    }

private:
    static constexpr int kMaxWin = 4096, kMaxTap = 257, kMaxFrame = 512, kMins = 50, kOrder = 12;

    double fs = 48000, fsA = 12000, sumP = 0, sumH = 0, sinceOnset = 1e9;
    int nWin = 576, D = 4, nTap = 33, nFrame = 300, nHop = 60, msEvery = 48;
    int wi = 0, ii = 0, fi = 0, mi = 0, msCount = 0, decCount = 0, hopCount = 0; long samples = 0;
    float hp = 0.99f, hfA = 0.3f, x1 = 0, y1 = 0, hfLp = 0; bool pendingLevel = false;
    float pw[kMaxWin], ph[kMaxWin], in[kMaxTap], taps[kMaxTap], frame[kMaxFrame], window[kMaxFrame], mins[kMins], med1[3], med2[3];

    static float median3 (const float* m) { return std::max (std::min (m[0], m[1]), std::min (std::max (m[0], m[1]), m[2])); }

    void analyse()
    {
        float s[kMaxFrame];
        for (int i = 0; i < nFrame; ++i) s[i] = frame[(fi + i) % nFrame];

        // voiced? the strongest repeat between 70 and 500 Hz
        {
            const int lo = std::max (2, (int) (fsA / 500.0)), hi = std::min (nFrame / 2, (int) (fsA / 70.0));
            double bestR = 0; int bestL = 0;
            for (int L = lo; L <= hi; ++L)
            {
                double r = 0, ea = 1e-12, eb = 1e-12;
                for (int i = 0; i + L < nFrame; ++i) { r += (double) s[i] * s[i + L]; ea += (double) s[i] * s[i]; eb += (double) s[i + L] * s[i + L]; }
                const double c = r / std::sqrt (ea * eb);
                if (c > bestR + 0.03) { bestR = c; bestL = L; }       // the shortest repeat wins unless a longer one is clearly better
            }
            voiced = open ? (float) std::clamp ((bestR - 0.3) / 0.4, 0.0, 1.0) : 0.0f;
            pitchHz = voiced >= 0.5f && bestL > 0 ? (float) (fsA / bestL) : 0.0f;
        }
        if (! open || sinceOnset < 0.012) return;       // formants hold through closures and bursts

        // linear prediction on the emphasised, windowed frame
        double r[kOrder + 1], a[kOrder + 1], tmp[kOrder + 1];
        {
            float w[kMaxFrame]; float prev = s[0];
            for (int i = 0; i < nFrame; ++i) { const float v = s[i] - emphasis * prev; prev = s[i]; w[i] = v * window[i]; }
            for (int k = 0; k <= kOrder; ++k) { double acc = 0; for (int i = 0; i + k < nFrame; ++i) acc += (double) w[i] * w[i + k]; r[k] = acc; }
            if (r[0] < 1e-14) return;
            r[0] *= 1.0001;
            double err = r[0]; a[0] = 1; for (int i = 1; i <= kOrder; ++i) a[i] = 0;
            for (int i = 1; i <= kOrder; ++i)
            {
                double acc = r[i]; for (int j = 1; j < i; ++j) acc += a[j] * r[i - j];
                const double k = -acc / err;
                for (int j = 1; j < i; ++j) tmp[j] = a[j] + k * a[i - j];
                for (int j = 1; j < i; ++j) a[j] = tmp[j];
                a[i] = k; err *= 1.0 - k * k;
                if (err <= 0) return;
            }
        }
        // its roots (Durand-Kerner): each resonance is a pair, frequency from the angle, width from the radius
        std::complex<double> z[kOrder];
        for (int i = 0; i < kOrder; ++i) z[i] = std::polar (0.9, 2 * 3.14159265358979 * (i + 0.35) / kOrder);
        for (int it = 0; it < 60; ++it)
        {
            double move = 0;
            for (int i = 0; i < kOrder; ++i)
            {
                std::complex<double> num = 1.0, den = 1.0;
                for (int j = 1; j <= kOrder; ++j) num = num * z[i] + a[j];
                for (int j = 0; j < kOrder; ++j) if (j != i) den *= z[i] - z[j];
                if (std::abs (den) < 1e-30) continue;
                const std::complex<double> d = num / den; z[i] -= d; move += std::abs (d);
            }
            if (move < 1e-9) break;
        }
        float cand[kOrder]; int n = 0;
        for (int i = 0; i < kOrder; ++i)
        {
            if (z[i].imag() <= 0) continue;
            const double f = std::arg (z[i]) * fsA / (2 * 3.14159265358979), bw = -std::log (std::max (1e-9, std::abs (z[i]))) * fsA / 3.14159265358979;
            if (f > 150 && f < 4000 && bw < 500) cand[n++] = (float) f;
        }
        std::sort (cand, cand + n);
        float g1 = 0, g2 = 0;
        for (int i = 0; i < n && g1 == 0; ++i) if (cand[i] >= 200 && cand[i] <= 1100) g1 = cand[i];
        for (int i = 0; i < n && g2 == 0; ++i) if (g1 > 0 && cand[i] >= std::max (550.0f, 1.25f * g1) && cand[i] <= 3300) g2 = cand[i];
        if (g1 == 0 || g2 == 0) return;
        med1[0] = med1[1]; med1[1] = med1[2]; med1[2] = g1; med2[0] = med2[1]; med2[1] = med2[2]; med2[2] = g2;
        const float k = 1.0f - std::exp (-(float) (nHop / fsA) / std::max (0.001f, smooth));
        f1 *= std::pow (median3 (med1) / f1, k); f2 *= std::pow (median3 (med2) / f2, k);
        float d = 0; vowel = vowelPosition (f1 / voiceSize, f2 / voiceSize, &d);
        vowelFit = std::clamp (1.0f - d, 0.0f, 1.0f);
    }
};

} // namespace windcore
