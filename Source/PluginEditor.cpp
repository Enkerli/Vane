#include "PluginEditor.h"

VaneEditor::VaneEditor(VaneProcessor& p)
    : AudioProcessorEditor(p), vaneProcessor(p)
{
    setSize(480, 320);
}

void VaneEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff111118));

    g.setColour(juce::Colour(0xffb0b8d0));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(22.0f)));
    g.drawText("Vane", getLocalBounds().removeFromTop(60),
               juce::Justification::centred);

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
    g.setColour(juce::Colour(0xff606878));
    g.drawText(juce::String::fromUTF8("MPE  \xc2\xb7  MTS-ESP  \xc2\xb7  CC modulation"),
               getLocalBounds().removeFromBottom(40),
               juce::Justification::centred);
}

void VaneEditor::resized() {}
