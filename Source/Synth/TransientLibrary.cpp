#include "TransientLibrary.h"
#include <BinaryDataTransients.h>
#include <cstring>
#include <cstdint>

// Minimal canonical-PCM .wav decoder for the BUNDLED transient assets (all mono
// Int16 RIFF/WAVE). Deliberately NOT juce::AudioFormatManager: the very first
// decode through JUCE's format machinery faults in a ~460ms one-time cost, and
// TransientLibrary is a by-value member of VaneProcessor — so that hit landed on
// the first plugin instantiation of every process (every auval run; first
// instance of a DAW session). The default patch decodes no wavetable, so once
// the transients bypass AudioFormatManager the AU opens with ~zero decode cost.
// Supports PCM int 8/16/24/32 and IEEE float 32, any channel count (mixed to
// mono). Returns false (sample skipped) on anything it doesn't recognise.
namespace {
    inline uint32_t rdU32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
    inline uint16_t rdU16 (const uint8_t* p) { return (uint16_t) ((uint16_t) p[0] | ((uint16_t) p[1] << 8)); }

    struct DecodedWav { juce::AudioBuffer<float> mono; double sampleRate = 44100.0; };

    bool decodePcmWavToMono (const void* data, int size, DecodedWav& out)
    {
        const auto* p = static_cast<const uint8_t*> (data);
        if (data == nullptr || size < 44) return false;
        if (std::memcmp (p, "RIFF", 4) != 0 || std::memcmp (p + 8, "WAVE", 4) != 0) return false;

        uint16_t fmt = 0, channels = 0, bits = 0;
        uint32_t sampleRate = 0;
        const uint8_t* pcm = nullptr; uint32_t pcmBytes = 0;

        // Walk the chunk list (fmt/data can be preceded by fact/LIST/etc.).
        size_t off = 12;
        while (off + 8 <= (size_t) size) {
            const uint8_t* ch = p + off;
            const uint32_t clen = rdU32 (ch + 4);
            const uint8_t* body = ch + 8;
            if (std::memcmp (ch, "fmt ", 4) == 0 && clen >= 16 && off + 8 + 16 <= (size_t) size) {
                fmt = rdU16 (body); channels = rdU16 (body + 2);
                sampleRate = rdU32 (body + 4); bits = rdU16 (body + 14);
                if (fmt == 0xFFFE && clen >= 40 && off + 8 + 26 <= (size_t) size)
                    fmt = rdU16 (body + 24);   // WAVE_FORMAT_EXTENSIBLE → real subformat tag
            } else if (std::memcmp (ch, "data", 4) == 0) {
                pcm = body;
                pcmBytes = (uint32_t) juce::jmin ((uint64_t) clen, (uint64_t) (size - (int) (off + 8)));
            }
            off += 8 + clen + (clen & 1u);   // chunks are word-aligned
        }

        if (pcm == nullptr || channels == 0 || sampleRate == 0) return false;
        const int bytesPerSample = bits / 8;
        if (bytesPerSample <= 0) return false;
        const bool isFloat = (fmt == 3);
        const bool isPcmInt = (fmt == 1);
        if (! isFloat && ! isPcmInt) return false;

        const int frameBytes = bytesPerSample * channels;
        const int numFrames = (int) (pcmBytes / (uint32_t) frameBytes);
        if (numFrames < 1) return false;

        out.sampleRate = (double) sampleRate;
        out.mono.setSize (1, numFrames, false, true, false);
        float* dst = out.mono.getWritePointer (0);
        const float chScale = 1.0f / (float) channels;

        auto readOne = [&] (const uint8_t* s) -> float {
            if (isFloat) { float f; std::memcpy (&f, s, sizeof f); return f; }
            if (bits == 16) return (float) (int16_t) rdU16 (s) * (1.0f / 32768.0f);
            if (bits == 24) { int32_t v = (int32_t) ((uint32_t) s[0] | ((uint32_t) s[1] << 8) | ((uint32_t) s[2] << 16));
                              if (v & 0x800000) v |= (int32_t) 0xFF000000; return (float) v * (1.0f / 8388608.0f); }
            if (bits == 32) return (float) (int32_t) rdU32 (s) * (1.0f / 2147483648.0f);
            if (bits == 8)  return ((float) s[0] - 128.0f) * (1.0f / 128.0f);   // 8-bit WAV is unsigned
            return 0.0f;
        };

        for (int f = 0; f < numFrames; ++f) {
            const uint8_t* frame = pcm + (size_t) f * (size_t) frameBytes;
            float acc = 0.0f;
            for (int c = 0; c < channels; ++c) acc += readOne (frame + c * bytesPerSample);
            dst[f] = acc * chScale;
        }
        return true;
    }
}

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
    loadFromMemory(BinaryDataTrn::gen_ping_wav,    BinaryDataTrn::gen_ping_wavSize,    "Metallic Ping", 440.0f, false);
    loadFromMemory(BinaryDataTrn::gen_squeak_wav,  BinaryDataTrn::gen_squeak_wavSize,  "Finger Squeak", 440.0f, false);
    loadFromMemory(BinaryDataTrn::gen_scratch_wav, BinaryDataTrn::gen_scratch_wavSize, "Bow Scratch",   440.0f, false);
    loadFromMemory(BinaryDataTrn::gen_tick_wav,    BinaryDataTrn::gen_tick_wavSize,    "Snare Tick",    440.0f, false);
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
    DecodedWav dec;
    if (! decodePcmWavToMono(data, size, dec)) return;   // unrecognised → skip this sample
    if (dec.mono.getNumSamples() < 2) return;

    TransientSample ts;
    ts.name       = name;
    ts.sampleRate = dec.sampleRate;
    ts.nativeHz   = nativeHz;
    ts.pitched    = pitched;
    ts.buffer     = std::move(dec.mono);   // already mono float, mixed on decode

    entryNames.add(name);
    entries.push_back(std::move(ts));
}
