#pragma once

#include "Widgets.h"
#include "Spectrum.h"
#include "LiveData.h"
#include "MascotView.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // Simple mode, variant A3 (spec 2.3) with the devil as assistant (approved concept, canvas artboard "Teufel"):
    //   left    after an analysis a small Vorher | Nachher switch top right, the live spectrum of the
    //           vocal (before = dashed line, after = filled curve), and under it the devil (bottom left;
    //           a click on him opens the Vocal Check) with a speech bubble holding "Vocal analysieren"
    //           (runs the automatic mix; "Neu analysieren" once there is a mix) and "Details" (report,
    //           Density, fair compare)
    //   right   the six macros as horizontal faders with value text:
    //           PRESENCE, SPACE, WIDTH, PUNCH (macroAggression), WARMTH, VOCAL LEVEL (outputGain)
    //   bottom  small IN / OUT meters with loudness
    class SimpleView final : public juce::Component, private juce::ChangeListener
    {
    public:
        SimpleView (VelvoxAudioProcessor& processor, const SpectrumSource* spectrum);
        ~SimpleView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // Once per frame from the editor
        void refresh (const LiveData& live);

        std::function<void()> onAnalyse;      // "Vocal analysieren"
        std::function<void()> onShowDetails;  // report overlay
        std::function<void()> onVocalCheck;   // click on the devil

        // For tests and the editor
        juce::TextButton& getAnalyseButton() noexcept { return analyse; }
        juce::TextButton& getBeforeButton() noexcept { return before; }
        juce::TextButton& getAfterButton() noexcept { return after; }
        juce::TextButton& getDetailsButton() noexcept { return details; }
        const std::vector<std::unique_ptr<HSlider>>& getMacroSliders() const noexcept { return macros; }
        juce::Rectangle<int> getSpectrumArea() const noexcept { return spectrumPanel; }
        juce::Rectangle<int> getMacroArea() const noexcept { return macroPanel; }
        juce::Rectangle<int> getMeterArea() const noexcept { return meterStrip; }
        CompactMeters& getMeters() noexcept { return meters; }
        MascotView& getMascot() noexcept { return mascot; }
        const juce::String& getBubbleText() const noexcept { return say; }

        struct MacroDef { const char* paramId; const char* label; juce::uint32 colour; };
        static const std::vector<MacroDef>& macroDefs();

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void updateCompare();
        void updateBubble (bool busy, bool silent);

        VelvoxAudioProcessor& proc;
        SpectrumView spectrumView;
        juce::TextButton analyse, before, after, details { "Details" };   // texts set in the constructor (language)
        std::vector<std::unique_ptr<HSlider>> macros;
        CompactMeters meters;
        MascotView mascot;
        juce::String say;
        double lastTick = 0.0, scoreAge = 1.0e9, silentFor = 0.0;
        int score = -1;
        bool wasBusy = false;
        juce::Rectangle<int> spectrumPanel, macroPanel, meterStrip, bubble, statusRow;
    };
}
