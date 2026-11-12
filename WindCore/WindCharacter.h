// The character layer of WindCore: twelve controls, each 0..1, turned into model settings
// plus the per-note tuning and level corrections that keep the horn in tune as they move.
// Control definitions and correction tables come from WindCoreData.h (generated).
#pragma once
#include "WindCoreData.h"

namespace windcore
{

class Character
{
public:
    static constexpr int kNum = data::kNumControls;

    Params params;      // model settings for the current control positions
    Tables tables;      // calibration for them: share with every engine playing this character

    Character() { for (int i = 0; i < kNum; ++i) pos[i] = defaultPosition (i); rebuild(); }

    static float toPhysical (int i, float v)
    {
        const auto& c = data::kControls[i];
        if (c.scale == data::kLog) return c.lo * std::pow (c.hi / c.lo, v);
        if (c.scale == data::kSquare) return c.lo + (c.hi - c.lo) * v * v;
        return c.lo + (c.hi - c.lo) * v;
    }
    static float defaultPosition (int i)
    {
        const auto& c = data::kControls[i];
        if (c.scale == data::kLog) return std::log (c.defaultValue / c.lo) / std::log (c.hi / c.lo);
        if (c.scale == data::kSquare) return std::sqrt (std::max (0.0f, (c.defaultValue - c.lo) / (c.hi - c.lo)));
        return (c.defaultValue - c.lo) / (c.hi - c.lo);
    }
    static int find (const char* id)
    {
        for (int i = 0; i < kNum; ++i) if (std::strcmp (data::kControls[i].id, id) == 0) return i;
        return -1;
    }

    float get (int i) const { return pos[i]; }

    /** Set every control (kNum values, 0..1). Returns true if anything changed.
        Cheap when nothing did; otherwise about 30 table rows of arithmetic, no allocation. */
    bool set (const float* v)
    {
        bool changed = false;
        for (int i = 0; i < kNum; ++i)
        {
            const float x = std::clamp (v[i], 0.0f, 1.0f);
            if (std::abs (x - pos[i]) > 0.0f) { pos[i] = x; changed = true; }
        }
        if (changed) rebuild();
        return changed;
    }

private:
    float pos[kNum];

    void add (const data::Trim& t, float w)
    {
        for (int n = 0; n < 128; ++n) { tables.tuneCents[n] += w * t.cents[n]; tables.gainDb[n] += w * t.gain[n]; }
    }

    void rebuild()
    {
        for (int n = 0; n < 128; ++n)
        {
            tables.tuneCents[n] = data::kBaseCents[n]; tables.tuneLoud[n] = data::kBaseLoud[n]; tables.gainDb[n] = data::kBaseGain[n];
            tables.tuneBend[n] = data::kBaseBend[n]; tables.bellAdd[n] = data::kBellAdd[n]; tables.floorAdd[n] = data::kFloorAdd[n];
            for (int k = 0; k < kNumCurves; ++k) tables.curve[k][n] = data::kCurves[k][n];
        }
        for (int i = 0; i < kNum; ++i)
        {
            const auto& c = data::kControls[i];
            params.*(c.param) = toPhysical (i, pos[i]);
            if (c.trim < 0) continue;
            const float d = defaultPosition (i), x = pos[i];
            if (std::abs (x - d) < 1e-9f) continue;
            if (x > d) add (data::kTrims[2 * c.trim + 1], (x - d) / (1.0f - d));
            else       add (data::kTrims[2 * c.trim], (d - x) / d);
        }
        // the mouth: tables at full strength for several positions, scaled by strength
        const float depth = toPhysical (data::kMouthDepth, pos[data::kMouthDepth]);
        if (data::kMouthIsTract && depth > 0)
        {
            const float lf = std::log (toPhysical (data::kMouth, pos[data::kMouth]));
            int i = 0;
            while (i < data::kNumMouthAnchors - 2 && lf >= std::log (data::kMouthAnchorHz[i + 1])) ++i;
            const float l0 = std::log (data::kMouthAnchorHz[i]), l1 = std::log (data::kMouthAnchorHz[i + 1]);
            const float t = std::clamp ((lf - l0) / (l1 - l0), 0.0f, 1.0f), w = depth / data::kControls[data::kMouthDepth].hi;
            add (data::kMouthTrims[i], w * (1.0f - t));
            add (data::kMouthTrims[i + 1], w * t);
        }
    }
};

} // namespace windcore
