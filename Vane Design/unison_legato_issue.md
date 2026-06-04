# Unison ✕ Legato — continuity gap

**Status:** RESOLVED (2026-06).  The cheap fixes below (gate the spread reset;
seed `filterR` from the L state) were tried first and measured *insufficient* —
mono legato is **cross-voice** (a new voice is allocated), so the detuned stack
and the right filter need the same cross-voice handoff machinery as the centre
osc.  Implemented: publish/restore the unison osc phases + `filterR` integrator
state across the legato boundary (processor `lastUnisonPhase[]`, `lastFilterRS1/2`
→ `SynthVoice::setUnisonHandoff`).  RenderProbe boundary/steady-Δ now matches the
single-voice baseline (2 voices: R 11.99× → 1.16×; 6 voices: 1.02×).

---

## Original analysis (kept for context)
**Reported:** legato smoothness is lost as soon as stereo unison is engaged — even
with 2 voices (one hard-panned per channel, so the voices don't interact). So the
problem is not voice interaction; it's that the unison machinery sits *outside*
the mono-legato continuity handoff.

---

## Background — how mono legato stays click-free (single-osc path)

When a new note starts while the previous one is still sounding (continuous
breath → `isLegato`), `SynthVoice::noteStarted()` performs a handoff so nothing
steps discontinuously:

- **Oscillator phase** — `osc.reset(sharedOscPhase)` continues the phase from the
  dying voice (cross-voice, via the `sharedOscPhase` atomic published at the end
  of `renderNextBlock`).
- **Inharmonicity FM phase** — `osc.setPmPhase(sharedPmPhase)`.
- **Filter state** — primes `filter` coefficients to the old voice's last cutoff
  /res, then restores the SVF integrators `s1/s2` (`sharedFilterS1/S2`,
  `sharedCutoffHz`).
- **VCA / pitch** — `initVCA` inherited, `smoothedHz` glides from `prevHz`.
- `legatoHandoffPending` tells the first render block to snap timbre smoothers to
  their continuous targets rather than ramp from stale per-voice state.

All of this targets exactly **one oscillator (`osc`) and one filter (`filter`)**.

---

## What unison added (and why it breaks legato)

`feat(dsp): stereo unison` (commit `9f45d79`) introduced, per voice:

- `unisonOscs[5]` — the extra detuned oscillators (voice 0 is `osc`).
- `filterR` — a second SVF so each channel is filtered independently → true stereo.

Two concrete continuity gaps:

### 1. Unison oscillators are phase-reset on **every** note-on, including legato
`noteStarted()` ends with an **unconditional** block (SynthVoice.cpp ~L382):

```cpp
// Spread the unison oscillators' phases around the centre osc …
float baseP = osc.getPhase();
for (size_t k = 0; k < unisonOscs.size(); ++k) {
    float p = baseP + (k+1)/(float)kMaxUnison;
    p -= std::floor(p);
    unisonOscs[k].reset(p);   // ← hard phase jump, even on a legato note
}
```

On a legato note the centre `osc` is continued (`osc.reset(sharedOscPhase)`), but
**every unison osc is yanked to a fresh spread phase** → a step discontinuity in
the detuned voices = the audible loss of smoothness. With 2 voices this is just
`unisonOscs[0]` jumping, but it's enough.

The spread reset is correct and *wanted* for a **non-legato** attack (so the stack
doesn't start phase-coherent and comb/flam). It must simply not fire on legato.

### 2. `filterR` has no legato state transfer
The handoff restores only `filter` (L). `filterR` is `prepare()`d and given
`setResonance` per block, but its integrator state is never primed/restored on a
legato note → the **right channel filter clicks** at the boundary. With width up
and 2 voices (one per channel), the right-hand note steps.

### 3. No cross-voice continuity for the unison oscs (deeper)
Even ignoring (1), mono legato can hand off to a *different* voice object. Only
`osc`'s phase/pm-phase cross the boundary (`sharedOscPhase`/`sharedPmPhase`). The
unison oscs' phases are not published/restored, so a cross-voice legato change
can't keep them continuous regardless. (Within-voice legato is the common case
and is fully covered once (1) is fixed.)

---

## Fix sketch (for when we act)

1. **Gate the spread reset on `!isLegato`.** Move the unison phase-spread block
   inside the non-legato path so legato notes leave the unison oscs running. This
   alone should restore most of the smoothness for within-voice legato.
2. **Hand off `filterR` state on legato.** Either (a) add `sharedFilterRS1/S2`
   atomics mirroring the L pair and restore them the same way, or (b) since both
   channels track the same cutoff, prime `filterR` from the same
   `sharedFilterS1/S2` (cheap, slightly approximate — the R signal differs only by
   the detune/pan mix, so the L state is a good seed).
3. **(Optional, deeper) cross-voice unison phase continuity** — publish/restore
   the unison osc phases (e.g. a small shared array) so a voice-stealing legato in
   mono mode also keeps the detuned stack continuous. Lower priority; only matters
   if (1)+(2) don't fully close it in practice.

## Verification plan
- RenderProbe: drive a mono-legato note change with unison on (2 voices, width 1)
  and measure the sample-to-sample discontinuity (max |Δ| at the note boundary)
  with the fix off vs on; expect it to drop to the single-osc baseline.
- A/B by ear on the Sylphyo: sustained breath, slurred intervals, 2-voice unison.

## Note on history
This is the same class of issue flagged "before the big revert" — the unison work
was the road-not-taken when wavetables took over, and its first incarnation never
got wired into the legato handoff. The current re-introduction has the same gap.
