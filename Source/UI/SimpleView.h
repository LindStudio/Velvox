#pragma once

#include "Widgets.h"
#include "Spectrum.h"
#include "LiveData.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // Simple mode, variant A3 (spec 2.3):
    //   left    live spectrum of the vocal: before = dashed line, after = filled curve;
    //           under it "Vocal analysieren" (runs the automatic mix), after an analysis a small
    //           Vorher | Nachher switch, and "Details" (report, Density, fair compare)
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

        struct MacroDef { const char* paramId; const char* label; juce::uint32 colour; };
        static const std::vector<MacroDef>& macroDefs();

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void updateCompare();

        VelvoxAudioProcessor& proc;
        SpectrumView spectrumView;
        juce::TextButton analyse { juce::String::fromUTF8 ("Vocal analysieren") },
                         before { "Vorher" }, after { "Nachher" }, details { "Details" };
        std::vector<std::unique_ptr<HSlider>> macros;
        CompactMeters meters;
        juce::Rectangle<int> spectrumPanel, macroPanel, meterStrip, buttonRow;
    };
}
