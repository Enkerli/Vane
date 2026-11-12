// VanePerfRender: play a performance file through VaneProcessor offline and write a WAV.
//
//   VanePerfRender perf.txt out.wav [sampleRate=48000] [seconds]
//
// perf.txt is plain text, one event per line, time in seconds, '#' for comments:
//   <t> on  <note> <velocity 1..127>
//   <t> off <note>
//   <t> cc  <number> <value 0..127>
//   <t> pb  <value -1..1>              (channel pitch bend, master channel)
//   <t> at  <value 0..127>             (channel pressure)
//   <t> param <id> <normalised 0..1>   (any APVTS parameter; applied when reached)
//   <t> set <id> <value>               (same, in the parameter's own units / choice index)
// Events go to MIDI channel 1 (the MPE master channel, i.e. plain MIDI).
// perf can also be a Standard MIDI File (.mid), e.g. the windbench test performance.
// VANE_STATE=FILE restores a saved Vane state first: a raw JUCE state chunk, or any
// file that embeds one (a Bitwig .clap-preset, a DAWproject's plugin state).
// Lines "<t> set <id> <value>" (or a .mid plus VANE_SET="id=value;id=value") then
// adjust parameters on top of that state.
#include "../../Source/PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>

struct Ev { double t; std::string kind; std::string id; double a = 0, b = 0; };

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: %s perf.txt out.wav [sampleRate] [seconds]\n", argv[0]); return 2; }
    juce::ScopedJuceInitialiser_GUI juceInit;
    const double sr = argc > 3 ? std::atof (argv[3]) : 48000.0;
    const int bs = 256;

    std::vector<Ev> evs;
    std::ifstream in (argv[1]);
    for (std::string line; std::getline (in, line);) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss (line); Ev e; ss >> e.t >> e.kind;
        if (e.kind == "param" || e.kind == "set") ss >> e.id >> e.a; else ss >> e.a >> e.b;
        evs.push_back (e);
    }
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& x, const Ev& y) { return x.t < y.t; });
    double secs = argc > 4 ? std::atof (argv[4]) : (evs.empty() ? 1.0 : evs.back().t + 1.0);

    // Standard MIDI File input: notes, CCs, pitch bend, channel pressure
    if (juce::String (argv[1]).endsWithIgnoreCase (".mid")) {
        evs.clear();
        juce::MidiFile mf; juce::FileInputStream fin (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
        if (! fin.openedOk() || ! mf.readFrom (fin)) { std::fprintf (stderr, "cannot read %s\n", argv[1]); return 1; }
        mf.convertTimestampTicksToSeconds();
        for (int tr = 0; tr < mf.getNumTracks(); ++tr)
            for (auto* me : *mf.getTrack (tr)) {
                const auto& m = me->message; Ev e; e.t = m.getTimeStamp();
                if (m.isNoteOn()) { e.kind = "on"; e.a = m.getNoteNumber(); e.b = m.getVelocity(); }
                else if (m.isNoteOff()) { e.kind = "off"; e.a = m.getNoteNumber(); }
                else if (m.isController()) { e.kind = "cc"; e.a = m.getControllerNumber(); e.b = m.getControllerValue(); }
                else if (m.isPitchWheel()) { e.kind = "pb"; e.a = (m.getPitchWheelValue() - 8192) / 8191.0; }
                else if (m.isChannelPressure()) { e.kind = "at"; e.a = m.getChannelPressureValue(); }
                else continue;
                evs.push_back (e);
            }
        if (const char* vs = std::getenv ("VANE_SET"))
            for (auto& kv : juce::StringArray::fromTokens (vs, ";", "")) {
                Ev e; e.t = 0; e.kind = "set"; e.id = kv.upToFirstOccurrenceOf ("=", false, false).trim().toStdString();
                e.a = kv.fromFirstOccurrenceOf ("=", false, false).getDoubleValue(); evs.insert (evs.begin(), e);
            }
        std::stable_sort (evs.begin(), evs.end(), [] (const Ev& x, const Ev& y) { return x.t < y.t; });
        if (argc <= 4) secs = (evs.empty() ? 1.0 : evs.back().t + 2.0);
    }

    VaneProcessor proc;
    if (const char* sf = std::getenv ("VANE_STATE")) {
        juce::MemoryBlock mb;
        if (! juce::File (sf).loadFileAsData (mb)) { std::fprintf (stderr, "cannot read state %s\n", sf); return 1; }
        // find an embedded JUCE binary state ("VC2!" + size + XML)
        const char* d = static_cast<const char*> (mb.getData()); size_t off = 0; bool found = false;
        for (size_t i = 0; i + 8 < mb.getSize(); ++i) if (std::memcmp (d + i, "VC2!", 4) == 0) { off = i; found = true; break; }
        if (! found) { std::fprintf (stderr, "no Vane state found in %s\n", sf); return 1; }
        proc.setStateInformation (d + off, (int) (mb.getSize() - off));
        std::printf ("restored Vane state from %s\n", sf);
    }
    proc.setPlayConfigDetails (0, 2, sr, bs);
    proc.prepareToPlay (sr, bs);
    juce::AudioBuffer<float> buf (2, bs);
    const int total = (int) (secs * sr);
    juce::AudioBuffer<float> out (2, total);
    size_t k = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int pos = 0; pos < total; pos += bs) {
        const int n = std::min (bs, total - pos);
        juce::MidiBuffer midi;
        while (k < evs.size() && evs[k].t * sr < pos + n) {
            const auto& e = evs[k++];
            const int off = std::clamp ((int) (e.t * sr) - pos, 0, n - 1);
            if (e.kind == "on")  midi.addEvent (juce::MidiMessage::noteOn (1, (int) e.a, (juce::uint8) std::clamp ((int) e.b, 1, 127)), off);
            else if (e.kind == "off") midi.addEvent (juce::MidiMessage::noteOff (1, (int) e.a), off);
            else if (e.kind == "cc") midi.addEvent (juce::MidiMessage::controllerEvent (1, (int) e.a, std::clamp ((int) e.b, 0, 127)), off);
            else if (e.kind == "at") midi.addEvent (juce::MidiMessage::channelPressureChange (1, std::clamp ((int) e.a, 0, 127)), off);
            else if (e.kind == "pb") midi.addEvent (juce::MidiMessage::pitchWheel (1, std::clamp ((int) std::lround (8192 + e.a * 8191), 0, 16383)), off);
            else if (e.kind == "param" || e.kind == "set") {
                if (auto* p = proc.apvts.getParameter (e.id))
                    p->setValueNotifyingHost (e.kind == "set" ? p->convertTo0to1 ((float) e.a) : (float) e.a);
                else std::fprintf (stderr, "unknown parameter %s\n", e.id.c_str());
            }
        }
        buf.setSize (2, n, false, false, true);
        buf.clear();
        proc.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
    }
    const double cpu = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    float peak = out.getMagnitude (0, total);
    std::printf ("rendered %.1f s at %.0f Hz in %.1f s (%.0f%% of real time); peak %.3f\n", secs, sr, cpu, 100 * cpu / secs, peak);

    juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
    if (! w) { std::fprintf (stderr, "cannot write %s\n", argv[2]); return 1; }
    w->writeFromAudioSampleBuffer (out, 0, total);
    return 0;
}
