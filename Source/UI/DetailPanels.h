#pragma once

#include "Widgets.h"
#include "../DSP/Routing.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // Pro-mode detail view for one module: header (power, title, one-line
    // explanation) and the module's full set of controls.
    class DetailPanel : public juce::Component
    {
    public:
        DetailPanel (VelvoxAudioProcessor& p, juce::String title, juce::String subtitle, juce::Colour accent, const char* onParamId);

        void paint (juce::Graphics&) override;
        void resized() override;
        virtual void refresh();

    protected:
        virtual void layoutContent (juce::Rectangle<int> area) = 0;

        // Knob bound to a parameter; shows the macro marker when relevant
        Knob& knob (const char* paramId, const juce::String& label);
        Toggle& toggle (const char* paramId, const juce::String& label);
        Choice& choice (const char* paramId, const juce::String& caption);

        static void row (std::initializer_list<juce::Component*> items, juce::Rectangle<int> area);
        void section (const juce::String& caption, juce::Rectangle<int> area); // draws a labelled group frame

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
        int lastOn = -1;
    };

    // Factory: the detail panel for a module or the output stage (id < 0)
    std::unique_ptr<DetailPanel> createDetailPanel (VelvoxAudioProcessor& p, int moduleIdOrOutput);
}
