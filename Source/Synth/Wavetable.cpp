#include "Wavetable.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <memory>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

namespace {
constexpr float kTwoPi = 6.283185307179586f;
}

// Band-limit each frame by FFT: forward once, then per mip level zero the bins
// above the level's harmonic cap (keeping the conjugate mirror) and inverse.
// DC is removed.  The whole table is then scaled by ONE global factor (so its
// loudest point is ±1) — this preserves the wavetable's authored inter-frame
// and inter-pitch dynamics, unlike per-frame normalisation which flattens them.
// Fast enough to build a few hundred frames in well under a second.
bool Wavetable::build (const std::vector<std::vector<float>>& rawFrames)
{
    framesData.clear();
    if (rawFrames.empty()) return false;

    constexpr int N = kTableSize;
    static_assert ((N & (N - 1)) == 0, "kTableSize must be a power of two");
    const int order = (int) std::log2 ((double) N);    // 11 for 2048
    juce::dsp::FFT fft (order);

    std::vector<float> spec ((size_t) (2 * N), 0.0f);   // forward result (interleaved complex)
    std::vector<float> work ((size_t) (2 * N), 0.0f);
    float globalPeak = 0.0f;

    for (const auto& raw : rawFrames) {
        if ((int) raw.size() != N) { framesData.clear(); return false; }

        std::fill (spec.begin(), spec.end(), 0.0f);
        for (int i = 0; i < N; ++i) spec[(size_t) i] = raw[(size_t) i];
        fft.performRealOnlyForwardTransform (spec.data());   // bin h at (spec[2h], spec[2h+1])

        Frame f;
        for (int k = 0; k < kNumMipLevels; ++k) {
            const int cap = std::min (1 << k, kMaxHarmonics);
            work = spec;
            work[0] = work[1] = 0.0f;                        // drop DC
            // Zero harmonics above the cap; bins cap+1 .. N-cap-1 (keeps the
            // conjugate mirror N-cap .. N-1 that pairs with 1 .. cap).
            for (int h = cap + 1; h < N - cap; ++h) { work[(size_t)(2*h)] = 0.0f; work[(size_t)(2*h+1)] = 0.0f; }
            fft.performRealOnlyInverseTransform (work.data());

            auto& lvl = f.levels[(size_t) k];
            lvl.assign ((size_t) N + 1, 0.0f);
            for (int i = 0; i < N; ++i) { lvl[(size_t) i] = work[(size_t) i];
                                          globalPeak = std::max (globalPeak, std::abs (work[(size_t) i])); }
        }
        framesData.push_back (std::move (f));
    }

    // Single global scale: loudest sample anywhere in the table → ±1.
    const float inv = (globalPeak > 1.0e-6f) ? 1.0f / globalPeak : 1.0f;
    for (auto& f : framesData)
        for (auto& lvl : f.levels) {
            for (int i = 0; i < N; ++i) lvl[(size_t) i] *= inv;
            lvl[(size_t) N] = lvl[0];                        // guard point
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

// Serum/Vital embed a "clm " chunk like  <!>2048 10000000 wavetable [name]
// whose leading integer is the authoritative frame size.  juce's reader doesn't
// surface it, so scan the RIFF ourselves; fall back to `fallback` if absent.
static int detectFrameSize (const juce::File& file, int fallback)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() < 12) return fallback;
    const auto* d = static_cast<const uint8_t*> (mb.getData());
    const size_t n = mb.getSize();
    if (std::memcmp (d, "RIFF", 4) != 0) return fallback;
    size_t i = 12;
    while (i + 8 <= n) {
        const uint32_t sz = (uint32_t) d[i+4] | ((uint32_t) d[i+5] << 8)
                          | ((uint32_t) d[i+6] << 16) | ((uint32_t) d[i+7] << 24);
        if (std::memcmp (d + i, "clm ", 4) == 0) {
            const auto avail = (int) juce::jmin ((size_t) sz, n - i - 8);
            const juce::String s = juce::String::fromUTF8 ((const char*) (d + i + 8), avail);
            const int p = s.indexOf ("<!>");
            if (p >= 0) { const int v = s.substring (p + 3).getIntValue();
                          if (v >= 64 && v <= 8192) return v; }
            return fallback;
        }
        i += 8 + sz + (sz & 1);
    }
    return fallback;
}

Wavetable Wavetable::loadFromWav (const juce::File& file, int frameSize)
{
    Wavetable wt;
    frameSize = detectFrameSize (file, frameSize);   // prefer the file's clm marker
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
