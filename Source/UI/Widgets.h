#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include "../DSP/EqualizerModule.h"

namespace velvox::ui
{
    using APVTS = juce::AudioProcessorValueTreeState;

    // Rotary control with name, value read-out (with unit), tooltip and
    // double-click-to-default. Optionally shows a marker where macros move the
    // value to (the effective value that is actually processed).
    class Knob : public juce::Component
    {
    public:
        Knob (APVTS& state, const char* paramId, const juce::String& label, juce::Colour accent);
        void resized() override;
        void paintOverChildren (juce::Graphics&) override;

        // Returns the processed value in parameter units; empty = no marker
        std::function<float()> effectiveValue;
        void refreshEffective();

        juce::Slider slider;

    protected:
        juce::Label name;

    private:
        std::unique_ptr<APVTS::SliderAttachment> attachment;
        float lastEffective = std::numeric_limits<float>::quiet_NaN();
    };

    // Pill toggle bound to a bool parameter
    class Toggle final : public juce::ToggleButton
    {
    public:
        Toggle (APVTS& state, const char* paramId, const juce::String& label, juce::Colour accent);

    private:
        std::unique_ptr<APVTS::ButtonAttachment> attachment;
    };

    // Drop-down bound to a choice parameter, with a small caption above
    class Choice final : public juce::Component
    {
    public:
        Choice (APVTS& state, const char* paramId, const juce::String& caption = {});
        void resized() override;

        juce::ComboBox box;

    private:
        juce::Label caption;
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

    // Stereo peak meter with hold, clip lamp and an optional LUFS read-out
    class LevelMeter final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        void push (float left, float right, bool clipped);
        void setLoudness (float lufs) { if (std::abs (lufs - loudness) > 0.05f) { loudness = lufs; repaint(); } }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { clipLatched = false; repaint(); }
        bool isClipLatched() const noexcept { return clipLatched; }
        float getPeakDb() const noexcept;

    private:
        std::array<float, 2> level {}, hold {};
        std::array<int, 2> holdFrames {};
        bool clipLatched = false;
        float loudness = -120.0f;
    };

    // Gain reduction bar, grows downwards from 0 dB
    class GainReductionMeter final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit GainReductionMeter (juce::Colour c, juce::String captionText = {}) : accent (c), caption (std::move (captionText)) {}
        void push (float reductionDb);
        void paint (juce::Graphics&) override;
        float getShown() const noexcept { return shown; }

    private:
        juce::Colour accent;
        juce::String caption;
        float shown = 0.0f;
        static constexpr float maxDb = 24.0f;
    };

    // -1..+1 phase correlation bar
    class CorrelationView final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        void push (float correlation);
        void paint (juce::Graphics&) override;

    private:
        float value = 1.0f;
    };

    // Frequency response, computed with the same filter design as the DSP
    class EqCurve final : public juce::Component
    {
    public:
        // Provides the effective EQ settings, whether the EQ is on, and its mix
        std::function<void (dsp::EqSettings&, bool&, float&)> provider;
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override { lastKey.clear(); refresh(); }

    private:
        juce::Array<float> lastKey;
        bool active = true;
        juce::Path curve;
    };

    // Static transfer curve of the saturation stage
    class SaturationCurve final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        // drive dB, mix 0..1, character, active
        std::function<void (float&, float&, int&, bool&)> provider;
        void refresh();
        void paint (juce::Graphics&) override;

    private:
        float drive = -1.0f, mix = -1.0f;
        int character = -1;
        bool active = false;
    };

    // 16-step chop pattern with the current step highlighted
    class StepPatternView final : public juce::Component
    {
    public:
        void set (const char* steps, int currentStep, bool active, juce::Colour accent);
        void paint (juce::Graphics&) override;

    private:
        juce::String pattern;
        int current = -1;
        bool isActive = false;
        juce::Colour colour;
    };

    // Short text hint line ("Input is clipping" ...), coloured by severity
    struct Hint
    {
        enum class Level { info, good, warning };
        Level level = Level::info;
        juce::String text;
    };
}
