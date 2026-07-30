cmake_minimum_required(VERSION 3.22)
project(Vane VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

# ── JUCE ─────────────────────────────────────────────────────────────────────
# Symlink JUCE alongside this directory:
#   ln -s /Users/alex/JUCE /Users/alex/Vane/JUCE
# ─────────────────────────────────────────────────────────────────────────────
find_package(JUCE CONFIG QUIET)
if(NOT JUCE_FOUND)
    set(JUCE_PATH "${CMAKE_SOURCE_DIR}/JUCE" CACHE PATH "Path to JUCE directory")
    # Probe a JUCE_PATH environment variable (one local JUCE for every repo —
    # export it once; on Linux there is no standard location) and the macOS
    # install, before falling back to fetching a private copy.
    if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt" AND EXISTS "$ENV{JUCE_PATH}/CMakeLists.txt")
        set(JUCE_PATH "$ENV{JUCE_PATH}")
    endif()
    if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt" AND EXISTS "/Applications/JUCE/CMakeLists.txt")
        set(JUCE_PATH "/Applications/JUCE")
    endif()
    if(EXISTS "${JUCE_PATH}/CMakeLists.txt")
        add_subdirectory("${JUCE_PATH}" JUCE)   # local symlink (dev machines)
    else()
        # No local JUCE (e.g. CI) — fetch it, matching the enkerli-juce archetype.
        message(STATUS "JUCE not found locally — fetching juce-framework/JUCE 8.0.13")
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        8.0.13
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()
endif()

# ── Platform ──────────────────────────────────────────────────────────────────
set(IS_IOS FALSE)
set(IS_MACOS FALSE)
set(IS_LINUX FALSE)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(IS_IOS TRUE)
    message(STATUS "Configuring for iOS / iPadOS")
elseif(APPLE)
    set(IS_MACOS TRUE)
    message(STATUS "Configuring for macOS")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(IS_LINUX TRUE)
    message(STATUS "Configuring for Linux (RPi / headless)")
endif()

if(IS_IOS)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "16.0" CACHE STRING "Minimum iOS version")
elseif(IS_MACOS)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# ── MTS-ESP ───────────────────────────────────────────────────────────────────
# Clone libMTS into Libs/MTS-ESP:
#   cd Libs/MTS-ESP && git clone https://github.com/ODDSound/MTS-ESP .
# ─────────────────────────────────────────────────────────────────────────────
if(EXISTS "${CMAKE_SOURCE_DIR}/Libs/MTS-ESP/libMTSClient.h")
    set(VANE_HAS_MTS TRUE)
    add_library(VaneMTS STATIC Libs/MTS-ESP/libMTSClient.cpp)
    target_include_directories(VaneMTS PUBLIC Libs/MTS-ESP)
    # Linked into shared modules (CLAP/LV2 .so on Linux), so it must be PIC —
    # else ld: "relocation R_X86_64_PC32 ... can not be used making a shared object".
    set_target_properties(VaneMTS PROPERTIES POSITION_INDEPENDENT_CODE ON)
    message(STATUS "MTS-ESP found — microtuning enabled")
else()
    set(VANE_HAS_MTS FALSE)
    message(STATUS "MTS-ESP not found — equal temperament fallback (see Libs/MTS-ESP/README.md)")
endif()

# ── Plugin ────────────────────────────────────────────────────────────────────
# ── Installing built plugins ──────────────────────────────────────────────────
# Mirrors enkerli-juce/cmake/EnkerliPlugin.cmake, which Vane reimplements inline
# (it resolves JUCE independently). Desktop builds hardcoded TRUE, so
# suite-build --no-install silently did nothing here and a Vane branch build
# would still have taken over the installed plugin — the exact hazard that flag
# exists to prevent. iOS stays FALSE below regardless: a cross-compile has no
# install location on the build host.
option(ENKERLI_INSTALL_PLUGINS "Copy built plugins into the user plug-in folders" ON)

set(PLUGIN_BASE_PROPS
    COMPANY_NAME                "Enkerli"
    COMPANY_WEBSITE             "https://enkerli.com"
    BUNDLE_ID                   "com.enkerli.Vane"
    PLUGIN_MANUFACTURER_CODE    Enke
    PLUGIN_CODE                 VAne
    PRODUCT_NAME                "Vane"
    DESCRIPTION                 "MPE synthesizer - responds to input, shapes sound"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    # App / plugin icon (Standalone .app, AUv3, installer).  JUCE generates the
    # platform icon sets (.icns / .ico) from these two PNGs.
    ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon/vane-1024.png"
    ICON_SMALL                  "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon/vane-128.png"
    # COPY_PLUGIN_AFTER_BUILD is set per-platform below — it must be FALSE for
    # iOS cross-compiles (there is no plugin install location on the build host),
    # and follows ENKERLI_INSTALL_PLUGINS on the desktop platforms.
)

# Apple Developer Team ID — used for iOS signing.
# Override on the command line if needed: -DVANE_IOS_TEAM_ID=XXXXXXXXXX
set(VANE_IOS_TEAM_ID "P8W7XXJN6C" CACHE STRING "Apple Developer Team ID for iOS signing")

if(IS_IOS)
    juce_add_plugin(Vane
        ${PLUGIN_BASE_PROPS}
        FORMATS                     AUv3 Standalone
        COPY_PLUGIN_AFTER_BUILD     FALSE       # cross-compiled — no local install path
        DEVELOPMENT_TEAM            "${VANE_IOS_TEAM_ID}"
        BACKGROUND_AUDIO_ENABLED    TRUE
        IPHONE_SCREEN_ORIENTATIONS
            UIInterfaceOrientationPortrait
            UIInterfaceOrientationPortraitUpsideDown
            UIInterfaceOrientationLandscapeLeft
            UIInterfaceOrientationLandscapeRight
        IPAD_SCREEN_ORIENTATIONS
            UIInterfaceOrientationPortrait
            UIInterfaceOrientationPortraitUpsideDown
            UIInterfaceOrientationLandscapeLeft
            UIInterfaceOrientationLandscapeRight
    )
elseif(IS_MACOS)
    juce_add_plugin(Vane
        ${PLUGIN_BASE_PROPS}
        FORMATS                     AU AUv3 VST3 Standalone
        COPY_PLUGIN_AFTER_BUILD     ${ENKERLI_INSTALL_PLUGINS}
        # JUCE unconditionally writes "network.client" and
        # "temporary-exception.files.all.read-write" into the AU's
        # Info.plist resourceUsage block.  Logic Pro (and GarageBand) treat
        # the broad "files.all" grant as a security violation and silently
        # refuse to load the AU — it never appears in the plug-in list.
        # Suppressing the whole block lets Logic load the AU; the resource
        # grants are meaningless for a plain AU that runs in Logic's own
        # process anyway.
        # See JUCE CMake API docs: SUPPRESS_AU_PLIST_RESOURCE_USAGE.
        SUPPRESS_AU_PLIST_RESOURCE_USAGE TRUE
    )
elseif(IS_LINUX)
    # Two Linux personalities, selected by VANE_LINUX_HEADLESS (default OFF →
    # the desktop DAW build):
    #   OFF (default) → desktop build: WebView UI (needs libwebkit2gtk dev) +
    #                   JACK, formats LV2 + VST3 + Standalone. This is the normal
    #                   Linux desktop DAW target (miniPC / Ubuntu Studio / any
    #                   distro): Ardour/Qtractor load the LV2, Reaper/Bitwig the
    #                   VST3, the Standalone patches into JACK.
    #   ON  → headless LV2 (+ Standalone), no WebView / WebKitGTK / VST3, for
    #         MODEP on PatchboxOS / Raspberry Pi specifically — MODEP renders
    #         controls from the LV2 ports and never shows a native GUI, so the
    #         WebView (and its heavy WebKitGTK dep) is dropped.
    # Build the two into separate dirs (e.g. build-linux/ and build-headless/).
    option(VANE_LINUX_HEADLESS
        "Linux: build the headless MODEP LV2 (Raspberry Pi / PatchboxOS) instead of the default desktop WebView build" OFF)
    if(VANE_LINUX_HEADLESS)
        set(_vane_linux_formats LV2 Standalone)
    else()
        set(_vane_linux_formats LV2 VST3 Standalone)
    endif()
    juce_add_plugin(Vane
        ${PLUGIN_BASE_PROPS}
        FORMATS                     ${_vane_linux_formats}
        LV2_URI                     "https://enkerli.com/plugins/vane"
        COPY_PLUGIN_AFTER_BUILD     ${ENKERLI_INSTALL_PLUGINS}
    )
else()
    # Windows (and any other desktop): VST3 is the plugin format there, and the
    # WebView needs the modern backend (NEEDS_WEB_BROWSER → WebView2 below).
    juce_add_plugin(Vane
        ${PLUGIN_BASE_PROPS}
        FORMATS                     VST3 Standalone
        NEEDS_WEB_BROWSER           TRUE
        NEEDS_CURL                  FALSE
        COPY_PLUGIN_AFTER_BUILD     FALSE
    )
endif()

# Windows: the WebView UI needs the Edge/WebView2 backend and its SDK headers,
# which JUCE doesn't vendor. Enable the backend and fetch the NuGet (a zip) so
# every target built after this (incl. Vane_CLAP) sees <WebView2.h>.
if(WIN32)
    target_compile_definitions(Vane PUBLIC JUCE_USE_WIN_WEBVIEW2=1)
    include(FetchContent)
    FetchContent_Declare(webview2
        URL https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2792.45
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(webview2)
    include_directories("${webview2_SOURCE_DIR}/build/native/include")
endif()

# ── CLAP (macOS / Linux / Windows) via clap-juce-extensions ──────────────────
# Adds a Vane_CLAP target that shares the plugin's sources/definitions (incl. the
# Linux WebView/WebKitGTK linkage below). CLAP is not an iOS format, so skip it
# there. CLAP_ID reuses the bundle id (stable — hosts key off it). Same integration
# the suite's enkerli-juce helper uses; Vane predates that helper so it's inline.
# On Linux the default build already includes the WebView GUI (desktop); only the
# headless MODEP/Pi build (-DVANE_LINUX_HEADLESS=ON) drops it.
if(NOT IS_IOS)
    if(NOT TARGET clap_juce_extensions)
        set(CLAP_JUCE_PATH "${CMAKE_SOURCE_DIR}/clap-juce-extensions" CACHE PATH "Path to clap-juce-extensions")
        # One shared clap-juce-extensions for every repo: probe $CLAP_JUCE_PATH
        # before FetchContent, exactly as JUCE_PATH is probed above. It pulls the
        # CLAP SDK as submodules, so a private copy per repo is the slowest
        # dependency in the tree (2026-07-27).
        if(NOT EXISTS "${CLAP_JUCE_PATH}/CMakeLists.txt" AND EXISTS "$ENV{CLAP_JUCE_PATH}/CMakeLists.txt")
            set(CLAP_JUCE_PATH "$ENV{CLAP_JUCE_PATH}")
        endif()
        if(EXISTS "${CLAP_JUCE_PATH}/CMakeLists.txt")
            add_subdirectory("${CLAP_JUCE_PATH}" "${CMAKE_BINARY_DIR}/clap-juce-extensions")
        else()
            message(STATUS "clap-juce-extensions not found locally — fetching free-audio/clap-juce-extensions")
            include(FetchContent)
            FetchContent_Declare(clap-juce-extensions
                GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
                GIT_TAG main)   # not GIT_SHALLOW: it carries the CLAP SDK as submodules
            FetchContent_MakeAvailable(clap-juce-extensions)
        endif()
    endif()
    clap_juce_extensions_plugin(TARGET Vane
        CLAP_ID       "com.enkerli.Vane"
        CLAP_FEATURES instrument synthesizer)
endif()

add_compile_options($<$<CXX_COMPILER_ID:GNU,Clang>:-finput-charset=UTF-8>)

# ── Build timestamp ───────────────────────────────────────────────────────────
# Regenerates BuildStamp.h on every build so __DATE__/__TIME__ don't freeze
# when PluginEditor.cpp stops changing.  Uses an ALL custom target so Ninja
# always re-runs the cmake script before compiling the plugin.
set(VANE_STAMP_H "${CMAKE_CURRENT_BINARY_DIR}/BuildStamp.h")
add_custom_target(Vane_GenBuildStamp ALL
    COMMAND "${CMAKE_COMMAND}"
            "-DOUTPUT=${VANE_STAMP_H}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenBuildStamp.cmake"
    BYPRODUCTS "${VANE_STAMP_H}"
    COMMENT "Generating build timestamp → BuildStamp.h"
    VERBATIM)
add_dependencies(Vane Vane_GenBuildStamp)
target_include_directories(Vane PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# ── Sources ───────────────────────────────────────────────────────────────────
target_sources(Vane PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ModMatrixEditor.cpp
    Source/PatchPanel.cpp
    Source/Synth/Oscillator.cpp
    Source/Synth/Wavetable.cpp
    Source/Synth/SynthVoice.cpp MiniSax/Source/MiniSaxVoice.cpp
    Source/Synth/SamplePlayer.cpp
    Source/Synth/TransientLibrary.cpp
    Source/Modulation/ModMatrix.cpp
    Source/MPE/TuningClient.cpp
    Source/Preset/PresetManager.cpp Source/Preset/ChordConfigStore.cpp
    Source/Profile/ProfileManager.cpp
    Source/WebUI/WebVaneEditor.cpp
    Source/Tests/ModMatrixTests.cpp
    Source/Tests/StringSafetyTests.cpp
    Source/Tests/TuningTests.cpp
    Source/Tests/TransientTests.cpp
    Source/Tests/WavetableTests.cpp
    Source/Tests/ChordTests.cpp
)

target_include_directories(Vane PRIVATE Source MiniSax/Source)

# ── WebUI bundle ──────────────────────────────────────────────────────────────
# The UI source (index.html) lives in the music-suite monorepo (apps/vane) —
# single source shared with the standalone webapp; only the C++ editor
# (WebVaneEditor.*) stays here. Default to the maintainer's sibling checkout;
# override per machine in webui.local.cmake (gitignored) — needed here since this
# repo isn't beside music-suite. The WebVaneEditor resource provider serves the
# embedded HTML to the WKWebView (no local server).
set(VANE_WEBUI_DIR "${CMAKE_SOURCE_DIR}/../music-suite/apps/vane"
    CACHE PATH "Path to the Vane WebUI source (music-suite/apps/vane)")
include("${CMAKE_CURRENT_SOURCE_DIR}/webui.local.cmake" OPTIONAL)
# The default above assumes music-suite is a SIBLING checkout. Before failing,
# probe the other supported layouts: this repo nested INSIDE the monorepo
# (…/music-suite/Vane), and a MUSIC_SUITE environment variable pointing at
# the monorepo checkout. webui.local.cmake still wins when present.
if(NOT EXISTS "${VANE_WEBUI_DIR}/index.html")
    foreach(_webui_cand "${CMAKE_SOURCE_DIR}/../apps/vane" "$ENV{MUSIC_SUITE}/apps/vane")
        if(EXISTS "${_webui_cand}/index.html")
            set(VANE_WEBUI_DIR "${_webui_cand}" CACHE PATH
                "Path to the Vane WebUI source (music-suite/apps/vane)" FORCE)
            message(STATUS "Vane WebUI: found at ${_webui_cand}")
            break()
        endif()
    endforeach()
endif()
if(NOT EXISTS "${VANE_WEBUI_DIR}/index.html")
    message(FATAL_ERROR
        "Vane WebUI source not found at ${VANE_WEBUI_DIR}. It now lives in the "
        "music-suite monorepo (apps/vane). Check out music-suite, or set "
        "VANE_WEBUI_DIR in webui.local.cmake.")
endif()
juce_add_binary_data(VaneWebUI
    HEADER_NAME BinaryDataWebUI.h
    NAMESPACE   BinaryData
    SOURCES     "${VANE_WEBUI_DIR}/index.html")
target_link_libraries(Vane PRIVATE VaneWebUI)

# ── Factory wavetable library (bundled, CC0 / Public Domain) ─────────────────
# A curated subset compiled into BinaryData so the browser works on AUv3/iOS
# without any file picker.  library.json is the manifest; .wav files are the
# tables.  Re-run Assets/library/build_manifest.py to regenerate the manifest.
juce_add_binary_data(VaneLibrary
    HEADER_NAME BinaryDataLibrary.h
    NAMESPACE   BinaryDataLib
    SOURCES
        Assets/library/library.json
        Assets/library/AKWF_violin.wav
        Assets/library/AKWF_oboe.wav
        Assets/library/AKWF_pluckalgo.wav
        Assets/library/AKWF_stringbox.wav
        Assets/library/AKWF_bw_perfectwaves.wav)
target_link_libraries(Vane PRIVATE VaneLibrary)

# ── Factory transient samples (PCM attacks / breath chiffs) ──────────────────
# All samples are CC0-1.0 (Versilian Studios VSCO-2-CE and VCSL).
# Trimmed to attack-onset only (280–380 ms), mono, 16-bit PCM.
# Sources:
#   Flute, Clarinet, Cello, Trumpet: github.com/sgossner/VSCO-2-CE
#   Recorder:                        github.com/sgossner/VCSL
juce_add_binary_data(VaneTransients
    HEADER_NAME BinaryDataTransients.h
    NAMESPACE   BinaryDataTrn
    SOURCES
        Assets/transients/flute_stac_A4.wav
        Assets/transients/recorder_stac_D4.wav
        Assets/transients/clarinet_stac_D4.wav
        Assets/transients/cello_spic_C3.wav
        Assets/transients/trumpet_stac_A4.wav
        Assets/transients/gen_tongue.wav
        Assets/transients/gen_click.wav
        Assets/transients/gen_chiff.wav
        Assets/transients/gen_knock.wav
        Assets/transients/gen_pick.wav
        Assets/transients/gen_buzz.wav
        Assets/transients/gen_ping.wav
        Assets/transients/gen_squeak.wav
        Assets/transients/gen_scratch.wav
        Assets/transients/gen_tick.wav)
target_link_libraries(Vane PRIVATE VaneTransients)

# On Linux, the opt-in headless MODEP/Pi build (-DVANE_LINUX_HEADLESS=ON) drops
# the WebView UI: MODEP renders plugin controls from the LV2 ports and never
# shows a plugin's native GUI, so the WebView is unnecessary — and dropping it
# removes the heavy WebKitGTK + GTK3 build dependency. The default desktop build
# keeps the WebView (and builds VST3).
if(IS_LINUX AND VANE_LINUX_HEADLESS)
    target_compile_definitions(Vane PUBLIC JUCE_WEB_BROWSER=0 VANE_HEADLESS=1)
    message(STATUS "Linux build is HEADLESS (MODEP/Pi: no WebView UI, no WebKitGTK, no VST3)")
else()
    target_compile_definitions(Vane PUBLIC JUCE_WEB_BROWSER=1)   # WKWebView on Apple / WebKitGTK on Linux
    if(IS_LINUX)
        # Desktop/VNC standalone: WebView UI (needs libwebkit2gtk-*-dev) + JACK
        # so Vane shows up as a JACK client you can patch in the Patchbox graph.
        target_compile_definitions(Vane PUBLIC JUCE_JACK=1)
        message(STATUS "Linux build uses WebView UI + JACK (needs libwebkit2gtk + JACK)")
        # juce_gui_extra's WebBrowserComponent includes <gtk/gtk.h> + WebKitGTK,
        # but JUCE's module CMake doesn't always propagate their include dirs to
        # the consuming target — add them explicitly (try 4.1, fall back to 4.0).
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(VANE_WEBKIT QUIET IMPORTED_TARGET webkit2gtk-4.1 gtk+-x11-3.0)
        if(NOT VANE_WEBKIT_FOUND)
            pkg_check_modules(VANE_WEBKIT REQUIRED IMPORTED_TARGET webkit2gtk-4.0 gtk+-x11-3.0)
        endif()
        target_link_libraries(Vane PRIVATE PkgConfig::VANE_WEBKIT)
    endif()
endif()
target_compile_definitions(Vane PUBLIC
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

if(VANE_HAS_MTS)
    target_compile_definitions(Vane PUBLIC VANE_HAS_MTS=1)
    target_link_libraries(Vane PRIVATE VaneMTS)
endif()

target_link_libraries(Vane PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
# LTO is fragile on a 4 GB Pi (link can OOM); enable it only off-device.
if(NOT IS_LINUX)
    target_link_libraries(Vane PRIVATE juce::juce_recommended_lto_flags)
endif()

# ── Offline render probe (headless diagnostics) ───────────────────────────────
# Instantiates VaneProcessor and renders audio with no GUI, so engine behaviour
# can be measured without a host.  Reuses the plugin's own source files.
juce_add_console_app(VaneRenderProbe PRODUCT_NAME VaneRenderProbe)
set_target_properties(VaneRenderProbe PROPERTIES EXCLUDE_FROM_ALL TRUE)  # build on demand only
target_sources(VaneRenderProbe PRIVATE
    Tools/RenderProbe/main.cpp
    Source/PluginProcessor.cpp
    Source/Synth/Oscillator.cpp
    Source/Synth/Wavetable.cpp
    Source/Synth/SynthVoice.cpp MiniSax/Source/MiniSaxVoice.cpp
    Source/Synth/SamplePlayer.cpp
    Source/Synth/TransientLibrary.cpp
    Source/Modulation/ModMatrix.cpp
    Source/MPE/TuningClient.cpp
    Source/Preset/PresetManager.cpp Source/Preset/ChordConfigStore.cpp
    Source/Profile/ProfileManager.cpp
    Source/WebUI/WebVaneEditor.cpp
    Source/Tests/ModMatrixTests.cpp
    Source/Tests/StringSafetyTests.cpp
    Source/Tests/TuningTests.cpp
    Source/Tests/TransientTests.cpp
    Source/Tests/WavetableTests.cpp
    Source/Tests/ChordTests.cpp
)
target_include_directories(VaneRenderProbe PRIVATE Source MiniSax/Source "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(VaneRenderProbe PUBLIC
    JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="Vane")
# Headless on Linux, mirroring the main plugin and VanePresetExport: the raw
# JUCE_WEB_BROWSER=1 define misses JUCE's WebKitGTK pkg-config plumbing there.
if(IS_LINUX)
    target_compile_definitions(VaneRenderProbe PUBLIC JUCE_WEB_BROWSER=0 VANE_HEADLESS=1)
else()
    target_compile_definitions(VaneRenderProbe PUBLIC JUCE_WEB_BROWSER=1)
endif()
add_dependencies(VaneRenderProbe Vane_GenBuildStamp)
if(VANE_HAS_MTS)
    target_compile_definitions(VaneRenderProbe PUBLIC VANE_HAS_MTS=1)
    target_link_libraries(VaneRenderProbe PRIVATE VaneMTS)
endif()
target_link_libraries(VaneRenderProbe PRIVATE
    VaneWebUI VaneLibrary VaneTransients
    juce::juce_audio_basics juce::juce_audio_devices juce::juce_audio_formats
    juce::juce_audio_processors juce::juce_audio_utils juce::juce_core
    juce::juce_data_structures juce::juce_dsp juce::juce_events
    juce::juce_graphics juce::juce_gui_basics juce::juce_gui_extra
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ── Preset generator (writes the playing-test presets, on demand) ─────────────
juce_add_console_app(VanePresetGen PRODUCT_NAME VanePresetGen)
set_target_properties(VanePresetGen PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(VanePresetGen PRIVATE
    Tools/PresetGen/main.cpp
    Source/PluginProcessor.cpp Source/Synth/Oscillator.cpp Source/Synth/Wavetable.cpp
    Source/Synth/SynthVoice.cpp MiniSax/Source/MiniSaxVoice.cpp Source/Synth/SamplePlayer.cpp Source/Synth/TransientLibrary.cpp
    Source/Modulation/ModMatrix.cpp Source/MPE/TuningClient.cpp Source/Preset/PresetManager.cpp Source/Preset/ChordConfigStore.cpp
    Source/Profile/ProfileManager.cpp Source/WebUI/WebVaneEditor.cpp
    Source/Tests/ModMatrixTests.cpp Source/Tests/StringSafetyTests.cpp Source/Tests/TuningTests.cpp
    Source/Tests/TransientTests.cpp Source/Tests/WavetableTests.cpp Source/Tests/ChordTests.cpp)
target_include_directories(VanePresetGen PRIVATE Source MiniSax/Source "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(VanePresetGen PUBLIC
    JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Vane")
if(IS_LINUX)   # headless on Linux (see VaneRenderProbe note)
    target_compile_definitions(VanePresetGen PUBLIC JUCE_WEB_BROWSER=0 VANE_HEADLESS=1)
else()
    target_compile_definitions(VanePresetGen PUBLIC JUCE_WEB_BROWSER=1)
endif()
add_dependencies(VanePresetGen Vane_GenBuildStamp)
if(VANE_HAS_MTS)
    target_compile_definitions(VanePresetGen PUBLIC VANE_HAS_MTS=1)
    target_link_libraries(VanePresetGen PRIVATE VaneMTS)
endif()
target_link_libraries(VanePresetGen PRIVATE
    VaneWebUI VaneLibrary VaneTransients
    juce::juce_audio_basics juce::juce_audio_devices juce::juce_audio_formats
    juce::juce_audio_processors juce::juce_audio_utils juce::juce_core
    juce::juce_data_structures juce::juce_dsp juce::juce_events
    juce::juce_graphics juce::juce_gui_basics juce::juce_gui_extra
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ── Preset exporter (→ MODEP-style LV2 preset bundles, on demand) ─────────────
# Converts every *.vanepreset into a /var/modep/lv2-presets/Vane-<name>.lv2 bundle
# by running the real getStateInformation() → base64.  Built headless on Linux so
# it pulls in no WebKitGTK (mirrors the main plugin's VANE_HEADLESS path).
juce_add_console_app(VanePresetExport PRODUCT_NAME VanePresetExport)
set_target_properties(VanePresetExport PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(VanePresetExport PRIVATE
    Tools/PresetExport/main.cpp
    Source/PluginProcessor.cpp Source/Synth/Oscillator.cpp Source/Synth/Wavetable.cpp
    Source/Synth/SynthVoice.cpp MiniSax/Source/MiniSaxVoice.cpp Source/Synth/SamplePlayer.cpp Source/Synth/TransientLibrary.cpp
    Source/Modulation/ModMatrix.cpp Source/MPE/TuningClient.cpp Source/Preset/PresetManager.cpp Source/Preset/ChordConfigStore.cpp
    Source/Profile/ProfileManager.cpp Source/WebUI/WebVaneEditor.cpp)
target_include_directories(VanePresetExport PRIVATE Source MiniSax/Source "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(VanePresetExport PUBLIC
    JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Vane")
if(IS_LINUX)
    target_compile_definitions(VanePresetExport PUBLIC JUCE_WEB_BROWSER=0 VANE_HEADLESS=1)
else()
    target_compile_definitions(VanePresetExport PUBLIC JUCE_WEB_BROWSER=1)
endif()
add_dependencies(VanePresetExport Vane_GenBuildStamp)
if(VANE_HAS_MTS)
    target_compile_definitions(VanePresetExport PUBLIC VANE_HAS_MTS=1)
    target_link_libraries(VanePresetExport PRIVATE VaneMTS)
endif()
target_link_libraries(VanePresetExport PRIVATE
    VaneWebUI VaneLibrary VaneTransients
    juce::juce_audio_basics juce::juce_audio_devices juce::juce_audio_formats
    juce::juce_audio_processors juce::juce_audio_utils juce::juce_core
    juce::juce_data_structures juce::juce_dsp juce::juce_events
    juce::juce_graphics juce::juce_gui_basics juce::juce_gui_extra
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ── VaneSelfTest ──────────────────────────────────────────────────────────────
# Authoritative headless unit-test gate.  Mirrors VanePresetGen's source/link set
# (so the in-tree tests + their deps compile in directly and are not dead-stripped
# as they are from the plugin's shared-code lib) but its main() runs UnitTestRunner
# and returns exit code == failure count.  Build + run on demand:
#   cmake --build build-mac --target VaneSelfTest && \
#     ./build-mac/VaneSelfTest_artefacts/<cfg>/VaneSelfTest ; echo $?
juce_add_console_app(VaneSelfTest PRODUCT_NAME VaneSelfTest)
set_target_properties(VaneSelfTest PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(VaneSelfTest PRIVATE
    Tools/SelfTest/main.cpp
    Source/PluginProcessor.cpp Source/Synth/Oscillator.cpp Source/Synth/Wavetable.cpp
    Source/Synth/SynthVoice.cpp MiniSax/Source/MiniSaxVoice.cpp Source/Synth/SamplePlayer.cpp Source/Synth/TransientLibrary.cpp
    Source/Modulation/ModMatrix.cpp Source/MPE/TuningClient.cpp Source/Preset/PresetManager.cpp Source/Preset/ChordConfigStore.cpp
    Source/Profile/ProfileManager.cpp Source/WebUI/WebVaneEditor.cpp
    Source/Tests/ModMatrixTests.cpp Source/Tests/StringSafetyTests.cpp Source/Tests/TuningTests.cpp
    Source/Tests/TransientTests.cpp Source/Tests/WavetableTests.cpp Source/Tests/ChordTests.cpp)
target_include_directories(VaneSelfTest PRIVATE Source MiniSax/Source "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(VaneSelfTest PUBLIC
    JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Vane")
if(IS_LINUX)   # headless on Linux (see VaneRenderProbe note)
    target_compile_definitions(VaneSelfTest PUBLIC JUCE_WEB_BROWSER=0 VANE_HEADLESS=1)
else()
    target_compile_definitions(VaneSelfTest PUBLIC JUCE_WEB_BROWSER=1)
endif()
add_dependencies(VaneSelfTest Vane_GenBuildStamp)
if(VANE_HAS_MTS)
    target_compile_definitions(VaneSelfTest PUBLIC VANE_HAS_MTS=1)
    target_link_libraries(VaneSelfTest PRIVATE VaneMTS)
endif()
target_link_libraries(VaneSelfTest PRIVATE
    VaneWebUI VaneLibrary VaneTransients
    juce::juce_audio_basics juce::juce_audio_devices juce::juce_audio_formats
    juce::juce_audio_processors juce::juce_audio_utils juce::juce_core
    juce::juce_data_structures juce::juce_dsp juce::juce_events
    juce::juce_graphics juce::juce_gui_basics juce::juce_gui_extra
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
