// Minimal juce_dsp stub — just juce::dsp::FFT, just the two methods
// Wavetable::build() calls. A plain iterative radix-2 Cooley-Tukey.
//
// Convention notes (matched to how build() consumes the results):
// - performRealOnlyForwardTransform(io): io holds N reals in the first N floats
//   of a 2N buffer; the result is the FULL N-bin complex spectrum interleaved
//   (re,im) — including the conjugate-mirror bins N−h, which build() reads and
//   rewrites (phaseAlign) and relies on for a real-valued inverse.
// - performRealOnlyInverseTransform(io): io holds N interleaved complex bins;
//   the result is N reals in the first N floats, scaled by 1/N.
// - Absolute scaling is otherwise unimportant here: build() peak-normalises
//   every (frame, mip) to ±1 afterwards, which washes out any convention
//   difference from the real JUCE FFT. Phase-align uses re/|z| ratios —
//   scale-invariant too.
#pragma once
#include <cmath>
#include <vector>
#include <cstddef>

namespace juce {
namespace dsp {

class FFT {
public:
    explicit FFT (int order) : size (1 << order) {}

    void performRealOnlyForwardTransform (float* io) const {
        std::vector<float> re ((size_t) size), im ((size_t) size, 0.0f);
        for (int i = 0; i < size; ++i) re[(size_t) i] = io[(size_t) i];
        transform (re.data(), im.data(), false);
        for (int i = 0; i < size; ++i) {
            io[(size_t) (2 * i)]     = re[(size_t) i];
            io[(size_t) (2 * i + 1)] = im[(size_t) i];
        }
    }

    void performRealOnlyInverseTransform (float* io) const {
        std::vector<float> re ((size_t) size), im ((size_t) size);
        for (int i = 0; i < size; ++i) {
            re[(size_t) i] = io[(size_t) (2 * i)];
            im[(size_t) i] = io[(size_t) (2 * i + 1)];
        }
        transform (re.data(), im.data(), true);
        const float inv = 1.0f / (float) size;
        for (int i = 0; i < size; ++i) io[(size_t) i] = re[(size_t) i] * inv;
    }

private:
    // In-place iterative radix-2 (bit-reversal permutation + butterfly passes).
    void transform (float* re, float* im, bool inverse) const {
        const int n = size;
        for (int i = 1, j = 0; i < n; ++i) {           // bit-reverse permutation
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (int len = 2; len <= n; len <<= 1) {
            const double ang = (inverse ? 2.0 : -2.0) * 3.141592653589793238 / (double) len;
            const float wRe = (float) std::cos (ang), wIm = (float) std::sin (ang);
            for (int i = 0; i < n; i += len) {
                float curRe = 1.0f, curIm = 0.0f;
                for (int k = 0; k < len / 2; ++k) {
                    const int a = i + k, b = i + k + len / 2;
                    const float tRe = re[b] * curRe - im[b] * curIm;
                    const float tIm = re[b] * curIm + im[b] * curRe;
                    re[b] = re[a] - tRe;  im[b] = im[a] - tIm;
                    re[a] += tRe;         im[a] += tIm;
                    const float nRe = curRe * wRe - curIm * wIm;
                    curIm = curRe * wIm + curIm * wRe;
                    curRe = nRe;
                }
            }
        }
    }

    int size;
};

} // namespace dsp
} // namespace juce
