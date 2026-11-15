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

// Set a list of parameters by id, in their own units (a choice by its index).
static void setAll (VaneProcessor& p, std::initializer_list<std::pair<const char*, float>> values) {
    for (auto& v : values) setRaw (p, v.first, v.second);
}
// WindCore as the sound: the reed model on, one voice.
static void windOn (VaneProcessor& p) {
    setRaw (p, "waveguideOn", 1.0f); setCh (p, "waveguideModel", 1); setRaw (p, "monoMode", 1.0f);
}

static juce::String onlyPrefix;   // VanePresetGen <prefix>: write only the presets whose name starts with it

static void make (const juce::String& name, std::function<void(VaneProcessor&)> cfg) {
    if (onlyPrefix.isNotEmpty() && ! name.startsWith (onlyPrefix)) return;
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

int main (int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;   // PresetManager touches File locations
    if (argc > 1) onlyPrefix = argv[1];
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

    // ── WindCore ─────────────────────────────────────────────────────────────────
    // Starting points for the reed model (docs/windcore.md). The first three are patches found by playing:
    // an attack sample through the pitch resonator at low gain is what brings WindCore closest to an acoustic
    // instrument; they sound best through a room or a convolution reverb after Vane.
    // Source choices also: 16=Voice Vowel 17=Voice Level 18=Voice Brightness 19=Input 1 20=Input 2
    // Dest choices also:   21=Wind Mouth 22=Wind Mouth Strength 25=Wind Air 26=Wind Bell 27=Wind Vibrato

    make ("WindCore - Attack", [](VaneProcessor& p){
        windOn (p);
        setAll (p, {
        { "filterCutoff", 20000.0f }, { "filterMode", 0.0f }, { "filterRes", 0.0f }, { "glideCurve", 2.0f }, { "glideMode", 1.0f }, { "glideTime", 0.0f },
        { "monoMode", 1.0f }, { "outputLevel", 0.7f }, { "synthBreathAttack", 1.6991f }, { "synthBreathDecay", 1.542f }, { "synthBreathMode", 1.0f },
        { "synthBreathRelease", 180.0f }, { "synthBreathSustain", 0.2991f }, { "transientChoice", 1.0f }, { "transientDamping", 0.6871f },
        { "transientDecay", 200.0f }, { "transientDynamics", 0.75f }, { "transientFilter", 1.0f }, { "transientGain", 0.1102f },
        { "transientMorph", 21.845f }, { "transientResonate", 0.665f }, { "transientTrigger", 1.0f }, { "transientVariation", 0.9441f },
        { "velocityMix", 0.0f }, { "windAir", 0.2576f }, { "windBaffle", 0.2888f }, { "windBell", 0.7366f }, { "windBite", 0.2869f },
        { "windBody", 0.7906f }, { "windBodySize", 0.7054f }, { "windChamber", 0.4481f }, { "windCone", 0.5667f }, { "windDrift", 0.4203f },
        { "windFingers", 0.8354f }, { "windHoles", 0.7062f }, { "windMouth", 0.5748f }, { "windMouthDepth", 0.8595f }, { "windReed", 0.7714f },
        { "windShimmer", 0.244f }, { "windSlur", 0.0438f }, { "windTongue", 0.8436f }, { "windVibRate", 0.0f }, { "windVibrato", 0.1679f },
        { "windWobble", 0.1103f }
        });
        route (p, 0, 1, 10, 1.0f);                                             // Breath → Transient
    });

    make ("WindCore - Chiffy", [](VaneProcessor& p){
        windOn (p);
        setAll (p, {
        { "filterCutoff", 20000.0f }, { "filterMode", 0.0f }, { "filterRes", 0.0f }, { "glideCurve", 2.0f }, { "glideMode", 1.0f }, { "glideTime", 0.0f },
        { "monoMode", 1.0f }, { "outputLevel", 0.7f }, { "synthBreathAttack", 1.3994f }, { "synthBreathDecay", 1.5219f }, { "synthBreathMode", 1.0f },
        { "synthBreathRelease", 2.4162f }, { "synthBreathSustain", 0.1512f }, { "transientChoice", 1.0f }, { "transientDamping", 0.6871f },
        { "transientDecay", 200.0f }, { "transientDynamics", 0.75f }, { "transientFilter", 1.0f }, { "transientGain", 0.1102f },
        { "transientMorph", 21.845f }, { "transientResonate", 0.665f }, { "transientTrigger", 1.0f }, { "transientVariation", 0.9441f },
        { "velocityMix", 0.0f }, { "windAir", 0.1273f }, { "windBaffle", 0.2997f }, { "windBell", 0.5305f }, { "windBite", 0.8821f },
        { "windBody", 0.4123f }, { "windBodySize", 0.7054f }, { "windChamber", 0.1857f }, { "windChiff", 0.1057f }, { "windCone", 0.8524f },
        { "windDrift", 0.1553f }, { "windFingers", 0.8354f }, { "windHoles", 0.0904f }, { "windKeyNoise", 0.2105f }, { "windMouth", 0.1809f },
        { "windMouthDepth", 0.9629f }, { "windReed", 0.39f }, { "windShimmer", 0.1806f }, { "windSlur", 0.0438f }, { "windTongue", 0.8436f },
        { "windVibRate", 0.0f }, { "windVibrato", 0.1285f }, { "windWobble", 0.235f }
        });
        route (p, 0, 1, 10, 1.0f);                                             // Breath → Transient
    });

    // A voice on the audio input plays it (docs/voice-input.md, docs/daw-setup.md): consonants tongue the
    // reed, vowels shape the mouth, loudness is the breath. With no voice there is no sound.
    make ("WindCore - Voice", [](VaneProcessor& p){
        windOn (p);
        setAll (p, {
        { "filterCutoff", 20000.0f }, { "filterMode", 0.0f }, { "filterRes", 0.0f }, { "glideCurve", 2.0f }, { "glideMode", 1.0f }, { "glideTime", 0.0f },
        { "monoMode", 1.0f }, { "outputLevel", 0.7f }, { "synthBreathAttack", 1.6991f }, { "synthBreathDecay", 8.8273f }, { "synthBreathMode", 1.0f },
        { "synthBreathRelease", 179.9998f }, { "synthBreathSustain", 0.2991f }, { "transientChoice", 1.0f }, { "transientDamping", 0.6871f },
        { "transientDecay", 200.0f }, { "transientDynamics", 0.75f }, { "transientFilter", 1.0f }, { "transientGain", 0.1102f },
        { "transientMorph", 21.845f }, { "transientResonate", 0.665f }, { "transientTrigger", 1.0f }, { "transientVariation", 0.9441f },
        { "velocityMix", 0.0f }, { "voiceBreath", 1.0f }, { "voiceChiff", 0.3213f }, { "voiceTongue", 1.0f }, { "voiceUpper", 1.0f }, { "voiceVowel", 1.0f },
        { "windAir", 0.4995f }, { "windBaffle", 0.26f }, { "windBell", 0.7366f }, { "windBite", 0.2869f }, { "windBloom", 0.28f },
        { "windBody", 0.7906f }, { "windBodySize", 0.7054f }, { "windChamber", 0.4481f }, { "windChiff", 0.0406f }, { "windChiffColor", 0.2328f },
        { "windCone", 0.5667f }, { "windDrift", 0.4203f }, { "windFingers", 0.8354f }, { "windHoles", 0.7062f }, { "windMouth", 0.5748f },
        { "windMouthDepth", 1.0f }, { "windReed", 0.7714f }, { "windResponse", 0.58f }, { "windShimmer", 0.244f }, { "windSlur", 0.1634f },
        { "windTongue", 1.0f }, { "windVibRate", 0.0f }, { "windVibrato", 0.1679f }, { "windWobble", 0.1103f }
        });
        route (p, 0, 1, 1, 1.0f);                                              // Breath → Cutoff
        route (p, 1, 15, 1, -1.0f);                                            // Keytrack → Cutoff (down)
    });

    // The same, without the tongue: notes sound from MIDI and breath alone, and the voice only colours them.
    make ("WindCore - Voice Vowels", [](VaneProcessor& p){
        windOn (p);
        setAll (p, {
        { "filterCutoff", 20000.0f }, { "filterMode", 0.0f }, { "filterRes", 0.0f }, { "glideCurve", 2.0f }, { "glideMode", 1.0f }, { "glideTime", 0.0f },
        { "monoMode", 1.0f }, { "outputLevel", 0.7f }, { "synthBreathAttack", 1.6991f }, { "synthBreathDecay", 8.8273f }, { "synthBreathMode", 1.0f },
        { "synthBreathRelease", 179.9998f }, { "synthBreathSustain", 0.2991f }, { "transientChoice", 1.0f }, { "transientDamping", 0.6871f },
        { "transientDecay", 200.0f }, { "transientDynamics", 0.75f }, { "transientFilter", 1.0f }, { "transientGain", 0.1102f },
        { "transientMorph", 21.845f }, { "transientResonate", 0.665f }, { "transientTrigger", 1.0f }, { "transientVariation", 0.9441f },
        { "velocityMix", 0.0f }, { "voiceBreath", 1.0f }, { "voiceChiff", 0.3213f }, { "voiceTongue", 1.0f }, { "voiceUpper", 1.0f }, { "voiceVowel", 1.0f },
        { "windAir", 0.4995f }, { "windBaffle", 0.26f }, { "windBell", 0.7366f }, { "windBite", 0.2869f }, { "windBloom", 0.28f },
        { "windBody", 0.7906f }, { "windBodySize", 0.7054f }, { "windChamber", 0.4481f }, { "windChiff", 0.0406f }, { "windChiffColor", 0.2328f },
        { "windCone", 0.5667f }, { "windDrift", 0.4203f }, { "windFingers", 0.8354f }, { "windHoles", 0.7062f }, { "windMouth", 0.5748f },
        { "windMouthDepth", 1.0f }, { "windReed", 0.7714f }, { "windResponse", 0.58f }, { "windShimmer", 0.244f }, { "windSlur", 0.1634f },
        { "windTongue", 1.0f }, { "windVibRate", 0.0f }, { "windVibrato", 0.1679f }, { "windWobble", 0.1103f }
        });
        setRaw (p, "voiceTongue", 0.0f); setRaw (p, "voiceBreath", 0.0f); setRaw (p, "voiceChiff", 0.0f);
        route (p, 0, 1, 1, 1.0f);
        route (p, 1, 15, 1, -1.0f);
    });

    // From docs/windcore.md, "Moving the character while you play".
    make ("WindCore - Talking Horn", [](VaneProcessor& p){
        windOn (p);
        setRaw (p, "windAir", 0.1f); setRaw (p, "windMouth", 0.05f); setRaw (p, "windMouthDepth", 0.9f);
        route (p, 0, 1, 21, 0.9f);                                             // Breath → Wind Mouth
    });

    make ("WindCore - Sine to Brass", [](VaneProcessor& p){
        windOn (p);
        setRaw (p, "windAir", 0.1f); setRaw (p, "windResponse", 1.0f); setRaw (p, "windBloom", 0.9f);
        route (p, 0, 1, 26, 0.6f);                                             // Breath → Wind Bell
    });

    // A control voltage plays the mouth and the air: Input 1 (breath CV) and Input 2 (bite CV) on a DC-coupled input.
    make ("WindCore - CV Mouth and Air", [](VaneProcessor& p){
        windOn (p);
        setRaw (p, "windAir", 0.05f); setRaw (p, "windMouth", 0.0f); setRaw (p, "windMouthDepth", 0.6f);
        route (p, 0, 19, 25, 0.6f);                                            // Input 1 → Wind Air
        route (p, 1, 20, 21, 1.0f);                                            // Input 2 → Wind Mouth
    });

    std::printf ("Done → ~/Library/Vane/Presets/\n");
    return 0;
}
