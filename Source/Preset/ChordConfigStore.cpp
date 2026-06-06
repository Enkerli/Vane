#include "ChordConfigStore.h"

// Bump when factory() changes so existing libraries additively pick up new
// shapes exactly once (see load()).  0 = pre-versioned files (original 4 shapes).
static constexpr int kFactoryVersion = 2;

juce::File ChordConfigStore::defaultFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Vane")
               .getChildFile ("ChordConfigs.json");
}

// Bundled starter palette.  Sequence lengths deliberately differ so the combined
// progression has period LCM(lengths) — the Kilgore "sounds random, is
// deterministic" effect.  The curated set leans on the qualities that make
// these musical: co-prime (often prime) loop lengths so the chord rarely
// repeats; intervals that move BOTH up and down (just ratios <1 are below the
// played note) for contrary / oblique motion; a few pointed dissonances among
// consonances (hold a note to sit on the tension, re-articulate to resolve);
// and large "multiple" / "extension" intervals (octave 2/1, twelfth 3/1,
// ninth 9/4, tenth 5/2 …).
juce::Array<ChordConfigStore::Config> ChordConfigStore::factory()
{
    juce::Array<Config> a;
    //       name                  seqs                                                 voices mode
    a.add ({ "Contrary Primes",   "3/2,5/4,2/1;2/3,5/4,3/2,7/5,4/3;1/2,3/5",            4, 1 }); // 3·5·2=30; up vs down; 7/5 bite
    a.add ({ "Brecker Extensions","3/2,9/4,5/4;2/1,5/2,3/1,15/8,3/2;2/3,4/3",           4, 1 }); // 3·5·2=30; 9th/10th/12th reach
    a.add ({ "Oblique Sevenths",  "5/4,6/5,7/4,3/2,9/8;3/2,4/3,7/5,2/1,5/3,16/9,3/2",   3, 1 }); // 5·7=35; consonance vs 7/4,7/5,16/9
    a.add ({ "Subharmonic Spiral","2/3,4/5,1/2;3/5,1/2,4/7,2/3,1/3",                    3, 1 }); // 3·5=15; all below -> contrary to melody
    a.add ({ "Wide Primes",       "5/4,2/1;3/2,5/3,15/8;2/3,1/2,4/3,7/5,3/4",           4, 1 }); // 2·3·5=30; wide spread, 7/5 tension
    a.add ({ "Just Triad Cycle",  "5/4,6/5,7/6;3/2,8/5",                                3, 1 }); // 3·2=6; gentle pure-triad starter
    return a;
}

ChordConfigStore::ChordConfigStore (juce::File storageFile)
    : storage (std::move (storageFile))
{
    load();
}

bool ChordConfigStore::contains (const juce::String& name) const
{
    for (const auto& c : configs) if (c.name == name) return true;
    return false;
}

void ChordConfigStore::save (const Config& c)
{
    if (c.name.trim().isEmpty()) return;
    for (auto& existing : configs)
        if (existing.name == c.name) { existing = c; persist(); return; }
    configs.add (c);
    persist();
}

void ChordConfigStore::remove (const juce::String& name)
{
    for (int i = configs.size(); --i >= 0;)
        if (configs[i].name == name) configs.remove (i);
    persist();
}

void ChordConfigStore::load()
{
    configs.clear();
    int fileVersion = 0;

    if (storage.existsAsFile()) {
        const auto parsed = juce::JSON::parse (storage.loadFileAsString());
        fileVersion = (int) parsed.getProperty ("factoryVersion", 0);
        if (auto* arr = parsed.getProperty ("configs", juce::var()).getArray()) {
            for (const auto& v : *arr) {
                Config c;
                c.name   = v.getProperty ("name",   "").toString();
                c.seqs   = v.getProperty ("seqs",   "").toString();
                c.voices = (int) v.getProperty ("voices", 2);
                c.mode   = (int) v.getProperty ("mode",   1);
                if (c.name.isNotEmpty()) configs.add (c);
            }
        }
    }

    // Empty or unreadable -> seed the factory palette and write it out so the user
    // has a starting point and a concrete example of the on-disk format.
    if (configs.isEmpty()) {
        configs = factory();
        persist();
        return;
    }

    // Upgrade: when the factory set grows, additively merge in any new shapes
    // (by name) ONCE — the version stamp means a shape the user later deletes
    // stays deleted across restarts rather than reappearing every launch.
    if (fileVersion < kFactoryVersion) {
        for (const auto& f : factory())
            if (! contains (f.name)) configs.add (f);
        persist();   // records the new factoryVersion
    }
}

void ChordConfigStore::persist() const
{
    juce::Array<juce::var> arr;
    for (const auto& c : configs) {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name",   c.name);
        o->setProperty ("seqs",   c.seqs);
        o->setProperty ("voices", c.voices);
        o->setProperty ("mode",   c.mode);
        arr.add (juce::var (o));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("configs", juce::var (arr));
    root->setProperty ("factoryVersion", kFactoryVersion);

    storage.getParentDirectory().createDirectory();
    storage.replaceWithText (juce::JSON::toString (juce::var (root)));
}
