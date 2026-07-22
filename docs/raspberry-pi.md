# Vane on Raspberry Pi (MODEP + standalone)

Vane has two Linux personalities, chosen at configure time by
`VANE_LINUX_HEADLESS` (default **OFF** → the desktop build). Build each into
its **own** build dir. The desktop build is the default everywhere (including
here); the headless MODEP build is the Pi-specific opt-in.

| Use case            | `VANE_LINUX_HEADLESS` | Formats               | UI               | Audio        |
|---------------------|-----------------------|-----------------------|------------------|--------------|
| Desktop / VNC synth | OFF (default)         | LV2, VST3, Standalone | WebView (Vane UI)| JACK         |
| MODEP plugin        | ON                    | LV2, Standalone       | none (headless)  | host (JACK)  |

---

## 1. MODEP (headless LV2) — the Pi opt-in

No WebView, so no WebKitGTK/GTK3 dependency. **Pass `-DVANE_LINUX_HEADLESS=ON`**
— the default build is now the desktop WebView one (§2), so MODEP is explicit.

```bash
cd ~/Vane
cmake -B build -DVANE_LINUX_HEADLESS=ON   # headless: LV2 + Standalone, no WebView
cmake --build build --target Vane_LV2 -j2
sudo cp -r build/Vane_artefacts/Release/LV2/Vane.lv2 /var/modep/lv2/
sudo systemctl restart modep-mod-ui modep-mod-host
```

Presets: use **User** presets via `Tools/PresetExport/` (converts `*.vanepreset`
→ self-contained MODEP User-preset bundles). **Ignore the "Factory" list** —
those are the LV2 baked *program* presets, which load by re-reading the
`.vanepreset` files at runtime from the home of the user `mod-host` runs as
(`modep`), not `/home/patch`, so they fail. User-preset bundles embed the full
state inline (no file dependency), so they always load. Prepare on the Mac, run
`VanePresetExport`, install the bundles.

The custom modgui is shelved on MODEP — see `Tools/modgui/README.md`.

---

## 2. Standalone (WebView UI + JACK) — the default build, for use outside MODEP

A normal JACK client with the full Vane UI, drivable over VNC. Runs alongside
or instead of MODEP (it's a separate build dir; the MODEP LV2 is untouched).
This is the **default** Linux build now (no flag needed) and it also produces
the desktop plugin formats (LV2 + VST3) for a Linux DAW, not just the Standalone.

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
cmake -B build-app -DCMAKE_BUILD_TYPE=Release   # desktop WebView is the default
cmake --build build-app --target Vane_Standalone -j2
# binary: build-app/Vane_artefacts/Release/Standalone/Vane
# (this build dir also has Vane_VST3 and Vane_LV2 targets for a Linux DAW)
```
**Set `-DCMAKE_BUILD_TYPE=Release`** — without it CMake builds `-O0` (no
optimisation): much higher CPU and audible glitches. If your artefacts land in
`Vane_artefacts/Standalone/` (no `Release/`), you forgot the flag — `rm -rf
build-app` and reconfigure.

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

### Patchbox: module + mode (recommended for the VNC-desktop workflow)
Patchbox boots into a **module** (`patchbox` CLI, or the boot menu) and a
**mode** (console/desktop):

- **Module**: the standalone uses **ALSA/PulseAudio** by default, while MODEP's
  module owns the audio device via **JACK**. For standalone-only use, select the
  **`none`** module (default Patchbox environment) so JACK/MODEP isn't holding
  the device. Keep **`modep`** only if you actually want both running (expect
  audio-device contention; you'd then want Vane on JACK, not Pulse).
  ```bash
  patchbox          # TUI: Modules → choose 'none'
  ```
- **Mode**: set **desktop** + **autologin** so an X session exists at boot
  (that's what your VNC shows). `raspi-config` ▸ System ▸ Boot/Auto Login ▸
  Desktop Autologin. With no monitor, set a forced/headless resolution
  (`raspi-config` ▸ Display) so the desktop has a framebuffer.

Then autostart Vane with a desktop entry (simplest for a desktop session):
```ini
# ~/.config/autostart/vane.desktop
[Desktop Entry]
Type=Application
Name=Vane
Exec=/home/patch/Vane/build-app/Vane_artefacts/Release/Standalone/Vane
X-GNOME-Autostart-enabled=true
```
Vane launches when the (auto-logged-in) desktop starts, and stays up when you
disconnect the VNC viewer. It restores its last audio/MIDI device + preset, so
once you've picked the Sylphyo + output and loaded a patch, it comes back the
same way each boot.

### Audio / MIDI
- **Output device matters a lot.** The default "Playback through PulseAudio"
  usually routes to the Pi's built-in `bcm2835` PWM headphone out, which is
  noisy and *glitches at higher output levels* (sounds like crackle that gets
  worse as you raise Output, and is buffer-independent). On a Pisound box, use
  the real DAC instead:
  - **JACK** device type (recommended) — same backend MODEP uses, bound to the
    Pisound; with `JUCE_JACK=1` Vane registers as a JACK client. Patch its
    outputs to `system:playback_*` (qjackctl / Patchbox routing) and save the
    JACK session. Note JACK can't be shared with MODEP at once — run module
    `none` (or stop MODEP) so JACK is free for the standalone.
  - or **ALSA → "pisound … Direct hardware device"** if you'd rather not run
    JACK. Avoid the bcm2835 outputs.
- Pick your **MIDI input** (e.g. the Sylphyo) in the same dialog. It's only
  restored on next launch if the device is present at launch (plug the Sylphyo
  in before boot for autostart).
- Vane reads `~/.config/Vane/Presets/` (the patch user's home), so your
  transferred presets are available here.

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
