#pragma once

namespace velvox::dsp
{
    // Reverb panel "Raumtiefe" (spec 5.12): the vocal is a dot in a room drawing; dragging it back adds
    // space. One depth value 0..1 (0 = dry-ish and in front, 1 = far back in the room) moves the three
    // parameters together along a curated path per room type, in the units of the APVTS parameters
    // reverbMix (%), reverbPreDelay (ms) and reverbSize (%). Pure functions, any thread.
    struct ReverbDepthPoint
    {
        float mixPercent = 0.0f;
        float preDelayMs = 0.0f;
        float sizePercent = 0.0f;
    };

    // depth and type are clamped (NaN depth = 0)
    ReverbDepthPoint reverbDepthPath (int type, float depth) noexcept;

    // Where to draw the dot for the current settings: the depth whose path point is closest
    // (weighted mostly by the mix - what is heard most), 0..1
    float reverbDepthFromSettings (int type, float mixPercent, float preDelayMs, float sizePercent) noexcept;
}
