#!/usr/bin/env node
// Regression suite for the standalone Web Audio voice (vane-dsp.wasm). Run after
// ANY change to vane-dsp.cpp — these behaviors have regressed silently before
// (pitchbend stopped affecting pitch during the amplitude-model rewrite) and the
// fix was "don't let that happen again, check before you ship."
//
// Usage:  node Tools/wasm/regression-test.mjs   (after Tools/wasm/build.sh)
//
// Each check gets a FRESH WASM instance — earlier checks leaving global state
// (breath CC, etc.) behind silently corrupted an earlier hand-rolled version of
// this exact test once; isolation is not optional here.
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const wasmPath = join(here, "vane-dsp.wasm");
const bytes = readFileSync(wasmPath);

let pass = 0, fail = 0;
function check(name, ok, detail) {
  if (ok) { pass++; console.log(`  ok  - ${name}`); }
  else { fail++; console.log(`FAIL  - ${name}${detail ? "  (" + detail + ")" : ""}`); }
}

async function fresh() {
  const { instance } = await WebAssembly.instantiate(bytes, {
    wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }),
  });
  // WASI-reactor convention: static C++ constructors (e.g. TuningClient's cents
  // table fill) only run when the embedder calls _initialize. Without it every
  // note plays at ~8 Hz. The worklet does the same.
  if (instance.exports._initialize) instance.exports._initialize();
  return instance.exports;
}
function render(e, n) { e.vane_render(n); return new Float32Array(e.memory.buffer, e.vane_buffer(), n); }
function trailingPeak(e, blocks, blockSize = 128) {
  let p = 0;
  for (let i = 0; i < blocks; i++) {
    const b = render(e, blockSize);
    if (i >= blocks - 10) for (const x of b) p = Math.max(p, Math.abs(x));
  }
  return p;
}
// Period-averaged frequency: time between the FIRST and LAST upward
// zero-crossing (linear-interpolated to sub-sample), divided by the crossing
// count. Naive crossings-per-window quantizes to the window length (±37 cents
// at 440 Hz over 106 ms) — useless for cents-level tuning checks.
function estimateHz(e, blocks, sr = 48000, blockSize = 128) {
  const samples = [];
  for (let i = 0; i < blocks; i++) samples.push(...render(e, blockSize));
  const t = [];
  for (let i = 1; i < samples.length; i++)
    if (samples[i - 1] < 0 && samples[i] >= 0)
      t.push(i - 1 + (-samples[i - 1]) / (samples[i] - samples[i - 1]));
  if (t.length < 2) return 0;
  return (t.length - 1) / ((t[t.length - 1] - t[0]) / sr);
}

// ── 1. No breath, full velocity -> silent. Velocity must not drive loudness
//      by default (VelVCA = 0, the real factory default). ──
{
  const e = await fresh();
  e.vane_init(48000); e.vane_set_param(8, 1.0);
  e.vane_note_on(69, 127, 1);
  const peak = trailingPeak(e, 115);
  check("silent at full velocity with no breath", peak < 0.001, `peak=${peak.toFixed(5)}`);
}

// ── 2. Breath (CC2) creates the dynamic envelope: rising opens it, dropping
//      closes it back down even with the note still held. ──
{
  const e = await fresh();
  e.vane_init(48000); e.vane_set_param(8, 1.0);
  e.vane_note_on(69, 100, 1);
  e.vane_set_cc(2, 0.9);
  const open = trailingPeak(e, 115);
  e.vane_set_cc(2, 0.0);
  // 250 blocks ≈ 667 ms ≈ 8× the 80 ms breath release slew — decay has settled.
  // (115 blocks measured mid-decay once slide-neutral raised the filter back to
  // its real cutoff and the tail passed more energy.)
  const closed = trailingPeak(e, 250);
  check("breath up makes it audible", open > 0.02, `peak=${open.toFixed(4)}`);
  check("breath down closes it back down (note still held)", closed < 0.005, `peak=${closed.toFixed(5)}`);
}

// ── 3. Mono legato: a new note while breath is still flowing glides — no
//      VCA dip / re-attack click. ──
{
  const e = await fresh();
  e.vane_init(48000); e.vane_set_param(8, 1.0); e.vane_set_mono(1);
  e.vane_set_cc(2, 0.8);
  e.vane_note_on(60, 100, 1);
  for (let i = 0; i < 40; i++) render(e, 32);
  e.vane_note_on(67, 100, 1);
  let dip = false;
  for (let i = 0; i < 5; i++) {
    const b = render(e, 32);
    const pk = Math.max(...[...b].map(Math.abs));
    if (i === 0 && pk < 0.01) dip = true;
  }
  check("mono legato note-change doesn't dip VCA to ~0", !dip);
}

// ── 3b. Mono "trill": hold A, tap B, release B -> reverts to A (still sounding,
//       glides back), NOT silent. Only releasing A too actually stops sound. ──
{
  const e = await fresh();
  e.vane_init(48000); e.vane_set_param(8, 1.0); e.vane_set_param(1, 18000); e.vane_set_mono(1);
  e.vane_set_cc(2, 0.8);
  e.vane_note_on(60, 100, 1);                 // hold A
  for (let i = 0; i < 10; i++) render(e, 128);
  e.vane_note_on(64, 100, 1);                 // tap B (A still held)
  for (let i = 0; i < 30; i++) render(e, 128);
  e.vane_note_off(64, 1);                     // release B -> should revert to A, not go silent
  for (let i = 0; i < 30; i++) render(e, 128);
  const hzAfterTrill = estimateHz(e, 20);
  check("releasing the top note (B) while A is held reverts pitch to A",
        Math.abs(hzAfterTrill - 261.6) < 15, `${hzAfterTrill.toFixed(1)}Hz (expect ~261.6, C4)`);
  e.vane_note_off(60, 1);                     // now release A too -> stack empty, should actually stop
  for (let i = 0; i < 150; i++) render(e, 128);
  const finalPeak = trailingPeak(e, 10);
  check("releasing the last held note (A) actually silences the voice", finalPeak < 0.01, `peak=${finalPeak.toFixed(5)}`);
}

// ── 3c. Velocity->Cutoff brightness is a real factory route but fires from raw
//       MIDI velocity independent of breath (a wind controller often sends a
//       fixed high velocity, giving every attack an unwanted brightness kick) —
//       standalone-gated (id 11), OFF by default to match the other versions. ──
{
  // Measured at C7 (2093 Hz), where the fundamental sits ABOVE the default
  // 1128 Hz cutoff — there the velocity-driven cutoff lift audibly gates the
  // note. (At A4 with slide-neutral the fundamental already passes, so the
  // kick is only a subtle harmonic change and peak barely moves.)
  async function peakAt(vel, enabled) {
    const e = await fresh();
    e.vane_init(48000); e.vane_set_param(8, 1.0);
    if (enabled) e.vane_set_param(11, 1);
    e.vane_note_on(96, vel, 1);
    e.vane_set_cc(2, 0.05); // small constant breath — isolates velocity's effect
    for (let i = 0; i < 7; i++) render(e, 128);
    const b = render(e, 128);
    let p = 0; for (const x of b) p = Math.max(p, Math.abs(x));
    return p;
  }
  const offLow = await peakAt(1, false), offHigh = await peakAt(127, false);
  const onLow  = await peakAt(1, true),  onHigh  = await peakAt(127, true);
  check("default (toggle off): velocity has no effect on brightness/loudness",
        Math.abs(offHigh / offLow - 1) < 0.05, `ratio=${(offHigh / offLow).toFixed(2)}`);
  check("toggle on: velocity restores the brightness kick",
        onHigh / onLow > 1.3, `ratio=${(onHigh / onLow).toFixed(2)}`);
}

// ── 3d. High-range audibility: with NO CC74 ever received, slide must default
//       to the MPE-neutral 0.5 — not 0, which slams the Slide->Cutoff route
//       (0.90 × ±5 oct) to ~50 Hz and made C7 ~60 dB quieter than C2. ──
{
  async function peakAtNote(note) {
    const e = await fresh();
    e.vane_init(48000); e.vane_set_param(8, 1.0);
    e.vane_set_cc(2, 0.8);
    e.vane_note_on(note, 100, 1);   // deliberately NO vane_set_expr — slide unset
    let p = 0;
    for (let i = 0; i < 80; i++) {
      const b = render(e, 128);
      if (i >= 60) for (const x of b) p = Math.max(p, Math.abs(x));
    }
    return p;
  }
  const c2 = await peakAtNote(36), c7 = await peakAtNote(96);
  check("high range stays audible without CC74 (slide defaults to neutral 0.5)",
        c7 > c2 * 0.3, `C2=${c2.toFixed(3)} C7=${c7.toFixed(3)} (was ~1000x quieter)`);
}

// ── 4. MPE pitchbend actually moves the oscillator frequency (regressed once —
//      received and stored but never applied to the oscillator). ──
{
  const e = await fresh();
  e.vane_init(48000); e.vane_set_param(8, 1.0); e.vane_set_param(1, 18000); e.vane_set_cc(2, 0.9);
  e.vane_note_on(69, 100, 1);
  for (let i = 0; i < 10; i++) render(e, 128);
  const centre = estimateHz(e, 40);
  e.vane_set_expr(1, 1.0, 0, 0);
  for (let i = 0; i < 30; i++) render(e, 128); // let the bend slewer settle
  const bentUp = estimateHz(e, 20);
  check("pitchbend centre ~ A4 440Hz", Math.abs(centre - 440) < 5, `${centre.toFixed(1)}Hz`);
  check("full bend up moves pitch up ~4 octaves (default 48-semi range)",
        bentUp > 6500 && bentUp < 7500, `${bentUp.toFixed(1)}Hz (expect ~7040)`);
}

// ── 5. A fresh vane_init() fully resets state — no leakage from a previous
//      session/instance reuse (found via a test-isolation bug; worth guarding). ──
{
  const e = await fresh();
  e.vane_init(48000); e.vane_set_cc(2, 0.9); e.vane_note_on(69, 100, 1);
  for (let i = 0; i < 20; i++) render(e, 128);
  e.vane_init(48000); // re-init WITHOUT clearing CC by hand — should reset internally
  e.vane_note_on(69, 127, 1);
  const peak = trailingPeak(e, 115);
  check("vane_init resets global CC state (no breath leakage across re-init)", peak < 0.001, `peak=${peak.toFixed(5)}`);
}

// ── 6. Internal tuning (the REAL TuningClient compiled into the wasm; MTS-ESP
//      can't work in web code, so standalone uses internal tunings instead).
//      Indices follow the UI's TUN_ORDER: 0 edo12 · 1 just · 2 pyth · 3 meanqc ·
//      4 werck3 · 5 diat7 · 6 edo19 · 7 bp. ──
{
  async function playHz(note, setup) {
    const e = await fresh();
    e.vane_init(48000); e.vane_set_param(8, 1.0); e.vane_set_param(1, 18000); e.vane_set_cc(2, 0.9);
    if (setup) setup(e);
    e.vane_note_on(note, 100, 1);
    for (let i = 0; i < 15; i++) render(e, 128);
    return estimateHz(e, 40);
  }
  // edo19: LINEAR key mapping anchored at A4=440 — one key = 1200/19 ≈ 63.16¢.
  const a4 = await playHz(69, (e) => { e.vane_set_tuning_source(1); e.vane_set_internal_tuning(6); });
  const a4up = await playHz(70, (e) => { e.vane_set_tuning_source(1); e.vane_set_internal_tuning(6); });
  check("edo19: A4 stays anchored at 440 Hz", Math.abs(a4 - 440) < 5, `${a4.toFixed(1)}Hz`);
  const stepCents = 1200 * Math.log2(a4up / a4);
  check("edo19: adjacent key is one 63.2-cent EDO step", Math.abs(stepCents - 63.16) < 8, `${stepCents.toFixed(1)}c`);

  // just: E above C is a pure 5/4 major third (386.3¢), ~13.7¢ flat of ET.
  const cJust = await playHz(60, (e) => { e.vane_set_tuning_source(1); e.vane_set_internal_tuning(1); });
  const eJust = await playHz(64, (e) => { e.vane_set_tuning_source(1); e.vane_set_internal_tuning(1); });
  const thirdCents = 1200 * Math.log2(eJust / cJust);
  check("just intonation: C→E is a pure 5/4 third (~386.3c)", Math.abs(thirdCents - 386.3) < 8, `${thirdCents.toFixed(1)}c`);

  // diat7: C# is a hole — noteToHz snaps to the nearest sounding degree (C),
  // the wind-controller behavior (portamento can land anywhere).
  const cDia  = await playHz(60, (e) => { e.vane_set_tuning_source(1); e.vane_set_internal_tuning(5); });
  const csDia = await playHz(61, (e) => { e.vane_set_tuning_source(1); e.vane_set_internal_tuning(5); });
  check("diat7: the C# hole snaps to C's pitch (never silent)", Math.abs(csDia - cDia) < 5, `C=${cDia.toFixed(1)} C#=${csDia.toFixed(1)}`);

  // bypass: always plain 12-EDO regardless of the internal selection.
  const a4Byp = await playHz(69, (e) => { e.vane_set_tuning_source(1); e.vane_set_internal_tuning(6); e.vane_set_tuning_source(2); });
  check("bypass: forces 12-EDO (A4=440) regardless of internal tuning", Math.abs(a4Byp - 440) < 5, `${a4Byp.toFixed(1)}Hz`);

  // Live retune: switch tuning WHILE a note is held — pitch follows without a
  // new note-on (the "change scale mid-breath" gesture).
  {
    const e = await fresh();
    e.vane_init(48000); e.vane_set_param(8, 1.0); e.vane_set_param(1, 18000); e.vane_set_cc(2, 0.9);
    e.vane_set_tuning_source(1); e.vane_set_internal_tuning(0);   // edo12
    e.vane_note_on(64, 100, 1);                                    // E4 = 329.6 ET
    for (let i = 0; i < 15; i++) render(e, 128);
    const before = estimateHz(e, 20);
    e.vane_set_internal_tuning(1);                                 // just — E drops ~13.7c
    for (let i = 0; i < 15; i++) render(e, 128);                   // let the retune glide settle
    const after = estimateHz(e, 40);
    const moved = 1200 * Math.log2(after / before);
    check("live retune: switching tuning mid-note moves the held pitch (~-13.7c)",
          Math.abs(moved - (-13.7)) < 8, `${moved.toFixed(1)}c`);
  }
}

// ── 7. Morph wavetable (the REAL Wavetable.cpp compiled in — Harmonic Stack:
//      frame 0 = pure sine … frame 15 = 16 saw harmonics — plus the oscillator's
//      PD pulse-width and hard-sync, all via Oscillator::nextMorphed). ──
{
  // Fraction of signal energy at the fundamental (Goertzel-style correlation):
  // ~1.0 for a pure sine, clearly lower as harmonics enter.
  function fundamentalFraction(samples, hz, sr = 48000) {
    let re = 0, im = 0, total = 0;
    for (let i = 0; i < samples.length; i++) {
      const w = 2 * Math.PI * hz * i / sr;
      re += samples[i] * Math.cos(w); im += samples[i] * Math.sin(w);
      total += samples[i] * samples[i];
    }
    const fund = 2 * (re * re + im * im) / samples.length;
    return total > 0 ? fund / total : 0;
  }
  async function capture(setup) {
    const e = await fresh();
    e.vane_init(48000); e.vane_set_param(8, 1.0); e.vane_set_param(1, 18000); e.vane_set_cc(2, 0.9);
    if (setup) setup(e);
    e.vane_note_on(69, 100, 1);
    for (let i = 0; i < 20; i++) render(e, 128);       // settle
    const s = [];
    for (let i = 0; i < 40; i++) s.push(...render(e, 128));
    return s;
  }
  const sine = fundamentalFraction(await capture((e) => e.vane_set_param(12, 0.0)), 440);
  const rich = fundamentalFraction(await capture((e) => e.vane_set_param(12, 1.0)), 440);
  check("morph 0 is the pure-sine frame (fundamental ≥ 95% of energy)", sine > 0.95, `frac=${sine.toFixed(3)}`);
  check("morph 1 is the rich 16-harmonic frame (fundamental clearly < morph 0)", rich < sine - 0.1, `frac=${rich.toFixed(3)}`);

  const pw = fundamentalFraction(await capture((e) => { e.vane_set_param(12, 0.0); e.vane_set_param(13, 0.95); }), 440);
  check("pulse-width warps the sine frame (spectrum widens)", pw < sine - 0.05, `frac=${pw.toFixed(3)}`);

  const sync = fundamentalFraction(await capture((e) => { e.vane_set_param(12, 0.0); e.vane_set_param(15, 4.0); }), 440);
  check("hard-sync at 4x adds a formant (fundamental fraction drops)", sync < sine - 0.05, `frac=${sync.toFixed(3)}`);
}

// ── 8. Mod matrix: per-note morph. Configure Slide→Morph, play TWO simultaneous
//      notes on different MPE channels — neutral slide on one, full slide on the
//      other. If morph is genuinely per-voice, the full-slide note is harmonically
//      rich while the neutral one stays a near-pure sine AT THE SAME TIME. ──
{
  const e = await fresh();
  e.vane_init(48000); e.vane_set_param(8, 1.0); e.vane_set_param(1, 18000); e.vane_set_cc(2, 0.9);
  e.vane_set_slot(10, 4, 4, 1.0, 0, 1);      // slot 10: Slide → Morph, amount 1.0, linear, on
  e.vane_note_on(58, 100, 2);                 // Bb3 ≈ 233 Hz, channel 2 — slide stays neutral
  e.vane_note_on(78, 100, 3);                 // F#5 ≈ 740 Hz, channel 3 — slide pushed to max
  e.vane_set_expr(3, 0, 1.0, 0);
  for (let i = 0; i < 30; i++) render(e, 128);   // settle slews
  const s = [];
  for (let i = 0; i < 60; i++) s.push(...render(e, 128));
  const energyAt = (hz) => {                    // Goertzel-style bin energy
    let re = 0, im = 0;
    for (let i = 0; i < s.length; i++) { const w = 2 * Math.PI * hz * i / 48000; re += s[i] * Math.cos(w); im += s[i] * Math.sin(w); }
    return re * re + im * im;
  };
  const f1 = 233.08, f2 = 739.99;
  const rNeutral = energyAt(f1 * 2) / energyAt(f1);   // Bb3 2nd harmonic vs fundamental
  const rSlid    = energyAt(f2 * 2) / energyAt(f2);   // F#5 2nd harmonic vs fundamental
  check("per-note morph: full-slide note is harmonically rich while the neutral note stays sine",
        rSlid > rNeutral * 5 && rNeutral < 0.1, `neutral h2/h1=${rNeutral.toFixed(4)} slid h2/h1=${rSlid.toFixed(4)}`);
}

// ── 9. Output safety net: two near-unison MPE notes at full VCA constructively
//      interfere at points in their beat cycle — the standalone webapp writes
//      straight to the browser's hardware output, which hard-clips at ±1.0
//      (heard as "quick beating that sounds like distortion", reported by ear).
//      softLimit() must keep the summed peak at/under the ceiling. ──
{
  const e = await fresh();
  e.vane_init(48000); e.vane_set_param(8, 1.0); e.vane_set_cc(2, 1.0);
  e.vane_note_on(69, 127, 2);   // A4, channel 2
  e.vane_note_on(70, 127, 3);   // Bb4, channel 3 — a semitone away, strong beating
  for (let i = 0; i < 30; i++) render(e, 128);   // settle VCA/cutoff slews
  let peak = 0;
  for (let i = 0; i < 120; i++) { const buf = render(e, 128); for (const v of buf) peak = Math.max(peak, Math.abs(v)); }
  check("polyphonic beat peaks never exceed the hardware ceiling (soft limiter engages)",
        peak <= 1.001, `peak=${peak.toFixed(4)}`);
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail > 0 ? 1 : 0);
