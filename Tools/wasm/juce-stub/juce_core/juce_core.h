// Minimal juce_core stub so Vane's DSP headers compile to WASM WITHOUT pulling in
// any JUCE module. The standalone Web Audio synth reuses Oscillator/SVFilter
// verbatim; those headers only touch juce::jlimit + juce::MathConstants, and
// Wavetable.h only *declares* methods taking juce::File (bodies live in
// Wavetable.cpp, which the WASM build does not compile). Keep this in sync if the
// DSP headers start using more of juce_core.
#pragma once
#include <algorithm>
#include <string>

namespace juce {

template <typename T>
inline T jlimit (T lowerLimit, T upperLimit, T valueToConstrain) noexcept {
    return valueToConstrain < lowerLimit ? lowerLimit
         : (valueToConstrain > upperLimit ? upperLimit : valueToConstrain);
}

template <typename T>
struct MathConstants {
    static constexpr T pi      = static_cast<T> (3.141592653589793238L);
    static constexpr T twoPi   = static_cast<T> (6.283185307179586477L);
    static constexpr T halfPi  = static_cast<T> (1.570796326794896619L);
    static constexpr T euler   = static_cast<T> (2.718281828459045235L);
    static constexpr T sqrt2   = static_cast<T> (1.414213562373095049L);
};

// Only referenced in Wavetable.h method *declarations* (loadFromWav/saveToWav),
// whose bodies are not compiled into the WASM voice. A forward declaration is
// enough for the header to parse.
class File;

// Minimal String — just enough for TuningClient (id storage, == comparisons
// against literals, construction from a literal in tuningName()). Wraps
// std::string; no JUCE unicode/refcounting semantics needed for those uses.
class String {
public:
    String() = default;
    String (const char* s) : value (s ? s : "") {}
    String (const std::string& s) : value (s) {}
    bool operator== (const char* other) const { return value == (other ? other : ""); }
    bool operator!= (const char* other) const { return ! (*this == other); }
    bool operator== (const String& other) const { return value == other.value; }
    const char* toRawUTF8() const { return value.c_str(); }
    bool isEmpty() const { return value.empty(); }
private:
    std::string value;
};

} // namespace juce
