#include <juce_core/juce_core.h>
#include <cmath>
#include "PluginProcessor.h"
#include "Preset/ChordConfigStore.h"

// ── Rotating-chord interval parsing ──────────────────────────────────────────
//
// VaneProcessor::parseChordInterval turns a sequence token into semitones.  The
// point of accepting just ratios (3:2, 5/4) is EXACTNESS: a 3:2 fifth must come
// out as 701.96c, not a rounded 700c — so these guard the conversion and the
// junk-rejection that keeps a stray token from silently becoming 0 semitones.

class ChordTests : public juce::UnitTest
{
public:
    ChordTests() : juce::UnitTest("Chord", "Vane") {}

    void runTest() override
    {
        beginTest("parseChordInterval: decimal semitones");
        expectWithinAbsoluteError(VaneProcessor::parseChordInterval("7"),     7.0f,   1e-5f);
        expectWithinAbsoluteError(VaneProcessor::parseChordInterval("-5"),   -5.0f,   1e-5f);
        expectWithinAbsoluteError(VaneProcessor::parseChordInterval(" 7.02 "), 7.02f, 1e-4f);
        expectWithinAbsoluteError(VaneProcessor::parseChordInterval("0"),     0.0f,   1e-5f);  // unison

        beginTest("parseChordInterval: just ratios are exact, not rounded");
        // 3:2 = 701.955c = 7.01955 st (NOT 7.0).  5/4 = 386.31c.  2:1 = octave.
        expectWithinAbsoluteError(VaneProcessor::parseChordInterval("3:2"),
                                  12.0f * std::log2(1.5f),  1e-5f);
        expect(std::abs(VaneProcessor::parseChordInterval("3:2") - 7.0f) > 0.015f,
               "3:2 must be ~702c, not a rounded 700c");
        expectWithinAbsoluteError(VaneProcessor::parseChordInterval("5/4"),
                                  12.0f * std::log2(1.25f), 1e-5f);
        expectWithinAbsoluteError(VaneProcessor::parseChordInterval("2:1"),  12.0f, 1e-5f);
        expectWithinAbsoluteError(VaneProcessor::parseChordInterval("1:2"), -12.0f, 1e-5f);
        expectWithinAbsoluteError(VaneProcessor::parseChordInterval("9/8"),
                                  12.0f * std::log2(9.0f / 8.0f), 1e-5f);

        beginTest("parseChordInterval: junk rejected as NaN (not silent 0)");
        expect(std::isnan(VaneProcessor::parseChordInterval("")),     "empty -> NaN");
        expect(std::isnan(VaneProcessor::parseChordInterval("   ")),  "blank -> NaN");
        expect(std::isnan(VaneProcessor::parseChordInterval("abc")),  "non-numeric -> NaN");
        expect(std::isnan(VaneProcessor::parseChordInterval("3:0")),  "zero denominator -> NaN");
        expect(std::isnan(VaneProcessor::parseChordInterval("0:2")),  "zero numerator -> NaN");

        chordConfigStoreRoundTrips();
    }

    // Save / recall / delete + persistence + factory seeding, all against a temp
    // file so the user's real ~/Library/Vane/ChordConfigs.json is never touched.
    void chordConfigStoreRoundTrips()
    {
        beginTest("ChordConfigStore: factory seed, save/recall/delete, persistence");
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("vane_chordcfg_test.json");
        tmp.deleteFile();

        {   // Fresh store on a missing file seeds the factory palette and writes it.
            ChordConfigStore store(tmp);
            expect(store.all().size() >= 3, "factory palette should seed several configs");
            expect(tmp.existsAsFile(), "store must persist the seed to disk");

            ChordConfigStore::Config c { "My Cycle", "5:4,6:5;3:2", 3, 1 };
            store.save(c);
            expect(store.contains("My Cycle"), "saved config should be present");

            // Overwrite by name (not duplicate).
            const int before = store.all().size();
            store.save({ "My Cycle", "7,12;5", 4, 1 });
            expectEquals(store.all().size(), before, "saving same name overwrites, not duplicates");
        }
        {   // A new store on the same file must read back what we persisted.
            ChordConfigStore reopened(tmp);
            expect(reopened.contains("My Cycle"), "config must survive reopen");
            ChordConfigStore::Config got;
            for (const auto& c : reopened.all()) if (c.name == "My Cycle") got = c;
            expectEquals(got.seqs, juce::String("7,12;5"), "overwritten value persisted");
            expectEquals(got.voices, 4);

            reopened.remove("My Cycle");
            expect(! reopened.contains("My Cycle"), "deleted config should be gone");
        }
        {   // Deletion persisted across reopen.
            ChordConfigStore reopened(tmp);
            expect(! reopened.contains("My Cycle"), "deletion must persist");
        }
        tmp.deleteFile();
    }
};

static ChordTests chordTests;
