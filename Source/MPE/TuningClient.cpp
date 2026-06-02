#include "TuningClient.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <cstddef>
#include <limits>   // std::numeric_limits (NaN sentinel for holes)

// ── Internal tuning tables ─────────────────────────────────────────────────
// Cents from C for each of the 12 pitch-classes (index = semitone 0..11).
// NaN = hole (note does not sound in this tuning).
// These mirror the JS TUN library in ext-tuning.js; keep in sync.
static const float kHole = std::numeric_limits<float>::quiet_NaN();

static const float kJust[12]   = { 0,kHole,203.9f,kHole,386.3f,498.0f,kHole,702.0f,kHole,884.4f,kHole,1088.3f };
// Full versions (no holes):
static const float kJustFull[12]   = { 0,111.7f,203.9f,315.6f,386.3f,498.0f,590.2f,702.0f,813.7f,884.4f,996.1f,1088.3f };
static const float kPyth[12]       = { 0,113.7f,203.9f,294.1f,407.8f,498.0f,611.7f,702.0f,815.6f,905.9f,996.1f,1109.8f };
static const float kMeanqc[12]     = { 0,76.0f, 193.2f,310.3f,386.3f,503.4f,579.5f,696.6f,772.6f,889.7f,1006.8f,1082.9f };
static const float kWerck3[12]     = { 0,90.2f, 192.2f,294.1f,390.2f,498.0f,588.3f,696.1f,792.2f,888.3f,996.1f,1092.2f };
static const float kDiat7[12]      = { 0,kHole, 203.9f,kHole,386.3f,498.0f,kHole,702.0f,kHole,884.4f,kHole,1088.3f };
// edo12 = standard 100¢ per semitone (identity; deviationCents = 0 for all)

TuningClient::TuningClient()
{
    // CRITICAL: fill internalCentsTable before any note can be sounded.  Without
    // this, the table is all-zeros; with the default FollowMTS source and no MTS
    // master, rawHz() reads 0 cents for every note and returns ~8.18 Hz (MIDI 0)
    // for the entire keyboard — every note plays as sub-audio rumble.
    setInternalTuning(internalId);   // default "edo12" → transparent 12-EDO fallback
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

// Raw Hz: tuning applied, no hole/filter check.  Used by noteToHz's quantisation
// search and by deviationCents.  Returns ET if the tuning gives a bad value.
float TuningClient::rawHz (int midiNote, int midiChannel) const
{
    if (source == TuningSource::Bypass)
        return equalTemperamentHz(midiNote);
#if VANE_HAS_MTS
    if (source == TuningSource::FollowMTS && mtsClient && MTS_HasMaster(mtsClient)) {
        const double hz = MTS_NoteToFrequency(mtsClient, (char) midiNote, (char) midiChannel);
        if (hz > 0.0 && std::isfinite(hz)) return static_cast<float>(hz);
    }
#endif
    if ((source == TuningSource::Internal || source == TuningSource::FollowMTS)
            && midiNote >= 0 && midiNote < 128) {
        const float c = internalCentsTable[(size_t) midiNote];
        if (!std::isnan(c)) return 440.0f * std::pow (2.0f, (c - 6900.0f) / 1200.0f);
    }
    return equalTemperamentHz(midiNote);
}

float TuningClient::noteToHz(int midiNote, int midiChannel) const
{
    if (source == TuningSource::Bypass)
        return equalTemperamentHz(midiNote);

    // Determine whether this note is a hole/filter.
    bool hole = false;
#if VANE_HAS_MTS
    if (source == TuningSource::FollowMTS && mtsClient && MTS_HasMaster(mtsClient))
        hole = MTS_ShouldFilterNote(mtsClient, (char) midiNote, (char) midiChannel);
#endif
    if (!hole && (source == TuningSource::Internal || source == TuningSource::FollowMTS)
            && midiNote >= 0 && midiNote < 128)
        hole = std::isnan(internalCentsTable[(size_t) midiNote]);

    if (!hole) return rawHz(midiNote, midiChannel);

    // Hole: snap to the nearest non-hole pitch (wind controllers need this —
    // portamento or breath vibrato can land on unmapped keys).
    // Search outward ±12 semitones, choosing the closer side first.
    for (int d = 1; d <= 12; ++d) {
        for (int sign : {-1, 1}) {
            const int n = midiNote + d * sign;
            if (n < 0 || n >= 128) continue;
            if (!isHole(n, midiChannel)) return rawHz(n, midiChannel);
        }
    }
    return 0.0f;   // no valid pitch in ±12 semitones (very unusual scale)
}

void TuningClient::setInternalTuning (const juce::String& id)
{
    internalId = id;
    // Fill internalCentsTable: 128 notes from MIDI 0.  Each note maps to its
    // pitch-class in the 12-note table (repeating every octave); EDO tunings use
    // equal division of the period; non-octave (BP) wraps by the period.
    const float* pc12 = nullptr;   // 12-note pitch-class cents (from C)
    bool isEDO = false;
    int  edoN  = 12;
    float period = 1200.0f;

    if      (id == "just")   { pc12 = kJustFull; }
    else if (id == "pyth")   { pc12 = kPyth;     }
    else if (id == "meanqc") { pc12 = kMeanqc;   }
    else if (id == "werck3") { pc12 = kWerck3;   }
    else if (id == "diat7")  { pc12 = kDiat7;    }
    else if (id == "edo19")  { isEDO = true; edoN = 19; period = 1200.0f; }
    else if (id == "bp")     { isEDO = true; edoN = 13; period = 1901.955f; }
    else /* edo12 / default */
    {
        // Standard 12-EDO — internalCentsTable matches ET (deviationCents = 0).
        for (int n = 0; n < 128; ++n)
            internalCentsTable[(size_t) n] = (float) n * 100.0f;
        return;
    }

    if (isEDO) {
        // Map each MIDI note into the EDO by computing its position in the period.
        // ET semitones→cents: note × 100; period in ET cents = 1200 (or 1901.955 for BP).
        // The note's EDO degree = round(note × (edoN / periodSemitones)).
        const float semPerPeriod = period / 100.0f;
        for (int n = 0; n < 128; ++n) {
            // How many full periods from C4 (note 60)?
            const float centsFromC0 = (float) n * 100.0f;
            const float periods     = std::floor (centsFromC0 / period);
            const float residual    = centsFromC0 - periods * period;
            const int   degree      = (int) std::lround (residual / period * (float) edoN) % edoN;
            internalCentsTable[(size_t) n] = periods * period + (float) degree * (period / (float) edoN);
        }
        return;
    }

    // 12-note repeating table.
    for (int n = 0; n < 128; ++n) {
        const int octave = n / 12, pc = n % 12;
        const float c = pc12[pc];
        internalCentsTable[(size_t) n] = std::isnan(c) ? c : (float) octave * 1200.0f + c;
    }
}

juce::String TuningClient::tuningName() const
{
    if (source == TuningSource::Bypass) return {};
#if VANE_HAS_MTS
    if (source == TuningSource::FollowMTS) {
        if (mtsClient && MTS_HasMaster(mtsClient)) {
            const char* n = MTS_GetScaleName(mtsClient);
            if (n && n[0]) return juce::String (n);
            return juce::String ("(unnamed tuning)");
        }
        // FollowMTS but no master: return empty — no tuning name to report.
        return {};
    }
#else
    if (source == TuningSource::FollowMTS) return {};
#endif
    // Internal tuning names (ASCII-only literals — non-ASCII through juce::String
    // traps on iOS; all names here are deliberately ASCII).
    struct { const char* id; const char* name; } table[] = {
        {"just",   "Just Intonation"}, {"pyth",   "Pythagorean"},
        {"meanqc", "Quarter-comma Meantone"}, {"werck3", "Werckmeister III"},
        {"diat7",  "Just Diatonic"}, {"edo19", "19-tone Equal"}, {"bp", "Bohlen-Pierce"}
    };
    for (auto& e : table)
        if (internalId == e.id) return juce::String (e.name);
    return juce::String ("12-tone Equal");
}

float TuningClient::deviationCents (int midiNote, int midiChannel) const
{
    if (source == TuningSource::Bypass) return 0.0f;
    if (isHole (midiNote, midiChannel)) return 0.0f;   // hole: deviation displayed separately
    const float hz = rawHz (midiNote, midiChannel);
    return (float) (1200.0 * std::log2 ((double) hz / (double) equalTemperamentHz(midiNote)));
}

bool TuningClient::isHole (int midiNote, int midiChannel) const
{
    if (source == TuningSource::Bypass) return false;
#if VANE_HAS_MTS
    if (source == TuningSource::FollowMTS && mtsClient && MTS_HasMaster(mtsClient))
        return MTS_ShouldFilterNote (mtsClient, (char) midiNote, (char) midiChannel);
#endif
    if (midiNote >= 0 && midiNote < 128)
        return std::isnan (internalCentsTable[(size_t) midiNote]);
    return false;
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
    return 440.0f * std::pow(2.0f, static_cast<float>(midiNote - 69) / 12.0f);
}
