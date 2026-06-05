#pragma once
#include <juce_core/juce_core.h>

// A small GLOBAL library of rotating-chord configurations, persisted as JSON at
// ~/Library/Vane/ChordConfigs.json (same directory as presets).  Independent of
// presets: the per-voice interval sequences already travel inside a preset, but
// this is the reusable palette you build up across patches.
//
// Each config captures the sequences PLUS the unison voice count and mode, so
// recalling one sets the chord up to voice as intended regardless of the patch
// it lands on.  The on-disk shape is:
//   { "configs": [ { "name": "...", "seqs": "3,7;7,12,10", "voices": 4, "mode": 1 }, ... ] }
class ChordConfigStore
{
public:
    struct Config {
        juce::String name;
        juce::String seqs;          // ";"-separated per-voice interval lists
        int          voices = 2;    // unison voice COUNT (1/2/3/4/6), not the param index
        int          mode   = 1;    // 0 = Detune, 1 = Chord
    };

    // storageFile defaults to the real location; tests pass a temp file so they
    // never touch the user's library.  Seeds the factory set if the file is absent.
    explicit ChordConfigStore (juce::File storageFile = defaultFile());

    juce::Array<Config> all() const { return configs; }
    bool   contains (const juce::String& name) const;
    void   save     (const Config& c);          // add or replace by name; persists
    void   remove   (const juce::String& name); // delete by name; persists

    juce::File getFile() const { return storage; }
    static juce::File defaultFile();
    static juce::Array<Config> factory();        // bundled starter palette

private:
    void load();
    void persist() const;

    juce::File          storage;
    juce::Array<Config> configs;
};
