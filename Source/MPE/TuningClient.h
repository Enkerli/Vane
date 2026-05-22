#pragma once

#if VANE_HAS_MTS
  #include "libMTSClient.h"
#endif

// Wraps the ODDSound MTS-ESP client API.
// When no MTS master is connected, falls back to equal temperament (A4 = 440 Hz).
// The type name avoids collision with the MTSClient opaque type in libMTS.h.
class TuningClient {
public:
    TuningClient();
    ~TuningClient();

    // Frequency in Hz for a MIDI note on a given channel (1-based).
    // Incorporates any MTS-ESP retuning when a master is present.
    float noteToHz(int midiNote, int midiChannel) const;

    // Whether an MTS-ESP master is currently connected
    bool hasMaster() const;

private:
    static float equalTemperamentHz(int midiNote);

#if VANE_HAS_MTS
    ::MTSClient* mtsClient = nullptr;
#endif
};
