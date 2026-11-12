cmake_minimum_required(VERSION 3.22)

project(Vocalypse VERSION 0.7.0 LANGUAGES C CXX)

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

option(VOCALYPSE_BUILD_TESTS "Build the offline DSP/state test runner" ON)
# Licence check (Lemon Squeezy): OFF until the store exists - docs/LICENSING.md
option(VOCALYPSE_LICENSING "Trial + licence key activation in the plugin" OFF)

# ---------------------------------------------------------------------------
# Sources shared by the plugin and the test runner
# ---------------------------------------------------------------------------
set(VOCALYPSE_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters/ParameterLayout.cpp
    Source/DSP/Filters.cpp
    Source/DSP/EqualizerModule.cpp
    Source/DSP/EqDesign.cpp
    Source/DSP/CompressorModule.cpp
    Source/DSP/DeEsserModule.cpp
    Source/DSP/SaturationModule.cpp
    Source/DSP/SatOversampler.cpp
    Source/DSP/DelayModule.cpp
    Source/DSP/ReverbModule.cpp
    Source/DSP/ReverbDepth.cpp
    Source/DSP/VocalChain.cpp
    Source/DSP/Routing.cpp
    Source/DSP/MixLaw.cpp
    Source/DSP/Loudness.cpp
    Source/DSP/StereoStage.cpp
    Source/DSP/DoublerModule.cpp
    Source/DSP/Ducker.cpp
    Source/DSP/SmashModule.cpp
    Source/DSP/DenoiseModule.cpp
    Source/DSP/BreathModule.cpp
    Source/DSP/SmoothModule.cpp
    Source/DSP/CheckModule.cpp
    Source/DSP/VocalAnalyzer.cpp
    Source/Check/CheckTargets.cpp
    Source/Check/CheckRules.cpp
    Source/Check/CheckFixPlanner.cpp
    Source/Check/VocalCheckController.cpp
    Source/DSP/RideModule.cpp
    Source/DSP/Macros.cpp
    Source/Presets/ParameterSnapshot.cpp
    Source/Presets/FactoryPresets.cpp
    Source/Presets/PresetManager.cpp
    Source/Presets/ModulePresets.cpp
    Source/Presets/UndoHistory.cpp
    Source/Mixing/ProposalSession.cpp
    Source/UI/Theme.cpp
    Source/UI/VocalypseLookAndFeel.cpp
    Source/UI/Widgets.cpp
    Source/UI/Segments.cpp
    Source/UI/ValueText.cpp
    Source/UI/SimpleView.cpp
    Source/UI/CleanupPanels.cpp
    Source/UI/MascotView.cpp
    Source/UI/PresetBrowser.cpp
    Source/UI/Spectrum.cpp
    Source/UI/ModuleViews.cpp
    Source/UI/ChainView.cpp
    Source/UI/ModuleBrowser.cpp
    Source/UI/DetailPanels.cpp
    Source/UI/VocalCheckView.cpp
    Source/UI/EqPanel.cpp
    Source/UI/ReverbPanel.cpp
    Source/UI/SmoothPanel.cpp
    Source/UI/AutoMixView.cpp
    Source/UI/AnalysisOverlay.cpp
    Source/UI/TopBar.cpp
    Source/Parameters/SettingsReader.cpp
    Source/Parameters/EqBands.cpp
    Source/Parameters/RoutingMigration.cpp
    Source/Parameters/MacroBake.cpp
    Source/Parameters/MacroMapper.cpp
    Source/AutoMix/CaptureBuffer.cpp
    Source/AutoMix/VocalProfiler.cpp
    Source/AutoMix/SegmentAnalysis.cpp
    Source/AutoMix/AnalysisStore.cpp
    Source/AutoMix/AutoMixPlanner.cpp
    Source/AutoMix/AutoEq.cpp
    Source/AutoMix/AutoMixEngine.cpp
    Source/AutoMix/AutoMixObjective.cpp
    Source/AutoMix/AutoMixController.cpp
    Source/AutoMix/MixTarget.cpp
    Source/AutoMix/DecisionLog.cpp
    Source/UI/UserSettings.cpp
    Source/UI/HelpContent.cpp
    Source/UI/HelpOverlay.cpp
    Source/UI/TutorialOverlay.cpp
    Source/UI/SettingsOverlay.cpp
    Source/UI/LicenceUI.cpp
    Source/Text/I18n.cpp
    Source/Text/ParamText.cpp
    Source/Licensing/License.cpp
    Source/Licensing/VocalypseLicence.cpp
)

# Saturation panel (0.6: character tiles, big Drive, Low / Mid / High strip)
list(APPEND VOCALYPSE_SOURCES Source/UI/SatPanel.cpp)

set(VOCALYPSE_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    VOCALYPSE_LICENSING=$<BOOL:${VOCALYPSE_LICENSING}>
    VOCALYPSE_VERSION_STRING="${PROJECT_VERSION}"   # the one place of the version: project() above
)

# Third-party, header-only (MIT): Signalsmith Stretch 1.4.0 + Signalsmith Linear 0.6.4
set(VOCALYPSE_THIRD_PARTY_INCLUDES
    third_party/signalsmith-stretch
    third_party/signalsmith-linear/include
)

# Embedded UI typefaces: Montserrat (SIL OFL 1.1, see Resources/Fonts/Montserrat-LICENSE.txt)
# and the VOCALYPSE logo (icon, word mark, full logo; transparent PNGs)
juce_add_binary_data(VocalypseAssets
    HEADER_NAME VocalypseBinaryData.h
    NAMESPACE VocalypseBinaryData
    SOURCES
        Resources/Fonts/Montserrat-Light.ttf
        Resources/Fonts/Montserrat-Regular.ttf
        Resources/Fonts/Montserrat-Medium.ttf
        Resources/Fonts/Montserrat-SemiBold.ttf
        Resources/Fonts/Montserrat-Bold.ttf
        Resources/Fonts/RobotoMono-Regular.ttf
        Resources/Icons/levelride.svg Resources/Icons/smooth.svg Resources/Icons/vocalsmash.svg
        Resources/Icons/delay.svg Resources/Icons/reverb.svg
        Resources/Icons/eq.svg Resources/Icons/comp.svg Resources/Icons/deess.svg Resources/Icons/sat.svg
        Resources/Icons/doubler.svg Resources/Icons/check.svg Resources/Icons/denoise.svg Resources/Icons/breath.svg
        Resources/Images/VocalypseIcon.png
        Resources/Images/VocalypseWordmark.png
        # Memphy's comic stickers, one per mood (tools/mascot/cut_comic.py)
        Resources/MascotSticker/listening.png Resources/MascotSticker/talking.png Resources/MascotSticker/wink.png
        Resources/MascotSticker/celebrate.png Resources/MascotSticker/sad.png Resources/MascotSticker/shocked.png
        Resources/MascotSticker/thinking.png Resources/MascotSticker/proud.png Resources/MascotSticker/annoyed.png)

set(VOCALYPSE_COMMON_LIBS
    VocalypseAssets
    juce::juce_audio_utils
    juce::juce_dsp
)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
juce_add_plugin(Vocalypse
    COMPANY_NAME                "Lind Audio"
    COMPANY_WEBSITE             ""
    BUNDLE_ID                   "com.lindaudio.vocalypse"
    # NEVER change the two codes: the VST3 class ID comes from them - hosts open projects saved with
    # Velvox / Headliner (the earlier names) with VOCALYPSE only because they stay the same
    PLUGIN_MANUFACTURER_CODE    Lnda
    PLUGIN_CODE                 Vlvx
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "VOCALYPSE"
    DESCRIPTION                 "Automatic vocal mixer: analysis, EQ, dynamics, de-essing, resonance control, effects"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Dynamics EQ Reverb
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Vocalypse PRIVATE ${VOCALYPSE_SOURCES})
target_include_directories(Vocalypse PRIVATE Source ${VOCALYPSE_THIRD_PARTY_INCLUDES})
if(MSVC)
    target_compile_options(Vocalypse PRIVATE /bigobj /utf-8)
endif()
target_compile_definitions(Vocalypse PUBLIC ${VOCALYPSE_COMMON_DEFINITIONS})
target_link_libraries(Vocalypse
    PRIVATE
        ${VOCALYPSE_COMMON_LIBS}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline test runner (console app, no host required)
# ---------------------------------------------------------------------------
if(VOCALYPSE_BUILD_TESTS)
    juce_add_console_app(VocalypseTests PRODUCT_NAME "VocalypseTests")
    target_sources(VocalypseTests PRIVATE ${VOCALYPSE_SOURCES} Tests/TestMain.cpp Tests/VocalypseTests.cpp Tests/RoutingTests.cpp Tests/OutputStageTests.cpp Tests/DoublerTests.cpp Tests/DuckingTests.cpp Tests/ProExtensionTests.cpp Tests/MacroTests.cpp Tests/MacroBakeTests.cpp Tests/UiTests.cpp Tests/PresetTests.cpp Tests/MixingTests.cpp Tests/PerformanceTests.cpp Tests/NewModuleTests.cpp Tests/UxTests.cpp Tests/AutoMixTests.cpp Tests/MixTargetTests.cpp Tests/StabilityTests.cpp Tests/FxQualityTests.cpp Tests/CompareTests.cpp Tests/VocalCheckTests.cpp Tests/VocalCheckFixTests.cpp Tests/CheckEval.cpp Tests/VocalCheckRealDataTests.cpp Tests/PerfProbe.cpp Tests/DesignTests.cpp Tests/UndoTests.cpp Tests/GestureTests.cpp Tests/ShellTests.cpp Tests/MixLawTests.cpp Tests/PanelFrameTests.cpp Tests/PresetBrowserTests.cpp Tests/EqModelTests.cpp Tests/EqModuleTests.cpp Tests/EqRoleTests.cpp Tests/EqTests.cpp Tests/SaturationTests.cpp Tests/ReverbQualityTests.cpp Tests/EqPanelTests.cpp Tests/ReverbPanelTests.cpp Tests/CheckPanelTests.cpp Tests/SmoothPanelTests.cpp Tests/DynamicsQualityTests.cpp Tests/StateRestoreTests.cpp Tests/LicenseTests.cpp Tests/ConsistencyHarness.cpp Tests/ConsistencyTests.cpp Tests/SoftDecisionTests.cpp Tests/SegmentTests.cpp)
    target_include_directories(VocalypseTests PRIVATE Source ${VOCALYPSE_THIRD_PARTY_INCLUDES})
    if(MSVC)
        target_compile_options(VocalypseTests PRIVATE /bigobj /utf-8)
    endif()
    # Modal loops only for the test runner (used to pump the message loop for UI snapshots)
    target_compile_definitions(VocalypseTests PRIVATE ${VOCALYPSE_COMMON_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=1
        VOCALYPSE_PROJECT_VERSION="${PROJECT_VERSION}")   # tests check ProductInfo against it
    target_link_libraries(VocalypseTests
        PRIVATE
            ${VOCALYPSE_COMMON_LIBS}
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Reference host (0.6 DSP quality): renders WAVs through one VOCALYPSE module or an
# installed VST3 (VST3 hosting enabled for this tool only). See docs/QUALITY_BASELINE.md.
# ---------------------------------------------------------------------------
if(VOCALYPSE_BUILD_TESTS)
    target_sources(VocalypseTests PRIVATE Tests/RefHostTests.cpp)
    target_include_directories(VocalypseTests PRIVATE tools/refhost)

    juce_add_console_app(VocalypseRefHost PRODUCT_NAME "VocalypseRefHost")
    target_sources(VocalypseRefHost PRIVATE ${VOCALYPSE_SOURCES} tools/refhost/RefHostMain.cpp)
    target_include_directories(VocalypseRefHost PRIVATE Source tools/refhost ${VOCALYPSE_THIRD_PARTY_INCLUDES})
    if(MSVC)
        target_compile_options(VocalypseRefHost PRIVATE /bigobj /utf-8)
    endif()
    target_compile_definitions(VocalypseRefHost PRIVATE ${VOCALYPSE_COMMON_DEFINITIONS}
        JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(VocalypseRefHost
        PRIVATE
            ${VOCALYPSE_COMMON_LIBS}
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

# Time modules quality tests (0.6: Delay, Pulse, Stutter)
if(VOCALYPSE_BUILD_TESTS)
    target_sources(VocalypseTests PRIVATE Tests/TimeModuleTests.cpp)
endif()

# Saturation panel tests (0.6)
if(VOCALYPSE_BUILD_TESTS)
    target_sources(VocalypseTests PRIVATE Tests/SatPanelTests.cpp)
endif()

# Chain solo + scrolling tests (0.6.1)
if(VOCALYPSE_BUILD_TESTS)
    target_sources(VocalypseTests PRIVATE Tests/ChainSoloTests.cpp)
endif()

# Time module panels (0.6: Delay echo map, Pulse like Kickstart, Stutter cuts on the waveform)
target_sources(Vocalypse PRIVATE Source/UI/TimePanels.cpp)
if(VOCALYPSE_BUILD_TESTS)
    target_sources(VocalypseTests PRIVATE Source/UI/TimePanels.cpp Tests/TimePanelTests.cpp)
    target_sources(VocalypseRefHost PRIVATE Source/UI/TimePanels.cpp)
endif()

# Voice module panels (0.6: Doubler like Waves Doubler, Choir half-circle stage)
target_sources(Vocalypse PRIVATE Source/UI/VoicesPanels.cpp)
if(VOCALYPSE_BUILD_TESTS)
    target_sources(VocalypseTests PRIVATE Source/UI/VoicesPanels.cpp Tests/VoicesPanelTests.cpp)
    target_sources(VocalypseRefHost PRIVATE Source/UI/VoicesPanels.cpp)
endif()

# Dynamics module panels (0.6: Compressor Pro-C style, De-Esser Pro-DS style, Vocal Smash, Level Ride word bars)
target_sources(Vocalypse PRIVATE Source/UI/DynPanels.cpp)
if(VOCALYPSE_BUILD_TESTS)
    target_sources(VocalypseTests PRIVATE Source/UI/DynPanels.cpp Tests/DynPanelTests.cpp)
    target_sources(VocalypseRefHost PRIVATE Source/UI/DynPanels.cpp)
endif()

# 0.6.1 auto-mix EQ (notches and broad tonal moves)
if(VOCALYPSE_BUILD_TESTS)
    target_sources(VocalypseTests PRIVATE Tests/AutoEqTests.cpp)
endif()

# Audition (0.6.1: EQ band solo, Saturation band solo; monitoring states, not parameters)
if(VOCALYPSE_BUILD_TESTS)
    target_sources(VocalypseTests PRIVATE Tests/AuditionTests.cpp Tests/I18nTests.cpp)
endif()

# Mascot (Memphy, the Vocalypse imp in SIMPEL)
if(VOCALYPSE_BUILD_TESTS)
    target_sources(VocalypseTests PRIVATE Tests/MascotTests.cpp Tests/DenoiseTests.cpp Tests/BreathTests.cpp)
endif()

# The auto mix hears the beat (plan 2026-10-28)
if(VOCALYPSE_BUILD_TESTS)
    target_sources(VocalypseTests PRIVATE Tests/BeatTests.cpp)
endif()

# Single plugins of the VOCALYPSE family
include(Products/Ember/Ember.cmake)
