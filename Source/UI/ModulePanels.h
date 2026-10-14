#pragma once

#include "Widgets.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // Card for one processing module: header with power switch, title and a
    // one-line description; content dims when the module is off.
    class ModulePanel : public juce::Component
    {
    public:
        ModulePanel (APVTS& state, juce::String title, juce::String subtitle, juce::Colour accent, const char* onParamId);

        void paint (juce::Graphics&) override;
        void resized() override;

        // Called ~30x per second by the editor
        virtual void refresh (VelvoxAudioProcessor& processor);

    protected:
        virtual void layoutContent (juce::Rectangle<int> area) = 0;
        static void layoutGrid (std::initializer_list<juce::Component*> items, juce::Rectangle<int> area, int columns, int rowHeight);

        APVTS& apvts;
        const juce::Colour accent;

    private:
        bool isOn() const;

        juce::String title, subtitle;
        const char* onParamId;
        std::unique_ptr<PowerButton> power;
        int lastOn = -1;
    };

    class EqPanel final : public ModulePanel
    {
    public:
        explicit EqPanel (APVTS&);
        void refresh (VelvoxAudioProcessor&) override;

    private:
        void layoutContent (juce::Rectangle<int>) override;

        EqCurve curve;
        Toggle lowCutOn;
        Choice slope;
        Knob lowCut, lowFreq, lowGain, midFreq, midGain, midQ, highFreq, highGain, mix;
    };

    class CompressorPanel final : public ModulePanel
    {
    public:
        explicit CompressorPanel (APVTS&);
        void refresh (VelvoxAudioProcessor&) override;

    private:
        void layoutContent (juce::Rectangle<int>) override;

        Knob threshold, ratio, attack, release, knee, makeup, mix;
        GainReductionMeter gr { colours::comp };
    };

    class DeEsserPanel final : public ModulePanel
    {
    public:
        explicit DeEsserPanel (APVTS&);
        void refresh (VelvoxAudioProcessor&) override;

    private:
        void layoutContent (juce::Rectangle<int>) override;

        Knob freq, threshold, range;
        Toggle listen;
        GainReductionMeter gr { colours::deess };
    };

    class SaturationPanel final : public ModulePanel
    {
    public:
        explicit SaturationPanel (APVTS&);
        void refresh (VelvoxAudioProcessor&) override;

    private:
        void layoutContent (juce::Rectangle<int>) override;

        SaturationCurve curve;
        Knob drive, output, mix;
    };

    class DelayPanel final : public ModulePanel
    {
    public:
        explicit DelayPanel (APVTS&);
        void refresh (VelvoxAudioProcessor&) override;

    private:
        void layoutContent (juce::Rectangle<int>) override;

        Toggle sync, pingPong;
        Knob time;
        juce::Component divisionSlot;
        juce::Label divisionLabel, info;
        Choice division;
        Knob feedback, mix, lowCut, highCut;
    };

    class ReverbPanel final : public ModulePanel
    {
    public:
        explicit ReverbPanel (APVTS&);

    private:
        void layoutContent (juce::Rectangle<int>) override;

        Choice type;
        Knob mix, decay, preDelay, size, tone, lowCut, width;
    };
}
