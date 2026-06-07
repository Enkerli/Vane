# Vane modgui (optional — NOT used by default)

This directory + `../../modgui/` contain a custom MOD-UI pedal GUI for Vane
(four param knobs — Cutoff/Glide/Mono/Voicing — a bypass footswitch, and
MIDI-in / audio-out jacks). It is applied to a *built* `Vane.lv2` bundle by:

```bash
python3 Tools/modgui/apply.py <path/to/Vane.lv2>
```

## Status: shelved on current MODEP (do not apply)

JUCE 8 exposes plugin parameters as LV2 **patch parameters** (`lv2:Parameter`,
`plug:<paramID>`), not control ports. MODEP's mod-ui (BlokasLabs fork, 1.13.0)
renders patch params in the auto-generated "generic" GUI fine, but its **custom
modgui** support for patch params is incomplete:

- knob widgets bind by `mod-role="input-parameter"` + `mod-parameter-uri` and
  *read* values correctly, but writing is intermittently blocked with
  "Parameter value change blocked by the active addressing" (mod-ui marks the
  patch-param widget read-only via its addressing bookkeeping, which keys off
  control ports — see modgui.js `disable(symbol)` / `prevent()`);
- the custom jack elements don't render as native sockets and connect
  unreliably, whereas control-port plugins (e.g. DPF/DISTRHO) get proper
  sockets that connect cleanly.

Net: on this MODEP, the **generic GUI is more functional** (editable knobs +
reliable MIDI). So we install Vane.lv2 **without** running `apply.py`.

### To get the generic GUI (recommended / default)
Build and install the bundle as-is; do **not** run `apply.py`:
```bash
cmake --build build --target Vane_LV2
sudo cp -r build/Vane_artefacts/Release/LV2/Vane.lv2 /var/modep/lv2/
sudo systemctl restart modep-mod-ui modep-mod-host
```

## When this could be revived
- a mod-ui version with first-class patch-parameter modgui support, or
- exposing Vane's params as LV2 control ports (would require patching the JUCE
  LV2 wrapper, which currently emits parameters as patch params unconditionally).

The template itself (`modgui/icon.html`, `style.css`) follows MOD's documented
contract (cns-scoped selectors, stock `/resources/` knob+footswitch sprites,
`mod-parameter-uri` binding), so it should work on a conformant mod-ui.
