#include <juce_core/juce_core.h>
#include "Synth/SamplePlayer.h"
#include "Synth/TransientLibrary.h"

class TransientTests : public juce::UnitTest {
public:
    TransientTests() : juce::UnitTest("Transients", "Vane") {}

    void runTest() override
    {
        beginTest("SamplePlayer: idle when not triggered");
        {
            SamplePlayer p;
            expect(!p.isPlaying());

            float buf[8] = {};
            float env = 1.0f;
            p.renderAdding(buf, 8, env, 0.99f, 1.0f);
            for (auto v : buf) expectEquals(v, 0.0f);
        }

        beginTest("SamplePlayer: trigger without data is a no-op");
        {
            SamplePlayer p;
            p.trigger();
            expect(!p.isPlaying());
        }

        beginTest("SamplePlayer: renders non-zero and stops at end");
        {
            // 4-sample impulse at index 1 so there is something to read.
            const float data[] = { 0.0f, 1.0f, 0.0f, 0.0f };
            SamplePlayer p;
            p.setSample(data, 4);
            p.trigger(1.0f);
            expect(p.isPlaying());

            float buf[16] = {};
            float env = 1.0f;
            p.renderAdding(buf, 16, env, 1.0f, 1.0f);   // decayCoeff=1 = no decay

            // Must have stopped (sample is only 4 samples long).
            expect(!p.isPlaying());

            // Buffer should contain at least one non-zero value.
            float sum = 0.0f;
            for (auto v : buf) sum += std::abs(v);
            expect(sum > 0.0f);
        }

        beginTest("SamplePlayer: stop() halts playback mid-sample");
        {
            const float data[] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
            SamplePlayer p;
            p.setSample(data, 8);
            p.trigger(1.0f);
            p.stop();
            expect(!p.isPlaying());

            float buf[8] = {};
            float env = 1.0f;
            p.renderAdding(buf, 8, env, 1.0f, 1.0f);
            for (auto v : buf) expectEquals(v, 0.0f);
        }

        beginTest("SamplePlayer: decay envelope attenuates output");
        {
            // Constant unity sample data.
            juce::HeapBlock<float> data(64);
            for (int i = 0; i < 64; ++i) data[i] = 1.0f;

            SamplePlayer p;
            p.setSample(data.getData(), 64);
            p.trigger(1.0f);

            float buf[8] = {};
            float env = 1.0f;
            const float coeff = 0.9f;
            p.renderAdding(buf, 8, env, coeff, 1.0f);

            // Each sample should be strictly less than the previous (decaying).
            for (int i = 1; i < 8; ++i)
                expect(buf[i] < buf[i - 1]);
        }

        beginTest("TransientLibrary: index 0 is None");
        {
            TransientLibrary lib;
            expect(lib.numEntries() >= 1);
            expectEquals(lib.names()[0], juce::String("None"));
            expect(lib.getSample(0) == nullptr);
            expect(lib.getSample(-1) == nullptr);
            expect(lib.getSample(lib.numEntries()) == nullptr);
        }

        beginTest("TransientLibrary: names match entry count");
        {
            TransientLibrary lib;
            expectEquals(lib.names().size(), lib.numEntries());
        }
    }
};

static TransientTests transientTests;
