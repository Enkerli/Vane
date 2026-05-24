#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Saves and loads named presets to/from disk as XML files.
//
// Storage: ~/Library/Vane/Presets/<name>.vanepreset
//          (juce::File::userApplicationDataDirectory / "Vane" / "Presets")
//
// Format: the raw APVTS ValueTree serialised to XML — identical to what
// getStateInformation() already produces, so the files are portable and
// human-readable.
//
// Thread model: all public methods must be called from the message thread.
// loadPreset() calls apvts.replaceState() which is not audio-thread safe.
class PresetManager
{
public:
    static constexpr const char* fileExtension = ".vanepreset";

    explicit PresetManager(juce::AudioProcessorValueTreeState& apvts);

    // Save the current APVTS state under this name (overwrites if it exists).
    void savePreset(const juce::String& name);

    // Load a preset by name.  No-op if the file does not exist.
    void loadPreset(const juce::String& name);

    // Delete a preset file.  currentPresetName is cleared if it matched.
    void deletePreset(const juce::String& name);

    // Alphabetically sorted list of all preset names (without extension).
    juce::StringArray getPresetNames() const;

    juce::File getPresetsDirectory() const;

    // The name of the last saved/loaded preset, or empty if none.
    const juce::String& getCurrentPresetName() const { return currentPresetName; }

private:
    juce::AudioProcessorValueTreeState& apvts;
    juce::String currentPresetName;
};
