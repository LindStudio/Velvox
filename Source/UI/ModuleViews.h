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
    class ModuleView : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        // Once per frame while the panel is visible
        virtual void update (const LiveData&) = 0;
        // Parameters the visual edits directly through its own handles (graph points, edges), so they
        // count as reachable in the panel without a knob of their own
        virtual juce::StringArray getEditedParameters() const { return {}; }
    };

    // `selection` = dsp::selectionCode (module, instance), or < 0 for the output stage
    std::unique_ptr<ModuleView> createModuleView (VelvoxAudioProcessor& p, int selection, const SpectrumSource* spectrum);
}
