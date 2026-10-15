#pragma once

#include "Widgets.h"
#include "Spectrum.h"
#include "../DSP/Routing.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // Pro-mode detail view for one module: header (power, title, one-line
    // explanation), a visual strip (live spectrum or a module-specific curve)
    // and the module's full set of controls.
    class DetailPanel : public juce::Component
    {
    public:
        DetailPanel (VelvoxAudioProcessor& p, juce::String title, juce::String subtitle, juce::Colour accent, const char* onParamId);

        void paint (juce::Graphics&) override;
        void resized() override;
        virtual void refresh();

        void setSpectrumSource (const SpectrumSource* source) { spectrumView.setSource (source); }

    protected:
        virtual void layoutContent (juce::Rectangle<int> area) = 0;

        // Knob bound to a parameter; shows the macro marker when relevant
        Knob& knob (const char* paramId, const juce::String& label);
        Toggle& toggle (const char* paramId, const juce::String& label);
        Choice& choice (const char* paramId, const juce::String& caption);

        static void row (std::initializer_list<juce::Component*> items, juce::Rectangle<int> area);
        void section (const juce::String& caption, juce::Rectangle<int> area); // labelled group behind controls

        // Visual strip above the controls; 0 = none (the panel draws its own)
        int visualHeight = 150;
        SpectrumView spectrumView;

        VelvoxAudioProcessor& proc;
        APVTS& apvts;
        const juce::Colour accent;

    private:
        juce::String title, subtitle;
        const char* onParamId;
        std::unique_ptr<PowerButton> power;
        std::vector<std::unique_ptr<juce::Component>> owned;
        std::vector<Knob*> knobs;
        struct Section { juce::String caption; juce::Rectangle<int> area; };
        std::vector<Section> sections;
        juce::Rectangle<int> visualArea;
        int lastOn = -1;
    };

    // Factory: the detail panel for a module or the output stage (id < 0)
    std::unique_ptr<DetailPanel> createDetailPanel (VelvoxAudioProcessor& p, int moduleIdOrOutput,
                                                    const SpectrumSource* spectrum = nullptr);
}
