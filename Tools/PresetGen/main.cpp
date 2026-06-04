// Preset generator: instantiates VaneProcessor, configures each test preset, and
// writes it via PresetManager → ~/Library/Vane/Presets/.
// These back the playing-test plan (Vane Design/playing_tests.md).  Run:
//   cmake --build build-mac --target VanePresetGen && ./…/VanePresetGen
#include "../../Source/PluginProcessor.h"
#include <cstdio>
#include <functional>

static void setRaw (VaneProcessor& p, const juce::String& id, float raw) {
    if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter (id)))
        prm->setValueNotifyingHost (prm->convertTo0to1 (raw));
}
static void setCh (VaneProcessor& p, const juce::String& id, int idx) {
    if (auto* prm = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (id)))
        *prm = idx;
}
// Configure a mod slot: source/dest/amount choices + curve + scale source.
static void route (VaneProcessor& p, int n, int src, int dst, float amt, int curve = 0, int scale = 0) {
    const juce::String b = "modSlot" + juce::String (n);
    setCh  (p, b + "_src",   src);
    setCh  (p, b + "_dst",   dst);
    setRaw (p, b + "_amt",   amt);
    setCh  (p, b + "_curve", curve);
    setCh  (p, b + "_scale", scale);
}

static void make (const juce::String& name, std::function<void(VaneProcessor&)> cfg) {
    VaneProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    cfg (proc);
    proc.presetManager.savePreset (name);
    // savePreset is silent on failure, so confirm the file landed on disk.
    auto f = proc.presetManager.getPresetsDirectory().getChildFile (name + ".vanepreset");
    std::printf ("  %s  %s\n", f.existsAsFile() ? "wrote " : "FAILED",
                 f.getFullPathName().toRawUTF8());
}

int main() {
    juce::ScopedJuceInitialiser_GUI juceInit;   // PresetManager touches File locations
    std::printf ("Generating Vane test presets…\n");

    // Source choices: 1=Breath 2=Expression 3=Pressure 6=Velocity 15=Keytrack
    // Dest choices:   0=VCA 1=Cutoff 2=Reso 4=Morph 8=Inharm 9=Sync 10=Transient 11=UniDetune
    // Voice mode: unisonVoices idx → {1,2,3,4,6}; unisonMode 0=Detune 1=Chord.

    make ("Test - Breath Lead", [](VaneProcessor& p){
        setRaw (p, "monoMode", 1.0f); setRaw (p, "velocityMix", 0.0f);
        setRaw (p, "filterCutoff", 1400.0f);
        setRaw (p, "glideTime", 80.0f); setCh (p, "glideCurve", 2);           // RC glide
        setCh  (p, "transientChoice", 2);  setRaw (p, "transientGain", 1.0f);  // Recorder Chiff
        setRaw (p, "transientDynamics", 0.8f); setCh (p, "transientTrigger", 1); // non-legato
        setRaw (p, "transientFilter", 1.0f);
    });

    make ("Test - Rotating Stack", [](VaneProcessor& p){
        setRaw (p, "monoMode", 1.0f); setRaw (p, "velocityMix", 0.0f);
        setCh  (p, "unisonVoices", 3);                                         // 4 voices
        setCh  (p, "unisonMode", 1);  setRaw (p, "unisonWidth", 0.6f);          // Chord
        p.setChordSeqs ("3,7;7,12,10;5,9");                                    // lengths 2,3,2 → rotates
        setRaw (p, "glideTime", 40.0f);
    });

    make ("Test - Stereo Unison Pad", [](VaneProcessor& p){
        setRaw (p, "monoMode", 1.0f); setRaw (p, "velocityMix", 0.0f);
        setCh  (p, "unisonVoices", 4); setCh (p, "unisonMode", 0);             // 6 voices, Detune
        setRaw (p, "unisonDetune", 16.0f); setRaw (p, "unisonWidth", 1.0f);
        route  (p, 20, 1, 4, 1.0f);                                            // Breath → Morph
    });

    make ("Test - Inharmonic Attack", [](VaneProcessor& p){
        setRaw (p, "monoMode", 1.0f); setRaw (p, "velocityMix", 0.0f);
        setCh  (p, "transientChoice", 6);  setRaw (p, "transientGain", 1.2f);  // Tongue (inharmonic)
        setRaw (p, "transientResonate", 0.5f); setRaw (p, "transientDamping", 0.5f);
        setRaw (p, "transientFilter", 1.0f);  setRaw (p, "transientDynamics", 0.7f);
        setRaw (p, "transientMorph", 12.0f);   setCh (p, "transientTrigger", 0); // Always
        setRaw (p, "glideTime", 40.0f);
    });

    make ("Test - Keytrack Filter", [](VaneProcessor& p){
        setRaw (p, "monoMode", 1.0f); setRaw (p, "velocityMix", 0.0f);
        setRaw (p, "filterCutoff", 900.0f);
        route  (p, 20, 15, 1, 0.8f);                                          // Keytrack → Cutoff
        p.setSlotCurveAnchors (20, { {0.5f, 0.22f} });                         // gentle low end
        route  (p, 21, 1, 1, 0.6f, 0, 15);                                    // Breath → Cutoff × Keytrack
    });

    make ("Test - Microtonal Glide", [](VaneProcessor& p){
        setRaw (p, "monoMode", 1.0f); setRaw (p, "velocityMix", 0.0f);
        p.apvts.state.setProperty ("tuningSource", "internal", nullptr);
        p.apvts.state.setProperty ("internalTuningId", "just", nullptr);
        setRaw (p, "glideTime", 200.0f); setCh (p, "glideCurve", 3);           // Bézier glide
        p.setGlideAnchors ({ {0.5f, 0.18f} });                                // ease-in trajectory
    });

    make ("Test - Morph Sweep", [](VaneProcessor& p){
        setRaw (p, "monoMode", 1.0f); setRaw (p, "velocityMix", 0.0f);
        route  (p, 20, 1, 4, 1.0f);                                            // Breath → Morph
        setRaw (p, "oscSync", 1.5f);                                           // mild formant
        route  (p, 21, 1, 8, 0.4f);                                            // Breath → Inharm
    });

    std::printf ("Done → ~/Library/Vane/Presets/\n");
    return 0;
}
