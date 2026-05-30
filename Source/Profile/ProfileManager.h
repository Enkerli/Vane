#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Modulation/ModSource.h"   // ModSlots::NumAux

// Saves/loads named CONTROLLER PROFILES — the configurable source bindings only:
//   controller name, breath/expression CC bindings, and each aux source's CC# +
//   label.  Distinct from presets (which carry the whole patch); a profile is
//   reusable across patches and describes "what my controller(s) send".
//
// Storage: ~/Library/Vane/Profiles/<name>.vaneprofile  (small XML).
// Thread model: message thread only (writes APVTS params + state properties).
class ProfileManager
{
public:
    static constexpr const char* fileExtension = ".vaneprofile";

    explicit ProfileManager(juce::AudioProcessorValueTreeState& apvts);

    void saveProfile  (const juce::String& name);   // overwrites if it exists
    void loadProfile  (const juce::String& name);   // applies bindings; no-op if absent
    void deleteProfile(const juce::String& name);

    juce::StringArray getProfileNames() const;       // sorted, no extension
    juce::File        getProfilesDirectory() const;
    const juce::String& getCurrentProfileName() const { return currentName; }

private:
    // The APVTS param ids a profile captures (binding state only).
    juce::StringArray bindingParamIds() const;

    juce::AudioProcessorValueTreeState& apvts;
    juce::String currentName;
};
