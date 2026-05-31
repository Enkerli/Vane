#include "PresetManager.h"

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& a)
    : apvts(a)
{}

juce::File PresetManager::getPresetsDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("Vane")
               .getChildFile("Presets");
}

void PresetManager::savePreset(const juce::String& name)
{
    auto dir = getPresetsDirectory();
    if (!dir.exists())
        dir.createDirectory();

    // Stamp the name into the state so it's saved in the file AND in the live
    // state (so a DAW project that captures this state shows the right preset).
    apvts.state.setProperty("presetName", name, nullptr);

    auto file = dir.getChildFile(name + fileExtension);
    auto xml  = apvts.copyState().createXml();
    if (xml && xml->writeTo(file))
        currentPresetName = name;
}

void PresetManager::loadPreset(const juce::String& name)
{
    auto file = getPresetsDirectory().getChildFile(name + fileExtension);
    if (!file.existsAsFile())
        return;

    auto xml = juce::XmlDocument::parse(file);
    if (xml && xml->hasTagName(apvts.state.getType())) {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        apvts.state.setProperty("presetName", name, nullptr);   // authoritative name
        currentPresetName = name;
    }
}

void PresetManager::deletePreset(const juce::String& name)
{
    getPresetsDirectory().getChildFile(name + fileExtension).deleteFile();
    if (currentPresetName == name)
        currentPresetName = {};
}

bool PresetManager::importFromClipboard()
{
    auto text = juce::SystemClipboard::getTextFromClipboard();
    if (text.isEmpty()) return false;

    // Validate the XML root tag — prevents silently importing unrelated clipboard
    // content (e.g. a URL or stray text) as a preset.
    auto xml = juce::XmlDocument::parse(text);
    if (!xml || !xml->hasTagName(apvts.state.getType())) return false;

    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    // Clear name: state is live in memory but not persisted to disk yet.
    // The editor shows "-- init --" until the user saves with a name.
    currentPresetName.clear();
    return true;
}

void PresetManager::exportToClipboard()
{
    auto xml = apvts.copyState().createXml();
    if (xml)
        juce::SystemClipboard::copyTextToClipboard(xml->toString());
}

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;
    // Non-recursive: presets live directly in the Presets/ directory, no sub-folders.
    auto files = getPresetsDirectory()
                     .findChildFiles(juce::File::findFiles, false,
                                     juce::String("*") + fileExtension);
    for (const auto& f : files)
        names.add(f.getFileNameWithoutExtension());
    // Case-insensitive alphabetical order so "Breathy Pad" and "breathy pad" sort together.
    names.sort(false /* caseSensitive */);
    return names;
}
