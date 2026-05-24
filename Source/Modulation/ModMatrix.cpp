#include "ModMatrix.h"
#include <algorithm>
#include <cmath>

// Apply per-route curve shaping to the slewed value.
// For bipolar sources the sign is preserved so curves are symmetric.
static float applyCurve(float x, ModRoute::CurveShape curve)
{
    switch (curve) {
        case ModRoute::CurveShape::Linear:
            return x;
        case ModRoute::CurveShape::Exponential:
            // x * |x| = sign(x) * x²: more resolution at low values, steeper at high.
            return x * std::abs(x);
        case ModRoute::CurveShape::SCurve: {
            // Smoothstep on |x|, sign preserved: ease-in at bottom, ease-out at top.
            float a = std::abs(x);
            a = a * a * (3.0f - 2.0f * a);
            return std::copysign(a, x);
        }
    }
    return x;
}

void ModMatrix::prepare(double sr, int bs)
{
    sampleRate = sr;
    blockSize  = bs;
    for (auto& r : routes)
        r.slewer.prepare(sr, bs);
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
        float raw          = getSourceValue(route.source, voiceVals);
        float slewed       = route.slewer.process(raw);
        float shaped       = applyCurve(slewed, route.curve);
        float contribution = shaped * route.amount;
        result[route.dest] = std::clamp(
            result[route.dest] + contribution, -1.0f, 1.0f);
    }

    return result;
}

void ModMatrix::addRoute(int source, int dest, float amount,
                          float attackMs, float releaseMs, ModRoute::CurveShape curve)
{
    ModRoute r;
    r.source = source;
    r.dest   = dest;
    r.amount = amount;
    r.curve  = curve;
    r.slewer.prepare(sampleRate, blockSize);
    r.slewer.setRates(attackMs, releaseMs);
    routes.push_back(std::move(r));
}

void ModMatrix::clearRoutes()
{
    routes.clear();
}
