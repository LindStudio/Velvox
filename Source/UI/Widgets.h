#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include "../DSP/EqualizerModule.h"

namespace velvox::ui
{
    using APVTS = juce::AudioProcessorValueTreeState;

    // Rotary control with name, value read-out (with unit), tooltip and
    // double-click-to-default. Bound to one parameter.
    class Knob final : public juce::Component
    {
    public:
        Knob (APVTS& state, const char* paramId, const juce::String& label, juce::Colour accent);
        void resized() override;

        juce::Slider slider;

    private:
        juce::Label name;
        std::unique_ptr<APVTS::SliderAttachment> attachment;
    };

    // Pill toggle bound to a bool parameter
    class Toggle final : public juce::ToggleButton
    {
    public:
        Toggle (APVTS& state, const char* paramId, const juce::String& label, juce::Colour accent);

    private:
        std::unique_ptr<APVTS::ButtonAttachment> attachment;
    };

    // Drop-down bound to a choice parameter
    class Choice final : public juce::ComboBox
    {
    public:
        Choice (APVTS& state, const char* paramId);

    private:
        std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
    };

    // Module on/off switch drawn as a power glyph
    class PowerButton final : public juce::ToggleButton
    {
    public:
        PowerButton (APVTS& state, const char* paramId, juce::Colour accent);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        juce::Colour accent;
        std::unique_ptr<APVTS::ButtonAttachment> attachment;
    };

    // Stereo peak meter with hold and clip lamp (values pushed by the editor timer)
    class LevelMeter final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        void push (float left, float right, bool clipped);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { clipLatched = false; repaint(); }

    private:
        std::array<float, 2> level {}, hold {};
        std::array<int, 2> holdFrames {};
        bool clipLatched = false;
    };

    // Gain reduction bar, grows downwards from 0 dB
    class GainReductionMeter final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit GainReductionMeter (juce::Colour c) : accent (c) {}
        void push (float reductionDb);
        void paint (juce::Graphics&) override;

    private:
        juce::Colour accent;
        float shown = 0.0f;
        static constexpr float maxDb = 24.0f;
    };

    // Frequency response of the EQ, computed with the same filter design as the DSP
    class EqCurve final : public juce::Component
    {
    public:
        explicit EqCurve (APVTS& state);
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override { lastKey.fill (-1.0e9f); refresh(); }

    private:
        APVTS& apvts;
        std::array<float, 12> lastKey {};
        bool active = true;
        juce::Path curve;
    };

    // Static transfer curve of the saturation stage
    class SaturationCurve final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit SaturationCurve (APVTS& state) : apvts (state) {}
        void refresh();
        void paint (juce::Graphics&) override;

    private:
        APVTS& apvts;
        float drive = -1.0f, mix = -1.0f;
        bool active = false;
    };
}
