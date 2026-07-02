#!/usr/bin/env bash
# Build Vane's standalone Web Audio voice to WASM, reusing the plugin's real DSP
# (Oscillator + SVFilter) via a tiny juce_core stub — no JUCE module linked.
# Requires Emscripten:  source ~/emsdk/emsdk_env.sh
#
# Output vane-dsp.wasm is copied into the music-suite monorepo (apps/vane/synth/),
# where the AudioWorklet instantiates it. CI can't run emcc, so the .wasm is a
# committed artifact in the monorepo; re-run this whenever the DSP changes.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../.."

emcc "$here/vane-dsp.cpp" "$root/Source/Synth/Oscillator.cpp" "$root/Source/MPE/TuningClient.cpp" \
  -I "$here/juce-stub" -I "$root/Source" \
  -O3 -std=c++17 -s STANDALONE_WASM=1 \
  -s EXPORTED_FUNCTIONS='["_vane_init","_vane_note_on","_vane_note_off","_vane_set_expr","_vane_set_cc","_vane_set_mono","_vane_set_param","_vane_set_tuning_source","_vane_set_internal_tuning","_vane_render","_vane_buffer"]' \
  --no-entry -o "$here/vane-dsp.wasm"
echo "built $here/vane-dsp.wasm ($(wc -c < "$here/vane-dsp.wasm") bytes)"

dest="${MUSIC_SUITE:-$HOME/Desktop/music-suite}/apps/vane/synth"
if [ -d "$dest" ]; then cp "$here/vane-dsp.wasm" "$dest/vane-dsp.wasm"; echo "copied → $dest/vane-dsp.wasm"; fi
