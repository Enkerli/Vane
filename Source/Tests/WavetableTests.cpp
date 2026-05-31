#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>
#include "Synth/Wavetable.h"
#include "Synth/Oscillator.h"

// ── Wavetable band-limiting tests (Stage 2a) ──────────────────────────────────
//
// The risk in WT import is anti-aliasing arbitrary content.  These guard the
// DFT → per-mip additive reconstruction:
//   1. buildRejectsBadInput     — empty / wrong-length frames are refused.
//   2. sineRoundTrips           — a pure sine frame reads back as that sine.
//   3. levelsNormalised         — every (frame, level) peaks at ±1.
//   4. guardPointContinuous     — table[N] == table[0] (wrap-safe interpolation).
//   5. mipsAreBandLimited       — fewer harmonics ⇒ smoother: a saw's low mip has
//                                 a far smaller max slope than its full mip.
//   6. allOutputsFiniteBounded  — no NaN/inf, |out| ≤ 1 everywhere.
//   7. harmonicStackSimpleToRich— the built-in default sweeps sine → richer.

class WavetableTests : public juce::UnitTest
{
public:
    WavetableTests() : juce::UnitTest("Wavetable", "Vane") {}

    static std::vector<float> sineCycle() {
        std::vector<float> v ((size_t) Wavetable::kTableSize);
        for (int i = 0; i < Wavetable::kTableSize; ++i)
            v[(size_t) i] = std::sin (6.283185307f * (float) i / (float) Wavetable::kTableSize);
        return v;
    }
    static std::vector<float> rampCycle() {           // naive saw: all harmonics
        std::vector<float> v ((size_t) Wavetable::kTableSize);
        for (int i = 0; i < Wavetable::kTableSize; ++i)
            v[(size_t) i] = 2.0f * (float) i / (float) Wavetable::kTableSize - 1.0f;
        return v;
    }
    static float maxSlope (const Wavetable& wt, int frame, int level) {
        float m = 0.0f;
        for (int i = 0; i < Wavetable::kTableSize; ++i)
            m = std::max (m, std::abs (wt.table (frame, level)[i + 1]
                                       - wt.table (frame, level)[i]));
        return m;
    }

    void runTest() override
    {
        beginTest ("buildRejectsBadInput");
        {
            Wavetable wt;
            expect (! wt.build ({}));                       // empty
            expect (! wt.isValid());
            std::vector<float> shortFrame (16, 0.0f);
            expect (! wt.build ({ shortFrame }));           // wrong length
        }

        beginTest ("sineRoundTrips");
        {
            Wavetable wt;
            expect (wt.build ({ sineCycle() }));
            expectEquals (wt.numFrames(), 1);
            const int top = Wavetable::kNumMipLevels - 1;
            float worst = 0.0f;
            for (int s = 0; s < 64; ++s) {
                const float ph = (float) s / 64.0f;
                const float ref = std::sin (6.283185307f * ph);
                worst = std::max (worst, std::abs (wt.read (0, top, ph) - ref));
            }
            expect (worst < 1.0e-2f, "sine reconstruction error " + juce::String (worst));
        }

        beginTest ("levelsNormalised");
        {
            Wavetable wt; wt.build ({ rampCycle() });
            for (int k = 0; k < Wavetable::kNumMipLevels; ++k) {
                float peak = 0.0f;
                for (int i = 0; i < Wavetable::kTableSize; ++i)
                    peak = std::max (peak, std::abs (wt.table (0, k)[i]));
                expect (peak > 0.95f && peak < 1.0001f, "level " + juce::String (k)
                        + " peak " + juce::String (peak));
            }
        }

        beginTest ("guardPointContinuous");
        {
            Wavetable wt; wt.build ({ rampCycle() });
            for (int k = 0; k < Wavetable::kNumMipLevels; ++k)
                expectEquals (wt.table (0, k)[Wavetable::kTableSize], wt.table (0, k)[0]);
        }

        beginTest ("mipsAreBandLimited");
        {
            Wavetable wt; wt.build ({ rampCycle() });
            const float lowSlope  = maxSlope (wt, 0, 0);                          // 1 harmonic
            const float highSlope = maxSlope (wt, 0, Wavetable::kNumMipLevels-1); // 1024 harmonics
            expect (highSlope > lowSlope * 3.0f,
                    "low " + juce::String (lowSlope) + " high " + juce::String (highSlope));
        }

        beginTest ("allOutputsFiniteBounded");
        {
            Wavetable wt; wt.build ({ sineCycle(), rampCycle() });
            for (int f = 0; f < wt.numFrames(); ++f)
                for (int k = 0; k < Wavetable::kNumMipLevels; ++k)
                    for (int i = 0; i <= Wavetable::kTableSize; ++i) {
                        const float v = wt.table (f, k)[i];
                        expect (std::isfinite (v) && std::abs (v) <= 1.0001f);
                    }
        }

        beginTest ("harmonicStackSimpleToRich");
        {
            auto wt = Wavetable::makeHarmonicStack (16);
            expectEquals (wt.numFrames(), 16);
            // Frame 0 = pure sine → smooth; last frame = many harmonics → steeper.
            expect (maxSlope (wt, 15, Wavetable::kNumMipLevels-1)
                    > maxSlope (wt, 0, Wavetable::kNumMipLevels-1) * 2.0f);
        }

        beginTest ("oscillatorReadsWavetable");
        {
            // The oscillator, pointed at the analytic WT, must produce finite,
            // bounded output, and morph 0 (sine frame) must be spectrally simpler
            // than morph 3 (saw frame) — i.e. the WT read path actually morphs.
            static const Wavetable analytic = Wavetable::makeAnalyticDefault();
            Oscillator osc;
            osc.prepare (48000.0);          // defaults wt to builtInDefault
            osc.setWavetable (&analytic);   // override with the analytic table
            osc.setFrequency (110.0f);

            auto renderEnergy = [&] (float morph) {
                osc.reset (0.0f);
                double prev = 0.0, slope = 0.0, peak = 0.0;
                for (int i = 0; i < 4096; ++i) {
                    const float s = osc.nextMorphed (morph, 0.5f, 0.0f);
                    expect (std::isfinite (s) && std::abs (s) <= 1.2f);
                    slope += std::abs (s - prev); prev = s;
                    peak = std::max (peak, (double) std::abs (s));
                }
                return std::make_pair (slope, peak);
            };
            auto sine = renderEnergy (0.0f);   // frame 0 = sine
            auto saw  = renderEnergy (3.0f);   // frame 3 = saw
            expect (saw.first > sine.first * 1.5,
                    "saw slope " + juce::String (saw.first)
                    + " vs sine " + juce::String (sine.first));
            expect (sine.second > 0.1 && saw.second > 0.1);   // both actually sound
        }
    }
};

static WavetableTests wavetableTests;
