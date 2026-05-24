#pragma once

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
class TuningClient {
public:
    TuningClient();
    ~TuningClient();

    // Frequency in Hz for a MIDI note on a given channel (1-based).
    // Returns 0.0f if MTS marks this note as filtered (should not sound).
    // Falls back to equal temperament if MTS returns a non-finite/zero value.
    float noteToHz(int midiNote, int midiChannel) const;

    // Whether an MTS-ESP master is currently connected.
    bool hasMaster() const;

    // Re-register with the MTS-ESP daemon.
    // Call from the message thread if a tuning preset change left Vane silent.
    // libMTSClient uses atomics internally so this is safe while audio is running.
    void reconnect();

    // Equal-temperament frequency for a MIDI note (A4 = 440 Hz reference).
    // Public so unit tests and any future fallback callers can use it directly.
    static float equalTemperamentHz(int midiNote);

private:

#if VANE_HAS_MTS
    ::MTSClient* mtsClient = nullptr;
#endif
};
