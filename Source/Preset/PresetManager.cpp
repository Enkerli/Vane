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
        currentPresetName = name;
    }
}

void PresetManager::deletePreset(const juce::String& name)
{
    getPresetsDirectory().getChildFile(name + fileExtension).deleteFile();
    if (currentPresetName == name)
        currentPresetName = {};
}

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;
    auto files = getPresetsDirectory()
                     .findChildFiles(juce::File::findFiles, false,
                                     juce::String("*") + fileExtension);
    for (const auto& f : files)
        names.add(f.getFileNameWithoutExtension());
    names.sort(false /* caseSensitive */);
    return names;
}
