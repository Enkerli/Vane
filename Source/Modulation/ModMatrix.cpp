#include "ModMatrix.h"
#include <algorithm>

void ModMatrix::prepare(double sr)
{
    sampleRate = sr;
    for (auto& r : routes)
        r.slewer.prepare(sr);
}

void ModMatrix::setCCValue(int ccNumber, float value)
{
    if (ccNumber >= 0 && ccNumber < 128)
        ccValues[ccNumber] = std::clamp(value, 0.0f, 1.0f);
}

float ModMatrix::getCCValue(int ccNumber) const
{
    if (ccNumber >= 0 && ccNumber < 128)
        return ccValues[ccNumber];
    return 0.0f;
}

float ModMatrix::getSourceValue(int sourceID,
    const std::array<float, ModSourceID::NumVoiceSources>& voiceVals) const
{
    if (sourceID >= 0 && sourceID < ModSourceID::NumVoiceSources)
        return voiceVals[sourceID];

    int ccNum = sourceID - ModSourceID::CC;
    if (ccNum >= 0 && ccNum < 128)
        return ccValues[ccNum];

    return 0.0f;
}

std::array<float, ModDestID::NumDests>
ModMatrix::evaluate(const std::array<float, ModSourceID::NumVoiceSources>& voiceVals)
{
    std::array<float, ModDestID::NumDests> result {};

    for (auto& route : routes) {
        float raw    = getSourceValue(route.source, voiceVals);
        float slewed = route.slewer.process(raw);
        float contribution = slewed * route.amount;
        result[route.dest] = std::clamp(
            result[route.dest] + contribution, -1.0f, 1.0f);
    }

    return result;
}

void ModMatrix::addRoute(int source, int dest, float amount,
                          float attackMs, float releaseMs)
{
    ModRoute r;
    r.source = source;
    r.dest   = dest;
    r.amount = amount;
    r.slewer.prepare(sampleRate);
    r.slewer.setRates(attackMs, releaseMs);
    routes.push_back(std::move(r));
}

void ModMatrix::clearRoutes()
{
    routes.clear();
}
