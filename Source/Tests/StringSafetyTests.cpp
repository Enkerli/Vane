#include <juce_core/juce_core.h>
#include "Preset/PresetManager.h"

// ── JUCE String safety tests ───────────────────────────────────────────────
//
// juce::String(const char*) asserts that every byte is valid ASCII (≤ 127).
// Bytes > 127 appear in UTF-8 multi-byte sequences (e.g. × = U+00D7 = \xc3\x97).
// Passing a UTF-8 string literal directly to juce::String, juce::TextButton{},
// or any JUCE API that accepts String via implicit const-char* conversion will
// fire jassert(CharPointer_ASCII::isValidString(...)) at runtime.
//
// Rule: every non-ASCII constant must go through String::fromUTF8() (or
//       CharPointer_UTF8) before entering JUCE's type system.
//
// The tests below catch the two most common failure modes so regressions show
// up immediately in Debug builds.

class StringSafetyTests : public juce::UnitTest
{
public:
    StringSafetyTests() : juce::UnitTest("StringSafety", "Vane") {}

    void runTest() override
    {
        fileExtensionIsAsciiSafe();
        utf8RoundTrip();
    }

private:
    // ── 1. Hardcoded const char* literals passed to String must be ASCII ──────
    // If fileExtension ever gains a non-ASCII character (e.g. an emoji) it will
    // crash the plugin on any String construction that uses it.
    void fileExtensionIsAsciiSafe()
    {
        beginTest("PresetManager::fileExtension is ASCII-safe");
        expect(juce::CharPointer_ASCII::isValidString(
                   PresetManager::fileExtension,
                   std::numeric_limits<int>::max()),
               "fileExtension contains non-ASCII bytes — "
               "use String::fromUTF8() or keep it pure ASCII");
    }

    // ── 2. Non-ASCII UI labels must use fromUTF8 ─────────────────────────────
    // Demonstrates the correct pattern for symbols like × (U+00D7, \xc3\x97).
    // Constructing String("×") directly asserts; fromUTF8("×") is correct.
    void utf8RoundTrip()
    {
        beginTest("non-ASCII label survives fromUTF8 round-trip");

        // \xc3\x97 is the UTF-8 encoding of × (MULTIPLICATION SIGN, U+00D7).
        // Both spellings must produce the same juce::String.
        auto fromEscapes = juce::String::fromUTF8("\xc3\x97");
        auto fromLiteral  = juce::String::fromUTF8("×");

        expectEquals(fromEscapes, fromLiteral);

        // And it must not be empty or the ASCII fallback "?".
        expect(fromEscapes.isNotEmpty());
        expect(fromEscapes != juce::String("?"));
    }
};

static StringSafetyTests stringSafetyTests;
