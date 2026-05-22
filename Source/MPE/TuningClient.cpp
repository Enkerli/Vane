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
    if (mtsClient && MTS_HasMaster(mtsClient))
        return static_cast<float>(MTS_NoteToFrequency(
            mtsClient,
            static_cast<char>(midiNote),
            static_cast<char>(midiChannel)));
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

float TuningClient::equalTemperamentHz(int midiNote)
{
    return 440.0f * std::pow(2.0f, (midiNote - 69) / 12.0f);
}
