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

  // Wavefold (id 17): folding a near-sine adds harmonics → fundamental fraction drops.
  const folded = fundamentalFraction(await capture((e) => { e.vane_set_param(12, 0.0); e.vane_set_param(17, 1.0); }), 440);
  check("wavefold adds harmonics to the sine frame (fundamental fraction drops)", folded < sine - 0.05, `frac=${folded.toFixed(3)}`);

  // Filter mode (id 16): HP (2) on a rich tone attenuates the fundamental vs LP (0).
  const richLP = fundamentalFraction(await capture((e) => { e.vane_set_param(12, 1.0); e.vane_set_param(1, 2000); e.vane_set_param(16, 0); }), 440);
  const richHP = fundamentalFraction(await capture((e) => { e.vane_set_param(12, 1.0); e.vane_set_param(1, 2000); e.vane_set_param(16, 2); }), 440);
  check("filter HP mode attenuates the fundamental vs LP", richHP < richLP - 0.05, `LP=${richLP.toFixed(3)} HP=${richHP.toFixed(3)}`);
}

// ── 7b. Vowel/formant filter (ids 18-25): a global post-mix formant resonator.
//      Play a rich low note (many harmonics land in the formant range) and
//      confirm (a) disabled = pass-through, (b) different vowels move the F1
//      peak: /a/ "ah" (open=1) concentrates energy near 850 Hz, /i/ "ee"
//      (open≈0) near ~290 Hz, so 850 Hz is far stronger for "ah" than "ee". ──
{
  const energyAt = (s, hz) => { let re = 0, im = 0; for (let i = 0; i < s.length; i++) { const w = 2*Math.PI*hz*i/48000; re += s[i]*Math.cos(w); im += s[i]*Math.sin(w); } return Math.sqrt(re*re + im*im); };
  const cap = async (setup) => {
    const e = await fresh();
    e.vane_init(48000); e.vane_set_param(8, 0.8); e.vane_set_param(1, 18000); e.vane_set_param(12, 1.0); e.vane_set_cc(2, 0.9);
    setup(e);
    e.vane_note_on(45, 100, 1);   // A2 ~110 Hz, rich saw
    for (let i = 0; i < 40; i++) render(e, 128);
    const s = []; for (let i = 0; i < 40; i++) s.push(...render(e, 128)); return s;
  };
  const dry = await cap(() => {});
  const ah  = await cap((e) => { e.vane_set_param(18, 1); e.vane_set_param(20, 1.0);  e.vane_set_param(21, 0.5); e.vane_set_param(23, 1); });
  const ee  = await cap((e) => { e.vane_set_param(18, 1); e.vane_set_param(20, 0.05); e.vane_set_param(21, 1.0); e.vane_set_param(23, 1); });
  const ah850 = energyAt(ah, 850), ee850 = energyAt(ee, 850);
  // enabling the vowel reshapes the spectrum: /i/ "ee" (low F1) strongly
  // attenuates the 850 Hz region that the dry saw has, so ee@850 << dry@850.
  check("vowel filter enabled reshapes the spectrum (not a pass-through)",
        ee850 < energyAt(dry, 850) * 0.5, `ee@850=${ee850.toFixed(2)} dry@850=${energyAt(dry, 850).toFixed(2)}`);
  check("vowel /a/ concentrates F1 near 850 Hz far more than /i/ (formants track the open axis)",
        ah850 > ee850 * 3 && ah850 > 0.5, `ah@850=${ah850.toFixed(2)} ee@850=${ee850.toFixed(2)}`);
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

// ── 9. Output safety net: two MPE notes at full VCA sum to ~1.6 — the standalone
//      webapp writes straight to the browser's hardware output, which hard-clips
//      at ±1.0. The master limiter must keep the peak at/under the ceiling. ──
{
  const e = await fresh();
  e.vane_init(48000); e.vane_set_param(8, 1.0); e.vane_set_cc(2, 1.0);
  e.vane_note_on(69, 127, 2);   // A4, channel 2
  e.vane_note_on(70, 127, 3);   // Bb4, channel 3 — a semitone away, strong beating
  for (let i = 0; i < 30; i++) render(e, 128);   // settle VCA/cutoff slews + limiter
  let peak = 0;
  for (let i = 0; i < 120; i++) { const buf = render(e, 128); for (const v of buf) peak = Math.max(peak, Math.abs(v)); }
  check("polyphonic peaks never exceed the hardware ceiling (master limiter engages)",
        peak <= 1.001, `peak=${peak.toFixed(4)}`);
}

// ── 9b. Poly headroom: 1-2 notes are left at full level (parity), 3+ notes get a
//      gentle taper (sqrt(2/N)) so the limiter engages less on chords. Verify a
//      single note is NOT attenuated and a dense chord is — measured below the
//      limiter (moderate output) so the taper, not the limiter, is what shows. ──
{
  const rmsAt = async (notes, out) => {
    const e = await fresh();
    e.vane_init(48000); e.vane_set_param(8, out); e.vane_set_cc(2, 1.0);
    for (let i = 0; i < notes; i++) e.vane_note_on(60 + i*4, 100, 2 + i);   // spread notes, own channels
    for (let i = 0; i < 60; i++) render(e, 128);
    const s = []; for (let i = 0; i < 40; i++) s.push(...render(e, 128));
    let sum = 0; for (const v of s) sum += v*v; return Math.sqrt(sum / s.length);
  };
  const one = await rmsAt(1, 0.6);            // 1 note, moderate — no limiting, no taper
  const four = await rmsAt(4, 0.15);          // 4 notes, low output — no limiting; taper (×0.707) applies
  const fourNoTaperEstimate = four / Math.sqrt(2/4);
  check("poly headroom tapers dense chords but leaves single notes at full level",
        one > 0.15 && four < fourNoTaperEstimate * 0.95, `1-note rms=${one.toFixed(3)}, 4-note tapered=${four.toFixed(3)}`);
}

// ── 10. Distortion: the limiter must be a smooth GAIN, not a per-sample
//      waveshaper. A linear gain scales the mix without adding frequencies; a
//      waveshaper (the old softLimit) adds intermodulation — the "roughness/
//      distortion" reported by ear with two notes. Measure the non-harmonic
//      (distortion) energy fraction of C4+E4 at a LOUD level (limiter active,
//      2 notes → ~1.6 pre-gain) vs a QUIET level (never limits → pure linear);
//      a clean gain limiter leaves the two nearly EQUAL, a waveshaper inflates
//      the loud one. (The absolute value is a DFT-leakage floor, hence the diff.) ──
{
  const distFrac = (s) => {                     // non-harmonic energy fraction of C4+E4
    const N = s.length, f1 = 261.63, f2 = 329.63;
    const mag = (hz) => { let re = 0, im = 0; for (let i = 0; i < N; i++) { const w = 2*Math.PI*hz*i/48000; re += s[i]*Math.cos(w); im += s[i]*Math.sin(w); } return re*re + im*im; };
    const isH = (hz) => { for (const f of [f1, f2]) { const k = Math.round(hz/f); if (k >= 1 && Math.abs(hz - k*f) < 4) return true; } return false; };
    let h = 0, r = 0; for (let hz = 30; hz < 18000; hz += 11) { const en = mag(hz); if (isH(hz)) h += en; else r += en; }
    return r / (h + r);
  };
  const run = async (out) => {
    const e = await fresh();
    e.vane_init(48000); e.vane_set_param(8, out); e.vane_set_cc(2, 1.0);
    e.vane_note_on(60, 100, 2); e.vane_note_on(64, 100, 3);
    e.vane_set_expr(2, 0, 0.5, 1.0); e.vane_set_expr(3, 0, 0.5, 1.0);
    for (let i = 0; i < 80; i++) render(e, 128);
    const s = []; for (let i = 0; i < 80; i++) s.push(...render(e, 128));
    return distFrac(s);
  };
  const loud = await run(0.80), quiet = await run(0.30);
  check("limiter adds no distortion vs pure-linear (gain-based, not a waveshaper)",
        (loud - quiet) < 0.008, `loud=${(loud*100).toFixed(2)}% quiet(linear)=${(quiet*100).toFixed(2)}% Δ=${((loud-quiet)*100).toFixed(2)}%`);
}

// ── 11. Legato preserves expression continuity. A wind-controller slur keeps
//      breath/slide/pressure flowing across the note change; re-zeroing them at
//      each note-on blipped the timbre (via the factory Slide→Cutoff route) so
//      legato never felt smooth however long the pitch glide. With a low base
//      cutoff, RMS tracks slide position — after a legato change with expression
//      NOT resent it must match the bright (slide-held) case, not neutral. ──
{
  const setup = (e) => {
    e.vane_init(48000); e.vane_set_param(8, 0.8); e.vane_set_param(10, 10); e.vane_set_param(1, 70);
    e.vane_set_mono(1);
    for (let s = 0; s < 24; s++) e.vane_set_slot(s, 0, 0, 0, 0, 0);
    e.vane_set_slot(0, 1, 0, 1.0, 0, 1);   // Breath→VCA
    e.vane_set_slot(1, 4, 1, 0.9, 0, 1);   // Slide→Cutoff
    e.vane_set_cc(2, 1.0);
  };
  const rms = (e) => { const b = []; for (let i = 0; i < 24; i++) b.push(...render(e, 128)); let s = 0; for (const v of b) s += v*v; return Math.sqrt(s/b.length); };
  const eHi = await fresh(); setup(eHi); eHi.vane_note_on(55, 100, 1); eHi.vane_set_expr(1, 0, 0.9, 0.0);
  for (let i = 0; i < 200; i++) render(eHi, 128); const hi = rms(eHi);
  const eLo = await fresh(); setup(eLo); eLo.vane_note_on(55, 100, 1); eLo.vane_set_expr(1, 0, 0.5, 0.0);
  for (let i = 0; i < 200; i++) render(eLo, 128); const lo = rms(eLo);
  const eT = await fresh(); setup(eT); eT.vane_note_on(48, 100, 1); eT.vane_set_expr(1, 0, 0.9, 0.0);
  for (let i = 0; i < 200; i++) render(eT, 128);
  eT.vane_note_on(55, 100, 1);           // legato, expression NOT resent
  for (let i = 0; i < 40; i++) render(eT, 128); const test = rms(eT);
  check("legato preserves expression (no per-note timbre blip)",
        Math.abs(test - hi) < Math.abs(test - lo), `test=${test.toFixed(3)} bright=${hi.toFixed(3)} neutral=${lo.toFixed(3)}`);
}

// ── 12. Mono legato-hold: a DETACHED note change (note-off, gap, note-on) must
//      NOT notch the amplitude while the gap is within the hold window — the
//      voice freezes and bridges to the next note. Without the hold the level
//      collapsed during the gap (~28% at a 50 ms gap). Tested with pressure as
//      the sole volume source, dropping to 0 on key-release (the worst case). ──
{
  const rmsOf = (e, blocks) => { const b = []; for (let i = 0; i < blocks; i++) b.push(...render(e, 128)); let s = 0; for (const v of b) s += v*v; return Math.sqrt(s/b.length); };
  const e = await fresh();
  e.vane_init(48000); e.vane_set_param(8, 0.8); e.vane_set_param(10, 120); e.vane_set_mono(1); e.vane_set_cc(2, 0.0);
  for (let s = 0; s < 24; s++) e.vane_set_slot(s, 0, 0, 0, 0, 0);
  e.vane_set_slot(0, 3, 0, 1.0, 0, 1);   // Pressure→VCA (volume entirely from pressure)
  e.vane_note_on(60, 100, 1); e.vane_set_expr(1, 0, 0.5, 0.9);
  for (let i = 0; i < 160; i++) render(e, 128);
  const steady = rmsOf(e, 16);
  const gapBlocks = Math.round(0.05 * 48000 / 128);   // 50 ms detached gap
  const env = [];
  e.vane_note_off(60, 1); e.vane_set_expr(1, 0, 0.5, 0.0);   // key released → pressure drops to 0
  for (let i = 0; i < gapBlocks; i++) env.push(rmsOf(e, 1));
  e.vane_note_on(64, 100, 1); e.vane_set_expr(1, 0, 0.5, 0.9);
  for (let i = 0; i < 40; i++) env.push(rmsOf(e, 1));
  const dip = Math.min(...env) / steady;
  check("mono legato-hold bridges a detached note change (no amplitude notch)",
        dip > 0.7, `dip=${(dip*100).toFixed(0)}% of steady (was ~28% without the hold)`);
}

// ── 13. Mono + MPE per-note pressure: an OVERLAPPING legato transition (press
//      the next note on a new channel while releasing the previous) must stay
//      continuous. Each MPE note carries its own channel pressure; a mono voice
//      following only the newest channel dips to that note's momentarily-low
//      pressure at every transition (~54%). Driving the mono VCA from the MAX
//      pressure across held notes keeps it continuous. ──
{
  const rmsOf = (e, blocks) => { const b = []; for (let i = 0; i < blocks; i++) b.push(...render(e, 128)); let s = 0; for (const v of b) s += v*v; return Math.sqrt(s/b.length); };
  const e = await fresh();
  e.vane_init(48000); e.vane_set_param(8, 0.8); e.vane_set_param(10, 80); e.vane_set_mono(1); e.vane_set_cc(2, 0.0);
  for (let s = 0; s < 24; s++) e.vane_set_slot(s, 0, 0, 0, 0, 0);
  e.vane_set_slot(0, 3, 0, 1.0, 0, 1);   // Pressure→VCA (volume from per-note pressure)
  e.vane_note_on(60, 100, 2);
  for (let i = 0; i < 60; i++) { e.vane_set_expr(2, 0, 0.5, Math.min(0.9, i/30*0.9)); render(e, 128); }
  const steady = rmsOf(e, 12);
  const env = [], ramp = Math.round(0.04 * 48000 / 128);
  e.vane_note_on(64, 100, 3);                                   // press B (ch3) while A (ch2) held
  for (let i = 0; i < ramp; i++) { e.vane_set_expr(3, 0, 0.5, 0.9*i/ramp); e.vane_set_expr(2, 0, 0.5, 0.9); env.push(rmsOf(e, 1)); }
  e.vane_note_off(60, 2);                                        // release A
  for (let i = 0; i < ramp; i++) { e.vane_set_expr(2, 0, 0.5, 0.9*(1-i/ramp)); e.vane_set_expr(3, 0, 0.5, 0.9); env.push(rmsOf(e, 1)); }
  for (let i = 0; i < 30; i++) { e.vane_set_expr(3, 0, 0.5, 0.9); env.push(rmsOf(e, 1)); }
  const dip = Math.min(...env) / steady;
  check("mono MPE legato stays continuous across a channel-switch (max-pressure)",
        dip > 0.8, `dip=${(dip*100).toFixed(0)}% of steady (was ~54% without max-pressure)`);
}

// ── 14. Waveguide (MiniSax) mode: physical breath response. Breath drives the
//      reed DIRECTLY — below the speaking threshold the reed must stay quiet
//      (subtone), above it the model sounds at (near) the requested pitch, and
//      silence with zero breath. No VCA routing involved (the factory
//      Breath→VCA routes are cleared to prove it). ──
{
  const rmsOf = (e, blocks) => { const b = []; for (let i = 0; i < blocks; i++) b.push(...render(e, 128)); let s = 0; for (const v of b) s += v*v; return Math.sqrt(s/b.length); };
  async function wgAtBreath(breath) {
    const e = await fresh();
    e.vane_init(48000); e.vane_set_param(8, 0.8); e.vane_set_mono(1);
    for (let s = 0; s < 24; s++) e.vane_set_slot(s, 0, 0, 0, 0, 0);   // NO routes at all
    e.vane_set_param(30, 1);                    // WaveguideOn
    e.vane_set_cc(2, breath);
    e.vane_note_on(60, 100, 2);
    for (let i = 0; i < 60; i++) render(e, 128);   // settle
    return e;
  }
  const eQuiet = await wgAtBreath(0.15);
  const quiet  = rmsOf(eQuiet, 20);
  const eLoud  = await wgAtBreath(0.85);
  const loud   = rmsOf(eLoud, 20);
  check("waveguide: reed stays quiet below the speaking threshold (subtone)",
        quiet < loud * 0.1, `rms@0.15=${quiet.toFixed(4)} vs rms@0.85=${loud.toFixed(4)}`);
  check("waveguide: full breath sounds without any VCA route (breath drives the reed directly)",
        loud > 0.05, `rms=${loud.toFixed(4)}`);
  // Pitch via Goertzel peak search around the nominal fundamental — the
  // conical shaper's strong H2/H4 give multiple zero crossings per period,
  // which fools the crossing-count estimator (reads ~3x); spectral detection
  // is what the plugin's RenderProbe uses for the same reason.
  const eHz = await wgAtBreath(0.85);
  const samples = [];
  for (let i = 0; i < 120; i++) samples.push(...render(eHz, 128));
  const goertzel = (hz) => { let re = 0, im = 0;
    for (let i = 0; i < samples.length; i++) { const w = 2*Math.PI*hz*i/48000; re += samples[i]*Math.cos(w); im += samples[i]*Math.sin(w); }
    return Math.hypot(re, im); };
  let best = 0, hz = 0;
  for (let f = 261.63*0.94; f <= 261.63*1.06; f += 0.5) { const m = goertzel(f); if (m > best) { best = m; hz = f; } }
  const cents = 1200 * Math.log2(hz / 261.63);
  check("waveguide: sounded pitch lands near the note (|err| < 60c)",
        Math.abs(cents) < 60 && best > 0.01 * samples.length,
        `C4 sounded ${hz.toFixed(1)} Hz (${cents.toFixed(0)}c)`);
  // Legato slur: breath held, note change on the SAME mono voice — the bore
  // keeps ringing (no reset), so the envelope must not dip toward silence.
  const e = await wgAtBreath(0.85);
  const steady = rmsOf(e, 16);
  const env = [];
  e.vane_note_on(65, 100, 2);   // slur up a fourth, breath still on
  for (let i = 0; i < 40; i++) env.push(rmsOf(e, 1));
  const dip = Math.min(...env) / steady;
  check("waveguide: legato slur keeps the bore ringing (no re-attack valley)",
        dip > 0.6, `trough=${(dip*100).toFixed(0)}% of steady`);
}

// ── 15. Noise blend: type shapes the spectrum, blend is VCA-gated silence-safe. ──
{
  const rmsOf = (e, blocks) => { const b = []; for (let i = 0; i < blocks; i++) b.push(...render(e, 128)); let s = 0; for (const v of b) s += v*v; return Math.sqrt(s/b.length); };
  const e = await fresh();
  e.vane_init(48000); e.vane_set_param(8, 0.8); e.vane_set_cc(2, 0.9);
  e.vane_set_param(26, 1.0);   // Noise blend full
  e.vane_note_on(60, 100, 2);
  for (let i = 0; i < 40; i++) render(e, 128);
  const white = rmsOf(e, 12);
  e.vane_set_param(27, 2);     // Brown
  for (let i = 0; i < 40; i++) render(e, 128);
  const brown = rmsOf(e, 12);
  check("noise blend: audible at full blend (white)", white > 0.02, `rms=${white.toFixed(4)}`);
  check("noise blend: brown type still renders (leaky integrator)", brown > 0.005, `rms=${brown.toFixed(4)}`);
  const e2 = await fresh();
  e2.vane_init(48000); e2.vane_set_param(8, 0.8); e2.vane_set_param(26, 1.0);
  e2.vane_set_cc(2, 0.0);      // no breath
  e2.vane_note_on(60, 100, 2);
  for (let i = 0; i < 30; i++) render(e2, 128);
  const silent = rmsOf(e2, 12);
  check("noise blend: silent with no breath (VCA still gates the noise)", silent < 0.001, `rms=${silent.toFixed(5)}`);
}

// ── 16. Detune: +100 cents moves the sounded pitch one semitone up. ──
{
  const e = await fresh();
  e.vane_init(48000); e.vane_set_param(8, 0.8); e.vane_set_cc(2, 0.9);
  e.vane_note_on(69, 100, 2);
  for (let i = 0; i < 40; i++) render(e, 128);
  const base = estimateHz(e, 40);
  e.vane_set_param(28, 100);   // Detune +100c
  for (let i = 0; i < 60; i++) render(e, 128);
  const detuned = estimateHz(e, 40);
  const cents = 1200 * Math.log2(detuned / base);
  check("detune: +100 cents raises pitch ~one semitone", Math.abs(cents - 100) < 8,
        `${base.toFixed(1)} -> ${detuned.toFixed(1)} Hz (${cents.toFixed(1)}c)`);
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail > 0 ? 1 : 0);
