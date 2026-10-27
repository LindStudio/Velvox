#include "CleanupPanels.h"
#include "../PluginProcessor.h"
#include "HelpContent.h"
#include "../Text/I18n.h"

namespace velvox::ui
{
    using dsp::ModuleId;
    using i18n::tr;

    ReductionHistoryView::ReductionHistoryView (VelvoxAudioProcessor& p, ModuleId id) : proc (p), module (id)
    {
        setTooltip (module == ModuleId::denoise ? tr ("Wie stark das Raumrauschen gerade abgesenkt wird", "How much the room noise is lowered right now")
                                                : tr ("Wie stark gerade ein Atmer abgesenkt wird", "How much a breath is lowered right now"));
    }

    void ReductionHistoryView::update (const LiveData&)
    {
        auto& chain = proc.getChain();
        const float db = module == ModuleId::denoise ? chain.getDenoise().getReductionDb() : chain.getBreath().getReductionDb();
        history[(size_t) write] = juce::jlimit (-40.0f, 0.0f, db);
        write = (write + 1) % historyLength;
        repaint();
    }

    void ReductionHistoryView::paint (juce::Graphics& g)
    {
        const auto area = getLocalBounds().withTrimmedRight (reservedRight).toFloat().reduced (12.0f, 10.0f);
        const auto hue = moduleColour (module);
        // 0 dB on top, -40 dB at the bottom; grid every 10 dB
        const auto yFor = [&] (float db) { return area.getY() + area.getHeight() * (-db / 40.0f); };
        g.setColour (colours::lineSoft);
        g.setFont (monoFont (10.0f));
        for (float db : { 0.0f, -10.0f, -20.0f, -30.0f })
        {
            const float y = yFor (db);
            g.drawHorizontalLine (juce::roundToInt (y), area.getX(), area.getRight());
            g.setColour (colours::textFaint);
            g.drawText (juce::String (juce::roundToInt (db)) + " dB", juce::Rectangle<float> (area.getX(), y, 60.0f, 14.0f), juce::Justification::topLeft);
            g.setColour (colours::lineSoft);
        }

        juce::Path curve;
        curve.startNewSubPath (area.getX(), area.getY());
        for (int i = 0; i < historyLength; ++i)
        {
            const float v = history[(size_t) ((write + i) % historyLength)];
            curve.lineTo (area.getX() + area.getWidth() * (float) i / (float) (historyLength - 1), yFor (v));
        }
        curve.lineTo (area.getRight(), area.getY());
        curve.closeSubPath();
        g.setColour (hue.withAlpha (0.22f));
        g.fillPath (curve);
        g.setColour (hue);
        g.strokePath (curve, juce::PathStrokeType (1.6f));

        g.setColour (colours::text);
        g.setFont (monoFont (13.0f));
        g.drawText (juce::String (getCurrentDb(), 1) + " dB", area.withTop (area.getBottom() - 18.0f), juce::Justification::bottomRight);
    }

    namespace
    {
        class CleanupPanel final : public DetailPanel
        {
        public:
            CleanupPanel (VelvoxAudioProcessor& p, ModuleId id)
                : DetailPanel (p, dsp::moduleDisplayName (id), help::shortDescription ((int) id),
                               id == ModuleId::denoise ? pid::denoiseOn : pid::breathOn)
            {
                amount = &knob (id == ModuleId::denoise ? pid::denoiseAmount : pid::breathAmount, tr ("Stärke", "Amount"));
                placeInVisual ({ amount });
                amount->setProminence (Knob::Prominence::primary);
                amount->setDialSize (dialsize::smash);
            }

        private:
            void layoutVisual (juce::Rectangle<int> area) override
            {
                const auto a = area.reduced (14, 10);
                constexpr int captionH = 44;
                const int dial = juce::jlimit (110, 180, a.getHeight() - captionH - 20);
                const int knobW = dial + 40;
                amount->setDialSize (dial);
                amount->setBounds (juce::Rectangle<int> (a.getRight() - knobW, a.getY(), knobW, a.getHeight()).withSizeKeepingCentre (knobW, dial + captionH));
                // the curve keeps clear of the knob (the view spans the whole visual area)
                if (auto* view = dynamic_cast<ReductionHistoryView*> (getVisual()))
                    view->setReservedRight (area.getRight() - (a.getRight() - knobW - 24));
            }

            Knob* amount = nullptr;
        };
    }

    std::unique_ptr<ModuleView> createCleanupView (VelvoxAudioProcessor& p, ModuleId id)
    {
        if (id == ModuleId::denoise || id == ModuleId::breath)
            return std::make_unique<ReductionHistoryView> (p, id);
        return nullptr;
    }

    std::unique_ptr<DetailPanel> createCleanupPanel (VelvoxAudioProcessor& p, ModuleId id)
    {
        if (id == ModuleId::denoise || id == ModuleId::breath)
            return std::make_unique<CleanupPanel> (p, id);
        return nullptr;
    }
}
