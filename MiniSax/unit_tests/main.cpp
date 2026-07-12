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

float goertzelLevel(const std::vector<float>& x, float freq, float sr)
{
    const double w = 2.0 * 3.14159265358979 * freq / sr;
    const double c = 2.0 * std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (const float v : x)
    {
        s0 = v + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return static_cast<float>(std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2))
         / static_cast<float>(x.size());
}

std::vector<float> renderAtConicalAmount(float conicalAmount)
{
    minisax::MiniSaxVoice voice;
    voice.prepare(48000.0, 42u);
    minisax::VoiceInputs in;
    in.params = minisax::Parameters{};
    in.params.breath = 0.62f;
    in.params.noiseAmount = 0.0f;
    in.params.conicalAmount = conicalAmount;
    // 187.5 Hz = 48000/256: an exact bin so goertzel harmonics line up.
    in.pitchHz = 187.5f;
    in.gate = 1.0f;
    std::vector<float> out;
    for (int n = 0; n < 96000; ++n)
    {
        const float s = voice.processSample(in);
        if (n >= 48000)
            out.push_back(s);
    }
    return out;
}

// v0.2 regression: the conical waveshaper must produce strong even harmonics
// (the sax-like body); at zero it stays odd-harmonic (clarinet-like).
// Both settings must sound at the SAME fundamental (no register change).
void testConicalEvenHarmonics()
{
    const float sr = 48000.0f, f0 = 187.5f;

    const auto sax = renderAtConicalAmount(minisax::Parameters{}.conicalAmount);
    const float saxH1 = goertzelLevel(sax, f0, sr);
    const float saxH2 = goertzelLevel(sax, 2.0f * f0, sr);
    CHECK(saxH1 > 1.0e-4f); // it speaks, at the fundamental
    CHECK(20.0f * std::log10(saxH2 / (saxH1 + 1e-12f)) > -10.0f); // strong H2

    const auto clar = renderAtConicalAmount(0.0f);
    const float clarH1 = goertzelLevel(clar, f0, sr);
    const float clarH2 = goertzelLevel(clar, 2.0f * f0, sr);
    CHECK(clarH1 > 1.0e-4f);
    CHECK(20.0f * std::log10(clarH2 / (clarH1 + 1e-12f)) < -25.0f); // even harmonics gone
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
    testConicalEvenHarmonics();
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
