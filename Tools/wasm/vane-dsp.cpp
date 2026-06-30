// vane-dsp.cpp — C-ABI Web Audio voice engine for the Vane *standalone* webapp.
//
// Reuses the plugin's real DSP — Oscillator (built-in band-limited waveforms) +
// SVFilter — compiled to WASM, wrapped in a small polyphonic voice pool with an
// amp ADSR and MPE expression. JUCE is stubbed (Tools/wasm/juce-stub) down to
// jlimit/MathConstants, so no JUCE module is linked. The morph wavetable
// (Oscillator::nextMorphed + Wavetable FFT loading) is a later stage; v1 uses
// the built-in waveforms, so Wavetable.cpp is NOT compiled here.
//
// The AudioWorklet drives this: vane_init(sr) once, then per MIDI event
// vane_note_on/off + vane_set_expr, and per render quantum vane_render(n) →
// read vane_buffer()[0..n) (mono) out of WASM memory.
#include "Synth/Oscillator.h"
#include "Synth/SVFilter.h"
#include "Synth/Wavetable.h"
#include <cmath>

// Oscillator::prepare() points its morph table at Wavetable::builtInDefault()
// (defined in the FFT-heavy Wavetable.cpp). v1 renders via Oscillator::next()
// (built-in analytic waveforms), which never reads the morph table, so a trivial
// empty default links it without compiling Wavetable.cpp. The real wavetable
// (offline-baked + loaded) arrives with the morph stage.
const Wavetable& Wavetable::builtInDefault() { static const Wavetable kEmpty; return kEmpty; }

namespace {

constexpr int kMaxVoices  = 16;
constexpr int kMaxBlock    = 2048;
double gSampleRate = 48000.0;

// Global (patch) params — ids match param-map.js on the JS side.
float pCutoff   = 4000.0f;   // base filter cutoff (Hz)
float pReso     = 0.2f;      // 0..1
float pAttack   = 0.005f;    // seconds
float pDecay    = 0.20f;
float pSustain  = 0.75f;     // 0..1
float pRelease  = 0.30f;
float pBendRange = 48.0f;    // MPE member-channel pitch-bend range (semitones)

struct Voice {
    Oscillator osc;
    SVFilter   filt;
    bool  active   = false;
    int   note     = -1;
    int   channel  = -1;
    float vel      = 0.0f;   // 0..1
    float bend     = 0.0f;   // -1..1  (MPE X)
    float slide    = 0.0f;   // 0..1   (MPE Y / CC74)
    float pressure = 0.0f;   // 0..1   (MPE Z)
    float env      = 0.0f;
    int   stage    = 0;      // 0 idle · 1 attack · 2 decay · 3 sustain · 4 release
};

Voice voices[kMaxVoices];
float renderBuf[kMaxBlock];

inline float midiToHz (float note) { return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f); }

} // namespace

extern "C" {

void vane_init (double sampleRate) {
    gSampleRate = sampleRate;
    for (auto& v : voices) {
        v.osc.prepare (sampleRate);
        v.osc.setWaveform (Oscillator::Waveform::Saw);
        v.filt.prepare (sampleRate);
        v.active = false; v.stage = 0; v.env = 0.0f;
    }
}

void vane_note_on (int note, int vel, int channel) {
    int idx = -1;
    for (int i = 0; i < kMaxVoices; ++i) if (! voices[i].active) { idx = i; break; }
    if (idx < 0) {                              // steal the quietest voice
        float lowest = 2.0f;
        for (int i = 0; i < kMaxVoices; ++i) if (voices[i].env < lowest) { lowest = voices[i].env; idx = i; }
        if (idx < 0) idx = 0;
    }
    Voice& v = voices[idx];
    v.active = true; v.note = note; v.channel = channel; v.vel = vel / 127.0f;
    v.bend = 0.0f; v.slide = 0.0f; v.pressure = 0.0f;
    v.osc.setFrequency (midiToHz ((float) note));
    v.stage = 1;                                // attack
}

void vane_note_off (int note, int channel) {
    for (auto& v : voices)
        if (v.active && v.note == note && (channel < 0 || v.channel == channel))
            v.stage = 4;                        // release
}

// Per-MPE-channel expression update (applies to the sounding voice on that channel).
void vane_set_expr (int channel, float bend, float slide, float pressure) {
    for (auto& v : voices)
        if (v.active && v.channel == channel) { v.bend = bend; v.slide = slide; v.pressure = pressure; }
}

void vane_set_param (int id, float val) {
    switch (id) {
        case 1: pCutoff    = val; break;
        case 2: pReso      = val; break;
        case 3: pAttack    = val; break;
        case 4: pDecay     = val; break;
        case 5: pSustain   = val; break;
        case 6: pRelease   = val; break;
        case 7: pBendRange = val; break;
        default: break;
    }
}

float* vane_buffer () { return renderBuf; }

void vane_render (int n) {
    if (n > kMaxBlock) n = kMaxBlock;
    for (int s = 0; s < n; ++s) renderBuf[s] = 0.0f;

    const float aInc = 1.0f / (pAttack  * (float) gSampleRate + 1.0f);
    const float dInc = 1.0f / (pDecay   * (float) gSampleRate + 1.0f);
    const float rInc = 1.0f / (pRelease * (float) gSampleRate + 1.0f);

    for (auto& v : voices) {
        if (! v.active) continue;
        // Block-rate param/expression application.
        v.osc.setFrequency (midiToHz ((float) v.note + v.bend * pBendRange));
        v.filt.setParameters (pCutoff * (1.0f + v.slide * 4.0f), pReso);   // MPE Y opens the filter
        const float ampScale = (0.3f + 0.7f * v.pressure) * v.vel * 0.2f;   // MPE Z → loudness

        for (int s = 0; s < n; ++s) {
            if      (v.stage == 1) { v.env += aInc; if (v.env >= 1.0f)     { v.env = 1.0f;     v.stage = 2; } }
            else if (v.stage == 2) { v.env -= dInc * (1.0f - pSustain); if (v.env <= pSustain) { v.env = pSustain; v.stage = 3; } }
            else if (v.stage == 4) { v.env -= rInc; if (v.env <= 0.0f)     { v.env = 0.0f; v.stage = 0; v.active = false; } }

            const float oscOut  = v.osc.next();
            const float filtOut = v.filt.process (oscOut, SVFilter::Mode::LP);
            renderBuf[s] += filtOut * v.env * ampScale;

            if (! v.active) break;
        }
    }
}

} // extern "C"
