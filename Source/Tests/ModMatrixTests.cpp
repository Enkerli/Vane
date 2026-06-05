#include <juce_core/juce_core.h>
#include "Modulation/ModMatrix.h"

// ── ModMatrix Unit Tests ───────────────────────────────────────────────────
//
// These run automatically on every Debug build via the UnitTestRunner in
// VaneProcessor's constructor.  Four scenarios are covered:
//
//   1. perVoiceFilterIsolation     — two voices with different slide positions
//                                    must produce independent filter mod values.
//   2. slewerResetSnapsCorrectly   — stale slewer state is zeroed by
//                                    resetVoiceSlewers() before new note-on.
//   3. exponentialCurveShaping     — x*|x| is symmetric and matches expectation.
//   4. amountParamOverridesAmount  — live atomic param silences a route even
//                                    when the hardcoded amount is non-zero.

class ModMatrixTests : public juce::UnitTest
{
public:
    ModMatrixTests() : juce::UnitTest("ModMatrix", "Vane") {}

    void runTest() override
    {
        perVoiceFilterIsolation();
        slewerResetSnapsCorrectly();
        exponentialCurveShaping();
        amountParamOverridesAmount();
        genericSlotRouting();
        slotEnableGatesRoute();
        bezierCurveLUT();
    }

    // Regression: a disabled slot must produce NO modulation, while preserving its
    // source/dest/amount so re-enabling restores it.  (The matrix UI toggle had no
    // backing parameter, so disabling a route in the UI kept modulating.)
    void slotEnableGatesRoute()
    {
        beginTest("slot enable gates the route");

        std::atomic<float> src { (float) 4 };  // Slide
        std::atomic<float> dst { (float) 1 };  // Cutoff
        std::atomic<float> amt { 1.0f };
        std::atomic<float> crv { 0.0f };
        std::atomic<float> scl { 0.0f };
        std::atomic<float> en  { 1.0f };       // enabled

        ModMatrix mm;
        mm.prepare(44100.0, 512);
        mm.addSlot(&src, &dst, &amt, &crv, &scl, &en);

        std::vector<Slewer> sl;
        mm.initVoiceSlewers(sl, 44100.0, 512);
        auto vals = makeVoiceVals(0.0f, +1.0f, 0.0f, 0.0f);  // slide fully open
        std::array<float, ModDestID::NumDests> r {};

        for (int i = 0; i < 6; ++i) r = mm.evaluate(vals, sl);
        expectWithinAbsoluteError(r[ModDestID::FilterCutoff], 1.0f, 0.02f);

        // Disable: cutoff must fall to zero even though src/dst/amt are unchanged.
        en = 0.0f;
        for (int i = 0; i < 6; ++i) r = mm.evaluate(vals, sl);
        expectWithinAbsoluteError(r[ModDestID::FilterCutoff], 0.0f, 0.001f);

        // Re-enable: modulation returns (settings were preserved).
        en = 1.0f;
        for (int i = 0; i < 6; ++i) r = mm.evaluate(vals, sl);
        expectWithinAbsoluteError(r[ModDestID::FilterCutoff], 1.0f, 0.02f);
    }

    void bezierCurveLUT()
    {
        beginTest("Bezier curve LUT (monotone spline)");
        std::array<float, ModRoute::kCurveLUT> lut {};
        constexpr int N = ModRoute::kCurveLUT;

        // Linear: a single mid anchor at (0.5,0.5) must reproduce the identity.
        ModMatrix::buildCurveLUT({ {0.5f, 0.5f} }, lut);
        expectWithinAbsoluteError(lut[0], 0.0f, 1e-4f);
        expectWithinAbsoluteError(lut[N-1], 1.0f, 1e-4f);
        expectWithinAbsoluteError(lut[(N-1)/2], 0.5f, 0.02f);

        // Endpoints are always pinned and the curve is monotonic for any anchors.
        ModMatrix::buildCurveLUT({ {0.32f, 0.16f}, {0.68f, 0.84f} }, lut);   // S-curve
        expectWithinAbsoluteError(lut[0], 0.0f, 1e-4f);
        expectWithinAbsoluteError(lut[N-1], 1.0f, 1e-4f);
        bool monotonic = true;
        for (int k = 1; k < N; ++k) if (lut[(size_t) k] < lut[(size_t)(k-1)] - 1e-4f) monotonic = false;
        expect(monotonic, "S-curve LUT must be non-decreasing");

        // Exponential-ish anchor (0.5, 0.27): below the diagonal at the midpoint.
        ModMatrix::buildCurveLUT({ {0.5f, 0.27f} }, lut);
        expect(lut[(N-1)/2] < 0.45f, "exp anchor should sit below linear at midpoint");
    }

private:
    // Helper: build a voiceVals array from named MPE dimensions.
    static std::array<float, ModSourceID::NumVoiceSources>
    makeVoiceVals(float pressure, float slideBipolar, float pitchbend, float velocity)
    {
        return { pressure, slideBipolar, pitchbend, velocity };
    }

    // ── 1. Per-voice filter isolation ─────────────────────────────────────────
    // Route: MPE_Slide → FilterCutoff (linear, amount 1.0).
    // Voice A has slide fully open (+1.0 bipolar), Voice B has slide closed (-1.0).
    // After enough evaluate() calls to settle the slewers, both voices must
    // converge to their own distinct target — not each other's.
    void perVoiceFilterIsolation()
    {
        beginTest("per-voice filter isolation");

        ModMatrix mm;
        mm.prepare(44100.0, 512);
        mm.addRoute(ModSourceID::MPE_Slide, ModDestID::FilterCutoff,
                    1.0f, 0.0f, 0.0f); // zero attack/release = instant (coeff = 0)

        std::vector<Slewer> slA, slB;
        mm.initVoiceSlewers(slA, 44100.0, 512);
        mm.initVoiceSlewers(slB, 44100.0, 512);

        // Slide is stored 0..1 in MPE, converted to -1..+1 bipolar in noteStarted.
        auto valsA = makeVoiceVals(0.0f, +1.0f, 0.0f, 0.5f);
        auto valsB = makeVoiceVals(0.0f, -1.0f, 0.0f, 0.5f);

        // With zero-ms rates the slewer snaps immediately — one call is enough.
        auto resultA = mm.evaluate(valsA, slA);
        auto resultB = mm.evaluate(valsB, slB);

        // A should be +1.0, B should be -1.0.  They must not be equal.
        expectWithinAbsoluteError(resultA[ModDestID::FilterCutoff],  1.0f, 0.001f);
        expectWithinAbsoluteError(resultB[ModDestID::FilterCutoff], -1.0f, 0.001f);
    }

    // ── 2. Slewer reset snaps stale state ─────────────────────────────────────
    // Simulates a voice that was holding a note with slide open (bipolar +1.0).
    // A new note arrives with slide neutral (0.0); without reset the slewer
    // still holds the old value and pollutes the start of the new note.
    // resetVoiceSlewers() must snap the slewer to the new note-on values.
    void slewerResetSnapsCorrectly()
    {
        beginTest("slewer reset snaps correctly");

        ModMatrix mm;
        mm.prepare(44100.0, 512);
        // Slow release (500 ms) — stale state persists for many blocks without reset.
        mm.addRoute(ModSourceID::MPE_Slide, ModDestID::FilterCutoff,
                    1.0f, 5.0f, 500.0f);

        std::vector<Slewer> sl;
        mm.initVoiceSlewers(sl, 44100.0, 512);

        // First note: slide fully open.
        auto valsOld = makeVoiceVals(0.0f, +1.0f, 0.0f, 0.5f);
        for (int i = 0; i < 20; ++i)
            mm.evaluate(valsOld, sl);   // settle slewer near +1.0

        // Verify stale state without reset: first block of new note still sees +1.0.
        auto valsNew = makeVoiceVals(0.0f, 0.0f, 0.0f, 0.5f);
        auto stale   = mm.evaluate(valsNew, sl);
        expect(stale[ModDestID::FilterCutoff] > 0.5f,
               "without reset, stale slewer should still be > 0.5");

        // Now reset and re-check: first block should snap to 0.
        mm.resetVoiceSlewers(sl, valsNew);
        auto fresh = mm.evaluate(valsNew, sl);
        expectWithinAbsoluteError(fresh[ModDestID::FilterCutoff], 0.0f, 0.05f);
    }

    // ── 3. Exponential curve shaping ─────────────────────────────────────────
    // The Exponential curve is x*|x|, which preserves sign and squares magnitude.
    // At x = 0.5 the result should be 0.25, at x = -0.5 it should be -0.25.
    // We test this end-to-end through ModMatrix with zero-ms slewers so the
    // slewer doesn't interfere.
    void exponentialCurveShaping()
    {
        beginTest("exponential curve shaping");

        ModMatrix mm;
        mm.prepare(44100.0, 512);
        mm.addRoute(ModSourceID::MPE_Slide, ModDestID::FilterCutoff,
                    1.0f, 0.0f, 0.0f,         // instant slew
                    ModRoute::CurveShape::Exponential);

        std::vector<Slewer> sl;
        mm.initVoiceSlewers(sl, 44100.0, 512);

        // Positive half: 0.5 → 0.25
        auto valsPos = makeVoiceVals(0.0f, 0.5f, 0.0f, 0.0f);
        auto resPos  = mm.evaluate(valsPos, sl);
        expectWithinAbsoluteError(resPos[ModDestID::FilterCutoff], 0.25f, 0.001f);

        // Negative half: -0.5 → -0.25  (symmetry check)
        auto valsNeg = makeVoiceVals(0.0f, -0.5f, 0.0f, 0.0f);
        auto resNeg  = mm.evaluate(valsNeg, sl);
        expectWithinAbsoluteError(resNeg[ModDestID::FilterCutoff], -0.25f, 0.001f);
    }

    // ── 4. amountParam overrides hardcoded amount ─────────────────────────────
    // A route with hardcoded amount 1.0 but an amountParam atomic set to 0.0
    // must produce zero contribution — the live parameter wins.
    void amountParamOverridesAmount()
    {
        beginTest("amountParam overrides hardcoded amount");

        std::atomic<float> liveAmount { 0.0f };

        ModMatrix mm;
        mm.prepare(44100.0, 512);
        mm.addRoute(ModSourceID::MPE_Pressure, ModDestID::VCALevel,
                    1.0f, 0.0f, 0.0f,          // hardcoded amount = 1.0, instant slew
                    ModRoute::CurveShape::Linear,
                    &liveAmount);               // but live param says 0.0

        std::vector<Slewer> sl;
        mm.initVoiceSlewers(sl, 44100.0, 512);

        auto vals   = makeVoiceVals(1.0f, 0.0f, 0.0f, 0.0f); // pressure = 1.0
        auto result = mm.evaluate(vals, sl);
        expectWithinAbsoluteError(result[ModDestID::VCALevel], 0.0f, 0.001f);

        // Now bump the live param to 0.5 — contribution should be 0.5.
        liveAmount = 0.5f;
        auto result2 = mm.evaluate(vals, sl);
        expectWithinAbsoluteError(result2[ModDestID::VCALevel], 0.5f, 0.001f);
    }

    // ── 5. Generic slot routing ───────────────────────────────────────────────
    // A configurable slot reads source/dest/amount/curve live from APVTS-style
    // atomics.  Verify: (a) a slot set to Slide→Cutoff routes correctly, (b) the
    // dest follows the dest choice, and (c) source "Off" (choice 0) disables it.
    void genericSlotRouting()
    {
        beginTest("generic slot routing");

        std::atomic<float> src { (float) 4 };  // 4 = Slide
        std::atomic<float> dst { (float) 1 };  // 1 = Cutoff
        std::atomic<float> amt { 1.0f };
        std::atomic<float> crv { 0.0f };       // Lin

        ModMatrix mm;
        mm.prepare(44100.0, 512);
        mm.addSlot(&src, &dst, &amt, &crv);

        std::vector<Slewer> sl;
        mm.initVoiceSlewers(sl, 44100.0, 512);

        auto vals = makeVoiceVals(0.0f, +1.0f, 0.0f, 0.0f);  // slide fully open
        std::array<float, ModDestID::NumDests> r {};
        for (int i = 0; i < 6; ++i) r = mm.evaluate(vals, sl);  // settle slew
        expectWithinAbsoluteError(r[ModDestID::FilterCutoff], 1.0f, 0.02f);

        // Re-point the destination to Reso; cutoff should fall silent.
        dst = (float) 2;  // 2 = Reso
        for (int i = 0; i < 6; ++i) r = mm.evaluate(vals, sl);
        expectWithinAbsoluteError(r[ModDestID::FilterCutoff], 0.0f, 0.001f);
        expectWithinAbsoluteError(r[ModDestID::FilterRes],    1.0f, 0.02f);

        // Source Off disables the slot entirely.
        src = 0.0f;
        for (int i = 0; i < 6; ++i) r = mm.evaluate(vals, sl);
        expectWithinAbsoluteError(r[ModDestID::FilterRes], 0.0f, 0.001f);
    }
};

// Static instance registers the test with JUCE's global UnitTestRunner.
static ModMatrixTests modMatrixTests;
