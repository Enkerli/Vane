// windcore-render: events in, WAV out.
//   windcore-render events.txt out.wav [--rate 48000] [--cal cal.txt] [--controls id=0..1,...] [--probe probe.f32] [name=value ...]
// Event lines (time in seconds, sorted):
//   <t> on <note>      <t> off <note>      <t> breath <0..1>      <t> bend <semitones>
//   <t> set <param> <value>      <t> end
// Breath values are joined by straight lines (controller data is sparse).
// cal.txt lines: <note> <cents c0> <c1> <gain dB> [<c2>]   with cents = c0 + c1*e + c2*e*e, e = sqrt(embouchure opening)
#include "../WindCharacter.h"
#include <cstdio>
#include <cstdlib>
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
        { "glide", &Params::glide }, { "body", &Params::body }, { "chiff", &Params::chiff }, { "chiffTime", &Params::chiffTime }, { "keyNoise", &Params::keyNoise }, { "chiffDirect", &Params::chiffDirect }, { "chiffColor", &Params::chiffColor }, { "bloom", &Params::bloom }, { "vowel", &Params::vowel }, { "vowelDepth", &Params::vowelDepth }, { "vowelQ", &Params::vowelQ }, { "bodySize", &Params::bodySize }, { "wobble", &Params::wobble }, { "shimmer", &Params::shimmer }, { "drift", &Params::drift }, { "vibDepth", &Params::vibDepth },
        { "vibRate", &Params::vibRate }, { "vibDelay", &Params::vibDelay }, { "slurDip", &Params::slurDip }, { "slurTime", &Params::slurTime }, { "reedComp", &Params::reedComp }, { "outGain", &Params::outGain },
    };
}

struct Ev { double t; std::string kind; double a = 0; std::string name; };

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
    Engine e; int rate = 48000; std::string cal, probe, curves; static Character ch; bool useCh = false;
    for (int i = 3; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--rate" && i + 1 < argc) rate = std::atoi (argv[++i]);
        else if (a == "--cal" && i + 1 < argc) cal = argv[++i];
        else if (a == "--probe" && i + 1 < argc) probe = argv[++i];
        else if (a == "--curves" && i + 1 < argc) curves = argv[++i];
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
            if (ev.kind == "on") e.noteOn ((int) ev.a);
            else if (ev.kind == "off") e.noteOff ((int) ev.a);
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
            e.setBreath ((float) b);
        }
        out[n] = e.tickHost();
        if (! probe.empty()) { pr.push_back (e.lastPressure); pr.push_back (e.lastFlow); pr.push_back (e.lastOpen); }
    }
    for (float v : out) if (! std::isfinite (v)) { std::cerr << "non-finite output\n"; return 1; }
    writeWav (argv[2], out, rate);
    if (! probe.empty()) { std::ofstream f (probe, std::ios::binary); f.write ((const char*) pr.data(), (std::streamsize) pr.size() * 4); }
    return 0;
}
