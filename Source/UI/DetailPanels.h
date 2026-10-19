#pragma once

#include "Widgets.h"
#include "ModuleViews.h"
#include "../DSP/Routing.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // Pro-mode detail view for one module: header (power, title, one-line
    // explanation), the module's own live view, and its full set of controls.
    class DetailPanel : public juce::Component
    {
    public:
        DetailPanel (VelvoxAudioProcessor& p, juce::String title, juce::String subtitle, const char* onParamId);

        void paint (juce::Graphics&) override;
        void resized() override;
        virtual void refresh();

        void setVisual (std::unique_ptr<ModuleView> view);
        void setLiveData (const LiveData* data) { live = data; }
        // Module identity: icon in the header and its module presets (if any)
        void setModule (int moduleIdOrOutput);

        // Asks the editor to show another module (e.g. from a VOCAL CHECK finding)
        std::function<void (int moduleId)> onOpenModule;
        // Opens the help page for this module (the editor shows the help overlay)
        std::function<void (int moduleIdOrOutput)> onHelp;
        juce::TextButton& getHelpButton() noexcept { return helpButton; }

    protected:
        virtual void layoutContent (juce::Rectangle<int> area) = 0;

        // Knob bound to a parameter; shows the macro marker when relevant
        Knob& knob (const char* paramId, const juce::String& label);
        Toggle& toggle (const char* paramId, const juce::String& label);
        Choice& choice (const char* paramId, const juce::String& caption);

        static void row (std::initializer_list<juce::Component*> items, juce::Rectangle<int> area);
        void section (const juce::String& caption, juce::Rectangle<int> area); // labelled group behind controls

        int visualHeight = 172;
        const LiveData* live = nullptr;

        VelvoxAudioProcessor& proc;
        APVTS& apvts;

    private:
        juce::String title, subtitle;
        const char* onParamId;
        void showModulePresets();

        std::unique_ptr<PowerButton> power;
        std::unique_ptr<ModuleView> visual;
        int moduleId = -2;
        juce::TextButton presetsButton { "Presets" }, helpButton { "?" };
        std::vector<std::unique_ptr<juce::Component>> owned;
        std::vector<Knob*> knobs;
        struct Section { juce::String caption; juce::Rectangle<int> area; };
        std::vector<Section> sections;
        juce::Rectangle<int> visualArea;
        int lastOn = -1;
    };

    // Factory: the detail panel for a module or the output stage (id < 0)
    std::unique_ptr<DetailPanel> createDetailPanel (VelvoxAudioProcessor& p, int moduleIdOrOutput,
                                                    const SpectrumSource* spectrum = nullptr, const LiveData* live = nullptr);
}
