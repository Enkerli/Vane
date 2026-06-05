#include "ChordConfigStore.h"

juce::File ChordConfigStore::defaultFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Vane")
               .getChildFile ("ChordConfigs.json");
}

// Bundled starter palette.  Sequence lengths deliberately differ so the combined
// progression has period LCM(lengths) — the Kilgore "sounds random, is
// deterministic" effect — and one config uses just ratios to show the format.
juce::Array<ChordConfigStore::Config> ChordConfigStore::factory()
{
    juce::Array<Config> a;
    //       name                 seqs                          voices mode
    a.add ({ "Brecker Stack",    "3,7;7,12,10;5,9",             4, 1 });  // lengths 2,3,2 -> LCM 6
    a.add ({ "Just Triad Cycle", "5:4,6:5,7:6;3:2,8:5",         3, 1 });  // ratios, lengths 3,2
    a.add ({ "Coprime Fifths",   "7,12;7,3,10;0,4,7,10,2",      4, 1 });  // lengths 2,3,5 -> LCM 30
    a.add ({ "Quartal Pair",     "5,10;3,8,1",                  3, 1 });  // lengths 2,3
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

    if (storage.existsAsFile()) {
        const auto parsed = juce::JSON::parse (storage.loadFileAsString());
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

    storage.getParentDirectory().createDirectory();
    storage.replaceWithText (juce::JSON::toString (juce::var (root)));
}
