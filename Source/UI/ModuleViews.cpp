#include "ModuleViews.h"
#include "CleanupPanels.h"
#include "EqPanel.h"
#include "ReverbPanel.h"
#include "SmoothPanel.h"
#include "../PluginProcessor.h"
#include "SatPanel.h"
#include "TimePanels.h"
#include "VoicesPanels.h"
#include "DynPanels.h"
#include "../Text/I18n.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        float yFor (float v, float lo, float hi, juce::Rectangle<float> r)
        {
            return juce::jmap (juce::jlimit (lo, hi, v), lo, hi, r.getBottom(), r.getY());
        }

        // Faint horizontal guides with labels on the right
        void hGrid (juce::Graphics& g, juce::Rectangle<float> r, std::initializer_list<float> values, float lo, float hi,
                    const juce::String& suffix = {})
        {
            g.setFont (medium (10.5f));
            for (float v : values)
            {
                const float y = yFor (v, lo, hi, r);
                g.setColour (colours::accent.withAlpha (0.035f));
                g.fillRect (juce::Rectangle<float> (r.getX(), y, r.getWidth(), 1.0f));
                g.setColour (colours::textFaint);
                g.drawText (juce::String ((int) v) + suffix, juce::Rectangle<float> (r.getRight() - 44.0f, y - 14.0f, 42.0f, 13.0f),
                            juce::Justification::right, false);
            }
        }

        void label (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                    juce::Justification j = juce::Justification::topLeft, juce::Colour c = colours::textDim)
        {
            drawCaption (g, text, r, j, c, 8.5f);
        }

        // Aurora line with a soft glow (gradient runs across `span`)
        void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Rectangle<float> span, float width = 1.6f, float alpha = 1.0f)
        {
            g.setGradientFill (auroraGradient (span.getTopLeft(), span.getTopRight(), 0.14f * alpha));
            g.strokePath (p, juce::PathStrokeType (width * 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            auto core = auroraGradient (span.getTopLeft(), span.getTopRight(), alpha);
            core.addColour (0.45, colours::teal.interpolatedWith (juce::Colours::white, 0.35f).withAlpha (alpha));
            g.setGradientFill (core);
            g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Newest value at the right edge
        juce::Path historyPath (const LiveData::History& h, juce::Rectangle<float> r, float lo, float hi, bool invert = false)
        {
            juce::Path p;
            const int n = LiveData::historySize;
            for (int i = 0; i < n; ++i)
            {
                const float x = r.getRight() - r.getWidth() * (float) i / (float) (n - 1);
                const float v = h.ago (i);
                const float y = invert ? juce::jmap (juce::jlimit (lo, hi, v), lo, hi, r.getY(), r.getBottom()) : yFor (v, lo, hi, r);
                if (i == 0)
                    p.startNewSubPath (x, y);
                else
                    p.lineTo (x, y);
            }
            return p;
        }

        juce::Path closedTo (juce::Path p, juce::Rectangle<float> r, float y)
        {
            p.lineTo (r.getX(), y);
            p.lineTo (r.getRight(), y);
            p.closeSubPath();
            return p;
        }

        juce::String hz (float f)
        {
            return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
        }

        float raw (VelvoxAudioProcessor& p, const char* id) { return p.getState().getRawParameterValue (id)->load(); }

        void setParam (VelvoxAudioProcessor& p, const char* id, float value)
        {
            if (auto* prm = p.getState().getParameter (id))
                prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        }

        //======================================================================
        // Output: goniometer and loudness over time
        class OutputView final : public ModuleView
        {
        public:
            OutputView()
            {
                legend.setItems ({ { i18n::tr ("Eingang", "Input"), colours::accent.withAlpha (0.5f), LegendItem::Style::line },
                                   { i18n::tr ("Ausgang", "Output"), colours::teal, LegendItem::Style::fill } });
                addAndMakeVisible (legend);
            }

            void resized() override
            {
                // As paint lays it out: top right of the loudness history, beside its title
                auto r = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
                r.removeFromLeft (std::max (80.0f, std::min (r.getHeight(), r.getWidth() - 24.0f - 120.0f)) + 24.0f);
                legend.setBounds (r.toNearestInt().removeFromTop (Legend::height).removeFromRight (legend.getIdealWidth()).translated (0, -3));
                legend.setVisible (r.getWidth() >= 90.0f + (float) legend.getIdealWidth());
            }

            void update (const LiveData& d) override
            {
                live = &d;
                repaint();
            }

            void paint (juce::Graphics& g) override
            {
                if (live == nullptr)
                    return;
                auto r = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
                // The goniometer gives way so the loudness history keeps room for its title (Mehr open: narrow)
                auto scope = r.removeFromLeft (std::max (80.0f, std::min (r.getHeight(), r.getWidth() - 24.0f - 120.0f)));
                r.removeFromLeft (24.0f);

                // Goniometer: mid up, side across
                label (g, i18n::tr ("Stereobild", "Stereo image"), scope.withHeight (12.0f));
                const auto box = scope.withTrimmedTop (16.0f).reduced (6.0f);
                const float side = std::min (box.getWidth(), box.getHeight());
                const auto sq = box.withSizeKeepingCentre (side, side);
                const auto c = sq.getCentre();
                const float half = sq.getWidth() * 0.5f;
                juce::Path diamond;
                diamond.startNewSubPath (c.x, sq.getY());
                diamond.lineTo (sq.getRight(), c.y);
                diamond.lineTo (c.x, sq.getBottom());
                diamond.lineTo (sq.getX(), c.y);
                diamond.closeSubPath();
                g.setColour (juce::Colours::black.withAlpha (0.3f));
                g.fillPath (diamond);
                g.setColour (colours::accent.withAlpha (0.06f));
                g.strokePath (diamond, juce::PathStrokeType (1.0f));
                g.fillRect (juce::Rectangle<float> (c.x - 0.5f, sq.getY(), 1.0f, sq.getHeight()));
                g.drawLine (sq.getX() + half * 0.5f, sq.getY() + half * 0.5f, sq.getRight() - half * 0.5f, sq.getBottom() - half * 0.5f, 1.0f);
                g.drawLine (sq.getRight() - half * 0.5f, sq.getY() + half * 0.5f, sq.getX() + half * 0.5f, sq.getBottom() - half * 0.5f, 1.0f);
                label (g, "L", juce::Rectangle<float> (16.0f, 12.0f).withCentre ({ sq.getX() + half * 0.42f, sq.getY() + half * 0.42f }), juce::Justification::centred);
                label (g, "R", juce::Rectangle<float> (16.0f, 12.0f).withCentre ({ sq.getRight() - half * 0.42f, sq.getY() + half * 0.42f }), juce::Justification::centred);

                float peak = 0.05f;
                for (int i = 0; i < LiveData::scopeSize; ++i)
                    peak = std::max (peak, std::max (std::abs (live->scopeL[(size_t) i]), std::abs (live->scopeR[(size_t) i])));
                const float scale = half * 0.85f / std::sqrt (peak);   // gentle auto-zoom
                for (int i = 0; i < LiveData::scopeSize; ++i)
                {
                    const float l = live->scopeL[(size_t) i], rr = live->scopeR[(size_t) i];
                    const float m = (l + rr) * 0.7071f, sd = (l - rr) * 0.7071f;
                    const auto sign = [] (float v) { return v < 0.0f ? -1.0f : 1.0f; };
                    const float x = c.x + sign (sd) * std::sqrt (std::abs (sd)) * scale * 0.9f;
                    const float y = c.y - sign (m) * std::sqrt (std::abs (m)) * scale * 0.9f;
                    const float age = (float) i / (float) LiveData::scopeSize;
                    g.setColour (aurora (std::abs (sd) * 4.0f, 0.12f + 0.45f * age));
                    g.fillRect (x, y, 1.5f, 1.5f);
                }

                // Loudness history
                label (g, i18n::tr ("Lautheit", "Loudness"), r.withHeight (12.0f));   // the legend (a child) sits right of it
                auto plot = r.withTrimmedTop (18.0f);
                hGrid (g, plot, { -12.0f, -18.0f, -24.0f, -30.0f, -36.0f }, -42.0f, -6.0f, " LUFS");
                const auto in = historyPath (live->inputLufs, plot, -42.0f, -6.0f);
                g.setColour (colours::accent.withAlpha (0.25f));
                g.strokePath (in, juce::PathStrokeType (1.0f));
                const auto out = historyPath (live->outputLufs, plot, -42.0f, -6.0f);
                g.setGradientFill (auroraGradient (plot.getTopLeft(), plot.getTopRight(), 0.10f));
                g.fillPath (closedTo (out, plot, plot.getBottom()));
                glowStroke (g, out, plot, 1.6f);
            }

        private:
            const LiveData* live = nullptr;
            Legend legend;
        };

    }

    std::unique_ptr<ModuleView> createModuleView (VelvoxAudioProcessor& p, int code, const SpectrumSource* spectrum)
    {
        if (code < 0)
            return std::make_unique<OutputView>();
        const int instance = dsp::selectionInstance (code);
        switch (dsp::selectionModule (code))
        {
            case ModuleId::eq:      return createEqGraph (p, spectrum, instance);
            case ModuleId::comp:    return createDynView (p, ModuleId::comp, instance);   // DynPanels.cpp
            case ModuleId::deess:   return createDynView (p, ModuleId::deess, instance);
            case ModuleId::sat:     return createSatView (p, instance);
            case ModuleId::doubler: return createVoicesModuleView (p, ModuleId::doubler, instance);
            case ModuleId::delay:   return createTimeModuleView (p, ModuleId::delay, instance);
            case ModuleId::reverb:  return createReverbRoom (p, instance);
            case ModuleId::smash:   return createDynView (p, ModuleId::smash, instance);
            case ModuleId::smooth:  return createSmoothGraph (p, instance);
            case ModuleId::check:   break;   // no chain module (review 2026-10-28)
            case ModuleId::ride:    return createDynView (p, ModuleId::ride, instance);
            case ModuleId::denoise:
            case ModuleId::breath:  return createCleanupView (p, dsp::selectionModule (code));
        }
        return std::make_unique<OutputView>();
    }
}
