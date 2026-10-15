#pragma once

#include "Widgets.h"
#include "Spectrum.h"
#include "LiveData.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // The visual stage at the top of every Pro panel: each module has its own
    // live picture (EQ curve with draggable bands, compressor history and
    // transfer curve, echo taps, reverb tail, stereo field ...).
    class ModuleView : public juce::Component
    {
    public:
        // Once per frame while the panel is visible
        virtual void update (const LiveData&) = 0;
    };

    std::unique_ptr<ModuleView> createModuleView (VelvoxAudioProcessor& p, int moduleIdOrOutput, const SpectrumSource* spectrum);
}
