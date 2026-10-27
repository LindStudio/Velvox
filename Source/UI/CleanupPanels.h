#pragma once

#include "DetailPanels.h"
#include "ModuleViews.h"

class VelvoxAudioProcessor;

// ROOM NOISE (Raumrauschen) and BREATH CONTROL (Atem-Absenker), 0.7: one control each (Jakob: the
// strength is set by the user). The panel holds one big Amount knob next to a live picture of the
// reduction over the last seconds; there is no main row (single-parameter modules).
namespace velvox::ui
{
    // Live reduction (dB) of the module over the last 4 s as a filled curve, the current value as text
    class ReductionHistoryView final : public ModuleView
    {
    public:
        ReductionHistoryView (VelvoxAudioProcessor& p, dsp::ModuleId id);

        void update (const LiveData&) override;
        void paint (juce::Graphics&) override;

        static constexpr int historyLength = 240;   // frames (4 s at 60 fps)
        void setReservedRight (int px) { reservedRight = std::max (0, px); repaint(); }   // room for the panel's knob
        float getCurrentDb() const noexcept { return history[(size_t) ((write + historyLength - 1) % historyLength)]; }

    private:
        VelvoxAudioProcessor& proc;
        dsp::ModuleId module;
        std::array<float, historyLength> history {};
        int write = 0, reservedRight = 0;
    };

    std::unique_ptr<ModuleView> createCleanupView (VelvoxAudioProcessor& p, dsp::ModuleId id);
    std::unique_ptr<DetailPanel> createCleanupPanel (VelvoxAudioProcessor& p, dsp::ModuleId id);
}
