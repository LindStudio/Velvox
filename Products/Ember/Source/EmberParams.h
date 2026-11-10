#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "EmberEngine.h"

// Parameters of EMBER De-Esser. The IDs are stored in projects and presets: never rename them.
// (Prefix "em" so no ID ever collides with a VOCALYPSE parameter when the shared widgets look up specs.)
namespace ember::pid
{
    inline constexpr const char* freq      = "emFreq";
    inline constexpr const char* threshold = "emThreshold";
    inline constexpr const char* range     = "emRange";
    inline constexpr const char* amount    = "emAmount";     // Amount (50 % = nominal)
    inline constexpr const char* attack    = "emAttack";
    inline constexpr const char* release   = "emRelease";
    inline constexpr const char* lookahead = "emLookahead";
    inline constexpr const char* mix       = "emMix";
    inline constexpr const char* output    = "emOutput";
    inline constexpr const char* mode      = "emMode";       // Split / Wideband
    inline constexpr const char* detection = "emDetection";  // Relative / Absolute
    inline constexpr const char* listen    = "emListen";     // Off / S band / Delta
    inline constexpr const char* bypass    = "emBypass";
}

namespace ember
{
    using APVTS = juce::AudioProcessorValueTreeState;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Current parameter values -> engine settings
    Settings readSettings (const APVTS& state);

    // The same with the parameter pointers looked up once (audio thread)
    struct ParamCache
    {
        explicit ParamCache (const APVTS& state);
        Settings read() const noexcept;
        std::atomic<float> *freq, *threshold, *range, *amount, *attack, *release, *lookahead, *mix, *output,
                           *mode, *detection, *listen, *bypass;
    };

    // Choice names (host / automation; the UI shows its own labels)
    inline const juce::StringArray modeChoices { "Split", "Wideband" };
    inline const juce::StringArray detectionChoices { "Relative", "Absolute" };
    inline const juce::StringArray listenChoices { "Off", "S band", "Delta" };

    // Stimme presets of SIMPLE (UI writes the frequency)
    inline constexpr float voiceLowHz = 4800.0f, voiceMidHz = 6500.0f, voiceHighHz = 8200.0f;
}
