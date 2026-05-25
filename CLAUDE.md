# Vane — notes for Claude Code

## JUCE String safety (non-ASCII assertion)

**Rule: every non-ASCII character must go through `juce::String::fromUTF8()`.
Never pass a UTF-8 string literal directly to any JUCE API that accepts `String`.**

`juce::String(const char*)` contains a debug assertion that fires on any byte > 127.
This trips on the most innocent-looking code and is hard to trace in a crash log.

### Common traps

| Trap | Bad | Good |
|---|---|---|
| Button brace-init | `TextButton { "×" }` | `setButtonText(String::fromUTF8("×"))` |
| APVTS param display name | `"Breath→VCA Curve"` | `"Breath to VCA Curve"` |
| ComboBox item | `addItem("résumé", 1)` | `addItem(String::fromUTF8("r\xc3\xa9sum\xc3\xa9"), 1)` |
| Any implicit conversion | `String label = "café"` | `String label = String::fromUTF8("café")` |

### Specific case that bit us (twice)

The arrow character **→** (U+2192) encodes as `\xe2\x86\x92` in UTF-8 — three
bytes all > 127.  It looks ASCII in the editor but crashes the plugin at startup.

```cpp
// CRASH — fired jassert in juce_String.cpp:327
makeCurveParam("breathVCACurve", "Breath→VCA Curve", 0.0f);

// SAFE
makeCurveParam("breathVCACurve", "Breath to VCA Curve", 0.0f);
```

Other non-ASCII characters that have appeared in this codebase and must use `fromUTF8`:
- `×` (U+00D7, `\xc3\x97`) — delete button label
- `·` (U+00B7, `\xc2\xb7`) — subtitle separator
- `→` (U+2192, `\xe2\x86\x92`) — matrix row arrow

### Detection

`Source/Tests/StringSafetyTests.cpp` runs on every Debug build and checks:
1. `PresetManager::fileExtension` is ASCII-safe
2. `fromUTF8` round-trips correctly for `×`
3. All APVTS curve-param display names are ASCII-safe (the specific regression)

When adding a new APVTS parameter: if you want an arrow or other symbol in the
display name, write it out in words instead.  The display name is used by DAW
automation lanes and plugin parameter lists — plain ASCII is universally safe.
