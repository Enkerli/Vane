#include "TransientLibrary.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <BinaryDataTransients.h>

TransientLibrary::TransientLibrary()
{
    // Index 0 is always "None" — no sample plays.
    entryNames.add("None");
    entries.emplace_back();   // empty sentinel; getSample(0) returns nullptr

    // All factory samples are CC0-1.0 (no attribution required).
    // Sources:
    //   Flute, Clarinet, Cello, Trumpet: Versilian Studios VSCO-2-CE
    //     github.com/sgossner/VSCO-2-CE — CC0-1.0
    //   Recorder: Versilian Community Sample Library (VCSL)
    //     github.com/sgossner/VCSL — CC0-1.0
    //
    // Samples are trimmed to the attack onset only (280–380 ms), mixed to
    // mono, and normalised.  nativeHz is the pitch of the recorded note;
    // SynthVoice::noteStarted() pitch-tracks playback so the transient
    // character follows the current note.

    // Flute staccato A4 (440 Hz) — breath chiff onset, concert flute
    loadFromMemory(BinaryDataTrn::flute_stac_A4_wav,
                   BinaryDataTrn::flute_stac_A4_wavSize,
                   "Flute Chiff", 440.0f);

    // Baroque alto recorder staccato D4 (293.66 Hz) — stronger chiff character
    loadFromMemory(BinaryDataTrn::recorder_stac_D4_wav,
                   BinaryDataTrn::recorder_stac_D4_wavSize,
                   "Recorder Chiff", 293.66f);

    // Clarinet staccato D4 (293.66 Hz) — reed articulation onset
    loadFromMemory(BinaryDataTrn::clarinet_stac_D4_wav,
                   BinaryDataTrn::clarinet_stac_D4_wavSize,
                   "Clarinet Attack", 293.66f);

    // Cello section spiccato C3 (130.81 Hz) — bow-on-string attack onset
    loadFromMemory(BinaryDataTrn::cello_spic_C3_wav,
                   BinaryDataTrn::cello_spic_C3_wavSize,
                   "Cello Spiccato", 130.81f);

    // Trumpet staccato A4 (440 Hz) — lip buzz / embouchure onset
    loadFromMemory(BinaryDataTrn::trumpet_stac_A4_wav,
                   BinaryDataTrn::trumpet_stac_A4_wavSize,
                   "Trumpet Attack", 440.0f);

    // ── Inharmonic (noise-based) transients ─────────────────────────────────────
    // Synthesised CC0 (Assets/transients/gen_inharmonic.py).  pitched=false: these
    // play at a fixed speed regardless of note, so the attack is identical across
    // the keyboard and there is no harmonic series to smear under transposition.
    loadFromMemory(BinaryDataTrn::gen_tongue_wav, BinaryDataTrn::gen_tongue_wavSize, "Tongue",     440.0f, false);
    loadFromMemory(BinaryDataTrn::gen_click_wav,  BinaryDataTrn::gen_click_wavSize,  "Key Click",  440.0f, false);
    loadFromMemory(BinaryDataTrn::gen_chiff_wav,  BinaryDataTrn::gen_chiff_wavSize,  "Air Chiff",  440.0f, false);
    loadFromMemory(BinaryDataTrn::gen_knock_wav,  BinaryDataTrn::gen_knock_wavSize,  "Wood Knock", 440.0f, false);
    loadFromMemory(BinaryDataTrn::gen_pick_wav,   BinaryDataTrn::gen_pick_wavSize,   "Pick Noise", 440.0f, false);
    loadFromMemory(BinaryDataTrn::gen_buzz_wav,   BinaryDataTrn::gen_buzz_wavSize,   "Reed Buzz",  440.0f, false);
}

const TransientSample* TransientLibrary::getSample(int index) const noexcept
{
    if (index <= 0 || index >= static_cast<int>(entries.size()))
        return nullptr;
    return &entries[static_cast<size_t>(index)];
}

void TransientLibrary::loadFromMemory(const char* data, int size,
                                       const char* name, float nativeHz, bool pitched)
{
    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (
        fmt.createReaderFor(
            std::make_unique<juce::MemoryInputStream>(data, (size_t) size, false)));
    if (reader == nullptr) return;

    const int numSamples = static_cast<int>(reader->lengthInSamples);
    if (numSamples < 2) return;

    TransientSample ts;
    ts.name       = name;
    ts.sampleRate = reader->sampleRate;
    ts.nativeHz   = nativeHz;
    ts.pitched    = pitched;
    ts.buffer.setSize(1, numSamples, false, true, false);

    // Read all channels into a temp buffer and mix down to mono.
    juce::AudioBuffer<float> tmp(static_cast<int>(reader->numChannels), numSamples);
    reader->read(&tmp, 0, numSamples, 0, true, true);

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
