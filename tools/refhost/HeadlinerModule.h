#pragma once

// The in-process Headliner processor reduced to one module: the routing holds only that
// instance (as an insert), the module is switched on and the global safety clip is off, so a
// render measures the module alone. A fresh processor uses the 0.6 true dry/wet mix law (MixLaws::allTrue). Parameters are then set by their APVTS IDs in real units.

#include "PluginProcessor.h"
#include "Parameters/ParameterIDs.h"
#include "DSP/Instances.h"
#include "DSP/Routing.h"
#include <memory>

namespace velvox::refhost
{
    inline const char* moduleOnParam (dsp::ModuleId id) noexcept
    {
        using M = dsp::ModuleId;
        switch (id)
        {
            case M::eq:      return pid::eqOn;
            case M::comp:    return pid::compOn;
            case M::deess:   return pid::deessOn;
            case M::sat:     return pid::satOn;
            case M::doubler: return pid::doublerOn;
            case M::delay:   return pid::delayOn;
            case M::reverb:  return pid::reverbOn;
            case M::smash:   return pid::smashOn;
            case M::smooth:  return pid::smoothOn;
            case M::check:   return pid::checkOn;
            case M::ride:    return pid::rideOn;
        }
        return pid::eqOn;
    }

    inline void setRaw (VelvoxAudioProcessor& proc, const juce::String& id, float value)
    {
        if (auto* p = proc.getState().getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    inline std::unique_ptr<VelvoxAudioProcessor> makeSingleModule (dsp::ModuleId id, int instance)
    {
        auto proc = std::make_unique<VelvoxAudioProcessor>();
        dsp::Routing r;
        r.insertAt (0, id, instance, true);
        proc->setRouting (r);

        setRaw (*proc, dsp::instanceParamId (moduleOnParam (id), instance), 1.0f);
        setRaw (*proc, pid::safetyClip, 0.0f);
        setRaw (*proc, pid::inputGain, 0.0f);
        setRaw (*proc, pid::outputGain, 0.0f);
        setRaw (*proc, pid::bypass, 0.0f);
        setRaw (*proc, pid::levelMatchMode, 0.0f);
        return proc;
    }
}
