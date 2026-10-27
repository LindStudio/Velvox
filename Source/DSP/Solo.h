#pragma once

#include "VocalChain.h"

namespace velvox::dsp
{
    // Module solo (0.6.1, spec item 4): a monitoring state, never a parameter. While one module instance is
    // soloed, every other instance in the chain is bypassed and only the soloed one is heard.
    //
    // A pure transform of one block's settings (audio thread, no allocation): the other instances get their
    // on-switches off, so each one fades out exactly as when the user switches it off (gates, module fades,
    // smoothed returns) - click-free. Latency stays: saturation, Smooth and Level Ride report theirs while in the
    // chain (on or off). The soloed instance keeps its own settings (a soloed module that is off stays off).

    // Check only measures (no effect on the sound): soloing it would mean nothing
    constexpr bool canSolo (ModuleId id) noexcept { return id != ModuleId::check; }

    inline void applySolo (ChainSettings& s, ModuleId soloId, int soloInstance) noexcept
    {
        const auto keep = [&] (ModuleId id, int k) { return id == soloId && k == soloInstance; };

        for (int k = 0; k <= maxExtraInstances; ++k)
        {
            auto b = s.instance (k);
            if (! keep (ModuleId::eq, k))      b.eqOn = false;
            if (! keep (ModuleId::comp, k))    b.compOn = false;
            if (! keep (ModuleId::deess, k))   b.deessOn = false;
            if (! keep (ModuleId::sat, k))     b.sat.enabled = false;
            if (! keep (ModuleId::doubler, k)) b.doubler.enabled = false;
            if (! keep (ModuleId::delay, k))   b.delayOn = false;
            if (! keep (ModuleId::reverb, k))  b.reverbOn = false;
            if (! keep (ModuleId::smash, k))   b.smash.enabled = false;
            if (! keep (ModuleId::smooth, k))  b.smooth.enabled = false;
            if (! keep (ModuleId::ride, k))    b.ride.enabled = false;
            if (k == 0)
                s.setInstance0 (b);
            else
                s.extra[(size_t) k - 1] = b;
        }
        if (! keep (ModuleId::denoise, 0)) s.denoise.enabled = false;
        if (! keep (ModuleId::breath, 0))  s.breath.enabled = false;
    }
}
