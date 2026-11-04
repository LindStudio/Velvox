#pragma once

#include <juce_core/juce_core.h>
#include <vector>

// Soft thresholds for the auto mix (plan 2026-11-04-automix-konsistenz): a decision that flips at one hard
// number flips with every tiny change of the measurement. These helpers make it a smooth weight instead.
namespace vocalypse::automix
{
    // Smoothstep: 0 at lo, 0.5 in the middle, 1 at hi, clamped outside; lo > hi = falling (1 at lo, 0 at hi)
    inline float soft (float x, float lo, float hi) noexcept
    {
        if (lo == hi)
            return x >= lo ? 1.0f : 0.0f;
        const float t = juce::jlimit (0.0f, 1.0f, (x - lo) / (hi - lo));
        return t * t * (3.0f - 2.0f * t);
    }

    // a + w * (b - a)
    inline float blend (float w, float a, float b) noexcept { return a + w * (b - a); }

    // Index of the option with the best score (higher = better). The current favourite `preferred` keeps
    // the choice unless another option beats it by more than `margin`. -1 for no options; a `preferred`
    // outside the list means no favourite.
    inline int chooseWithMargin (const std::vector<float>& scores, int preferred, float margin) noexcept
    {
        if (scores.empty())
            return -1;
        int best = 0;
        for (int i = 1; i < (int) scores.size(); ++i)
            if (scores[(size_t) i] > scores[(size_t) best])
                best = i;
        if (preferred < 0 || preferred >= (int) scores.size())
            return best;
        return scores[(size_t) best] - scores[(size_t) preferred] > margin ? best : preferred;
    }
}
