#pragma once

#if VANE_HAS_MTS
  #include "libMTSClient.h"
#endif

// Wraps the ODDSound MTS-ESP client API.
// When no MTS master is connected, falls back to equal temperament (A4 = 440 Hz).
// The type name avoids collision with the MTSClient opaque type in libMTSClient.h.
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

private:
    static float equalTemperamentHz(int midiNote);

#if VANE_HAS_MTS
    ::MTSClient* mtsClient = nullptr;
#endif
};
