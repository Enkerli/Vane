#pragma once
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace minisax
{

// Minimal mono 16-bit PCM WAV writer.  16-bit PCM keeps the files readable by
// every analysis tool (the Python analyzer's integer path included).
inline void writeWav16(const std::string& path, const std::vector<float>& samples, int sampleRate)
{
    std::ofstream out(path, std::ios::binary);
    if (!out)
        throw std::runtime_error("cannot open WAV for writing: " + path);

    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2);
    const uint32_t byteRate = static_cast<uint32_t>(sampleRate) * 2;

    auto write16 = [&out](uint16_t v) {
        char b[2] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF)};
        out.write(b, 2);
    };
    auto write32 = [&out](uint32_t v) {
        char b[4] = {static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                     static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF)};
        out.write(b, 4);
    };

    out.write("RIFF", 4);
    write32(36 + dataBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    write32(16);        // PCM fmt chunk size
    write16(1);         // PCM
    write16(1);         // mono
    write32(static_cast<uint32_t>(sampleRate));
    write32(byteRate);
    write16(2);         // block align
    write16(16);        // bits per sample
    out.write("data", 4);
    write32(dataBytes);

    for (float s : samples)
    {
        const float clamped = std::clamp(s, -1.0f, 1.0f);
        const auto v = static_cast<int16_t>(clamped * 32767.0f);
        write16(static_cast<uint16_t>(v));
    }

    if (!out)
        throw std::runtime_error("failed while writing WAV: " + path);
}

} // namespace minisax
