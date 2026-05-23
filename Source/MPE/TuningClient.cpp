#include "TuningClient.h"
#include <cmath>

TuningClient::TuningClient()
{
#if VANE_HAS_MTS
    mtsClient = MTS_RegisterClient();
#endif
}

TuningClient::~TuningClient()
{
#if VANE_HAS_MTS
    if (mtsClient)
        MTS_DeregisterClient(mtsClient);
#endif
}

float TuningClient::noteToHz(int midiNote, int midiChannel) const
{
#if VANE_HAS_MTS
    if (mtsClient && MTS_HasMaster(mtsClient)) {
        // Honour the master's "filter this note" flag (mapped to silence in the tuning).
        // Return 0.0f — SynthVoice treats this as "don't start".
        if (MTS_ShouldFilterNote(mtsClient,
                                  static_cast<char>(midiNote),
                                  static_cast<char>(midiChannel)))
            return 0.0f;

        double hz = MTS_NoteToFrequency(mtsClient,
                                         static_cast<char>(midiNote),
                                         static_cast<char>(midiChannel));

        // A corrupt/bad MTS preset can return 0, NaN, or inf.
        // Any of these would drive the oscillator to DC (freq=0) and poison the
        // SVFilter's integrator states, silencing the voice permanently.
        // Fall back to equal temperament instead.
        if (hz > 0.0 && std::isfinite(hz))
            return static_cast<float>(hz);
    }
#else
    (void)midiChannel;
#endif
    return equalTemperamentHz(midiNote);
}

bool TuningClient::hasMaster() const
{
#if VANE_HAS_MTS
    return mtsClient && MTS_HasMaster(mtsClient);
#else
    return false;
#endif
}

void TuningClient::reconnect()
{
#if VANE_HAS_MTS
    if (mtsClient)
        MTS_DeregisterClient(mtsClient);
    mtsClient = MTS_RegisterClient();
#endif
}

float TuningClient::equalTemperamentHz(int midiNote)
{
    return 440.0f * std::pow(2.0f, (midiNote - 69) / 12.0f);
}
