# Vane on Raspberry Pi (MODEP + standalone)

Vane has two Linux personalities, chosen at configure time by
`VANE_LINUX_WEBVIEW` (default **OFF**). Build each into its **own** build dir.

| Use case            | `VANE_LINUX_WEBVIEW` | Formats         | UI               | Audio        |
|---------------------|----------------------|-----------------|------------------|--------------|
| MODEP plugin        | OFF (default)        | LV2, Standalone | none (headless)  | host (JACK)  |
| Desktop / VNC synth | ON                   | Standalone      | WebView (Vane UI)| JACK         |

---

## 1. MODEP (headless LV2) — the default

No WebView, so no WebKitGTK/GTK3 dependency.

```bash
cd ~/Vane
cmake -B build                       # VANE_LINUX_WEBVIEW defaults OFF
cmake --build build --target Vane_LV2 -j2
sudo cp -r build/Vane_artefacts/Release/LV2/Vane.lv2 /var/modep/lv2/
sudo systemctl restart modep-mod-ui modep-mod-host
```

Presets: see `Tools/PresetExport/` (converts `*.vanepreset` → MODEP User presets).
The custom modgui is shelved on MODEP — see `Tools/modgui/README.md`.

---

## 2. Standalone (WebView UI + JACK) — for use outside MODEP

A normal JACK client with the full Vane UI, drivable over VNC. Runs alongside
or instead of MODEP (it's a separate build dir; the MODEP LV2 is untouched).

### Dependencies (one-time)
```bash
sudo apt update
sudo apt install -y libwebkit2gtk-4.1-dev libgtk-3-dev   # WebView UI
# JACK runtime is already present on Patchbox; libjack is loaded at runtime.
```
If `libwebkit2gtk-4.1-dev` isn't found, try `libwebkit2gtk-4.0-dev`.

### Build
```bash
cd ~/Vane
cmake -B build-app -DVANE_LINUX_WEBVIEW=ON
cmake --build build-app --target Vane_Standalone -j2
# binary: build-app/Vane_artefacts/Release/Standalone/Vane
```

### Run headless so it survives VNC disconnect
The standalone is a GUI app — it needs an X display even though audio runs
without one. Use a **persistent virtual** display (not screen-mirroring of a
non-existent monitor), and **autostart** it so it's not tied to your login.

**Option A — TigerVNC virtual desktop (you can VNC in to see/tweak the UI):**
```bash
sudo apt install -y tigervnc-standalone-server
vncserver :1 -geometry 1280x800 -localhost no    # persistent display :1
# VNC to <pi>:1 to see the desktop; launch Vane there, or autostart (below).
```
The `:1` X session keeps running after you disconnect the viewer, so Vane
keeps playing.

**Option B — no VNC at all (headless framebuffer):**
```bash
sudo apt install -y xvfb
DISPLAY=:99 xvfb-run -a build-app/Vane_artefacts/Release/Standalone/Vane &
```

### Autostart on boot (systemd user service)
```ini
# ~/.config/systemd/user/vane.service
[Unit]
Description=Vane standalone synth
After=jack.service sound.target

[Service]
# Point at a display that exists at boot (TigerVNC :1, or Xvfb :99).
Environment=DISPLAY=:1
ExecStart=%h/Vane/build-app/Vane_artefacts/Release/Standalone/Vane
Restart=on-failure

[Install]
WantedBy=default.target
```
```bash
systemctl --user daemon-reload
systemctl --user enable --now vane.service
loginctl enable-linger "$USER"     # so the user service runs without a login session
```

### Audio / MIDI
- In Vane's standalone **Options** (wrench icon, via VNC), pick the **JACK**
  device type and your **MIDI input** (e.g. the Sylphyo). With `JUCE_JACK=1`
  Vane registers as a JACK client.
- Patch its outputs to `system:playback_*` (or via `qjackctl` / Patchbox's
  routing) once and save the JACK session.
- Vane reads `~/.config/Vane/Presets/` (same dir as the MODEP build), so your
  transferred presets are available here too.

### Notes
- WebView on Linux uses WebKitGTK; first launch can be slow as it initialises.
- This is heavier than the headless LV2 (GTK + WebKit). For pure performance,
  MODEP's headless LV2 is lighter; the standalone is for standalone/desktop use.

---

## Build optimisation — dead end on 32-bit armhf (do not retry here)

Tried LTO + `-mcpu=native` for runtime headroom (branch `claude/linux-opt`,
**not merged**). On Patchbox's **32-bit armhf** toolchain it was a net
*regression* (~12% CPU vs <10%): Raspbian's armhf GCC defaults to
`-march=armv6` (Pi 1/Zero compat), which **conflicts** with
`-mcpu=cortex-a72`:

```
lto1: warning: switch '-mcpu=cortex-a72' conflicts with '-march=armv6' switch
```

GCC resolves the conflict conservatively → worse codegen, and LTO adds its own
overhead. The clean `main` build (consistent armv6 default, no LTO) is faster.

Would only pay off on a **64-bit (aarch64)** Patchbox/Pi OS (e.g. Pi 5), where
`-mcpu=native` works cleanly with NEON-fp-armv8. Revisit there, not on armhf.
xruns are handled by raising the JACK buffer to 256 frames regardless.
