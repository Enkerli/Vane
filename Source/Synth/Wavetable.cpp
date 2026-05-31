#include "Wavetable.h"
#include <cmath>
#include <algorithm>
#include <memory>
#include <juce_audio_formats/juce_audio_formats.h>

namespace {
constexpr float kTwoPi = 6.283185307179586f;

// Direct DFT of a real single cycle → harmonic cosine/sine coefficients.
//   a[h] = (2/N) Σ x[i] cos(2π h i / N)
//   b[h] = (2/N) Σ x[i] sin(2π h i / N)
// h = 1..maxH (DC dropped).  O(N·maxH); see header note on FFT for the loader.
void analyse (const std::vector<float>& x, int maxH,
              std::vector<float>& a, std::vector<float>& b)
{
    const int N = (int) x.size();
    a.assign ((size_t) maxH + 1, 0.0f);
    b.assign ((size_t) maxH + 1, 0.0f);
    for (int h = 1; h <= maxH; ++h) {
        const float omega    = kTwoPi * (float) h / (float) N;
        const float cosOmega = std::cos (omega);
        const float sinOmega = std::sin (omega);
        float s = 0.0f, c = 1.0f;       // sin(0), cos(0); rotate per sample
        float ah = 0.0f, bh = 0.0f;
        for (int i = 0; i < N; ++i) {
            ah += x[(size_t) i] * c;
            bh += x[(size_t) i] * s;
            const float ns = s * cosOmega + c * sinOmega;
            const float nc = c * cosOmega - s * sinOmega;
            s = ns; c = nc;
        }
        a[(size_t) h] = 2.0f * ah / (float) N;
        b[(size_t) h] = 2.0f * bh / (float) N;
    }
}

// Reconstruct one cycle from harmonics 1..cap into tbl[0..N-1] (+ guard at N).
// y[i] = Σ_{h=1..cap} a[h]·cos(2π h i/N) + b[h]·sin(2π h i/N), normalised to ±1.
void reconstruct (const std::vector<float>& a, const std::vector<float>& b,
                  int cap, std::vector<float>& tbl)
{
    const int N = Wavetable::kTableSize;
    std::fill (tbl.begin(), tbl.end(), 0.0f);
    for (int h = 1; h <= cap; ++h) {
        const float ah = a[(size_t) h], bh = b[(size_t) h];
        if (ah == 0.0f && bh == 0.0f) continue;
        const float omega    = kTwoPi * (float) h / (float) N;
        const float cosOmega = std::cos (omega);
        const float sinOmega = std::sin (omega);
        float s = 0.0f, c = 1.0f;
        for (int i = 0; i < N; ++i) {
            tbl[(size_t) i] += ah * c + bh * s;
            const float ns = s * cosOmega + c * sinOmega;
            const float nc = c * cosOmega - s * sinOmega;
            s = ns; c = nc;
        }
    }
    float peak = 0.0f;
    for (int i = 0; i < N; ++i) peak = std::max (peak, std::abs (tbl[(size_t) i]));
    if (peak > 1.0e-6f) { const float inv = 1.0f / peak;
        for (int i = 0; i < N; ++i) tbl[(size_t) i] *= inv; }
    tbl[(size_t) N] = tbl[0];   // guard point
}
} // namespace

bool Wavetable::build (const std::vector<std::vector<float>>& rawFrames)
{
    framesData.clear();
    if (rawFrames.empty()) return false;

    std::vector<float> a, b;
    for (const auto& raw : rawFrames) {
        if ((int) raw.size() != kTableSize) { framesData.clear(); return false; }

        analyse (raw, kMaxHarmonics, a, b);

        Frame f;
        for (int k = 0; k < kNumMipLevels; ++k) {
            const int cap = std::min (1 << k, kMaxHarmonics);
            f.levels[(size_t) k].assign ((size_t) kTableSize + 1, 0.0f);
            reconstruct (a, b, cap, f.levels[(size_t) k]);
        }
        framesData.push_back (std::move (f));
    }
    return true;
}

Wavetable Wavetable::makeHarmonicStack (int numFrames)
{
    numFrames = std::max (1, numFrames);
    std::vector<std::vector<float>> raw;
    raw.reserve ((size_t) numFrames);

    // Frame k: sum of the first (k+1) sawtooth harmonics (amplitude 1/h).
    // k = 0 is a pure sine; the last frame is a band-rich saw → simple→complex.
    for (int k = 0; k < numFrames; ++k) {
        const int harmonics = k + 1;
        std::vector<float> cycle ((size_t) kTableSize, 0.0f);
        for (int h = 1; h <= harmonics; ++h) {
            const float amp = 1.0f / (float) h;
            for (int i = 0; i < kTableSize; ++i)
                cycle[(size_t) i] += amp * std::sin (kTwoPi * (float) h * (float) i / (float) kTableSize);
        }
        raw.push_back (std::move (cycle));
    }

    Wavetable wt;
    wt.build (raw);
    return wt;
}

Wavetable Wavetable::makeAnalyticDefault()
{
    constexpr float pi = 3.14159265358979f;
    std::vector<std::vector<float>> raw (4, std::vector<float> ((size_t) kTableSize, 0.0f));
    for (int i = 0; i < kTableSize; ++i) {
        const float p = (float) i / (float) kTableSize;   // 0..1
        const float t = kTwoPi * p;
        raw[0][(size_t) i] = std::sin (t);                                 // Sine
        raw[1][(size_t) i] = (2.0f / pi) * std::asin (std::sin (t));       // Triangle
        raw[2][(size_t) i] = (p < 0.5f) ? 1.0f : -1.0f;                    // Square
        raw[3][(size_t) i] = 2.0f * p - 1.0f;                             // Saw (ramp)
    }
    Wavetable wt;
    wt.build (raw);   // build() band-limits each via DFT, so these naive shapes are safe
    return wt;
}

const Wavetable& Wavetable::builtInDefault()
{
    static Wavetable wt = makeHarmonicStack (16);   // built once, shared by all instances
    return wt;
}

Wavetable Wavetable::loadFromWav (const juce::File& file, int frameSize)
{
    Wavetable wt;
    if (frameSize <= 0) return wt;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (file));
    if (rd == nullptr) return wt;

    const int total = (int) rd->lengthInSamples;
    if (total < frameSize) return wt;
    const int nframes = total / frameSize;

    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, rd->numChannels), total);
    rd->read (&buf, 0, total, 0, true, false);   // channel 0 = the wavetable
    const float* src = buf.getReadPointer (0);

    std::vector<std::vector<float>> raw;
    raw.reserve ((size_t) nframes);
    for (int f = 0; f < nframes; ++f) {
        const int off = f * frameSize;
        std::vector<float> cycle ((size_t) kTableSize, 0.0f);
        if (frameSize == kTableSize) {
            for (int i = 0; i < kTableSize; ++i) cycle[(size_t) i] = src[off + i];
        } else {
            // Linear-resample one frame (frameSize → kTableSize), wrap-aware.
            for (int i = 0; i < kTableSize; ++i) {
                const float pos = (float) i / (float) kTableSize * (float) frameSize;
                const int   s0  = (int) pos;
                const float fr  = pos - (float) s0;
                const int   s1  = (s0 + 1) % frameSize;
                cycle[(size_t) i] = src[off + s0] * (1.0f - fr) + src[off + s1] * fr;
            }
        }
        raw.push_back (std::move (cycle));
    }
    wt.build (raw);
    return wt;
}

bool Wavetable::saveToWav (const juce::File& file) const
{
    if (! isValid()) return false;

    file.deleteFile();
    auto os = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
    if (os == nullptr || ! os->openedOk()) return false;

    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        fmt.createWriterFor (os.get(), 44100.0, 1, 32, {}, 0));   // 32-bit float
    if (writer == nullptr) return false;
    os.release();   // the writer owns the stream now

    const int top = kNumMipLevels - 1;     // full-bandwidth representation of each frame
    juce::AudioBuffer<float> one (1, kTableSize);
    for (int f = 0; f < numFrames(); ++f) {
        one.copyFrom (0, 0, table (f, top), kTableSize);
        writer->writeFromAudioSampleBuffer (one, 0, kTableSize);
    }
    return true;   // flushes/closes on writer destruction
}
