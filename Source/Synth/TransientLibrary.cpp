#include "TransientLibrary.h"
#include <juce_audio_formats/juce_audio_formats.h>

TransientLibrary::TransientLibrary()
{
    // Index 0 is always "None" — no sample plays.
    entryNames.add("None");
    entries.emplace_back();   // empty sentinel; getSample(0) returns nullptr

    // Factory samples go here once .wav files are bundled into the
    // VaneTransients binary-data target (see CMakeLists.txt).
    //
    // Pattern for each sample:
    //   #include <BinaryDataTransients.h>
    //   loadFromMemory(BinaryDataTrn::sample_wav,
    //                  BinaryDataTrn::sample_wavSize,
    //                  "Display Name", 440.0f);
    //
    // The nativeHz argument should match the fundamental of the recorded
    // attack.  SynthVoice scales playback speed so the transient pitch-
    // tracks the current note — important for samples with clear tonality
    // (reed attacks, bowed string attacks) less so for pure noise (breath
    // chiffs, key clicks).
}

const TransientSample* TransientLibrary::getSample(int index) const noexcept
{
    if (index <= 0 || index >= static_cast<int>(entries.size()))
        return nullptr;
    return &entries[static_cast<size_t>(index)];
}

void TransientLibrary::loadFromMemory(const char* data, int size,
                                       const char* name, float nativeHz)
{
    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();

    auto stream  = std::make_unique<juce::MemoryInputStream>(data, (size_t) size, false);
    auto* reader = fmt.createReaderFor(std::move(stream));
    if (reader == nullptr) return;

    const int numSamples = static_cast<int>(reader->lengthInSamples);
    if (numSamples < 2) { delete reader; return; }

    TransientSample ts;
    ts.name       = name;
    ts.sampleRate = reader->sampleRate;
    ts.nativeHz   = nativeHz;
    ts.buffer.setSize(1, numSamples, false, true, false);

    // Read all channels into a temp buffer and mix down to mono.
    juce::AudioBuffer<float> tmp(static_cast<int>(reader->numChannels), numSamples);
    reader->read(&tmp, 0, numSamples, 0, true, true);
    delete reader;

    auto*       dest  = ts.buffer.getWritePointer(0);
    const float scale = 1.0f / static_cast<float>(tmp.getNumChannels());
    for (int c = 0; c < tmp.getNumChannels(); ++c) {
        const auto* src = tmp.getReadPointer(c);
        if (c == 0)
            for (int i = 0; i < numSamples; ++i) dest[i]  = src[i] * scale;
        else
            for (int i = 0; i < numSamples; ++i) dest[i] += src[i] * scale;
    }

    entryNames.add(name);
    entries.push_back(std::move(ts));
}
