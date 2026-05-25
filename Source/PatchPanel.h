#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

class VaneProcessor;

// Compact patch parameter panel — exposes all APVTS synthesis parameters
// in a scrolling list so they can be dialled in and saved as named presets
// without opening a DAW.
//
// Sits in the same metersArea rectangle as ModMatrixEditor, toggled by a
// "Patch" button in the controls row.  Only one of meters / matrix / patch
// is visible at a time.  All controls are wired via APVTS Attachments so
// changes are live and captured by the preset system automatically.
class PatchPanel : public juce::Component {
public:
    explicit PatchPanel(VaneProcessor&);
    ~PatchPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct ContentComponent;            // defined in PatchPanel.cpp
    VaneProcessor& processor;
    std::unique_ptr<ContentComponent> content;  // declared before viewport
    juce::Viewport viewport;                    // destroyed before content ✓

    static constexpr int kHdrH = 16;   // eyebrow header height

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PatchPanel)
};
