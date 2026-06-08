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
    if(NOT EXISTS "${JUCE_PATH}/CMakeLists.txt")
        message(FATAL_ERROR
            "JUCE not found.\n"
            "  ln -s /Users/alex/JUCE ${CMAKE_SOURCE_DIR}/JUCE\n")
    endif()
    add_subdirectory("${JUCE_PATH}" JUCE)
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
    message(STATUS "MTS-ESP found — microtuning enabled")
else()
    set(VANE_HAS_MTS FALSE)
    message(STATUS "MTS-ESP not found — equal temperament fallback (see Libs/MTS-ESP/README.md)")
endif()

# ── Plugin ────────────────────────────────────────────────────────────────────
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
    # iOS cross-compiles (there is no plugin install location on the build host).
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
        COPY_PLUGIN_AFTER_BUILD     TRUE
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
    # Two Linux personalities, selected by VANE_LINUX_WEBVIEW (default OFF):
    #   OFF → headless LV2 (+ Standalone) for MODEP: no WebView, no WebKitGTK.
    #   ON  → WebView Standalone for desktop/VNC use (needs libwebkit2gtk dev)
    #         with JACK audio.  No LV2 here — a WebView LV2 is useless to MODEP.
    # Build the two into separate dirs (e.g. build/ and build-app/).
    option(VANE_LINUX_WEBVIEW
        "Linux: build the WebView Standalone (+JACK) for desktop/VNC instead of the headless MODEP LV2" OFF)
    if(VANE_LINUX_WEBVIEW)
        set(_vane_linux_formats Standalone)
    else()
        set(_vane_linux_formats LV2 Standalone)
    endif()
    juce_add_plugin(Vane
        ${PLUGIN_BASE_PROPS}
        FORMATS                     ${_vane_linux_formats}
        LV2_URI                     "https://enkerli.com/plugins/vane"
        COPY_PLUGIN_AFTER_BUILD     TRUE
    )
else()
    juce_add_plugin(Vane
        ${PLUGIN_BASE_PROPS}
        FORMATS                     Standalone
        COPY_PLUGIN_AFTER_BUILD     FALSE
    )
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
    Source/Synth/SynthVoice.cpp
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

target_include_directories(Vane PRIVATE Source)

# ── WebUI bundle ──────────────────────────────────────────────────────────────
# Compile the self-contained UI (Source/WebUI/index.html) into BinaryData; the
# WebVaneEditor resource provider serves it to the WKWebView (no local server).
# HEADER_NAME keeps it in its own header so it never clashes with other binary
# data; linking the target adds its include dir for <BinaryDataWebUI.h>.
juce_add_binary_data(VaneWebUI
    HEADER_NAME BinaryDataWebUI.h
    NAMESPACE   BinaryData
    SOURCES     Source/WebUI/index.html)
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

# On Linux (RPi / MODEP) build HEADLESS: MODEP renders plugin controls from the
# LV2 ports and never shows a plugin's native GUI, so the WebView UI is dropped —
# which also removes the heavy WebKitGTK + GTK3 build dependency.
if(IS_LINUX AND NOT VANE_LINUX_WEBVIEW)
    target_compile_definitions(Vane PUBLIC JUCE_WEB_BROWSER=0 VANE_HEADLESS=1)
    message(STATUS "Linux build is HEADLESS (no WebView UI; no WebKitGTK dependency)")
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
    Source/Synth/SynthVoice.cpp
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
target_include_directories(VaneRenderProbe PRIVATE Source "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(VaneRenderProbe PUBLIC
    JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="Vane")
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
    Source/Synth/SynthVoice.cpp Source/Synth/SamplePlayer.cpp Source/Synth/TransientLibrary.cpp
    Source/Modulation/ModMatrix.cpp Source/MPE/TuningClient.cpp Source/Preset/PresetManager.cpp Source/Preset/ChordConfigStore.cpp
    Source/Profile/ProfileManager.cpp Source/WebUI/WebVaneEditor.cpp
    Source/Tests/ModMatrixTests.cpp Source/Tests/StringSafetyTests.cpp Source/Tests/TuningTests.cpp
    Source/Tests/TransientTests.cpp Source/Tests/WavetableTests.cpp Source/Tests/ChordTests.cpp)
target_include_directories(VanePresetGen PRIVATE Source "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(VanePresetGen PUBLIC
    JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Vane")
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
    Source/Synth/SynthVoice.cpp Source/Synth/SamplePlayer.cpp Source/Synth/TransientLibrary.cpp
    Source/Modulation/ModMatrix.cpp Source/MPE/TuningClient.cpp Source/Preset/PresetManager.cpp Source/Preset/ChordConfigStore.cpp
    Source/Profile/ProfileManager.cpp Source/WebUI/WebVaneEditor.cpp)
target_include_directories(VanePresetExport PRIVATE Source "${CMAKE_CURRENT_BINARY_DIR}")
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
    Source/Synth/SynthVoice.cpp Source/Synth/SamplePlayer.cpp Source/Synth/TransientLibrary.cpp
    Source/Modulation/ModMatrix.cpp Source/MPE/TuningClient.cpp Source/Preset/PresetManager.cpp Source/Preset/ChordConfigStore.cpp
    Source/Profile/ProfileManager.cpp Source/WebUI/WebVaneEditor.cpp
    Source/Tests/ModMatrixTests.cpp Source/Tests/StringSafetyTests.cpp Source/Tests/TuningTests.cpp
    Source/Tests/TransientTests.cpp Source/Tests/WavetableTests.cpp Source/Tests/ChordTests.cpp)
target_include_directories(VaneSelfTest PRIVATE Source "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(VaneSelfTest PUBLIC
    JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="Vane")
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
