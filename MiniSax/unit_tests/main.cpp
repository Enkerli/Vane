// MiniSax unit tests — plain asserts, no framework dependency.
// Covers the acceptance criteria in docs/FIRST_ISSUES.md issues 3-5.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../Source/Envelope.h"
#include "../Source/FractionalDelay.h"
#include "../Source/MiniSaxVoice.h"
#include "../Source/PresetIO.h"
#include "../Source/ReedNonlinearity.h"

namespace
{
int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(std::abs((a) - (b)) <= (tol))

void testFractionalDelayInteger()
{
    minisax::FractionalDelay d;
    d.prepare(64);
    d.clear();
    d.setDelay(10.0f);
    std::vector<float> out;
    for (int n = 0; n < 32; ++n)
    {
        out.push_back(d.read());
        d.write(n == 0 ? 1.0f : 0.0f);
    }
    // read-before-write convention: impulse written at n=0 emerges at n=10.
    for (int n = 0; n < 32; ++n)
        CHECK_NEAR(out[static_cast<size_t>(n)], n == 10 ? 1.0f : 0.0f, 1e-6f);
}

void testFractionalDelayHalfSample()
{
    minisax::FractionalDelay d;
    d.prepare(64);
    d.clear();
    d.setDelay(10.5f);
    std::vector<float> out;
    for (int n = 0; n < 32; ++n)
    {
        out.push_back(d.read());
        d.write(n == 0 ? 1.0f : 0.0f);
    }
    // Linear interpolation spreads the impulse across samples 10 and 11.
    CHECK_NEAR(out[10], 0.5f, 1e-6f);
    CHECK_NEAR(out[11], 0.5f, 1e-6f);
    CHECK_NEAR(out[9], 0.0f, 1e-6f);
    CHECK_NEAR(out[12], 0.0f, 1e-6f);
}

void testFractionalDelayDynamicAndBounds()
{
    minisax::FractionalDelay d;
    d.prepare(128);
    d.clear();
    // Sweep delay through and beyond its legal range while feeding a signal;
    // output must stay bounded (no out-of-bounds garbage).
    for (int n = 0; n < 4096; ++n)
    {
        d.setDelay(-5.0f + 0.05f * static_cast<float>(n)); // deliberately exceeds both ends
        const float y = d.read();
        CHECK(std::isfinite(y));
        CHECK(std::abs(y) <= 1.0f + 1e-6f);
        d.write(n % 2 == 0 ? 1.0f : -1.0f);
        if (failures > 0) return; // don't spam thousands of lines
    }
}

void testReedNonlinearitySweep()
{
    minisax::ReedNonlinearity reed;
    // Grid sweep over normalized controls and pressure differences
    // (FIRST_ISSUES.md issue 4): no NaN/Inf, reflection within [-1, 1].
    for (int a = 0; a <= 10; ++a)
        for (int s = 0; s <= 10; ++s)
            for (int e = 0; e <= 10; ++e)
            {
                reed.setControls(a * 0.1f, s * 0.1f, e * 0.1f);
                for (float pd = -5.0f; pd <= 5.0f; pd += 0.25f)
                {
                    const float r = reed.reflection(pd);
                    if (!std::isfinite(r) || r < -1.0f || r > 1.0f)
                    {
                        CHECK(false);
                        return;
                    }
                }
            }
    CHECK(true);
}

std::vector<float> renderSeconds(const minisax::Parameters& p, float breath,
                                 double seconds, uint32_t seed)
{
    minisax::MiniSaxVoice voice;
    voice.prepare(48000.0, seed);
    minisax::VoiceInputs in;
    in.params = p;
    in.params.breath = breath;
    in.pitchHz = 261.63f;
    in.gate = 1.0f;
    const auto n = static_cast<size_t>(48000.0 * seconds);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i)
        out[i] = voice.processSample(in);
    return out;
}

void testVoiceDeterminism()
{
    minisax::Parameters p;
    p.noiseAmount = 0.3f; // make the noise path matter
    const auto a = renderSeconds(p, 0.6f, 0.5, 1234u);
    const auto b = renderSeconds(p, 0.6f, 0.5, 1234u);
    CHECK(a == b);
    const auto c = renderSeconds(p, 0.6f, 0.5, 5678u);
    CHECK(a != c); // different seed must actually change the noise
}

void testVoiceSpeaksAndStaysFinite()
{
    minisax::Parameters p; // defaults ~ breathy_001 territory
    const auto out = renderSeconds(p, 0.6f, 1.0, 42u);
    float peak = 0.0f;
    bool finite = true;
    // Skip the first 100 ms of attack when judging sustained level.
    for (size_t i = 4800; i < out.size(); ++i)
    {
        peak = std::max(peak, std::abs(out[i]));
        finite = finite && std::isfinite(out[i]);
    }
    CHECK(finite);
    CHECK(peak > 1.0e-3f); // the voice must actually speak
    CHECK(peak < 1.0f);    // and not clip at default settings
}

void testVoiceExtremeParametersStayFinite()
{
    // Corner-of-the-cube sweep: every parameter at 0 or 1.
    for (int mask = 0; mask < (1 << 7); ++mask)
    {
        minisax::Parameters p;
        p.embouchure = (mask & 1) ? 1.0f : 0.0f;
        p.reedStiffness = (mask & 2) ? 1.0f : 0.0f;
        p.reedAperture = (mask & 4) ? 1.0f : 0.0f;
        p.boreDamping = (mask & 8) ? 1.0f : 0.0f;
        p.bellBrightness = (mask & 16) ? 1.0f : 0.0f;
        p.noiseAmount = (mask & 32) ? 1.0f : 0.0f;
        p.growlAmount = (mask & 64) ? 1.0f : 0.0f;
        p.outputGain = 1.0f;
        const auto out = renderSeconds(p, 1.0f, 0.25, 7u);
        for (const float s : out)
            if (!std::isfinite(s))
            {
                CHECK(false);
                std::printf("  (non-finite at parameter mask %d)\n", mask);
                return;
            }
    }
    CHECK(true);
}

void testEnvelopeSemantics()
{
    minisax::Envelope env(0.1f);
    env.add(1.0, 0.5f, false); // step at t=1
    env.add(3.0, 1.0f, true);  // linear ramp reaching 1.0 at t=3
    env.finalize();
    CHECK_NEAR(env.value(0.0), 0.1f, 1e-6f);
    CHECK_NEAR(env.value(0.999), 0.1f, 1e-6f);
    CHECK_NEAR(env.value(1.0), 0.5f, 1e-6f);
    CHECK_NEAR(env.value(2.0), 0.75f, 1e-6f);
    CHECK_NEAR(env.value(3.0), 1.0f, 1e-6f);
    CHECK_NEAR(env.value(9.0), 1.0f, 1e-6f);
}

void testParametersHash()
{
    minisax::Parameters a;
    minisax::Parameters b = a;
    CHECK(minisax::parametersHash(a) == minisax::parametersHash(b));
    b.reedStiffness += 0.01f;
    CHECK(minisax::parametersHash(a) != minisax::parametersHash(b));
    CHECK(minisax::parametersHashHex(a).size() == 16);
}

} // namespace

int main()
{
    testFractionalDelayInteger();
    testFractionalDelayHalfSample();
    testFractionalDelayDynamicAndBounds();
    testReedNonlinearitySweep();
    testVoiceDeterminism();
    testVoiceSpeaksAndStaysFinite();
    testVoiceExtremeParametersStayFinite();
    testEnvelopeSemantics();
    testParametersHash();

    if (failures == 0)
    {
        std::printf("All MiniSax unit tests passed.\n");
        return 0;
    }
    std::printf("%d failure(s).\n", failures);
    return 1;
}
