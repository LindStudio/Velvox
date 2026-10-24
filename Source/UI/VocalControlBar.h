#pragma once

#include "Widgets.h"
#include "LiveData.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // VOCAL CONTROL of the HEADLINER VOCAL design: six macro knobs (54 px) in the macro
    // colours plus a gain-reduction read-out of the compressor.
    //   PRESENCE -> macroPresence, SPACE -> macroSpace, WIDTH -> macroWidth,
    //   PUNCH -> macroAggression, WARMTH -> macroWarmth, VOCAL LEVEL -> outputGain
    class VocalControlBar final : public juce::Component
    {
    public:
        VocalControlBar (VelvoxAudioProcessor& processor, const LiveData& live);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();   // once per frame: GR read-out and macro markers

        juce::Rectangle<int> getGainReductionArea() const noexcept { return grArea; }

    private:
        const LiveData& live;
        std::vector<std::unique_ptr<Knob>> knobs;
        juce::Rectangle<int> grArea;
        float shownGr = 0.0f;
    };
}
