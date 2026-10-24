#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include "../DSP/EqualizerModule.h"

namespace velvox::ui
{
    using APVTS = juce::AudioProcessorValueTreeState;

    // Implemented by the editor canvas: shows the hover info card for a control
    struct InfoPopupHost
    {
        virtual ~InfoPopupHost() = default;
        // `value` is the current read-out with its unit (may be empty)
        virtual void showInfo (juce::Component& anchor, const juce::String& title, const juce::String& value, const juce::String& text) = 0;
        virtual void hideInfo (juce::Component& anchor) = 0;
    };

    // Floating glass card with a title and a description; fades and slides in
    class InfoPopup final : public juce::Component
    {
    public:
        InfoPopup();
        void show (juce::Rectangle<int> anchorInParent, const juce::String& title, const juce::String& value, const juce::String& text);
        void hide();
        bool isShowingFor (juce::Rectangle<int> anchor) const { return shown && anchor == anchorArea; }
        void paint (juce::Graphics&) override;

    private:
        void tick();

        juce::String title, value, text;
        juce::Rectangle<int> anchorArea, finalBounds;
        Smoothed fade;
        bool shown = false;
        juce::VBlankAttachment vblank { this, [this] { tick(); } };
    };

    // Shows the info card of a control (name, short description, current value with unit) after the
    // pointer has rested on it for motion::hoverInfoDelayMs. Attach one to any control; the value stays
    // current while the card is open (call refresh() once per frame or from a VBlank).
    class HoverInfo final : private juce::MouseListener, private juce::Timer
    {
    public:
        HoverInfo (juce::Component& anchor, std::function<juce::String()> title,
                   std::function<juce::String()> value, std::function<juce::String()> description);
        ~HoverInfo() override;
        void refresh();
        bool isShown() const noexcept { return shown; }

    private:
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}
        void timerCallback() override;
        void hide();

        juce::Component& anchor;
        std::function<juce::String()> title, value, description;
        bool shown = false;
        juce::String shownValue;
    };

    // The slider of every knob and fader (spec 2.6 gestures, shared by all controls):
    //   drag (incremental: pressing Shift or Ctrl switches to fine = 1/10 at any moment without a jump),
    //   double-click or Alt+click = default, wheel = step, click on the value = type a value (units
    //   accepted, see parseValueText), right-click = menu (reset, enter, copy, paste, module items).
    class FineSlider final : public juce::Slider
    {
    public:
        static constexpr int normalPixels = 220;   // pixels for the full range
        static constexpr int fineFactor = 10;      // Shift / Ctrl: this many times finer

        // Binds the gestures to a parameter: typed values, default, menu
        void bindParameter (juce::RangedAudioParameter* p);
        juce::RangedAudioParameter* getParameter() const noexcept { return parameter; }

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::RangedAudioParameter* parameter = nullptr;
        juce::Point<float> lastPos;
        double dragProportion = 0.0;   // unsnapped, so stepped parameters still move in fine mode
        bool ignoreDrag = false;       // after a reset click or a menu
    };

    // Rotary control with caption and value read-out. The dial glides to new
    // values (presets, automation), lights up on hover, and after resting the
    // mouse on it shows its info card (name, description, value).
    class Knob : public juce::Component
    {
    public:
        Knob (APVTS& state, const char* paramId, const juce::String& label, juce::Colour accent = colours::accent);
        ~Knob() override;
        void resized() override;
        void paintOverChildren (juce::Graphics&) override;
        // The value box is created by the look-and-feel; panels build their knobs
        // before they join the editor, so refresh it once the knob is in place
        void parentHierarchyChanged() override { slider.sendLookAndFeelChange(); }

        // Large dial with value-driven glow (Beginner macros)
        void setHero (bool isHero);
        // Visual weight: primary controls get the large dial and a brighter name
        enum class Prominence { primary, secondary, detail };
        void setProminence (Prominence p);
        Prominence getProminence() const noexcept { return prominence; }
        void setMaxDialSize (int px) { maxDial = px; resized(); }
        // Design layout: dial (this diameter) on top, value (Roboto Mono) and name below
        void setDialSize (int px) { maxDial = px; resized(); }
        int getDialSize() const noexcept { return maxDial; }
        juce::Rectangle<int> getDialArea() const noexcept { return dialArea; }
        juce::Rectangle<int> getValueArea() const noexcept { return valueArea; }
        juce::Rectangle<int> getNameArea() const noexcept { return name.getBounds(); }
        void setDescription (const juce::String& text) { description = text; }
        const juce::String& getDescription() const noexcept { return description; }
        const juce::String& getParamId() const noexcept { return paramId; }
        // The caption under the dial (e.g. "MIX", or "SEND" while a delay runs as a send)
        void setLabel (const juce::String& text) { name.setText (text.toUpperCase(), juce::dontSendNotification); }
        juce::String getLabel() const { return name.getText(); }

        // Returns the processed value in parameter units; empty = no marker
        std::function<float()> effectiveValue;
        void refreshEffective();

        FineSlider slider;

    protected:
        juce::Label name;

    private:
        juce::Rectangle<float> dialBounds() const;
        void applyLabelStyle();
        void tick();

        std::unique_ptr<APVTS::SliderAttachment> attachment;
        juce::String paramId, description;
        float lastEffective = std::numeric_limits<float>::quiet_NaN();
        int maxDial = dialsize::secondary;
        int valueBoxH = 25;
        juce::Rectangle<int> dialArea, valueArea;
        bool hero = false;
        Prominence prominence = Prominence::secondary;
        Smoothed shownPos, hover;
        std::unique_ptr<HoverInfo> info;
        juce::VBlankAttachment vblank { this, [this] { tick(); } };
    };

    // Horizontal fader with name and value text (Simple mode macros): the same gestures as a knob,
    // dragged along its axis.   NAME  [=========o------]  25 %
    class HSlider final : public juce::Component
    {
    public:
        HSlider (APVTS& state, const char* paramId, const juce::String& label, juce::Colour accent);
        ~HSlider() override;
        void resized() override;
        void paint (juce::Graphics&) override;
        void parentHierarchyChanged() override { slider.sendLookAndFeelChange(); }
        const juce::String& getParamId() const noexcept { return paramId; }
        juce::String getLabel() const { return label; }
        juce::Colour getAccent() const noexcept { return accent; }

        // Processed value marker (macros), like Knob::effectiveValue
        std::function<float()> effectiveValue;

        FineSlider slider;

    private:
        std::unique_ptr<APVTS::SliderAttachment> attachment;
        juce::String paramId, label, description;
        juce::Colour accent;
        juce::Rectangle<int> nameArea;
        std::unique_ptr<HoverInfo> info;
        juce::VBlankAttachment vblank { this, [this] { if (info != nullptr) info->refresh(); } };
    };

    // Pill toggle bound to a bool parameter
    class Toggle final : public juce::ToggleButton
    {
    public:
        Toggle (APVTS& state, const char* paramId, const juce::String& label, juce::Colour accent = colours::accent);
        // Disabled switches fade back so they never read as "off but usable"
        void enablementChanged() override { setAlpha (isEnabled() ? 1.0f : 0.5f); }
        const juce::String& getParamId() const noexcept { return paramId; }

    private:
        std::unique_ptr<APVTS::ButtonAttachment> attachment;
        juce::String paramId;
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
        PowerButton (APVTS& state, const char* paramId, juce::Colour accent = colours::accent);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        void setHue (juce::Colour c) { hue = c; repaint(); }

    private:
        std::unique_ptr<APVTS::ButtonAttachment> attachment;
        juce::Colour hue = colours::teal;
    };

    // Small IN / OUT meters of the 0.6 shell: peak bar + short-term loudness for the input and the
    // output, a clip lamp on the output (click resets it). Laid out side by side (Simple mode, bottom
    // strip) or stacked (Advanced, end of the chain row).
    class CompactMeters final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        void setStacked (bool shouldStack) { stacked = shouldStack; repaint(); }
        // Peak levels in dBFS, loudness in LUFS; `clipped` latches the lamp
        void setLevels (float inDb, float outDb, float inLufs, float outLufs, bool clipped);
        float getInputDb() const noexcept { return level[0]; }
        float getOutputDb() const noexcept { return level[1]; }
        bool isClipLatched() const noexcept { return clipLatched; }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override { clipLatched = false; repaint(); }

    private:
        std::array<float, 2> level { -100.0f, -100.0f }, lufs { -100.0f, -100.0f };
        bool clipLatched = false, stacked = false;
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

        void setScaleOnLeft (bool left) { scaleLeft = left; repaint(); }

    private:
        std::array<float, 2> level {}, hold {};
        std::array<int, 2> holdFrames {};
        bool clipLatched = false;
        bool scaleLeft = true;
        float loudness = -120.0f;
    };

    // Gain reduction bar, grows downwards from 0 dB
    class GainReductionMeter final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit GainReductionMeter (juce::Colour = colours::accent, juce::String captionText = {}) : caption (std::move (captionText)) {}
        void push (float reductionDb);
        void paint (juce::Graphics&) override;
        float getShown() const noexcept { return shown; }

    private:
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

    // Frequency response, computed with the same filter design as the DSP.
    // Transparent: drawn over a SpectrumView with the same frequency axis.
    class EqCurve final : public juce::Component
    {
    public:
        EqCurve() { setInterceptsMouseClicks (false, false); }

        // Provides the effective EQ settings, whether the EQ is on, and its mix
        std::function<void (dsp::EqSettings&, bool&, float&)> provider;
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override { lastKey.clear(); refresh(); }

        static constexpr float rangeDb = 18.0f;
        static juce::Rectangle<float> plotArea (juce::Rectangle<float> bounds) { return bounds.reduced (0.0f, 10.0f); }

    private:
        juce::Array<float> lastKey;
        bool active = true;
        juce::Path curve;
    };

    // Mouse wheel over an EQ band: new Q. Up (positive delta) narrows the band, down widens it;
    // the step is multiplicative (a constant feel at any width), `fine` divides it by 5.
    // Always inside [minQ, maxQ].
    float eqWheelQ (float currentQ, float wheelDeltaY, bool fine, float minQ, float maxQ) noexcept;

    // What the automatic mix does with a parameter (German, empty = no role)
    juce::String autoMixRole (const juce::String& paramId);

    // Short text hint line ("Input is clipping" ...), coloured by severity
    struct Hint
    {
        enum class Level { info, good, warning };
        Level level = Level::info;
        juce::String text;
    };
}
