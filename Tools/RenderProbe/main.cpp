// Offline render probe: instantiate VaneProcessor, play one sustained note on
// the DEFAULT patch, dump RMS + dominant-frequency analysis of the output.
// Purpose: reproduce the "sub-bass / no sustained tone" report headlessly.
#include "../../Source/PluginProcessor.h"
#include "../../Source/Synth/Oscillator.h"
#include "../../Source/Synth/TransientLibrary.h"
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

    const bool transientTest = std::getenv("TRANSIENT") != nullptr;

    juce::AudioBuffer<float> buf (2, bs);

    const int   note = std::getenv("NOTE") ? atoi(std::getenv("NOTE")) : 55;
    const int   chan = 2;             // MPE member channel
    const float vel  = std::getenv("VEL") ? (float)atof(std::getenv("VEL")) : 0.8f;
    auto pv = [&](const char* id){ auto* p = proc.apvts.getRawParameterValue(id); return p ? p->load() : -999.0f; };
    std::printf ("outputLevel=%.3f veloMix=%.3f | morph=%.3f pw=%.3f fold=%.3f inharm=%.3f sync=%.3f detune=%.3f noise=%.3f\n",
                 pv("outputLevel"), pv("velocityMix"), pv("oscMorphPos"), pv("oscPW"),
                 pv("oscFold"), pv("oscInharm"), pv("oscSync"), pv("oscDetune"), pv("noiseBlend"));

    // Transient test: choose sample 1, gain 2.0, trigger Always; measure the
    // attack-window energy (should spike if the transient fires).
    if (transientTest) {
        auto setNorm = [&](const char* id, float v){ if (auto* p = proc.apvts.getParameter(id)) p->setValueNotifyingHost(v); };
        int pick = std::getenv("TCHOICE") ? atoi(std::getenv("TCHOICE")) : 1;
        if (auto* cp = dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("transientChoice"))) {
            std::printf ("[transient] choice list (%d): ", cp->choices.size());
            for (auto& c : cp->choices) std::printf ("'%s' ", c.toRawUTF8());
            std::printf ("\n");
            *cp = pick;   // pick sample (env TCHOICE)
        }
        setNorm ("transientGain", 1.0f);      // normalised 1.0 → max (gain 2.0)
        // Trigger mode: default Always; TRIG=1 → Non-legato (Sylphyo default).
        const float trigMode = std::getenv("TRIG") ? (float)atoi(std::getenv("TRIG")) : 0.0f;
        setNorm ("transientTrigger", trigMode);   // 0=Always 1=Non-legato (normalised: 0 or 1)
        // BREATH=1 → wind-controller realism: veloMix=0, breath drives the VCA
        // (ramps from 0 at note-on).  Otherwise steady velocity VCA.
        const bool breathMode = std::getenv("BREATH") != nullptr;
        // ISOLATE: silence the note (no VCA, no breath, no dynamics gating) so the
        // output is ONLY the transient — lets us measure its pitch from the resonator.
        const bool isolate = std::getenv("ISOLATE") != nullptr;
        setNorm ("velocityMix", (breathMode || isolate) ? 0.0f : 1.0f);
        // Filter route + variation (normalised: bool 0/1, var 0..1).
        setNorm ("transientFilter",    std::getenv("TFILT") ? (float)atof(std::getenv("TFILT")) : 1.0f);
        setNorm ("transientVariation", std::getenv("TVAR")  ? (float)atof(std::getenv("TVAR"))  : 0.0f);
        setNorm ("transientDynamics",  isolate ? 0.0f : (std::getenv("TDYN") ? (float)atof(std::getenv("TDYN")) : 0.0f));
        setNorm ("transientResonate",  std::getenv("TRESO") ? (float)atof(std::getenv("TRESO")) : 0.0f);
        setNorm ("transientDamping",   std::getenv("TDAMP") ? (float)atof(std::getenv("TDAMP")) : 0.5f);
        // Morph is normalised on a skewed range; pass 0 unless asked.
        if (auto* mp = dynamic_cast<juce::AudioParameterFloat*>(proc.apvts.getParameter("transientMorph")))
            *mp = std::getenv("TMORPH") ? (float)atof(std::getenv("TMORPH")) : 0.0f;
        std::printf ("[transient] filter=%.0f var=%.2f dyn=%.2f reso=%.2f damp=%.2f morph=%.1fms\n",
                     pv("transientFilter"), pv("transientVariation"), pv("transientDynamics"),
                     pv("transientResonate"), pv("transientDamping"), pv("transientMorph"));
        if (std::getenv("MONO")) { setNorm ("monoMode", 1.0f); std::printf ("[transient] MONO legato mode\n"); }
        std::printf ("[transient] mode: %s VCA, trigger=%s\n",
                     breathMode?"breath":"velocity", trigMode>0.5f?"Non-legato":"Always");
        std::printf ("[transient] gain=%.2f decay=%.1f trig=%.0f choice=%.0f\n",
                     pv("transientGain"), pv("transientDecay"), pv("transientTrigger"), pv("transientChoice"));
        // Inspect the library samples directly (own instance, same ctor).
        TransientLibrary lib;
        std::printf ("[transient] library entries=%d\n", lib.numEntries());
        for (int i = 1; i < lib.numEntries(); ++i)
            if (auto* s = lib.getSample(i)) {
                const float* d = s->buffer.getReadPointer(0);
                int ns = s->buffer.getNumSamples();
                float pk=0,sq=0; int firstLoud=-1;
                for (int j=0;j<ns;++j){ float v=std::abs(d[j]); pk=std::max(pk,v); sq+=(double)v*v; if(firstLoud<0&&v>0.1f)firstLoud=j; }
                std::printf ("    [%d] '%s' n=%d sr=%.0f nativeHz=%.1f peak=%.4f rms=%.4f firstLoud@%d (%.1fms)\n",
                             i, s->name.toRawUTF8(), ns, s->sampleRate, s->nativeHz,
                             pk, std::sqrt(sq/ns), firstLoud, firstLoud<0?-1.0:firstLoud*1000.0/s->sampleRate);
            }
            else std::printf ("    [%d] NULL\n", i);
    }

    // Accumulate ~1.5 s of sustained output (after a short settle) into one vector.
    std::vector<float> out;
    const int totalBlocks = (int) (sr * 2.0 / bs);
    const int noteOnBlock  = 2;
    const int captureStart = (int) (sr * 0.5 / bs);   // skip attack/settle

    std::vector<float> fromNoteOn;   // full capture from the note-on (for transient attack analysis)
    for (int b = 0; b < totalBlocks; ++b) {
        buf.clear();
        juce::MidiBuffer midi;
        if (b == noteOnBlock)
            midi.addEvent (juce::MidiMessage::noteOn (chan, note, vel), 0);
        // MONO legato test: hold breath, then play a 2nd note legato at ~0.25s.
        const bool monoTest = transientTest && std::getenv("MONO");
        const int legatoBlock = (int)(sr * 0.25 / bs);
        if (monoTest) {
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 2, 110), 0);
            midi.addEvent (juce::MidiMessage::channelPressureChange (chan, 100), 0);
            if (b == legatoBlock) {
                midi.addEvent (juce::MidiMessage::noteOn (chan, note + 5, vel), 0);  // legato 2nd note
                std::printf ("[transient] 2nd (legato) note at block %d (~250ms)\n", b);
            }
        }
        // Drive breath when: default (non-transient) wind mode, OR transient BREATH mode.
        const bool driveVca = (!velVca && !transientTest)
                            || (transientTest && std::getenv("BREATH"));
        if (driveVca && !monoTest) {   // breath/pressure drive the VCA (wind mode)
            const int bval = std::getenv("BVAL") ? atoi(std::getenv("BVAL")) : 110;
            if (b >= noteOnBlock) {
                midi.addEvent (juce::MidiMessage::controllerEvent (1, 2, bval), 0);
                midi.addEvent (juce::MidiMessage::channelPressureChange (chan, bval), 0);
            }
        }
        proc.processBlock (buf, midi);
        if (b >= noteOnBlock)
            for (int i = 0; i < bs; ++i) fromNoteOn.push_back (buf.getReadPointer(0)[i]);
        if (b == noteOnBlock || b == captureStart || b == totalBlocks/2) {
            double r = 0; for (int i=0;i<bs;++i){ float v=buf.getReadPointer(0)[i]; r+=(double)v*v; }
            std::printf ("  block %d rms=%.5f\n", b, std::sqrt(r/bs));
        }
        if (b >= captureStart)
            for (int i = 0; i < bs; ++i) out.push_back (buf.getReadPointer(0)[i]);
    }

    if (transientTest) {
        // Attack window = first 60 ms after note-on; sustain = 300-500 ms.
        auto winPeakRms = [&](double t0, double t1){
            int a=(int)(sr*t0), z=(int)(sr*t1); double pk=0,sq=0; int n=0;
            for (int i=a;i<z && i<(int)fromNoteOn.size();++i){ float v=fromNoteOn[i]; pk=std::max(pk,(double)std::abs(v)); sq+=(double)v*v; ++n; }
            return std::pair<double,double>(pk, std::sqrt(sq/std::max(1,n)));
        };
        if (std::getenv("MONO")) {
            // Measure transient spike around the 2nd (legato) note at 250ms.
            auto before = winPeakRms(0.18, 0.245);
            auto onLeg  = winPeakRms(0.25, 0.31);
            std::printf ("[transient] MONO: before-legato(180-245ms) peak=%.4f rms=%.4f | on-legato(250-310ms) peak=%.4f rms=%.4f | ratio=%.2fx\n",
                         before.first, before.second, onLeg.first, onLeg.second, onLeg.second/(before.second+1e-9));
        } else {
            auto atk = winPeakRms(0.0, 0.06);
            auto sus = winPeakRms(0.30, 0.50);
            // Spectral centroid of the attack window (filter route should lower it).
            int a0=0, a1=(int)(sr*0.06); std::vector<float> aw(fromNoteOn.begin()+a0, fromNoteOn.begin()+std::min(a1,(int)fromNoteOn.size()));
            for (size_t i=0;i<aw.size();++i) aw[i]*=0.5f-0.5f*std::cos(juce::MathConstants<float>::twoPi*i/(aw.size()-1));
            juce::dsp::FFT f2(12); std::vector<float> sp(8192,0.f); std::copy(aw.begin(),aw.begin()+std::min((size_t)4096,aw.size()),sp.begin());
            f2.performFrequencyOnlyForwardTransform(sp.data());
            double num=0,den=0; for(int k=1;k<2048;++k){double fr=(double)k*sr/4096; num+=fr*sp[k]; den+=sp[k];}
            std::printf ("[transient] attack(0-60ms) peak=%.4f rms=%.4f | sustain peak=%.4f rms=%.4f | attack/sustain=%.2fx | attackCentroid=%.0fHz\n",
                         atk.first, atk.second, sus.first, sus.second, atk.second/(sus.second+1e-9), num/(den+1e-9));
            // Periodicity of the transient (0-150ms) at the note period — rises with Resonate.
            double noteHz = 440.0*std::pow(2.0,(note-69)/12.0); int D=(int)(sr/noteHz);
            int w0=0,w1=std::min((int)(sr*0.15),(int)fromNoteOn.size());
            double r0=0,rD=0; for(int i=w0;i<w1-D;++i){ r0+=fromNoteOn[i]*fromNoteOn[i]; rD+=fromNoteOn[i]*fromNoteOn[i+D]; }
            std::printf ("[transient] noteHz=%.0f period-autocorr=%.3f (rises with Resonate = acquiring pitch)\n",
                         noteHz, rD/(r0+1e-9));
        }
        return 0;
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
