/*
    BreathEnvelope unit checks — standalone, no build-system entanglement:

        clang++ -std=c++17 -O1 -o /tmp/bt Source/Synth/BreathEnvelope.test.cpp && /tmp/bt

    Header-only class, so this needs no JUCE and no CMake target. Kept next to
    the header rather than in Tools/ because it tests one class, not the plugin.
*/
#include "BreathEnvelope.h"
#include <cstdio>
int fails = 0;
void ck(bool c, const char* m){ printf("  %s %s\n", c?"ok  ":"FAIL", m); if(!c) ++fails; }
int main(){
  BreathEnvelope::Params p;             // defaults: A35 D120 S0.80 R180
  const double SR = 48000.0; const int BLK = 512;
  auto run=[&](BreathEnvelope& e,int blocks){ float v=0; for(int i=0;i<blocks;i++) v=e.advance(BLK,p); return v; };

  printf("a plain note now speaks\n");
  BreathEnvelope e; e.prepare(SR);
  ck(e.current()==0.0f && !e.isActive(), "silent before any note");
  e.noteOn(0.8f,false);
  float after1 = run(e,1);
  ck(after1 > 0.0f, "breath rises on note-on with no controller at all");
  float settled = run(e,40);            // ~430 ms: past attack+decay
  ck(settled > 0.5f && settled <= 0.8f, "settles to sustain, scaled by velocity");

  printf("\nvelocity is the dynamics\n");
  BreathEnvelope soft; soft.prepare(SR); soft.noteOn(0.25f,false); float s=run(soft,40);
  ck(s < settled, "a softer note sustains lower");

  printf("\nharder notes speak sooner\n");
  BreathEnvelope hard; hard.prepare(SR); hard.noteOn(1.0f,false);
  BreathEnvelope quiet; quiet.prepare(SR); quiet.noteOn(0.1f,false);
  float h=hard.advance(BLK,p)/1.0f, q=quiet.advance(BLK,p)/0.1f;   // normalised by peak
  ck(h > q, "at equal time, the loud note is further through its attack");

  printf("\nMELISMA — several notes inside ONE breath\n");
  BreathEnvelope m; m.prepare(SR); m.noteOn(0.7f,false); float mid=run(m,30);
  m.noteOn(0.7f,true);                        // legato: new pitch, same breath
  float justAfter = m.advance(BLK,p);
  ck(justAfter > mid*0.9f, "a legato note does NOT drop back to zero — no re-attack");
  BreathEnvelope r; r.prepare(SR); r.noteOn(0.7f,false); run(r,30);
  r.noteOn(0.7f,false);                       // same note, NOT legato
  float reattack = r.advance(BLK,p);
  // One 10.7 ms block is already ~half of a velocity-shortened 22 ms attack,
  // so "near zero" is the wrong bar. What matters is the CONTRAST: a
  // re-articulation dips below the level it was holding, a slur does not.
  ck(reattack < mid,      "a non-legato note DOES re-attack — drops below its sustain");
  ck(reattack < justAfter, "and it lands lower than the legato case at the same moment");

  // The synth allocates a FRESH voice for a mono legato note, so the melisma
  // above only holds within one voice. Across the handoff the envelope starts
  // Idle and needs the level the previous voice was publishing — same shared
  // value the bore and VCA hand off on.
  BreathEnvelope fresh; fresh.prepare(SR);
  fresh.noteOn(0.7f, true, mid);              // new voice, mid-phrase
  float handed = fresh.advance(BLK,p);
  ck(handed > mid*0.9f, "a fresh voice inheriting the breath does NOT re-attack");
  BreathEnvelope cold; cold.prepare(SR);
  cold.noteOn(0.7f, true, 0.0f);              // legato claimed, nothing in the air
  ck(cold.advance(BLK,p) < handed, "legato with no inherited breath still attacks");

  printf("\nrelease\n");
  BreathEnvelope d; d.prepare(SR); d.noteOn(0.9f,false); run(d,40); d.noteOff();
  float mid_r = run(d,8); ck(mid_r>0.0f && mid_r<0.9f, "decays after note-off, not instant");
  run(d,40); ck(!d.isActive() && d.current()==0.0f, "reaches silence and goes idle");

  printf("\n%s\n", fails? "FAILURES" : "all ok");
  return fails?1:0;
}
