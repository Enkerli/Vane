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
    juce_add_plugin(Vane
        ${PLUGIN_BASE_PROPS}
        FORMATS                     LV2 Standalone
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
    Source/Modulation/ModMatrix.cpp
    Source/MPE/TuningClient.cpp
    Source/Preset/PresetManager.cpp
    Source/Profile/ProfileManager.cpp
    Source/WebUI/WebVaneEditor.cpp
    Source/Tests/ModMatrixTests.cpp
    Source/Tests/StringSafetyTests.cpp
    Source/Tests/TuningTests.cpp
    Source/Tests/WavetableTests.cpp
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

target_compile_definitions(Vane PUBLIC
    JUCE_WEB_BROWSER=1          # WebView UI (WebVaneEditor) — WKWebView on Apple
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
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
