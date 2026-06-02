#include <juce_core/juce_core.h>
#include <cmath>
#include "Synth/Oscillator.h"
#include "MPE/TuningClient.h"

// ── Tuning Pipeline Tests ──────────────────────────────────────────────────
//
// Covers two layers of the pitch-generation stack:
//
//   TuningClient:
//     1. equalTemperamentA4       — MIDI 69 maps to exactly 440.0 Hz.
//     2. octaveSymmetry           — every +12 semitones doubles the frequency.
//     3. noMtsMasterFallsBackToET — without a connected MTS master, noteToHz()
//                                   returns the equal-temperament value.
//
//   Oscillator (regression guard for the phaseInc > 1 blow-up fixed in
//   commit <oscillator-nyquist-clamp>):
//     4. outputBoundedAboveNyquist — freq > sr → no NaN/inf, |out| bounded.
//     5. phaseStaysInUnitInterval  — getPhase() always in [0, 1) even at 2×sr.
//     6. edgeCaseExactSampleRate   — freq == sr doesn't produce inf.
//     7. highNoteMaxMpePitchbend   — MIDI 102 + 48 st MPE bend stays finite
//                                    (the specific scenario that triggered the bug).
//     8. normalOperationPreserved  — sine at 440 Hz stays within [-1, 1].

class TuningTests : public juce::UnitTest
{
public:
    TuningTests() : juce::UnitTest("Tuning", "Vane") {}

    void runTest() override
    {
        equalTemperamentA4();
        octaveSymmetry();
        noMtsMasterFallsBackToET();
        internalTableInitialisedOnConstruction();
        oscillatorOutputBoundedAboveNyquist();
        oscillatorPhaseStaysInUnitInterval();
        oscillatorEdgeCaseExactSampleRate();
        oscillatorHighNoteMaxMpePitchbend();
        oscillatorNormalOperationPreserved();
    }

private:
    // ── 1. A4 = 440 Hz ────────────────────────────────────────────────────────
    void equalTemperamentA4()
    {
        beginTest("TuningClient: A4 (MIDI 69) = 440.0 Hz");
        expectWithinAbsoluteError(TuningClient::equalTemperamentHz(69), 440.0f, 0.001f);
    }

    // ── 2. Octave symmetry ────────────────────────────────────────────────────
    // Every +12 semitones in equal temperament must double the frequency.
    // Tested across the full playable MIDI range (0..127).
    void octaveSymmetry()
    {
        beginTest("TuningClient: +12 semitones doubles frequency");
        for (int n = 0; n <= 115; ++n)
        {
            float lo    = TuningClient::equalTemperamentHz(n);
            float hi    = TuningClient::equalTemperamentHz(n + 12);
            float ratio = hi / lo;
            expectWithinAbsoluteError(ratio, 2.0f, 0.0001f);
        }
    }

    // ── 3. No MTS master falls back to ET ────────────────────────────────────
    // When no MTS-ESP master is running (or VANE_HAS_MTS is not defined),
    // noteToHz() must return the equal-temperament value for every note.
    // This guards against a future MTS integration inadvertently breaking the
    // no-master path and silencing notes.
    void internalTableInitialisedOnConstruction()
    {
        // Regression: a default-constructed TuningClient must have its internal
        // cents table filled to 12-EDO.  When it was left all-zeros, the Internal
        // source (and the FollowMTS no-master fallback) returned ~8.18 Hz (MIDI 0)
        // for EVERY note — the whole keyboard played as sub-audio rumble.
        // This test forces Internal source so it does not depend on whether an
        // MTS master happens to be present on the test machine.
        beginTest("TuningClient: internal table is 12-EDO on construction");
        TuningClient tc;
        tc.setTuningSource(TuningSource::Internal);
        for (int note : { 0, 48, 55, 60, 69, 96, 127 })
        {
            float expected = TuningClient::equalTemperamentHz(note);
            float actual   = tc.noteToHz(note, 1);
            expectWithinAbsoluteError(actual, expected, expected * 0.0005f);
        }
    }

    void noMtsMasterFallsBackToET()
    {
        beginTest("TuningClient: no MTS master falls back to equal temperament");
        TuningClient tc;
        for (int note : { 0, 48, 60, 69, 96, 127 })
        {
            float expected = TuningClient::equalTemperamentHz(note);
            float actual   = tc.noteToHz(note, 1);
            // Relative tolerance: Hz values span 8 Hz (MIDI 0) to 12544 Hz (MIDI 127).
            float tol = expected * 0.0001f;
            expectWithinAbsoluteError(actual, expected, tol);
        }
    }

    // ── 4. Oscillator: output bounded above Nyquist ───────────────────────────
    //
    // Regression for the phaseInc > 1 bug that caused DAW hard-mute.
    //
    // Root cause (pre-fix): Oscillator::next() only subtracted 1.0 once from
    // phase, so after "phase += 2.0" → phase = 2.something → phase -= 1.0 →
    // phase = 1.something → still >= 1.0 but not wrapped.  Phase then grew by
    // the fractional part of phaseInc every sample without bound.
    // polyBlep() computed (phase/phaseInc)², an enormous number, poisoning the
    // audio buffer and triggering the DAW safety mute within milliseconds.
    //
    // Fix: setFrequency() clamps phaseInc to <= 0.9999; next() uses floor-based
    // wrap so any residual >= 1.0 is handled even if clamping is somehow bypassed.
    void oscillatorOutputBoundedAboveNyquist()
    {
        beginTest("Oscillator: output finite and bounded at 2x sample rate");

        const float sr = 44100.0f;
        Oscillator  osc;
        osc.prepare(sr);
        osc.setWaveform(Oscillator::Waveform::Saw);
        osc.setFrequency(sr * 2.0f);   // phaseInc was 2.0 before the fix

        bool allOk = true;
        for (int i = 0; i < 4096; ++i)
        {
            float s = osc.next();
            if (!std::isfinite(s) || std::abs(s) > 2.0f)
            {
                allOk = false;
                break;
            }
        }
        expect(allOk, "saw output is not bounded at 2x sample rate (phaseInc wrap bug)");
    }

    // ── 5. Oscillator: phase stays in [0, 1) above Nyquist ───────────────────
    void oscillatorPhaseStaysInUnitInterval()
    {
        beginTest("Oscillator: getPhase() stays in [0, 1) at 2x sample rate");

        const float sr = 44100.0f;
        Oscillator  osc;
        osc.prepare(sr);
        osc.setWaveform(Oscillator::Waveform::Square);
        osc.setFrequency(sr * 2.0f);

        bool bounded = true;
        for (int i = 0; i < 4096; ++i)
        {
            osc.next();
            float p = osc.getPhase();
            if (p < 0.0f || p >= 1.0f)
            {
                bounded = false;
                break;
            }
        }
        expect(bounded, "phase escaped [0, 1) - floor-wrap is not working");
    }

    // ── 6. Oscillator: edge case — freq exactly equals sample rate ────────────
    // phaseInc = 1.0 is the boundary: the old if-only wrap produced phase = 0.0
    // on the first sample, then jumped back to exactly 1.0 on the second,
    // and the if-guard never fired again.  The clamp (phaseInc → 0.9999) plus
    // floor-wrap must handle this cleanly.
    void oscillatorEdgeCaseExactSampleRate()
    {
        beginTest("Oscillator: output finite at freq == sample rate");

        const float sr = 44100.0f;
        Oscillator  osc;
        osc.prepare(sr);
        osc.setWaveform(Oscillator::Waveform::Saw);
        osc.setFrequency(sr);   // phaseInc = 1.0 exactly before clamping

        bool allOk = true;
        for (int i = 0; i < 1024; ++i)
        {
            float s = osc.next();
            if (!std::isfinite(s) || std::abs(s) > 2.0f) { allOk = false; break; }
        }
        expect(allOk, "output not finite at freq == sample rate");
    }

    // ── 7. High note + maximum MPE pitchbend ─────────────────────────────────
    // Reproduces the specific scenario that triggered the MTS-ESP + pitchbend
    // bug report: a note at MIDI 102 (F#7, ~2794 Hz in ET) with a full +48
    // semitone MPE pitchbend multiplies the fundamental by 2^(48/12) = 16,
    // giving ~44 700 Hz — just above the 44100 Hz sample rate.
    //
    // Before the fix: phaseInc ≈ 1.013 → phase escaped [0, 1) → inf output.
    // After the fix: setFrequency() clamps to 0.9999 → no blow-up.
    void oscillatorHighNoteMaxMpePitchbend()
    {
        beginTest("Oscillator: MIDI 102 + 48-st MPE bend stays finite");

        const float sr       = 44100.0f;
        const float baseHz   = TuningClient::equalTemperamentHz(102);   // ~2794 Hz
        const float bendMult = std::pow(2.0f, 48.0f / 12.0f);          // 16.0x
        const float finalHz  = baseHz * bendMult;                       // ~44 700 Hz

        // Confirm this scenario genuinely exceeds the sample rate (else the test
        // is not covering what it claims).
        expect(finalHz > sr, "test setup: finalHz should exceed the sample rate");

        Oscillator osc;
        osc.prepare(sr);
        osc.setWaveform(Oscillator::Waveform::Saw);
        osc.setFrequency(finalHz);

        bool allOk = true;
        for (int i = 0; i < 4096; ++i)
        {
            float s = osc.next();
            if (!std::isfinite(s) || std::abs(s) > 2.0f) { allOk = false; break; }
        }
        expect(allOk, "saw output blew up at MIDI 102 + 48 st MPE bend");
    }

    // ── 8. Normal operation not broken by the fix ─────────────────────────────
    // A sine wave at 440 Hz must stay within [-1, 1] — the fix must not
    // alter behaviour for in-range frequencies.
    void oscillatorNormalOperationPreserved()
    {
        beginTest("Oscillator: sine at 440 Hz stays within [-1, 1]");

        Oscillator osc;
        osc.prepare(44100.0);
        osc.setWaveform(Oscillator::Waveform::Sine);
        osc.setFrequency(440.0f);

        bool ok = true;
        for (int i = 0; i < 4096; ++i)
        {
            float s = osc.next();
            if (!std::isfinite(s) || s < -1.0001f || s > 1.0001f) { ok = false; break; }
        }
        expect(ok, "sine at 440 Hz exceeded [-1, 1]");
    }
};

// Static instance auto-registers with JUCE's global UnitTestRunner.
static TuningTests tuningTests;
