#pragma once

// Comparison of every ChainSettings field a Simple slider / macro can move (instance 1 of
// EQ, De-Esser, Saturation, Compressor, Doubler, Stereo, Delay, Reverb, ducking).

#include "DSP/Macros.h"

namespace velvox::test
{
    // "" when equal (within `tol`), otherwise the name of the first differing field
    inline juce::String macroFieldDifference (const dsp::ChainSettings& a, const dsp::ChainSettings& b, float tol = 0.0f)
    {
        const auto f = [tol] (float x, float y) { return std::abs (x - y) > tol; };
        if (a.eqOn != b.eqOn) return "eqOn";
        for (size_t i = 0; i < a.eq.bands.size(); ++i)
        {
            const auto& x = a.eq.bands[i];
            const auto& y = b.eq.bands[i];
            if (x.on != y.on || x.type != y.type || x.slope != y.slope || x.role != y.role || f (x.freq, y.freq) || f (x.gain, y.gain) || f (x.q, y.q))
                return "eq band " + juce::String ((int) i + 1);
        }
        if (a.deessOn != b.deessOn) return "deessOn";
        if (f (a.deess.rangeDb, b.deess.rangeDb)) return "deess.rangeDb";
        if (a.sat.enabled != b.sat.enabled) return "sat.enabled";
        if (f (a.sat.mix, b.sat.mix)) return "sat.mix";
        if (f (a.sat.driveDb, b.sat.driveDb)) return "sat.driveDb";
        for (size_t i = 0; i < a.sat.bands.size(); ++i)
            if (f (a.sat.bands[i].driveDb, b.sat.bands[i].driveDb)) return "sat band drive " + juce::String ((int) i);
        if (a.compOn != b.compOn) return "compOn";
        if (f (a.comp.thresholdDb, b.comp.thresholdDb)) return "comp.thresholdDb";
        if (f (a.comp.ratio, b.comp.ratio)) return "comp.ratio";
        if (f (a.comp.attackMs, b.comp.attackMs)) return "comp.attackMs";
        if (f (a.comp.makeupDb, b.comp.makeupDb)) return "comp.makeupDb";
        if (a.doubler.enabled != b.doubler.enabled) return "doubler.enabled";
        if (f (a.doubler.mix, b.doubler.mix)) return "doubler.mix";
        if (f (a.doubler.dryDb, b.doubler.dryDb)) return "doubler.dryDb";
        if (f (a.stereo.width, b.stereo.width)) return "stereo.width";
        if (a.reverbOn != b.reverbOn) return "reverbOn";
        if (f (a.reverbMix, b.reverbMix)) return "reverbMix";
        if (a.delayOn != b.delayOn) return "delayOn";
        if (f (a.delayMix, b.delayMix)) return "delayMix";
        if (f (a.reverbDuck.amountDb, b.reverbDuck.amountDb)) return "reverbDuck.amountDb";
        if (f (a.delayDuck.amountDb, b.delayDuck.amountDb)) return "delayDuck.amountDb";
        return {};
    }
}
