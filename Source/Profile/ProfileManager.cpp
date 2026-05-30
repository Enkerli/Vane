#include "ProfileManager.h"

ProfileManager::ProfileManager(juce::AudioProcessorValueTreeState& a)
    : apvts(a)
{}

juce::File ProfileManager::getProfilesDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("Vane")
               .getChildFile("Profiles");
}

juce::StringArray ProfileManager::bindingParamIds() const
{
    juce::StringArray ids { "macroBreathSrc", "macroBreathCC", "macroExprSrc", "macroExprCC" };
    for (int g = 0; g < ModSlots::NumAux; ++g)
        ids.add("aux" + juce::String(g) + "_cc");
    return ids;
}

void ProfileManager::saveProfile(const juce::String& name)
{
    auto dir = getProfilesDirectory();
    if (! dir.exists())
        dir.createDirectory();

    juce::ValueTree t("VaneProfile");
    t.setProperty("name",       name, nullptr);
    t.setProperty("controller", apvts.state.getProperty("controllerName", juce::String()), nullptr);

    for (const auto& id : bindingParamIds())
        if (auto* p = apvts.getRawParameterValue(id))
            t.setProperty(id, p->load(), nullptr);

    for (int g = 0; g < ModSlots::NumAux; ++g) {
        const auto k = "auxLabel" + juce::String(g);
        t.setProperty(k, apvts.state.getProperty(k, "Aux " + juce::String(g + 1)), nullptr);
    }

    if (auto xml = t.createXml())
        if (xml->writeTo(dir.getChildFile(name + fileExtension)))
            currentName = name;
}

void ProfileManager::loadProfile(const juce::String& name)
{
    auto file = getProfilesDirectory().getChildFile(name + fileExtension);
    if (! file.existsAsFile())
        return;

    auto xml = juce::XmlDocument::parse(file);
    if (! xml)
        return;
    auto t = juce::ValueTree::fromXml(*xml);
    if (! t.isValid())
        return;

    apvts.state.setProperty("controllerName", t.getProperty("controller", juce::String()), nullptr);

    for (const auto& id : bindingParamIds())
        if (t.hasProperty(id))
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id)))
                p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(static_cast<double>(t.getProperty(id)))));

    for (int g = 0; g < ModSlots::NumAux; ++g) {
        const auto k = "auxLabel" + juce::String(g);
        if (t.hasProperty(k))
            apvts.state.setProperty(k, t.getProperty(k), nullptr);
    }

    currentName = name;
}

void ProfileManager::deleteProfile(const juce::String& name)
{
    getProfilesDirectory().getChildFile(name + fileExtension).deleteFile();
    if (currentName == name)
        currentName = {};
}

juce::StringArray ProfileManager::getProfileNames() const
{
    juce::StringArray names;
    for (const auto& f : getProfilesDirectory()
                             .findChildFiles(juce::File::findFiles, false,
                                             juce::String("*") + fileExtension))
        names.add(f.getFileNameWithoutExtension());
    names.sort(false /* caseSensitive */);
    return names;
}
