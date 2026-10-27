#include "ModuleViews.h"
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
                label (g, i18n::tr ("Lautheit", "Loudness"), r.withHeight (12.0f));
                if (r.getWidth() >= 260.0f)   // the legend only where it fits beside the title (Mehr open: narrow)
                {
                    label (g, i18n::tr ("Eingang", "Input"), r.withHeight (12.0f).withTrimmedRight (90.0f), juce::Justification::topRight, colours::textFaint);
                    label (g, i18n::tr ("Ausgang", "Output"), r.withHeight (12.0f), juce::Justification::topRight, colours::teal);
                }
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
        };

        //======================================================================
        // VOCAL CHECK: what the analysis measured - long-term spectrum and balance
        class CheckView final : public ModuleView
        {
        public:
            explicit CheckView (VelvoxAudioProcessor& p) : proc (p) {}

            void update (const LiveData& d) override
            {
                on = d.settings.checkOn;
                pulse = (float) (0.5 + 0.5 * std::sin (d.time * 3.0));
                repaint();
            }

            void paint (juce::Graphics& g) override
            {
                const auto& r = proc.getLatestAnalysis();
                const float alpha = on ? 1.0f : 0.4f;
                auto area = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
                auto stats = area.removeFromRight (230.0f);
                area.removeFromRight (18.0f);

                // Long-term spectrum of the voice
                label (g, i18n::tr ("Mittleres Spektrum der Stimme", "Average spectrum of the voice"), area.withHeight (12.0f));
                auto plot = area.withTrimmedTop (18.0f).withTrimmedBottom (14.0f);
                SpectrumView::drawFrequencyGrid (g, plot.withTrimmedBottom (-14.0f), true);
                if (! r.spectrumDb.empty())
                {
                    float top = -200.0f;
                    for (auto v : r.spectrumDb)
                        top = std::max (top, v);
                    juce::Path p;
                    bool started = false;
                    float firstX = 0.0f, lastX = 0.0f;
                    for (int b = 0; b < (int) r.spectrumDb.size(); ++b)
                    {
                        if (r.spectrumDb[(size_t) b] < -150.0f)
                            continue;
                        const float f = dsp::VocalAnalyzer::bandCentre (b);
                        const float x = SpectrumView::xForFrequency (std::max (20.0f, f), plot);
                        const float y = yFor (r.spectrumDb[(size_t) b] - top, -60.0f, 3.0f, plot);
                        if (! started) { p.startNewSubPath (x, y); started = true; firstX = x; }
                        else p.lineTo (x, y);
                        lastX = x;
                    }
                    if (started)
                    {
                        g.setGradientFill (auroraGradient (plot.getTopLeft(), plot.getTopRight(), 0.12f * alpha));
                        juce::Path fill (p);
                        fill.lineTo (lastX, plot.getBottom());
                        fill.lineTo (firstX, plot.getBottom());
                        fill.closeSubPath();
                        g.fillPath (fill);
                        glowStroke (g, p, plot, 1.5f, alpha);
                    }
                }
                else
                {
                    label (g, on ? i18n::tr ("Hört zu …", "Listening …") : i18n::tr ("VOCAL CHECK einschalten, um zu analysieren", "Switch VOCAL CHECK on to analyze"), plot, juce::Justification::centred, colours::textDim);
                }

                // Balance bars (relative to the 300 Hz - 2 kHz core) and key numbers
                label (g, i18n::tr ("Balance zum Kern", "Balance to the core"), stats.withHeight (12.0f));
                auto bars = stats.withTrimmedTop (18.0f).removeFromTop (stats.getHeight() * 0.55f);
                const char* names[] { "Low", "Body", "Core", "Pres", "S", "Air" };
                const float bw = bars.getWidth() / 6.0f;
                const float zeroY = yFor (0.0f, -30.0f, 6.0f, bars.withTrimmedBottom (14.0f));
                g.setColour (colours::accent.withAlpha (0.08f));
                g.fillRect (juce::Rectangle<float> (bars.getX(), zeroY, bars.getWidth(), 1.0f));
                for (int i = 0; i < 6; ++i)
                {
                    const auto cell = juce::Rectangle<float> (bars.getX() + bw * (float) i, bars.getY(), bw, bars.getHeight());
                    if (r.enoughMaterial || ! r.spectrumDb.empty())
                    {
                        const float v = i == 2 ? 0.0f : juce::jlimit (-30.0f, 6.0f, r.bandDb[(size_t) i]);
                        const float y = yFor (v, -30.0f, 6.0f, bars.withTrimmedBottom (14.0f));
                        const auto bar = juce::Rectangle<float> (cell.getCentreX() - 5.0f, std::min (y, zeroY), 10.0f, std::max (2.0f, std::abs (y - zeroY)));
                        const auto c = aurora ((float) i / 5.0f);
                        g.setColour (c.withAlpha (0.85f * alpha));
                        g.fillRoundedRectangle (bar, 3.0f);
                    }
                    label (g, names[i], cell.withY (cell.getBottom() - 12.0f).withHeight (12.0f), juce::Justification::centred);
                }

                auto lines = stats.withTrimmedTop (18.0f + stats.getHeight() * 0.55f + 6.0f);
                const auto line = [&] (const juce::String& k, const juce::String& v)
                {
                    auto row = lines.removeFromTop (15.0f);
                    label (g, k, row, juce::Justification::centredLeft);
                    g.setColour (colours::text.withAlpha (alpha));
                    g.setFont (medium (11.0f));
                    g.drawText (v, row, juce::Justification::centredRight, false);
                };
                line (i18n::tr ("Stimme analysiert", "Voice analyzed"), juce::String (r.voicedSeconds, 1) + " s");
                line (i18n::tr ("Spitze", "Peak"), r.peakDb <= -99.0f ? juce::String ("-") : juce::String (r.peakDb, 1) + " dBFS");
                line (i18n::tr ("Pegelumfang", "Loudness range"), r.loudnessRangeDb > 0.0f ? juce::String (r.loudnessRangeDb, 1) + " dB" : juce::String ("-"));
                line (i18n::tr ("S zu Vokalen", "S to vowels"), r.sibilanceVsVowelDb > -99.0f ? juce::String (r.sibilanceVsVowelDb, 1) + " dB" : juce::String ("-"));

                if (on)
                    drawGlow (g, { stats.getRight() - 4.0f, stats.getY() + 5.0f }, 8.0f, 0.3f + 0.4f * pulse, colours::teal);
            }

        private:
            VelvoxAudioProcessor& proc;
            bool on = false;
            float pulse = 0.0f;
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
            case ModuleId::check:   return std::make_unique<CheckView> (p);
            case ModuleId::ride:    return createDynView (p, ModuleId::ride, instance);
        }
        return std::make_unique<OutputView>();
    }
}
