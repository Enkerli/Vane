#pragma once
#include <algorithm>
#include <cmath>

/**
 * Synthetic breath — an envelope that stands in for a wind controller.
 *
 * WHY THIS EXISTS. The MiniSax waveguide is fed by
 * `max(breathCC, expression, MPE pressure, velocityMix * sqrt(velocity))`, and
 * a sequencer sends none of the first three. With `velocityMix` at its default
 * of 0 the reed gets nothing and the instrument is **silent** — measured, not
 * inferred: peak 0.00000 against 0.99997 with breath at 0.7
 * (docs/sequencer-playability.md). Serpe, `msuite play`, a DAW piano roll and
 * most keyboards all land in that hole.
 *
 * NOT A GENERIC ADSR, and the differences are the point:
 *
 *   · **Attack is slow by default (35 ms).** A reed does not snap; the
 *     speaking threshold takes real time to cross, and an instant attack is
 *     what makes synthetic wind sound synthetic.
 *   · **Velocity scales the PEAK, and shortens the attack.** Blowing harder
 *     both gets louder and speaks sooner. One control, two consequences —
 *     which is what a player actually does.
 *   · **Legato does not retrigger.** This is the melisma case Alex asked for:
 *     several notes inside ONE breath. A legato note re-aims the target and
 *     leaves the current level alone, so the phrase keeps its shape instead of
 *     re-articulating on every pitch. It mirrors what the voice already does
 *     with the bore and the VCA on a mono legato transition.
 *
 * LATER: the shape wants to be a recorded DrawnQurve "qurve" rather than four
 * numbers — that is why `levelFor()` is a pure function of a phase and the
 * segment times, so a curve lookup can replace the ramp maths without moving
 * anything else.
 *
 * Block-rate is fine: the waveguide smooths breath internally over ~20 ms.
 */
class BreathEnvelope
{
public:
    struct Params
    {
        float attackMs  = 35.0f;   // reed speaking time at full velocity
        float decayMs   = 120.0f;  // settle from the initial push down to sustain
        float sustain   = 0.80f;   // the body of the note, as a fraction of peak
        float releaseMs = 180.0f;  // breath dying after the note ends
        // How much velocity shortens the attack. 1 = a fff note speaks
        // immediately, 0 = every note takes attackMs regardless.
        float velToAttack = 0.6f;
    };

    void prepare (double sampleRate) { sr = sampleRate > 0 ? sampleRate : 48000.0; reset(); }
    void reset()                     { stage = Stage::Idle; level = 0.0f; peak = 0.0f; phase = 0.0f; }

    bool isActive() const  { return stage != Stage::Idle; }
    float current() const  { return level; }

    /**
     * @param velocity 0..1
     * @param legato   true when this note continues a phrase already sounding.
     *                 The envelope then keeps its level and only re-aims — one
     *                 breath across several notes.
     */
    void noteOn (float velocity, bool legato)
    {
        peak = std::clamp (velocity, 0.0f, 1.0f);
        if (legato && stage != Stage::Idle)
        {
            // Mid-phrase: no new attack. Go straight to the sustain segment at
            // whatever level we are already at, so a louder note swells rather
            // than restarting. Re-attacking here is exactly what makes a
            // slurred line sound typed instead of played.
            stage = Stage::Sustain;
            return;
        }
        stage = Stage::Attack;
        phase = 0.0f;
    }

    void noteOff()
    {
        if (stage == Stage::Idle) return;
        releaseFrom = level;
        stage = Stage::Release;
        phase = 0.0f;
    }

    /** Advance by `samples` and return the new level. Call once per block. */
    float advance (int samples, const Params& p)
    {
        if (stage == Stage::Idle) return 0.0f;
        const float dt = static_cast<float> (samples / sr) * 1000.0f;   // ms

        switch (stage)
        {
            case Stage::Attack:
            {
                const float ms = attackMsFor (p);
                phase += dt;
                if (phase >= ms) { stage = Stage::Decay; phase = 0.0f; level = peak; }
                else             { level = peak * (ms > 0.0f ? phase / ms : 1.0f); }
                break;
            }
            case Stage::Decay:
            {
                const float ms = std::max (1.0f, p.decayMs);
                phase += dt;
                const float target = peak * std::clamp (p.sustain, 0.0f, 1.0f);
                if (phase >= ms) { stage = Stage::Sustain; level = target; }
                else             { level = peak + (target - peak) * (phase / ms); }
                break;
            }
            case Stage::Sustain:
                // Track the peak so a legato note that re-aimed louder actually
                // arrives there instead of sitting at the old level.
                level += (peak * std::clamp (p.sustain, 0.0f, 1.0f) - level) * 0.05f;
                break;

            case Stage::Release:
            {
                const float ms = std::max (1.0f, p.releaseMs);
                phase += dt;
                if (phase >= ms) { reset(); }
                else             { level = releaseFrom * (1.0f - phase / ms); }
                break;
            }
            case Stage::Idle: break;
        }
        return level = std::clamp (level, 0.0f, 1.0f);
    }

private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    /** Harder notes speak sooner — the attack shortens toward 15% of nominal. */
    float attackMsFor (const Params& p) const
    {
        const float shorten = std::clamp (p.velToAttack, 0.0f, 1.0f) * peak;
        return std::max (1.0f, p.attackMs * (1.0f - 0.85f * shorten));
    }

    double sr = 48000.0;
    Stage  stage = Stage::Idle;
    float  level = 0.0f, peak = 0.0f, phase = 0.0f, releaseFrom = 0.0f;
};
