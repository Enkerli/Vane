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

    // Rotating-chord test: chord mode with a known sequence; verify the harmony
    // interval appears in the spectrum, and that it ROTATES on successive notes.
    if (std::getenv("CHORD")) {
        auto setNorm=[&](const char* id,float v){ if(auto* p=proc.apvts.getParameter(id)) p->setValueNotifyingHost(v); };
        setNorm("monoMode",1.0f); setNorm("velocityMix",1.0f); setNorm("unisonWidth",0.0f);
        if(auto* cp=dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("unisonVoices"))) *cp=1; // 2 voices: melody+1 harmony
        if(auto* mp=dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("unisonMode"))) *mp=1;   // Chord
        proc.setChordSeqs("4,7,3");   // one harmony voice cycling +4, +7, +3 semitones
        const int N0=60; const double f0=440*std::pow(2.0,(N0-69)/12.0);
        auto energyAt=[&](std::vector<float>& o,double hz){ const int M=16384; std::vector<float> w(2*M,0.f);
            for(int i=0;i<M&&i<(int)o.size();++i) w[i]=o[(size_t)i]*(0.5f-0.5f*std::cos(juce::MathConstants<float>::twoPi*i/(M-1)));
            juce::dsp::FFT f(14); f.performFrequencyOnlyForwardTransform(w.data());
            int k=(int)std::round(hz*M/sr); double e=0; for(int d=-2;d<=2;++d) e+=w[(size_t)(k+d)]; return e; };
        // Three legato notes (breath held throughout) — rotation: note0=non-legato→step0(+4),
        // note1→step1(+7), note2→step2(+3).  Measure each note's dominant harmony interval.
        for(int note_i=0;note_i<3;++note_i){ std::vector<float> o;
            for(int b=0;b<(int)(sr*0.45/bs);++b){ buf.clear(); juce::MidiBuffer m;
                m.addEvent(juce::MidiMessage::controllerEvent(1,2,110),0); m.addEvent(juce::MidiMessage::channelPressureChange(2,100),0);
                if(b==1) m.addEvent(juce::MidiMessage::noteOn(2,N0,vel),0);   // legato (breath already on)
                proc.processBlock(buf,m);
                if(b>=(int)(sr*0.15/bs)) for(int i=0;i<bs;++i) o.push_back(buf.getReadPointer(0)[i]); }
            double e4=energyAt(o,f0*std::pow(2.0,4/12.0)), e7=energyAt(o,f0*std::pow(2.0,7/12.0)), e3=energyAt(o,f0*std::pow(2.0,3/12.0));
            const char* iv = (e4>e7&&e4>e3)?"+4":(e7>e3?"+7":"+3");
            std::printf("[chord] note %d harmony peak → %s  (e+4=%.1f e+7=%.1f e+3=%.1f)\n", note_i, iv, e4, e7, e3);
        }
        return 0;
    }

    // Mod-of-mod test: Breath→Cutoff on slot 23, optionally SCALED by Keytrack.
    // With scaling, a low note's breath→cutoff is reduced → darker (lower centroid).
    if (std::getenv("MODMOD")) {
        auto setNorm=[&](const char* id,float v){ if(auto* p=proc.apvts.getParameter(id)) p->setValueNotifyingHost(v); };
        for(int n=0;n<24;++n) if(auto* s=dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("modSlot"+juce::String(n)+"_src"))) *s=0; // clear all routes
        // slot22: Breath→VCA (so the note sounds); slot23: Breath→Cutoff scaled by Velocity.
        if(auto* s=dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("modSlot22_src"))) *s=1;
        if(auto* s=dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("modSlot22_dst"))) *s=0;
        if(auto* a=dynamic_cast<juce::AudioParameterFloat*>(proc.apvts.getParameter("modSlot22_amt"))) *a=1.0f;
        if(auto* sp=dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("modSlot23_src"))) *sp=1;  // Breath
        if(auto* dp=dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("modSlot23_dst"))) *dp=1;  // Cutoff
        if(auto* ap=dynamic_cast<juce::AudioParameterFloat*>(proc.apvts.getParameter("modSlot23_amt"))) *ap=0.6f;
        if(auto* mp=dynamic_cast<juce::AudioParameterFloat*>(proc.apvts.getParameter("oscMorphPos"))) *mp=1.0f;  // saw = rich
        int scale = std::getenv("SCALESRC") ? atoi(std::getenv("SCALESRC")) : 0;   // 0=off, 6=Velocity
        if(auto* cp=dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("modSlot23_scale"))) *cp=scale;
        setNorm("velocityMix", 0.0f);   // velocity does NOT open the VCA (breath does) — isolate the scaling
        setNorm("filterRes", 0.0f);     // no resonant peak — cutoff change reads cleanly as centroid
        std::vector<float> o;
        for(int b=0;b<(int)(sr*1.0/bs);++b){ buf.clear(); juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::controllerEvent(1,2,127),0);   // full breath
            if(b==2) m.addEvent(juce::MidiMessage::noteOn(chan,note,vel),0);
            proc.processBlock(buf,m);
            if(b>=(int)(sr*0.4/bs)) for(int i=0;i<bs;++i) o.push_back(buf.getReadPointer(0)[i]); }
        const int M=16384; std::vector<float> w(2*M,0.f);
        for(int i=0;i<M&&i<(int)o.size();++i) w[i]=o[(size_t)i]*(0.5f-0.5f*std::cos(juce::MathConstants<float>::twoPi*i/(M-1)));
        juce::dsp::FFT f(14); f.performFrequencyOnlyForwardTransform(w.data());
        double num=0,den=0; for(int k=1;k<M/2;++k){double fr=(double)k*sr/M; num+=fr*w[(size_t)k]; den+=w[(size_t)k];}
        std::printf("[modmod] vel=%.2f scale=%-8s  centroid=%.0f Hz (cutoff tracks scale)\n", vel, scale?"Velocity":"off", num/(den+1e-9));
        return 0;
    }

    // Glide-curve test: mono legato glide note A→B over a long time with Bézier
    // mode; measure the instantaneous pitch ~30% in.  An ease-in curve should keep
    // the pitch nearer the START than a linear (identity) trajectory.
    if (std::getenv("GLIDE")) {
        auto setNorm=[&](const char* id,float v){ if(auto* p=proc.apvts.getParameter(id)) p->setValueNotifyingHost(v); };
        setNorm("monoMode", 1.0f); setNorm("velocityMix", 1.0f);
        if(auto* gc=dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("glideCurve"))) *gc=3; // Bézier
        // glide time → ~1s (normalised on a 0..2000 skewed range; set raw via the param object)
        if(auto* gp=dynamic_cast<juce::AudioParameterFloat*>(proc.apvts.getParameter("glideTime"))) *gp=1000.0f;
        if(std::getenv("EASE")) proc.setGlideAnchors({{0.5f,0.12f}});   // slow start
        else                    proc.setGlideAnchors({});               // identity (linear-in-log)
        const int A=48, B=60; const double gA=440*std::pow(2.0,(A-69)/12.0), gB=440*std::pow(2.0,(B-69)/12.0);
        std::vector<float> o;
        for(int b=0;b<(int)(sr*1.4/bs);++b){ buf.clear(); juce::MidiBuffer m;
            if(b==2) m.addEvent(juce::MidiMessage::noteOn(2,A,vel),0);
            if(b==(int)(sr*0.20/bs)) m.addEvent(juce::MidiMessage::noteOn(2,B,vel),0);  // legato glide start @0.2s
            proc.processBlock(buf,m);
            for(int i=0;i<bs;++i) o.push_back(buf.getReadPointer(0)[i]); }
        // pitch via zero-crossings in a 60ms window centred at 30% of the 1s glide (≈0.2+0.3=0.5s)
        auto pitchAt=[&](double t){ int a=(int)(sr*(t-0.03)),z=(int)(sr*(t+0.03)); int zc=0;
            for(int i=a+1;i<z&&i<(int)o.size();++i) if((o[i-1]<0)!=(o[i]<0))++zc; return zc/2.0/0.06; };
        std::printf("[glide] %s  startHz=%.0f targetHz=%.0f  pitch@30%%=%.0fHz (linear-expect~%.0f)\n",
                    std::getenv("EASE")?"ease-in":"identity", gA, gB, pitchAt(0.50),
                    std::exp2(std::log2(gA)+0.30*(std::log2(gB)-std::log2(gA))));
        return 0;
    }

    // Waveguide-mode test: enable the MiniSax reed/bore engine, hold breath
    // (CC2 + pressure), play NOTE; report RMS, the tuned pitch, and H2/H3
    // relative to H1 — the conical even-harmonic signature.  BREATH=0..127
    // sets the breath level (default 110); at low breath the reed must not
    // speak loudly (physical threshold), at high breath H2 should be strong.
    if (std::getenv("WAVEGUIDE")) {
        auto setNorm=[&](const char* id,float v){ if(auto* p=proc.apvts.getParameter(id)) p->setValueNotifyingHost(v); };
        setNorm("waveguideOn", 1.0f);
        setNorm("monoMode", 1.0f);
        const int breathCC = std::getenv("BREATH") ? atoi(std::getenv("BREATH")) : 110;
        const double f0 = proc.tuning.noteToHz(note, chan);
        std::vector<float> o;
        for (int b=0;b<(int)(sr*1.5/bs);++b){ buf.clear(); juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::controllerEvent(1,2,breathCC),0);
            m.addEvent(juce::MidiMessage::channelPressureChange(chan,breathCC),0);
            if(b==2) m.addEvent(juce::MidiMessage::noteOn(chan,note,vel),0);
            proc.processBlock(buf,m);
            if(b>=(int)(sr*0.5/bs)) for(int i=0;i<bs;++i) o.push_back(buf.getReadPointer(0)[i]); }
        double sq=0; for(float s:o) sq+=(double)s*s;
        const int M=16384; std::vector<float> w(2*M,0.f);
        for(int i=0;i<M&&i<(int)o.size();++i) w[i]=o[(size_t)i]*(0.5f-0.5f*std::cos(juce::MathConstants<float>::twoPi*i/(M-1)));
        juce::dsp::FFT f(14); f.performFrequencyOnlyForwardTransform(w.data());
        // Detect the sounded fundamental (search ±4%) and measure harmonics
        // around k×that, with the window scaled by k — fixed bins around the
        // nominal pitch under-read every harmonic once the model runs a few
        // cents off (each harmonic drifts k× as far in Hz).
        auto binPeak=[&](double lo,double hi){ int a=std::max(1,(int)std::floor(lo*M/sr)),
            z=std::min(M/2-1,(int)std::ceil(hi*M/sr)); double e=0;
            for(int k=a;k<=z;++k) e=std::max(e,(double)w[(size_t)k]); return e; };
        double fd=f0, best=0; for(double fr=f0*0.96;fr<=f0*1.04;fr+=0.25){
            double e=binPeak(fr-1.5,fr+1.5); if(e>best){best=e;fd=fr;} }
        auto hK=[&](int k){ return binPeak(fd*k-3.0*k, fd*k+3.0*k); };
        double h1=hK(1), h2=hK(2), h3=hK(3);
        std::printf("[waveguide] note=%d f0=%.1fHz sounded=%.1fHz (%+.0fc) breathCC=%d rms=%.4f  H2/H1=%+.1fdB H3/H1=%+.1fdB\n",
                    note, f0, fd, 1200.0*std::log2(fd/f0), breathCC,
                    std::sqrt(sq/(double)std::max<size_t>(1,o.size())),
                    20.0*std::log10(h2/(h1+1e-12)), 20.0*std::log10(h3/(h1+1e-12)));
        return 0;
    }

    // Legato-continuity test: mono mode, sustained breath, slur note A→B; measure
    // the worst sample-to-sample jump at the boundary vs the steady-state slope.
    // A clean handoff → boundary jump ≈ steady slope (ratio ~1); a click → ratio≫1.
    // WG=1 runs the same measurement with the waveguide (MiniSax) mode enabled.
    if (std::getenv("LEGATO")) {
        auto setNorm=[&](const char* id,float v){ if(auto* p=proc.apvts.getParameter(id)) p->setValueNotifyingHost(v); };
        SynthVoice::s_unisonLegatoFix = !std::getenv("NOFIX");
        setNorm("monoMode", 1.0f);
        if (std::getenv("WG")) setNorm("waveguideOn", 1.0f);
        if (std::getenv("UVOX")) if(auto* cp=dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("unisonVoices"))) *cp=atoi(std::getenv("UVOX"));
        setNorm("unisonWidth", 1.0f);
        const int legB = (int)(sr*0.30/bs);
        std::vector<float> L, R;
        for (int b=0;b<(int)(sr*0.6/bs);++b){ buf.clear(); juce::MidiBuffer m;
            // breath CC2 + pressure held the whole time → continuous VCA (legato).
            m.addEvent(juce::MidiMessage::controllerEvent(1,2,110),0);
            m.addEvent(juce::MidiMessage::channelPressureChange(chan,100),0);
            if(b==2)    m.addEvent(juce::MidiMessage::noteOn(chan,note,vel),0);
            if(b==legB) m.addEvent(juce::MidiMessage::noteOn(chan,note+5,vel),0);   // legato slur
            proc.processBlock(buf,m);
            for(int i=0;i<bs;++i){ L.push_back(buf.getReadPointer(0)[i]); R.push_back(buf.getReadPointer(1)[i]); } }
        auto maxDelta=[&](std::vector<float>& x,double t0,double t1){ int a=(int)(sr*t0),z=(int)(sr*t1); double mx=0;
            for(int i=a+1;i<z&&i<(int)x.size();++i) mx=std::max(mx,(double)std::abs(x[i]-x[i-1])); return mx; };
        double bndL=maxDelta(L,0.298,0.306), stdL=maxDelta(L,0.20,0.28);
        double bndR=maxDelta(R,0.298,0.306), stdR=maxDelta(R,0.20,0.28);
        std::printf("[legato] boundary/steady Δ  L=%.2fx  R=%.2fx  (1≈clean handoff, ≫1 = click)\n",
                    bndL/(stdL+1e-9), bndR/(stdR+1e-9));
        // Amplitude continuity through the slur: with breath held, a legato
        // transition must not dip toward silence and swell back (the re-attack
        // valley).  Report the minimum 10 ms RMS across the boundary region
        // relative to the pre-slur steady RMS — 1 ≈ seamless, ≪1 = dropout.
        auto rmsWin=[&](std::vector<float>& x,double t0,double t1){ int a=(int)(sr*t0),z=std::min((int)(sr*t1),(int)x.size());
            double s=0; int n=std::max(1,z-a); for(int i=a;i<z;++i) s+=(double)x[i]*x[i]; return std::sqrt(s/n); };
        double steady=rmsWin(L,0.20,0.28), trough=1e9;
        for(double t=0.28;t<0.45;t+=0.01) trough=std::min(trough,rmsWin(L,t,t+0.01));
        std::printf("[legato] slur envelope trough/steady = %.2f  (1≈seamless, ≪1 = dip-and-swell)\n",
                    trough/(steady+1e-9));
        return 0;
    }

    // Keytrack test: route Keytrack→Cutoff on a spare slot, play NOTE, measure the
    // output spectral centroid (higher note should be brighter when keytrack works).
    if (std::getenv("KEYTRACK")) {
        auto setNorm=[&](const char* id,float v){ if(auto* p=proc.apvts.getParameter(id)) p->setValueNotifyingHost(v); };
        // slot 23: src = Keytrack (choice 15), dst = Cutoff (choice 1), amount +1.
        if (auto* sp=dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("modSlot23_src"))) *sp=15;
        if (auto* dp=dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("modSlot23_dst"))) *dp=1;
        setNorm("modSlot23_amt", 1.0f);   // normalised → +1
        setNorm("velocityMix", 1.0f);
        std::vector<float> o;
        for (int b=0;b<(int)(sr*1.2/bs);++b){ buf.clear(); juce::MidiBuffer m;
            if(b==2) m.addEvent(juce::MidiMessage::noteOn(chan,note,vel),0);
            proc.processBlock(buf,m);
            if(b>=(int)(sr*0.4/bs)) for(int i=0;i<bs;++i) o.push_back(buf.getReadPointer(0)[i]); }
        const int M=16384; std::vector<float> w(2*M, 0.f);
        for(int i=0;i<M&&i<(int)o.size();++i) w[i]=o[(size_t)i]*(0.5f-0.5f*std::cos(juce::MathConstants<float>::twoPi*i/(M-1)));
        juce::dsp::FFT f(14); f.performFrequencyOnlyForwardTransform(w.data());
        double num=0,den=0; for(int k=1;k<M/2;++k){double fr=(double)k*sr/M; num+=fr*w[(size_t)k]; den+=w[(size_t)k];}
        std::printf("[keytrack] note=%d centroid=%.0f Hz\n", note, num/(den+1e-9));
        return 0;
    }

    // Stereo-unison test: set voices, render with steady VCA, capture L+R, report
    // per-channel RMS (level parity) and L/R correlation (1 = mono, <1 = width).
    if (std::getenv("UNISON")) {
        auto setNorm=[&](const char* id,float v){ if(auto* p=proc.apvts.getParameter(id)) p->setValueNotifyingHost(v); };
        if (auto* cp=dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("unisonVoices")))
            *cp = std::getenv("UVOX") ? atoi(std::getenv("UVOX")) : 4;   // choice index
        setNorm("unisonDetune", std::getenv("UDET")?(float)atof(std::getenv("UDET")):0.28f); // ~14c
        setNorm("unisonWidth",  std::getenv("UWID")?(float)atof(std::getenv("UWID")):0.7f);
        setNorm("velocityMix", 1.0f);
        std::printf("[unison] voices=%.0f(idx) detune=%.1fc width=%.2f\n",
                    pv("unisonVoices"), pv("unisonDetune"), pv("unisonWidth"));
        std::vector<float> L, R;
        for (int b=0;b<(int)(sr*1.5/bs);++b){ buf.clear(); juce::MidiBuffer m;
            if(b==2) m.addEvent(juce::MidiMessage::noteOn(chan,note,vel),0);
            proc.processBlock(buf,m);
            if(b>=(int)(sr*0.4/bs)) for(int i=0;i<bs;++i){ L.push_back(buf.getReadPointer(0)[i]); R.push_back(buf.getReadPointer(1)[i]); } }
        double sl=0,sr2=0,sx=0; for(size_t i=0;i<L.size();++i){ sl+=(double)L[i]*L[i]; sr2+=(double)R[i]*R[i]; sx+=(double)L[i]*R[i]; }
        double rmsL=std::sqrt(sl/L.size()), rmsR=std::sqrt(sr2/L.size());
        double corr=sx/(std::sqrt(sl*sr2)+1e-12);
        std::printf("[unison] rmsL=%.4f rmsR=%.4f  L/R corr=%.3f (1=mono, lower=wider)\n", rmsL, rmsR, corr);
        return 0;
    }

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
