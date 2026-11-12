# ---------------------------------------------------------------------------
# EMBER De-Esser - the first single plugin of the VOCALYPSE family (plan 2026-11-10).
# Own plugin code (Embr), own parameters and state; shares the VOCALYPSE UI kit (knobs, segments,
# look-and-feel, spectrum, fonts) and DSP primitives, so every Lind Audio plugin looks and feels the same.
# Mascot frames: drop PNGs into Products/Ember/Mascot/frames (see the README there) and re-run CMake.
# ---------------------------------------------------------------------------
set(EMBER_VERSION 1.0.0)

# Licence check (Lemon Squeezy): OFF until the shop ids are in Products/Ember/Source/EmberLicence.h
option(EMBER_LICENSING "EMBER: 14-day trial + licence key activation" OFF)

set(EMBER_SOURCES
    Products/Ember/Source/EmberEngine.cpp
    Products/Ember/Source/EmberAuto.cpp
    Products/Ember/Source/EmberParams.cpp
    Products/Ember/Source/EmberMascot.cpp
    Products/Ember/Source/EmberPresets.cpp
    Products/Ember/Source/EmberProcessor.cpp
    Products/Ember/Source/EmberEditor.cpp
)

# The shared kit (subset of VOCALYPSE_SOURCES that the widgets need)
set(EMBER_SHARED_SOURCES
    Source/DSP/Filters.cpp
    Source/DSP/Routing.cpp
    Source/DSP/EqualizerModule.cpp
    Source/DSP/EqDesign.cpp
    Source/UI/Theme.cpp
    Source/UI/VocalypseLookAndFeel.cpp
    Source/UI/Widgets.cpp
    Source/UI/Segments.cpp
    Source/UI/Spectrum.cpp
    Source/UI/ValueText.cpp
    Source/Text/I18n.cpp
    Source/Text/ParamText.cpp
    Source/Parameters/ParameterLayout.cpp
    Source/UI/LicenceUI.cpp
    Source/Licensing/License.cpp
)

file(GLOB EMBER_MASCOT_FRAMES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Products/Ember/Mascot/frames/*.png")
juce_add_binary_data(EmberMascotData
    HEADER_NAME EmberMascotData.h
    NAMESPACE EmberMascotData
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/Products/Ember/Mascot/mascot.json ${EMBER_MASCOT_FRAMES})

set(EMBER_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    EMBER_VERSION_STRING="${EMBER_VERSION}"
    EMBER_LICENSING=$<BOOL:${EMBER_LICENSING}>)

juce_add_plugin(Ember
    VERSION                     ${EMBER_VERSION}
    COMPANY_NAME                "Lind Audio"
    BUNDLE_ID                   "com.lindaudio.ember"
    PLUGIN_MANUFACTURER_CODE    Lnda
    PLUGIN_CODE                 Embr          # NEVER change: the VST3 class ID comes from it
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "EMBER De-Esser"
    DESCRIPTION                 "De-esser with smart detection, auto setup and Ember the imp"
    ICON_BIG                    "${CMAKE_CURRENT_SOURCE_DIR}/Products/Ember/Installer/ember_icon.png"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(Ember PRIVATE ${EMBER_SOURCES} ${EMBER_SHARED_SOURCES})
target_include_directories(Ember PRIVATE Source Products/Ember/Source)
if(MSVC)
    target_compile_options(Ember PRIVATE /bigobj /utf-8)
endif()
target_compile_definitions(Ember PUBLIC ${EMBER_DEFINITIONS})
target_link_libraries(Ember
    PRIVATE
        VocalypseAssets
        EmberMascotData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(VOCALYPSE_BUILD_TESTS)
    juce_add_console_app(EmberTests PRODUCT_NAME "EmberTests")
    target_sources(EmberTests PRIVATE ${EMBER_SOURCES} ${EMBER_SHARED_SOURCES} Products/Ember/Tests/EmberTests.cpp)
    target_include_directories(EmberTests PRIVATE Source Products/Ember/Source)
    if(MSVC)
        target_compile_options(EmberTests PRIVATE /bigobj /utf-8)
    endif()
    target_compile_definitions(EmberTests PRIVATE ${EMBER_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(EmberTests
        PRIVATE
            VocalypseAssets
            EmberMascotData
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
