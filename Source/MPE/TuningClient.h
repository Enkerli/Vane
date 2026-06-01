#pragma once

#include <juce_core/juce_core.h>
#include <array>

#if VANE_HAS_MTS
  #include "libMTSClient.h"
#endif

// Wraps the ODDSound MTS-ESP client API.
// When no MTS master is connected, falls back to equal temperament (A4 = 440 Hz).
// The type name avoids collision with the MTSClient opaque type in libMTSClient.h.
//
// ── How MTS-ESP works on macOS ────────────────────────────────────────────────
// libMTSClient.cpp does NOT link against any MTS library at build time.  At
// runtime it calls dlopen("/Library/Application Support/MTS-ESP/libMTS.dylib"),
// which is installed by the MTS master plugin (e.g. ODDSound MTS-ESP Master).
// The dylib exposes function pointers (GetTuning, ShouldFilterNote, …) that
// client plugins call.  The tuning data is shared via a memory-mapped region
// that libMTS.dylib manages.
//
// Consequence for plugin formats:
//   Plain AU — runs in the host's process, no sandbox, dlopen succeeds. ✓
//   AUv3     — runs as a sandboxed App Extension.  The sandbox blocks both the
//              dlopen of the MTS dylib and the cross-process shared-memory IPC
//              that libMTS.dylib uses internally.  MTS-ESP silently has no master
//              in AUv3 regardless of what is running on the system.  The plain AU
//              is the correct choice for Logic Pro on macOS.
//   VST3     — no sandbox, dlopen succeeds (same as plain AU). ✓
//
// ── noteToHz and MTS-filtered notes ──────────────────────────────────────────
// MTS_ShouldFilterNote() lets the master mark specific notes as "do not play".
// noteToHz() returns 0.0f for filtered notes.  Callers MUST check for 0.0f
// and suppress the voice — failing to do so drives the oscillator to DC (0 Hz),
// which poisons the SVFilter integrator states (s1, s2) permanently, silencing
// the voice for the rest of the session even after the MTS master is changed.
//
// ── reconnect() ──────────────────────────────────────────────────────────────
// Call from the message thread if Vane loses its MTS connection after a tuning
// preset change (the master briefly deregisters and re-registers, which can leave
// clients without a master pointer).  libMTSClient uses atomics internally so
// this is safe to call while audio is running, but only from the message thread.
// Tuning source precedence:
//   FollowMTS  — use MTS-ESP master when connected (falls back to ET if not).
//   Internal   — use Vane's own built-in tuning table (ignores MTS master).
//   Bypass     — always use 12-EDO (tuning disabled).
enum class TuningSource { FollowMTS, Internal, Bypass };

class TuningClient {
public:
    TuningClient();
    ~TuningClient();

    // Frequency in Hz for a MIDI note on a given channel (1-based).
    // Holes (MTS-filtered or internal NaN) snap to the nearest non-hole pitch
    // instead of silencing — correct for wind controllers where portamento
    // can land anywhere.  Returns 0.0f only if no valid pitch is found ±12.
    float noteToHz(int midiNote, int midiChannel) const;

    // Raw Hz for a note without hole/filter checking — used internally for
    // quantisation searches and deviation display.
    float rawHz(int midiNote, int midiChannel) const;

    // Whether an MTS-ESP master is currently connected.
    bool hasMaster() const;

    // Re-register with the MTS-ESP daemon.
    void reconnect();

    // Equal-temperament frequency for a MIDI note (A4 = 440 Hz reference).
    static float equalTemperamentHz(int midiNote);

    // ── Tuning-surface engine hooks ───────────────────────────────────────────

    // Active tuning name for the UI ("Werckmeister III", "Just Intonation", …).
    // Returns the MTS master's scale name when connected + following; the
    // internal tuning name otherwise; empty when bypassed.
    juce::String tuningName() const;

    // Cents deviation from 12-ET for the given MIDI note under the current
    // tuning (0 = equal temperament). Used to populate the deviation-profile map.
    float deviationCents(int midiNote, int midiChannel) const;

    // True if this note is silenced by the current tuning (MTS filter, or an
    // internal-tuning hole).  The stage map marks these explicitly.
    bool isHole(int midiNote, int midiChannel) const;

    // ── Source / internal-tuning control ─────────────────────────────────────
    void setTuningSource  (TuningSource s) { source = s; }
    TuningSource getTuningSource() const   { return source; }

    // Set the active internal tuning by id matching the JS TUN keys
    // ("edo12", "just", "pyth", "meanqc", "werck3", "diat7", "edo19", "bp").
    void setInternalTuning (const juce::String& id);
    juce::String getInternalTuningId() const { return internalId; }

    // Per-degree data for the active internal tuning (filled by setInternalTuning,
    // read by noteToHz and deviationCents).  128 entries (one per MIDI note);
    // a NaN value marks a hole.
    std::array<float, 128> internalCentsTable {};   // cents from C0 (A4=6900¢)

private:
    TuningSource source   { TuningSource::FollowMTS };
    juce::String internalId { "just" };

#if VANE_HAS_MTS
    ::MTSClient* mtsClient = nullptr;
#endif
};
