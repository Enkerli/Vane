#pragma once
#include <algorithm>
#include <vector>

namespace minisax
{

// Piecewise step/linear breakpoint envelope.
//
// Semantics (documented in README.md):
//   - A breakpoint (time, value, linear=true) means the value is *reached* at
//     `time`, ramping linearly from the previous breakpoint.
//   - linear=false means the value steps to `value` at `time`.
//   - Before the first breakpoint the envelope holds `initialValue`.
class Envelope
{
public:
    struct Breakpoint
    {
        double time = 0.0;
        float value = 0.0f;
        bool linear = false;
    };

    explicit Envelope(float initial = 0.0f) : initialValue(initial) {}

    void add(double time, float value, bool linear)
    {
        points.push_back({time, value, linear});
    }

    void finalize()
    {
        std::stable_sort(points.begin(), points.end(),
                         [](const Breakpoint& a, const Breakpoint& b) { return a.time < b.time; });
        cursor = 0;
    }

    bool empty() const { return points.empty(); }

    // Evaluate at time t.  Calls must have non-decreasing t (the render loop
    // guarantees this); the cursor makes evaluation O(1) amortized.
    float value(double t)
    {
        while (cursor < points.size() && points[cursor].time <= t)
            ++cursor;
        // points[cursor-1] is the last breakpoint at or before t (if any).
        const float prevValue = cursor == 0 ? initialValue : points[cursor - 1].value;
        if (cursor >= points.size())
            return prevValue;

        const Breakpoint& next = points[cursor];
        if (!next.linear)
            return prevValue;

        const double prevTime = cursor == 0 ? 0.0 : points[cursor - 1].time;
        const double span = next.time - prevTime;
        if (span <= 0.0)
            return next.value;
        const double frac = std::clamp((t - prevTime) / span, 0.0, 1.0);
        return prevValue + static_cast<float>(frac) * (next.value - prevValue);
    }

private:
    std::vector<Breakpoint> points;
    float initialValue = 0.0f;
    size_t cursor = 0;
};

} // namespace minisax
