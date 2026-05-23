#include "PluginEditor.h"

VaneEditor::VaneEditor(VaneProcessor& p)
    : AudioProcessorEditor(p), vaneProcessor(p)
{
    setSize(480, 320);

    addAndMakeVisible(reconnectMtsButton);
    reconnectMtsButton.onClick = [this] {
        vaneProcessor.reconnectMTS();
    };
    startTimerHz(2);   // refresh MTS label twice per second
}

VaneEditor::~VaneEditor() { stopTimer(); }

void VaneEditor::timerCallback()
{
    bool connected = vaneProcessor.mtsConnected();
    reconnectMtsButton.setButtonText(connected ? "MTS-ESP: connected" : "MTS-ESP: reconnect");
    reconnectMtsButton.setColour(juce::TextButton::buttonColourId,
        connected ? juce::Colour(0xff1a3a1a) : juce::Colour(0xff3a1a1a));
}

void VaneEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff111118));

    g.setColour(juce::Colour(0xffb0b8d0));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(22.0f)));
    g.drawText("Vane", getLocalBounds().removeFromTop(55),
               juce::Justification::centred);

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
    g.setColour(juce::Colour(0xff606878));
    g.drawText(juce::String::fromUTF8("MPE  \xc2\xb7  MTS-ESP  \xc2\xb7  CC modulation"),
               getLocalBounds().withTrimmedTop(56).removeFromTop(20),
               juce::Justification::centred);

    // Version + build stamp
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.setColour(juce::Colour(0xff3a3f4a));
    g.drawText(juce::String("v") + JucePlugin_VersionString
                   + "  \xc2\xb7  built " __DATE__ " " __TIME__,
               getLocalBounds().removeFromBottom(16),
               juce::Justification::centred);
}

void VaneEditor::resized()
{
    reconnectMtsButton.setBounds(
        getLocalBounds().withSizeKeepingCentre(160, 26).translated(0, 30));
}
