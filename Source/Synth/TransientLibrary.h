#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// A single factory transient — always mono, float.
// nativeHz is the pitch at which the sample was recorded; SynthVoice adjusts
// playback speed so the transient tracks the current note's pitch.
struct TransientSample {
    juce::String             name;
    juce::AudioBuffer<float> buffer;    // 1 channel, float
    double                   sampleRate = 44100.0;
    float                    nativeHz   = 440.0f;   // default: A4
};

// Owns the collection of bundled transient samples.
// Constructed once in VaneProcessor; voices hold a const pointer to it.
//
// Thread safety: construction happens on the message thread before the audio
// thread starts, and the library is never mutated after that — all access from
// SynthVoice is read-only via getSample().
class TransientLibrary {
public:
    TransientLibrary();

    // Total number of entries including the "None" sentinel at index 0.
    int numEntries() const noexcept { return static_cast<int>(entries.size()); }

    // Names for AudioParameterChoice.  Index 0 is always "None".
    const juce::StringArray& names() const noexcept { return entryNames; }

    // Returns nullptr for index 0 ("None") or any out-of-range index.
    const TransientSample* getSample(int index) const noexcept;

    // Load a factory sample from raw WAV bytes and add it to the library.
    // Called during construction from BinaryData once WAV files are bundled.
    void loadFromMemory(const char* data, int size,
                        const char* name, float nativeHz = 440.0f);

private:
    // entries[0] is an empty sentinel corresponding to "None".
    std::vector<TransientSample> entries;
    juce::StringArray            entryNames;
};
