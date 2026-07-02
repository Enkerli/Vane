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
function estimateHz(e, blocks, sr = 48000, blockSize = 128) {
  const samples = [];
  for (let i = 0; i < blocks; i++) samples.push(...render(e, blockSize));
  let crossings = 0;
  for (let i = 1; i < samples.length; i++) if (samples[i - 1] < 0 && samples[i] >= 0) crossings++;
  return crossings / (samples.length / sr);
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

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail > 0 ? 1 : 0);
