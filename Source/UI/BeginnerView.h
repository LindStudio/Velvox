#pragma once

#include "Widgets.h"
#include "Spectrum.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // Beginner mode: a large live spectrum, six macros with plain-language
    // descriptions, and live hints about level, clipping, mono compatibility
    // and fair comparison.
    class BeginnerView final : public juce::Component
    {
    public:
        explicit BeginnerView (VelvoxAudioProcessor& processor);

        void paint (juce::Graphics&) override;
        void resized() override;
        void setHints (const std::vector<Hint>& hints);

        void setSpectrumSource (const SpectrumSource* source) { spectrum.setSource (source); }
        void refreshSpectrum() { spectrum.refresh(); }

    private:
        struct Macro
        {
            std::unique_ptr<Knob> knob;
            juce::String description;
            juce::Rectangle<int> textArea;
        };

        std::array<Macro, 6> macros;
        std::unique_ptr<Choice> levelMatch;
        SpectrumView spectrum;
        std::vector<Hint> currentHints;
        juce::Rectangle<int> heroArea, macroArea, hintArea;
    };
}
