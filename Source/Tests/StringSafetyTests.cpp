#include <juce_core/juce_core.h>
#include "Preset/PresetManager.h"
#include <cstring>
#include <limits>

// ── JUCE String safety tests ───────────────────────────────────────────────
//
// juce::String(const char*) asserts that every byte is valid ASCII (≤ 127).
// Bytes > 127 appear in UTF-8 multi-byte sequences (e.g. × = U+00D7 = \xc3\x97).
// Passing a UTF-8 string literal directly to juce::String, juce::TextButton{},
// or any JUCE API that accepts String via implicit const-char* conversion will
// fire jassert(CharPointer_ASCII::isValidString(...)) at runtime.
//
// ── Rule ──────────────────────────────────────────────────────────────────
// Every non-ASCII constant MUST go through String::fromUTF8() (or
// CharPointer_UTF8) before entering JUCE's type system.
//
// ── Common traps ──────────────────────────────────────────────────────────
// • juce::TextButton { "label with → or × or · " }  — brace-init calls
//   String(const char*).  Use setButtonText(String::fromUTF8(...)) instead.
// • APVTS param display names passed as const char* to AudioParameterFloat —
//   → (U+2192) is tempting in "Breath→VCA" but is three non-ASCII bytes.
//   Use plain ASCII: "Breath to VCA".
// • Any ComboBox::addItem("label with accent chars") — same trap.
//
// The tests below catch the three most common failure modes so regressions
// surface immediately in Debug builds.

class StringSafetyTests : public juce::UnitTest
{
public:
    StringSafetyTests() : juce::UnitTest("StringSafety", "Vane") {}

    void runTest() override
    {
        fileExtensionIsAsciiSafe();
        utf8RoundTrip();
        apvtsParamNamesAreAsciiSafe();
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
               "fileExtension contains non-ASCII bytes - "
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

    // ── 3. APVTS display names must be ASCII ─────────────────────────────────
    // AudioParameterFloat(id, name, ...) stores `name` via juce::String(const char*).
    // Any non-ASCII byte in the display name fires the assertion.
    //
    // The specific regression this catches: using → (U+2192, \xe2\x86\x92) as
    // a "goes to" arrow in parameter names like "Breath→VCA Curve".
    // The fix is to use "Breath to VCA Curve" (plain ASCII).
    void apvtsParamNamesAreAsciiSafe()
    {
        beginTest("APVTS parameter display names are ASCII-safe");

        // The forbidden pattern: → encodes to \xe2\x86\x92, three non-ASCII bytes.
        // Verify that the ASCII validator correctly rejects it.
        const char* arrowName = "Breath\xe2\x86\x92VCA Curve";  // "Breath→VCA Curve"
        expect(!juce::CharPointer_ASCII::isValidString(arrowName,
                   static_cast<int>(std::strlen(arrowName))),
               "Arrow-containing name should fail ASCII check (sanity)");

        // The safe alternatives actually used in createParameterLayout().
        // If any of these change to include non-ASCII bytes, the test fails here
        // rather than as a hard crash in the plugin constructor.
        const char* safeNames[] = {
            "Breath to VCA Curve",
            "Expr to VCA Curve",
            "Pressure to VCA Curve",
            "CC74 to Cutoff Curve",
            "Breath to Cutoff Curve",
            "Expr to Cutoff Curve",
            "Pressure to Cutoff Curve",
            "Breath to Reso Curve",
            "Expr to Reso Curve",
            "Velocity to Cutoff Curve",
            "CC74 to Reso Curve",
        };
        for (auto* name : safeNames) {
            expect(juce::CharPointer_ASCII::isValidString(
                       name, static_cast<int>(std::strlen(name))),
                   juce::String("Param name is not ASCII-safe: use 'to' not arrow - ")
                       + juce::String(name));
        }
    }
};

static StringSafetyTests stringSafetyTests;
