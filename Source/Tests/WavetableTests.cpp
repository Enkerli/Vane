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

        beginTest ("globallyNormalised");
        {
            // One global factor: the loudest point anywhere in the table is ±1,
            // and nothing exceeds it — but individual frames/levels may be quieter
            // (that's the point — authored dynamics are preserved).
            Wavetable wt; wt.build ({ sineCycle(), rampCycle() });
            float globalPeak = 0.0f;
            for (int fr = 0; fr < wt.numFrames(); ++fr)
                for (int k = 0; k < Wavetable::kNumMipLevels; ++k)
                    for (int i = 0; i < Wavetable::kTableSize; ++i)
                        globalPeak = std::max (globalPeak, std::abs (wt.table (fr, k)[i]));
            expect (globalPeak > 0.95f && globalPeak < 1.0001f,
                    "global peak " + juce::String (globalPeak));
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

        beginTest ("wavRoundTrips");
        {
            auto src = Wavetable::makeHarmonicStack (8);
            auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("vane_wt_roundtrip.wav");
            expect (src.saveToWav (tmp));
            auto loaded = Wavetable::loadFromWav (tmp);
            expectEquals (loaded.numFrames(), src.numFrames());
            const int top = Wavetable::kNumMipLevels - 1;
            float worst = 0.0f;
            for (int f = 0; f < src.numFrames(); ++f)
                for (int i = 0; i < Wavetable::kTableSize; i += 8) {
                    const float ph = (float) i / (float) Wavetable::kTableSize;
                    worst = std::max (worst, std::abs (src.read (f, top, ph) - loaded.read (f, top, ph)));
                }
            expect (worst < 2.0e-2f, "round-trip error " + juce::String (worst));
            tmp.deleteFile();
        }

        beginTest ("loadsRealVitalTable");
        {
            // Guarded by existence — runs on the dev machine, skipped elsewhere.
            // "Annoying OP.wav": Vital table, int16, clm marker <!>2048 → 87 frames.
            juce::File f ("/Users/alex/Music/Vital/Glorkglunk/Wavetables/Annoying OP.wav");
            if (f.existsAsFile()) {
                auto wt = Wavetable::loadFromWav (f);
                expect (wt.isValid());
                expectEquals (wt.numFrames(), 87);   // clm frame size detected
                const int top = Wavetable::kNumMipLevels - 1;
                for (int fr = 0; fr < wt.numFrames(); fr += 11)
                    for (int i = 0; i < Wavetable::kTableSize; i += 64)
                        expect (std::isfinite (wt.read (fr, top, (float) i / Wavetable::kTableSize)));
            }
        }

        beginTest ("loadRejectsMissingFile");
        {
            auto wt = Wavetable::loadFromWav (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                                  .getChildFile ("vane_no_such_wt.wav"));
            expect (! wt.isValid());
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
            auto sine = renderEnergy (0.0f);   // morph 0 → frame 0 = sine
            auto saw  = renderEnergy (1.0f);   // morph 1 → last frame = saw
            expect (saw.first > sine.first * 1.5,
                    "saw slope " + juce::String (saw.first)
                    + " vs sine " + juce::String (sine.first));
            expect (sine.second > 0.1 && saw.second > 0.1);   // both actually sound
        }
    }
};

static WavetableTests wavetableTests;
