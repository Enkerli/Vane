// Offline render probe: instantiate VaneProcessor, play one sustained note on
// the DEFAULT patch, dump RMS + dominant-frequency analysis of the output.
// Purpose: reproduce the "sub-bass / no sustained tone" report headlessly.
#include "../../Source/PluginProcessor.h"
#include "../../Source/Synth/Oscillator.h"
#include <juce_dsp/juce_dsp.h>
#include <cstdio>
#include <cmath>
#include <cstdlib>

static void bareOscillatorTest (double sr, const Wavetable* wt, const char* label)
{
    Oscillator osc;
    osc.prepare (sr);
    if (wt) osc.setWavetable (wt);
    osc.setFrequency (196.0f);
    int zc = 0; float prev = 0; double sq = 0; int N = (int)(sr * 0.2);
    for (int i = 0; i < N; ++i) {
        float s = osc.nextMorphed (0.0f, 0.5f, 0.0f, 1.0f);
        if ((prev < 0) != (s < 0)) ++zc;
        prev = s; sq += (double) s * s;
    }
    std::printf ("[bare osc @196Hz %s] zc/0.2s=%d => ~%.1f Hz  rms=%.4f  frames=%d\n",
                 label, zc, zc / 2.0 / 0.2, std::sqrt (sq / N), wt ? wt->numFrames() : 0);
}

int main()
{
    const double sr = 48000.0;
    const int    bs = 512;

    bareOscillatorTest (sr, nullptr, "analytic");
    bareOscillatorTest (sr, &Wavetable::builtInDefault(), "builtin-WT");

    VaneProcessor proc;
    std::printf ("[tuning] noteToHz(55,ch1)=%.3f  noteToHz(55,ch2)=%.3f  noteToHz(69,ch1)=%.3f (expect 196,196,440)\n",
                 proc.tuning.noteToHz (55, 1), proc.tuning.noteToHz (55, 2), proc.tuning.noteToHz (69, 1));
    proc.setRateAndBufferSizeDetails (sr, bs);
    proc.prepareToPlay (sr, bs);

    // Isolation switch: VANE_PROBE_VELVCA=1 → steady velocity-driven VCA, no
    // breath/pressure mod, to test the oscillator pitch in isolation.
    const bool velVca = std::getenv("VELVCA") != nullptr;
    if (velVca)
        if (auto* p = proc.apvts.getParameter("velocityMix"))
            p->setValueNotifyingHost (1.0f);

    juce::AudioBuffer<float> buf (2, bs);

    const int   note = 55;            // G3 ~196 Hz
    const int   chan = 2;             // MPE member channel
    const float vel  = 0.8f;
    auto pv = [&](const char* id){ auto* p = proc.apvts.getRawParameterValue(id); return p ? p->load() : -999.0f; };
    std::printf ("outputLevel=%.3f veloMix=%.3f | morph=%.3f pw=%.3f fold=%.3f inharm=%.3f sync=%.3f detune=%.3f noise=%.3f\n",
                 pv("outputLevel"), pv("velocityMix"), pv("oscMorphPos"), pv("oscPW"),
                 pv("oscFold"), pv("oscInharm"), pv("oscSync"), pv("oscDetune"), pv("noiseBlend"));

    // Accumulate ~1.5 s of sustained output (after a short settle) into one vector.
    std::vector<float> out;
    const int totalBlocks = (int) (sr * 2.0 / bs);
    const int noteOnBlock  = 2;
    const int captureStart = (int) (sr * 0.5 / bs);   // skip attack/settle

    for (int b = 0; b < totalBlocks; ++b) {
        buf.clear();
        juce::MidiBuffer midi;
        if (b == noteOnBlock)
            midi.addEvent (juce::MidiMessage::noteOn (chan, note, vel), 0);
        if (!velVca) {   // breath/pressure drive the VCA (default wind mode)
            if (b >= noteOnBlock) {
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 2, 110), 0);
                midi.addEvent (juce::MidiMessage::channelPressureChange (chan, 100), 0);
            }
        }
        proc.processBlock (buf, midi);
        if (b == noteOnBlock || b == captureStart || b == totalBlocks/2) {
            double r = 0; for (int i=0;i<bs;++i){ float v=buf.getReadPointer(0)[i]; r+=(double)v*v; }
            std::printf ("  block %d rms=%.5f\n", b, std::sqrt(r/bs));
        }
        if (b >= captureStart)
            for (int i = 0; i < bs; ++i) out.push_back (buf.getReadPointer(0)[i]);
    }

    // Print zero-crossings of a 0.1s window to estimate the true fundamental.
    {
        int zc = 0; int n0 = (int)(sr*0.5), n1 = (int)(sr*0.6);
        for (int i=n0+1;i<n1 && i<(int)out.size();++i)
            if ((out[i-1]<0)!=(out[i]<0)) ++zc;
        std::printf ("zero-crossings in 0.1s = %d  => ~%.1f Hz\n", zc, zc/2.0/0.1);
    }

    // RMS + peak + DC
    double sum = 0, sq = 0, peak = 0;
    for (float v : out) { sum += v; sq += (double) v * v; peak = std::max (peak, (double) std::abs (v)); }
    const double mean = sum / out.size();
    const double rms  = std::sqrt (sq / out.size());
    std::printf ("samples=%zu  peak=%.4f  rms=%.4f  DC(mean)=%.5f\n", out.size(), peak, rms, mean);

    // Spectrum: 16384-pt FFT on a Hann-windowed chunk
    const int M = 16384;
    if ((int) out.size() >= M) {
        juce::dsp::FFT fft (14);
        std::vector<float> w (2 * M, 0.0f);
        for (int i = 0; i < M; ++i)
            w[i] = out[i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (M - 1)));
        fft.performFrequencyOnlyForwardTransform (w.data());
        // band energy split + top bins
        double lo = 0, mid = 0, hi = 0;
        std::vector<std::pair<double,double>> bins;     // (freq, mag)
        for (int k = 1; k <= M/2; ++k) {
            double f = (double) k * sr / M;
            double m = w[k];
            if (f < 60)        lo  += m*m;
            else if (f < 2000) mid += m*m;
            else               hi  += m*m;
            bins.push_back ({f, m});
        }
        double tot = lo + mid + hi + 1e-12;
        std::printf ("band energy  <60Hz=%.3f  60-2k=%.3f  >2k=%.3f\n", lo/tot, mid/tot, hi/tot);
        std::sort (bins.begin(), bins.end(), [](auto&a, auto&b){ return a.second > b.second; });
        std::printf ("top bins: ");
        for (int i = 0; i < 8; ++i) std::printf ("%.1fHz ", bins[i].first);
        std::printf ("\n");
    }
    return 0;
}
