#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Synth/SynthVoice.h"
#include "Modulation/ModMatrix.h"
#include "MPE/TuningClient.h"
#include "Preset/PresetManager.h"

class VaneProcessor : public juce::AudioProcessor {
public:
    VaneProcessor();
    ~VaneProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    bool supportsMPE()  const override { return true; }
    double getTailLengthSeconds() const override { return 0.5; }

    int  getNumPrograms() override                              { return 1; }
    int  getCurrentProgram() override                          { return 0; }
    void setCurrentProgram(int) override                       {}
    const juce::String getProgramName(int) override            { return {}; }
    void changeProgramName(int, const juce::String&) override  {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presetManager;
    ModMatrix    modMatrix;
    TuningClient tuning;

    // ── Editor meter values ────────────────────────────────────────────────────
    // Written on the audio thread (processBlock / MPE voice callbacks).
    // Read on the message thread (editor timer) — lock-free via relaxed atomics.
    // CC-based sources: updated every processBlock from the mod matrix.
    // MPE per-voice sources: written by the most recently active SynthVoice;
    // "last voice wins" is acceptable for visual meters.
    std::atomic<float> meterBreath    { 0.0f };   // CC2,  0..1
    std::atomic<float> meterExpr      { 0.0f };   // CC11, 0..1
    std::atomic<float> meterPressure  { 0.0f };   // MPE Z channel pressure, 0..1
    std::atomic<float> meterSlide     { 0.0f };   // CC74 / MPE Y timbre, 0..1
    std::atomic<float> meterPitchbend { 0.0f };   // MPE pitchbend, -1..1 (signed)
    std::atomic<float> meterVelocity  { 0.0f };   // last note-on velocity, 0..1 (held)

    // ── Per-voice MPE expression snapshot (for the per-note visualiser) ─────────
    // Filled each processBlock (audio thread) from each SynthVoice's live state;
    // read by the editor timer (message thread).  Index = MPESynthesiser voice.
    struct VoiceMeter {
        std::atomic<float> level    { 0.0f };   // 0 = off; >0 = sounding (fades on release)
        std::atomic<float> noteHz   { 0.0f };
        std::atomic<float> pressure { 0.0f };   // 0..1  (MPE Z)
        std::atomic<float> slide    { 0.5f };   // 0..1  (CC74 / Y)
        std::atomic<float> bend     { 0.0f };   // -1..1 (X)
        std::atomic<float> velocity { 0.0f };   // 0..1
    };
    static constexpr int kNumVoices = 15;
    std::array<VoiceMeter, kNumVoices> voiceMeters;

    // ── Aux-source MIDI-learn ──────────────────────────────────────────────────
    // The editor arms an aux index (ccLearnArm = 0..NumAux-1); the audio thread
    // captures the next incoming CC number into ccLearnResult and disarms.  The
    // editor polls the result, writes the aux{g}_cc param, and clears it.
    std::atomic<int> ccLearnArm    { -1 };
    std::atomic<int> ccLearnResult { -1 };

    void reconnectMTS()      { tuning.reconnect(); }
    bool mtsConnected() const { return tuning.hasMaster(); }

    // All-notes-off / reset — called from the UI "panic" bridge event.
    void panic() { synth.turnOffAllVoices (false); }

    // Hz of the most recently started note (for the UI ♪ note readout).
    float currentNoteHz() const { return lastNoteHz.load(); }

private:
    juce::MPESynthesiser synth;

    // SynthVoice* for each synth voice, cast once at construction so processBlock
    // can read per-voice meter state without a per-block dynamic_cast.
    std::vector<SynthVoice*> voicePtrs;

    // Shared across all voices: the Hz of the most recently started note.
    // New voices read this in noteStarted() to glide from the previous pitch
    // when glideTime > 0. Monophonic controllers only — MPE poly is unaffected
    // because players set glideTime = 0 for poly use.
    std::atomic<float> lastNoteHz { 0.0f };

    // Shared VCA level: the smoothed VCA amplitude of the most recently active voice.
    // New voices initialise their smoothedVCA here so legato note transitions start
    // at the current breath level rather than snapping in from zero.
    std::atomic<float> lastVCALevel { 0.0f };

    // Legato voice-kill counter. Incremented by each new legato note; old voices
    // compare their stored generation against this and die immediately at the top
    // of their next renderNextBlock, eliminating the two-voice phase-beating artifact.
    std::atomic<uint32_t> legatoGeneration { 0 };

    // Phase of the most recently active oscillator, published at end of every block.
    // New legato voices read this + advance one phaseInc to continue the waveform
    // without a phase discontinuity (audible as a click at the note boundary).
    std::atomic<float> lastOscPhase { 0.0f };

    // Phase of the inharmonicity FM modulator, published alongside lastOscPhase.
    // Handed to new legato voices so the √2-ratio modulator stays continuous across
    // the note boundary — without this it jumps, stepping the FM sidebands (a subtle
    // discontinuity, masked by the inharmonic spectrum but real).
    std::atomic<float> lastPmPhase { 0.0f };

    // Filter state for legato handoff.  Even with perfect oscillator phase sync,
    // switching to a new voice with different SVF integrator states (s1, s2) causes
    // a step-response transient (filter ringing) audible as a multi-sample click.
    // Publishing s1/s2 + the exact cutoff Hz lets the new voice prime its filter
    // coefficients and restore the state before its first process() call.
    std::atomic<float> lastFilterS1   { 0.0f };
    std::atomic<float> lastFilterS2   { 0.0f };
    std::atomic<float> lastCutoffHz   { 1200.0f };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Reconstruct generic mod slots from a pre-slot preset's legacy route params.
    void migrateLegacyRoutingToSlots(const juce::XmlElement& loadedState);

    // Cached raw parameter pointers — safe to read on the audio thread.
    // Initialised in the constructor after the APVTS is constructed.
    std::atomic<float>* pOutputLevel      = nullptr;

    // Macro source bindings — resolved each processBlock via setMacroSlot().
    // Breath: 0 = CC (pMacroBreathCC), 1 = Aftertouch, 2 = MPE Pressure.
    std::atomic<float>* pMacroBreathSrc   = nullptr;
    std::atomic<float>* pMacroBreathCC    = nullptr;
    // Expression: 0 = CC (pMacroExprCC), 1 = Aftertouch.
    std::atomic<float>* pMacroExprSrc     = nullptr;
    std::atomic<float>* pMacroExprCC      = nullptr;

    // Aux global-source CC bindings (controller profiles) — cached pointers,
    // pushed to the ModMatrix each processBlock.
    std::array<std::atomic<float>*, ModSlots::NumAux> pAuxCC {};

    // Per-block smoothed output gain to avoid clicks during automation.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VaneProcessor)
};
