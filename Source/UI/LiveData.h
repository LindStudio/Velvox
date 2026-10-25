#pragma once

#include "../DSP/VocalChain.h"

namespace velvox::ui
{
    // Everything the animated views need, gathered once per frame by the editor
    // (meters are pulled exactly once, then shared by every view).
    struct LiveData
    {
        static constexpr int historySize = 360; // 6 s at 60 fps

        struct History
        {
            explicit History (float initial = 0.0f) { values.fill (initial); }

            std::array<float, historySize> values {};
            int pos = 0;

            void push (float v) noexcept { values[(size_t) pos] = v; pos = (pos + 1) % historySize; }
            // 0 = newest
            float ago (int i) const noexcept { return values[(size_t) ((pos - 1 - i + 2 * historySize) % historySize)]; }
        };

        // Levels start at silence, reductions at zero
        History inputDb { -100.0f }, outputDb { -100.0f }, inputLufs { -100.0f }, outputLufs { -100.0f };
        History compGr, deessGr, delayDuck, reverbDuck;

        // 0.3 modules
        History smashGr, rideGain, rideLevel { -100.0f };

        // Most recent stereo samples of the output (goniometer), oldest first
        static constexpr int scopeSize = 1024;
        std::array<float, scopeSize> scopeL {}, scopeR {};

        // Activity of every module instance (0..1: gain reduction, ducking, movement) for the chain cards
        std::array<std::array<float, dsp::maxExtraInstances + 1>, dsp::numModules> activity {};

        dsp::ChainSettings settings;   // effective (macros applied)
        double time = 0.0;             // seconds, for idle animations
        double sampleRate = 48000.0;
        float effectiveDelayMs = 375.0f;
        int chopStep = -1;
        bool hasSignal = false;
        bool clipped = false;          // the output went over 0 dBFS in this frame

        float level() const noexcept   // 0..1 output energy, drives the ambient light
        {
            return juce::jlimit (0.0f, 1.0f, juce::jmap (outputDb.ago (0), -50.0f, -6.0f, 0.0f, 1.0f));
        }
    };
}
