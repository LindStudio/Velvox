cmake_minimum_required(VERSION 3.22)

project(Velvox VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE location
#   Pass -DJUCE_DIR=<path to JUCE checkout>, or set the JUCE_DIR environment
#   variable. JUCE is intentionally NOT stored inside this (Google-Drive-synced)
#   source folder.
# ---------------------------------------------------------------------------
if(NOT JUCE_DIR AND DEFINED ENV{JUCE_DIR})
    set(JUCE_DIR "$ENV{JUCE_DIR}")
endif()
set(JUCE_DIR "${JUCE_DIR}" CACHE PATH "Path to the JUCE framework checkout (8.0.x)")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found. Configure with -DJUCE_DIR=<path-to-JUCE> "
        "(e.g. C:/Users/<you>/dev/JUCE-8.0.9) or set the JUCE_DIR environment variable.")
endif()

add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

option(VELVOX_BUILD_TESTS "Build the offline DSP/state test runner" ON)

# ---------------------------------------------------------------------------
# Sources shared by the plugin and the test runner
# ---------------------------------------------------------------------------
set(VELVOX_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters/ParameterLayout.cpp
    Source/DSP/Filters.cpp
    Source/DSP/EqualizerModule.cpp
    Source/DSP/CompressorModule.cpp
    Source/DSP/DeEsserModule.cpp
    Source/DSP/SaturationModule.cpp
    Source/DSP/DelayModule.cpp
    Source/DSP/ReverbModule.cpp
    Source/DSP/VocalChain.cpp
    Source/DSP/Routing.cpp
    Source/DSP/Loudness.cpp
    Source/DSP/StereoStage.cpp
    Source/DSP/DoublerModule.cpp
    Source/DSP/Ducker.cpp
    Source/DSP/PitchModule.cpp
    Source/DSP/ChopModule.cpp
    Source/DSP/Macros.cpp
    Source/Presets/ParameterSnapshot.cpp
    Source/Presets/FactoryPresets.cpp
    Source/Presets/PresetManager.cpp
    Source/UI/VelvoxLookAndFeel.cpp
    Source/UI/Widgets.cpp
    Source/UI/ChainView.cpp
    Source/UI/DetailPanels.cpp
    Source/UI/BeginnerView.cpp
    Source/UI/TopBar.cpp
)

set(VELVOX_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

# Third-party, header-only (MIT): Signalsmith Stretch 1.4.0 + Signalsmith Linear 0.6.4
set(VELVOX_THIRD_PARTY_INCLUDES
    third_party/signalsmith-stretch
    third_party/signalsmith-linear/include
)

set(VELVOX_COMMON_LIBS
    juce::juce_audio_utils
    juce::juce_dsp
)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
juce_add_plugin(Velvox
    COMPANY_NAME                "Lind Audio"
    COMPANY_WEBSITE             ""
    BUNDLE_ID                   "com.lindaudio.velvox"
    PLUGIN_MANUFACTURER_CODE    Lnda
    PLUGIN_CODE                 Vlvx
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "Velvox"
    DESCRIPTION                 "Vocal chain: EQ, compressor, de-esser, saturation, delay, reverb"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Dynamics EQ Reverb
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Velvox PRIVATE ${VELVOX_SOURCES})
target_include_directories(Velvox PRIVATE Source ${VELVOX_THIRD_PARTY_INCLUDES})
if(MSVC)
    target_compile_options(Velvox PRIVATE /bigobj)
endif()
target_compile_definitions(Velvox PUBLIC ${VELVOX_COMMON_DEFINITIONS})
target_link_libraries(Velvox
    PRIVATE
        ${VELVOX_COMMON_LIBS}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline test runner (console app, no host required)
# ---------------------------------------------------------------------------
if(VELVOX_BUILD_TESTS)
    juce_add_console_app(VelvoxTests PRODUCT_NAME "VelvoxTests")
    target_sources(VelvoxTests PRIVATE ${VELVOX_SOURCES} Tests/TestMain.cpp Tests/VelvoxTests.cpp Tests/RoutingTests.cpp Tests/OutputStageTests.cpp Tests/DoublerTests.cpp Tests/DuckingTests.cpp Tests/PitchTests.cpp Tests/PitchDiagnostics.cpp Tests/ChopTests.cpp Tests/ProExtensionTests.cpp Tests/MacroTests.cpp Tests/UiTests.cpp)
    target_include_directories(VelvoxTests PRIVATE Source ${VELVOX_THIRD_PARTY_INCLUDES})
    if(MSVC)
        target_compile_options(VelvoxTests PRIVATE /bigobj)
    endif()
    # Modal loops only for the test runner (used to pump the message loop for UI snapshots)
    target_compile_definitions(VelvoxTests PRIVATE ${VELVOX_COMMON_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(VelvoxTests
        PRIVATE
            ${VELVOX_COMMON_LIBS}
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
