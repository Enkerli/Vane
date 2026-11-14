// windcore-render: events in, WAV out.
//   windcore-render events.txt out.wav [--rate 48000] [--cal cal.txt] [--controls id=0..1,...] [--probe probe.f32] [name=value ...]
// Event lines (time in seconds, sorted):
//   <t> on <note>      <t> off <note>      <t> breath <0..1>      <t> bend <semitones>
//   <t> set <param> <value>      <t> end
// Breath values are joined by straight lines (controller data is sparse).
//
// A voice playing the instrument: --voice voice.wav (mono or mixed to mono, any rate), read alongside the events, which
// still give the notes. What the voice does is chosen with v.<name>=<value>:
//   v.vowel=<dB>     its vowel, as a place on the oo..ah..ee line, sets the vowel resonances on the sound (vowel, vowelDepth)
//   v.formants=<dB>  its first two formants set those resonances directly (vowelF1, vowelF2)
//   v.tongue=1       its closures stop the reed and its onsets tongue it, so "t", "k", "p" articulate the notes
//   v.chiff=<0..3>   onset noise at those onsets: as loud as the consonant, bright for "t", dark for "p" (chiff, chiffColor)
//   v.breath=<0..1>  its level drives the breath, by this share (the events' breath is the ceiling)
//   v.gain=<x>       level of the voice fed to the mouth (it is first scaled to peak at 1); used with voiceSing=, voiceAir=
//   v.rise= v.dip= v.open= v.close= v.size= v.smooth= v.emphasis=   tracker settings (see VoiceTracker.h)
// --voice-log f.txt writes what was tracked: "f <t> <level dB> <open> <state> <voiced> <pitch Hz> <F1> <F2> <vowel> <fit>" every
// 5 ms and "o <t> <bright> <level>" at each onset.
// cal.txt lines: <note> <cents c0> <c1> <gain dB> [<c2>]   with cents = c0 + c1*e + c2*e*e, e = sqrt(embouchure opening)
#include "../WindCharacter.h"
#include "../VoiceTracker.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace windcore;

static std::map<std::string, float Params::*> table()
{
    return {
        { "mouthFreq", &Params::mouthFreq }, { "mouthDepth", &Params::mouthDepth }, { "mouthQ", &Params::mouthQ },
        { "mouth2Ratio", &Params::mouth2Ratio }, { "mouth2Level", &Params::mouth2Level },
        { "reedFreq", &Params::reedFreq }, { "reedLift", &Params::reedLift }, { "reedDamp", &Params::reedDamp }, { "aperture", &Params::aperture },
        { "lay", &Params::lay }, { "air", &Params::air }, { "airTone", &Params::airTone }, { "airTilt", &Params::airTilt },
        { "coneBeta", &Params::coneBeta }, { "coneMax", &Params::coneMax }, { "hornTop", &Params::hornTop }, { "topNote", &Params::topNote }, { "chamber", &Params::chamber }, { "baffle", &Params::baffle },
        { "cutoffK", &Params::cutoffK }, { "cutoffMin", &Params::cutoffMin }, { "boreLoss", &Params::boreLoss },
        { "lossHz", &Params::lossHz }, { "lossRatio", &Params::lossRatio }, { "bell", &Params::bell },
        { "breathFloor", &Params::breathFloor }, { "breathTop", &Params::breathTop }, { "breathCurve", &Params::breathCurve }, { "embMin", &Params::embMin }, { "embTrack", &Params::embTrack }, { "embFloor", &Params::embFloor }, { "loudOpen", &Params::loudOpen }, { "breathKnee", &Params::breathKnee },
        { "breathAttack", &Params::breathAttack }, { "breathRelease", &Params::breathRelease },
        { "keyTime", &Params::keyTime }, { "tongueTime", &Params::tongueTime }, { "tongueStop", &Params::tongueStop },
        { "glide", &Params::glide }, { "body", &Params::body }, { "chiff", &Params::chiff }, { "chiffTime", &Params::chiffTime }, { "keyNoise", &Params::keyNoise }, { "chiffDirect", &Params::chiffDirect }, { "chiffColor", &Params::chiffColor }, { "bloom", &Params::bloom }, { "respond", &Params::respond }, { "weight", &Params::weight }, { "scoop", &Params::scoop }, { "vowel", &Params::vowel }, { "vowelDepth", &Params::vowelDepth }, { "vowelQ", &Params::vowelQ }, { "vowelF1", &Params::vowelF1 }, { "vowelF2", &Params::vowelF2 }, { "bodySize", &Params::bodySize }, { "wobble", &Params::wobble }, { "shimmer", &Params::shimmer }, { "drift", &Params::drift }, { "vibDepth", &Params::vibDepth },
        { "vibRate", &Params::vibRate }, { "vibDelay", &Params::vibDelay }, { "slurDip", &Params::slurDip }, { "slurTime", &Params::slurTime }, { "reedComp", &Params::reedComp }, { "voiceSing", &Params::voiceSing }, { "voiceAir", &Params::voiceAir }, { "outGain", &Params::outGain },
    };
}

struct Ev { double t; std::string kind; double a = 0; std::string name; };

// Any PCM or float WAV, mixed to mono.
static bool readWav (const std::string& path, std::vector<float>& x, int& rate)
{
    std::ifstream f (path, std::ios::binary); if (! f) return false;
    std::vector<unsigned char> d ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp (d.data(), "RIFF", 4) != 0) return false;
    auto u32 = [&] (size_t p) { return (uint32_t) d[p] | ((uint32_t) d[p + 1] << 8) | ((uint32_t) d[p + 2] << 16) | ((uint32_t) d[p + 3] << 24); };
    auto u16 = [&] (size_t p) { return (uint16_t) (d[p] | (d[p + 1] << 8)); };
    int fmt = 1, ch = 1, bits = 16; rate = 48000;
    for (size_t pos = 12; pos + 8 <= d.size();)
    {
        const uint32_t size = u32 (pos + 4); const size_t body = pos + 8;
        if (std::memcmp (&d[pos], "fmt ", 4) == 0 && body + 16 <= d.size())
        {
            fmt = u16 (body); ch = std::max (1, (int) u16 (body + 2)); rate = (int) u32 (body + 4); bits = u16 (body + 14);
            if (fmt == 0xFFFE && body + 26 <= d.size()) fmt = u16 (body + 24);     // extensible: the real format follows
        }
        else if (std::memcmp (&d[pos], "data", 4) == 0)
        {
            const size_t bytes = std::min ((size_t) size, d.size() - body), bps = (size_t) bits / 8, n = bytes / (bps * (size_t) ch);
            x.assign (n, 0.0f);
            for (size_t i = 0; i < n; ++i)
            {
                double acc = 0;
                for (int c = 0; c < ch; ++c)
                {
                    const size_t q = body + (i * (size_t) ch + (size_t) c) * bps;
                    if (fmt == 3 && bits == 32) { float v; std::memcpy (&v, &d[q], 4); acc += v; }
                    else if (bits == 16) acc += (int16_t) u16 (q) / 32768.0;
                    else if (bits == 24) acc += ((int32_t) ((u32 (q - 1)) & 0xFFFFFF00u)) / 2147483648.0;
                    else if (bits == 32) acc += (int32_t) u32 (q) / 2147483648.0;
                    else return false;
                }
                x[i] = (float) (acc / ch);
            }
            return true;
        }
        pos = body + size + (size & 1);
    }
    return false;
}

static void writeWav (const std::string& path, const std::vector<float>& x, int rate)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t bytes = (uint32_t) x.size() * 4;
    f.write ("RIFF", 4); u32 (36 + bytes); f.write ("WAVEfmt ", 8); u32 (16); u16 (3); u16 (1);
    u32 ((uint32_t) rate); u32 ((uint32_t) rate * 4); u16 (4); u16 (32); f.write ("data", 4); u32 (bytes);
    f.write ((const char*) x.data(), bytes);
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::cerr << "usage: windcore-render events.txt out.wav [--rate N] [--cal f] [--probe f] [name=value ...]\n"; return 2; }
    const auto tab = table();
    Engine e; int rate = 48000; std::string cal, probe, curves, voicePath, voiceLog; static Character ch; bool useCh = false;
    std::map<std::string, float> vo { { "vowel", 0 }, { "formants", 0 }, { "tongue", 0 }, { "chiff", 0 }, { "breath", 0 }, { "gain", 1 },
                                      { "rise", 0 }, { "open", 0 }, { "close", 0 }, { "size", 0 }, { "smooth", 0 }, { "emphasis", -1 }, { "dip", 0 } };
    for (int i = 3; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--rate" && i + 1 < argc) rate = std::atoi (argv[++i]);
        else if (a == "--cal" && i + 1 < argc) cal = argv[++i];
        else if (a == "--probe" && i + 1 < argc) probe = argv[++i];
        else if (a == "--curves" && i + 1 < argc) curves = argv[++i];
        else if (a == "--voice" && i + 1 < argc) voicePath = argv[++i];
        else if (a == "--voice-log" && i + 1 < argc) voiceLog = argv[++i];
        else if (a.rfind ("v.", 0) == 0 && a.find ('=') != std::string::npos)
        {
            const auto eq = a.find ('='); auto it = vo.find (a.substr (2, eq - 2));
            if (it == vo.end()) { std::cerr << "unknown voice option " << a << "\n"; return 2; }
            it->second = std::strtof (a.c_str() + eq + 1, nullptr);
        }
        else if (a == "--controls" && i + 1 < argc)
        {
            // the character layer: id=position,... (0..1); settings and calibration then come from it
            float v[Character::kNum]; for (int k = 0; k < Character::kNum; ++k) v[k] = ch.get (k);
            std::istringstream ss (argv[++i]); std::string kv;
            while (std::getline (ss, kv, ','))
            {
                if (kv.empty()) continue;
                const auto q = kv.find ('='); const int k = q == std::string::npos ? -1 : Character::find (kv.substr (0, q).c_str());
                if (k < 0) { std::cerr << "unknown control " << kv << "\n"; return 2; }
                v[k] = std::strtof (kv.c_str() + q + 1, nullptr);
            }
            ch.set (v); useCh = true;
        }
        else if (auto eq = a.find ('='); eq != std::string::npos)
        {
            auto it = tab.find (a.substr (0, eq));
            if (it == tab.end()) { std::cerr << "unknown parameter " << a << "\n"; return 2; }
            e.p.*(it->second) = std::strtof (a.c_str() + eq + 1, nullptr);
        }
        else { std::cerr << "unknown argument " << a << "\n"; return 2; }
    }
    if (! cal.empty())
    {
        std::ifstream c (cal); std::string line;
        while (std::getline (c, line))
        {
            std::istringstream ls (line); int n; float cents = 0, loud = 0, db = 0, bend = 0;
            if (! (ls >> n >> cents >> loud >> db) || n < 0 || n > 127) continue;
            ls >> bend;   // optional fifth column
            e.own.tuneCents[n] = cents; e.own.tuneLoud[n] = loud; e.own.gainDb[n] = db; e.own.tuneBend[n] = bend;
        }
    }

    if (useCh) { e.p = ch.params; e.shared = &ch.tables; }
    if (std::string (argv[1]) == "--dump")
    {
        // settings and tables as the character layer made them (for checking against tools/voice.py)
        for (auto& kv : tab) std::printf ("p %s %.9g\n", kv.first.c_str(), (double) (ch.params.*(kv.second)));
        for (int n = 0; n < 128; ++n) std::printf ("t %d %.6f %.6f %.6f %.6f\n", n, ch.tables.tuneCents[n], ch.tables.tuneLoud[n], ch.tables.gainDb[n], ch.tables.tuneBend[n]);
        return 0;
    }

    if (! curves.empty())
    {
        // the horn across the range: <note> <nine multipliers in Curve order> <bell dB added> <embouchure floor added>
        std::ifstream c (curves); std::string line;
        while (std::getline (c, line))
        {
            std::istringstream ls (line); int n; if (! (ls >> n) || n < 0 || n > 127) continue;
            for (int k = 0; k < kNumCurves; ++k) ls >> e.own.curve[k][n];
            ls >> e.own.bellAdd[n] >> e.own.floorAdd[n];
        }
    }

    std::vector<Ev> evs; double tEnd = 0;
    {
        std::ifstream in (argv[1]); std::string line;
        if (! in) { std::cerr << "cannot read " << argv[1] << "\n"; return 2; }
        while (std::getline (in, line))
        {
            std::istringstream s (line); Ev ev;
            if (! (s >> ev.t >> ev.kind)) continue;
            if (ev.kind == "set") s >> ev.name >> ev.a; else s >> ev.a;
            tEnd = std::max (tEnd, ev.t);
            evs.push_back (ev);
        }
    }

    // the voice, at the render's rate and peaking at 1
    std::vector<float> voice; VoiceTracker vt; std::FILE* vlog = nullptr;
    if (! voicePath.empty())
    {
        std::vector<float> raw; int vr = 0;
        if (! readWav (voicePath, raw, vr) || raw.empty()) { std::cerr << "cannot read " << voicePath << "\n"; return 2; }
        float pk = 1e-9f; for (float v : raw) pk = std::max (pk, std::abs (v));
        voice.resize ((size_t) ((double) raw.size() * rate / vr));
        for (size_t n = 0; n < voice.size(); ++n)
        {
            const double q = (double) n * vr / rate; const size_t i = (size_t) q; const float fr = (float) (q - (double) i);
            voice[n] = (raw[i] + fr * (raw[std::min (i + 1, raw.size() - 1)] - raw[i])) / pk;
        }
        if (vo["rise"] > 0) vt.rise = vo["rise"];
        if (vo["open"] < 0) vt.openDb = vo["open"];
        if (vo["close"] < 0) vt.closeDb = vo["close"];
        if (vo["size"] > 0) vt.voiceSize = vo["size"];
        if (vo["smooth"] > 0) vt.smooth = vo["smooth"];
        if (vo["dip"] > 0) vt.dip = vo["dip"];
        if (vo["emphasis"] >= 0) vt.emphasis = vo["emphasis"];
        vt.prepare (rate);
        if (! voiceLog.empty()) vlog = std::fopen (voiceLog.c_str(), "w");
    }
    const bool useVoice = ! voice.empty();
    const float vVowel = vo["vowel"], vFormants = vo["formants"], vChiff = vo["chiff"], vBreath = std::clamp (vo["breath"], 0.0f, 1.0f), vGain = vo["gain"];
    const bool vTongue = vo["tongue"] > 0;
    int heldNotes = 0; float depthS = 0, breathS = 0;

    e.prepare (rate);
    const size_t N = (size_t) (tEnd * rate) + 1;
    std::vector<float> out (N), pr;
    if (! probe.empty()) pr.reserve (N * 3);

    // breath as line segments
    std::vector<std::pair<double, double>> br;
    for (auto& ev : evs) if (ev.kind == "breath") br.push_back ({ ev.t, ev.a });
    size_t bi = 0, ei = 0;
    for (size_t n = 0; n < N; ++n)
    {
        const double t = (double) n / rate;
        while (ei < evs.size() && evs[ei].t <= t)
        {
            auto& ev = evs[ei++];
            if (ev.kind == "on") { e.noteOn ((int) ev.a); ++heldNotes; }
            else if (ev.kind == "off") { e.noteOff ((int) ev.a); heldNotes = std::max (0, heldNotes - 1); }
            else if (ev.kind == "bend") e.setBend ((float) ev.a);
            else if (ev.kind == "set")
            {
                auto it = tab.find (ev.name);
                if (it != tab.end()) e.p.*(it->second) = (float) ev.a;
            }
        }
        if (! br.empty())
        {
            while (bi + 1 < br.size() && br[bi + 1].first <= t) ++bi;
            double b = br[bi].second;
            if (t < br[0].first) b = 0;
            else if (bi + 1 < br.size())
            {
                const double span = br[bi + 1].first - br[bi].first;
                if (span > 1e-9 && span < 0.25) b += (br[bi + 1].second - b) * (t - br[bi].first) / span;
            }
            if (useVoice && vBreath > 0)
            {
                // the voice's level as breath: 30 dB below its loudest is no breath
                const float lv = vt.open ? std::clamp (1.0f + (vt.levelDb - vt.peakDb) / 30.0f, 0.0f, 1.0f) : 0.0f;
                breathS += (1.0f - std::exp (-1.0f / (0.008f * (float) rate))) * (lv - breathS);
                b *= 1.0 - vBreath + vBreath * breathS;
            }
            e.setBreath ((float) b);
        }
        if (useVoice)
        {
            const float v = n < voice.size() ? voice[n] : 0.0f;
            const bool onset = vt.process (v);
            e.setMouthInput (v * vGain);
            if (onset && vlog != nullptr) std::fprintf (vlog, "o %.5f %.3f %.3f\n", t, vt.onsetBright, vt.onsetLevel);
            if (onset && vChiff > 0)
            {
                // the consonant's noise: its loudness gives the amount, its brightness the colour
                e.p.chiff = vChiff * (0.4f + 0.6f * vt.onsetLevel);
                e.p.chiffColor = std::clamp (2.5f * vt.onsetBright, 0.0f, 1.0f);
            }
            if (vTongue)
            {
                if (onset && heldNotes > 0) e.tongueStroke();
                e.setGate (heldNotes > 0 && vt.open);
            }
            if (vt.frameReady)
            {
                // the vowel resonances fade in with a sound that has formants, and hold their place through closures
                const float want = vt.open ? std::max (vVowel, vFormants) : 0.0f;
                depthS += 0.25f * (want - depthS);
                if (vVowel > 0 || vFormants > 0) e.p.vowelDepth = depthS;
                if (vVowel > 0) e.p.vowel = vt.vowel;
                if (vFormants > 0) { e.p.vowelF1 = vt.f1 / vt.voiceSize; e.p.vowelF2 = vt.f2 / vt.voiceSize; }
                if (vlog != nullptr)
                    std::fprintf (vlog, "f %.5f %.1f %d %d %.2f %.1f %.1f %.1f %.3f %.2f\n", t, vt.levelDb, (int) vt.open, (int) vt.state, vt.voiced, vt.pitchHz, vt.f1, vt.f2, vt.vowel, vt.vowelFit);
            }
        }
        out[n] = e.tickHost();
        if (! probe.empty()) { pr.push_back (e.lastPressure); pr.push_back (e.lastFlow); pr.push_back (e.lastOpen); }
    }
    for (float v : out) if (! std::isfinite (v)) { std::cerr << "non-finite output\n"; return 1; }
    if (vlog != nullptr) std::fclose (vlog);
    writeWav (argv[2], out, rate);
    if (! probe.empty()) { std::ofstream f (probe, std::ios::binary); f.write ((const char*) pr.data(), (std::streamsize) pr.size() * 4); }
    return 0;
}
