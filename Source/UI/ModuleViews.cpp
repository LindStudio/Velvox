#include "ModuleViews.h"
#include "EqPanel.h"
#include "ReverbPanel.h"
#include "SmoothPanel.h"
#include "PitchPanel.h"
#include "../PluginProcessor.h"
#include "SatPanel.h"
#include "../DSP/ChopModule.h"
#include "TimePanels.h"
#include "VoicesPanels.h"

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
        // Compressor: level history with gain reduction, plus the transfer curve
        class CompView final : public ModuleView
        {
        public:
            void update (const LiveData& d) override
            {
                live = &d;
                s = d.settings.comp;
                on = d.settings.compOn;
                level.target = d.inputDb.ago (0);
                level.tick (0.35f);
                repaint();
            }

            void paint (juce::Graphics& g) override
            {
                if (live == nullptr)
                    return;
                auto r = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
                const float alpha = on ? 1.0f : 0.4f;

                auto transfer = r.removeFromRight (r.getHeight()).reduced (4.0f, 0.0f);
                r.removeFromRight (18.0f);
                label (g, "Level and gain reduction", r.withHeight (12.0f));
                auto plot = r.withTrimmedTop (18.0f);

                hGrid (g, plot, { -12.0f, -24.0f, -36.0f, -48.0f }, -60.0f, 0.0f, " dB");

                // Input (reference) and output
                const auto in = historyPath (live->inputDb, plot, -60.0f, 0.0f);
                g.setColour (colours::accent.withAlpha (0.05f));
                g.fillPath (closedTo (in, plot, plot.getBottom()));
                g.setColour (colours::accent.withAlpha (0.20f));
                g.strokePath (in, juce::PathStrokeType (1.0f));

                const auto out = historyPath (live->outputDb, plot, -60.0f, 0.0f);
                g.setGradientFill (auroraGradient (plot.getTopLeft(), plot.getTopRight(), 0.12f * alpha));
                g.fillPath (closedTo (out, plot, plot.getBottom()));
                glowStroke (g, out, plot, 1.4f, alpha);

                // Gain reduction hangs from the top edge
                auto grArea = plot.withHeight (plot.getHeight() * 0.45f);
                const auto gr = historyPath (live->compGr, grArea, 0.0f, 24.0f, true);
                g.setGradientFill (juce::ColourGradient (colours::blue.withAlpha (0.30f * alpha), 0.0f, grArea.getY(),
                                                         colours::blue.withAlpha (0.05f * alpha), 0.0f, grArea.getBottom(), false));
                g.fillPath (closedTo (gr, grArea, grArea.getY()));
                g.setColour (colours::blue.withAlpha (0.9f * alpha));
                g.strokePath (gr, juce::PathStrokeType (1.2f));
                g.setColour (colours::text);
                g.setFont (display (20.0f));
                const float grNow = live->compGr.ago (0);
                g.drawText ((grNow > 0.05f ? "-" : "") + juce::String (grNow, 1), plot.withTrimmedRight (46.0f).withHeight (24.0f).translated (0.0f, 6.0f),
                            juce::Justification::topRight, false);
                label (g, "GR dB", plot.withTrimmedRight (46.0f).withHeight (12.0f).translated (0.0f, 32.0f), juce::Justification::topRight);

                // Transfer curve
                g.setColour (juce::Colours::black.withAlpha (0.25f));
                g.fillRoundedRectangle (transfer, 10.0f);
                g.setColour (colours::lineSoft);
                g.drawRoundedRectangle (transfer.reduced (0.5f), 10.0f, 1.0f);
                const auto t = transfer.reduced (12.0f);
                for (int i = 1; i < 4; ++i)
                {
                    g.setColour (colours::accent.withAlpha (0.035f));
                    g.fillRect (juce::Rectangle<float> (t.getX() + t.getWidth() * (float) i / 4.0f, t.getY(), 1.0f, t.getHeight()));
                    g.fillRect (juce::Rectangle<float> (t.getX(), t.getY() + t.getHeight() * (float) i / 4.0f, t.getWidth(), 1.0f));
                }
                g.setColour (colours::accent.withAlpha (0.08f));
                g.drawLine (t.getX(), t.getBottom(), t.getRight(), t.getY(), 1.0f);

                const auto mapX = [&] (float db) { return juce::jmap (juce::jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, t.getX(), t.getRight()); };
                const auto mapY = [&] (float db) { return yFor (db, -60.0f, 0.0f, t); };
                juce::Path curve;
                for (int i = 0; i <= 80; ++i)
                {
                    const float x = -60.0f + 60.0f * (float) i / 80.0f;
                    const auto pt = juce::Point<float> (mapX (x), mapY (computer (x) + s.makeupDb));
                    if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
                }
                {
                    juce::Graphics::ScopedSaveState save (g);
                    g.reduceClipRegion (t.toNearestInt());
                    glowStroke (g, curve, t, 2.0f, alpha);
                }

                // Threshold marker
                const float tx = mapX (s.thresholdDb);
                g.setColour (colours::teal.withAlpha (0.35f * alpha));
                const float dash[] { 2.0f, 3.0f };
                g.drawDashedLine (juce::Line<float> (tx, t.getY(), tx, t.getBottom()), dash, 2, 1.0f);

                // Where the signal sits right now
                if (live->hasSignal)
                {
                    const auto dot = juce::Point<float> (mapX (level.current), mapY (computer (level.current) + s.makeupDb));
                    drawGlow (g, dot, 16.0f, 0.6f * alpha, colours::teal);
                    g.setColour (juce::Colours::white);
                    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
                }
                label (g, "Transfer", transfer.reduced (12.0f, 10.0f));
                label (g, juce::String (s.ratio, 1) + ":1", transfer.reduced (12.0f, 10.0f), juce::Justification::bottomRight, colours::text);
            }

        private:
            float computer (float x) const
            {
                const float T = s.thresholdDb, R = std::max (1.0f, s.ratio), W = std::max (0.0f, s.kneeDb);
                if (2.0f * (x - T) < -W)
                    return x;
                if (W > 0.0f && 2.0f * std::abs (x - T) <= W)
                    return x + (1.0f / R - 1.0f) * (x - T + W * 0.5f) * (x - T + W * 0.5f) / (2.0f * W);
                return T + (x - T) / R;
            }

            const LiveData* live = nullptr;
            dsp::CompSettings s;
            bool on = true;
            Smoothed level;
        };

        //======================================================================
        // De-esser: spectrum with the S band lit by the current reduction
        class DeEssView final : public ModuleView
        {
        public:
            explicit DeEssView (const SpectrumSource* src) : spectrum (src)
            {
                addAndMakeVisible (spectrum);
                spectrum.setRange (-90.0f, 0.0f);
                spectrum.setIntensity (0.7f);
            }

            void resized() override { spectrum.setBounds (getLocalBounds().withTrimmedBottom (46)); }

            void update (const LiveData& d) override
            {
                live = &d;
                s = d.settings.deess;
                on = d.settings.deessOn;
                gr.target = d.deessGr.ago (0);
                gr.tick (0.3f);
                spectrum.refresh();
                repaint();
            }

            void paintOverChildren (juce::Graphics& g) override
            {
                if (live == nullptr)
                    return;
                const auto plot = spectrum.getBounds().toFloat();
                const float x = SpectrumView::xForFrequency (s.freq, plot);
                const float amount = juce::jlimit (0.0f, 1.0f, gr.current / std::max (1.0f, s.rangeDb));
                const float alpha = on ? 1.0f : 0.4f;

                const auto band = s.mode == 0 ? plot.withLeft (x) : plot;
                g.setGradientFill (juce::ColourGradient (colours::blue.withAlpha ((0.05f + 0.30f * amount) * alpha), 0.0f, band.getY(),
                                                         colours::blue.withAlpha (0.0f), 0.0f, band.getBottom(), false));
                g.fillRect (band);

                drawGlow (g, { x, plot.getY() + 20.0f }, 30.0f, 0.3f * alpha, colours::teal);
                g.setColour (aurora (0.3f, 0.9f * alpha));
                g.fillRect (juce::Rectangle<float> (x - 0.75f, plot.getY(), 1.5f, plot.getHeight()));
                label (g, (s.mode == 0 ? "S band from " : "Detect from ") + hz (s.freq), plot.reduced (12.0f, 10.0f).withLeft (x + 8.0f),
                       juce::Justification::topLeft, colours::text);

                // Reduction over time
                auto strip = getLocalBounds().toFloat().withTop (plot.getBottom() + 8.0f).reduced (12.0f, 2.0f);
                const auto path = historyPath (live->deessGr, strip, 0.0f, 12.0f, true);
                g.setGradientFill (auroraGradient (strip.getTopLeft(), strip.getTopRight(), 0.25f * alpha));
                g.fillPath (closedTo (path, strip, strip.getY()));
                glowStroke (g, path, strip, 1.2f, alpha);
                label (g, "Reduction  " + juce::String (gr.current, 1) + " dB", strip.withHeight (12.0f).translated (0.0f, strip.getHeight() - 12.0f));
            }

        private:
            SpectrumView spectrum;
            const LiveData* live = nullptr;
            dsp::DeEssSettings s;
            bool on = true;
            Smoothed gr;
        };

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
                auto scope = r.removeFromLeft (r.getHeight());
                r.removeFromLeft (24.0f);

                // Goniometer: mid up, side across
                label (g, "Stereo image", scope.withHeight (12.0f));
                const auto sq = scope.withTrimmedTop (16.0f).reduced (6.0f);
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
                label (g, "Loudness", r.withHeight (12.0f));
                label (g, "Input", r.withHeight (12.0f).withTrimmedRight (90.0f), juce::Justification::topRight, colours::textFaint);
                label (g, "Output", r.withHeight (12.0f), juce::Justification::topRight, colours::teal);
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
        // VOCAL SMASH: reduction over time, and the curve of the squashed path
        // (input level -> level of the added path) against the original
        class SmashView final : public ModuleView
        {
        public:
            SmashView (VelvoxAudioProcessor& p, int k) : proc (p), instance (k) {}

            void update (const LiveData& d) override
            {
                live = &d;
                s = d.settings.smash;
                level.target = d.inputDb.ago (0);
                level.tick (0.35f);
                added.target = proc.getChain().getSmash (instance).blendLevelDb.load();
                added.tick (0.3f);
                repaint();
            }

            void paint (juce::Graphics& g) override
            {
                if (live == nullptr)
                    return;
                const float alpha = s.enabled ? 1.0f : 0.4f;
                auto r = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
                auto transfer = r.removeFromRight (r.getHeight()).reduced (4.0f, 0.0f);
                r.removeFromRight (18.0f);

                label (g, "Squashed path  -  gain reduction", r.withHeight (12.0f));
                auto plot = r.withTrimmedTop (18.0f);
                hGrid (g, plot, { 6.0f, 12.0f, 18.0f }, 24.0f, 0.0f, " dB");

                const auto gr = historyPath (live->smashGr, plot, 0.0f, 24.0f, true);
                g.setGradientFill (juce::ColourGradient (colours::blue.withAlpha (0.35f * alpha), 0.0f, plot.getY(),
                                                         colours::teal.withAlpha (0.04f * alpha), 0.0f, plot.getBottom(), false));
                g.fillPath (closedTo (gr, plot, plot.getY()));
                glowStroke (g, gr, plot, 1.4f, alpha);

                g.setColour (colours::text);
                g.setFont (display (22.0f));
                g.drawText ("-" + juce::String (live->smashGr.ago (0), 1), plot.withTrimmedRight (46.0f).withHeight (26.0f).translated (0.0f, plot.getHeight() - 40.0f),
                            juce::Justification::bottomRight, false);
                label (g, "Blend " + juce::String (juce::roundToInt (s.blend * 100.0f)) + " %   added path " + juce::String (added.current, 1) + " dBFS",
                       plot.withTrimmedRight (46.0f).withY (plot.getBottom() - 12.0f).withHeight (12.0f), juce::Justification::bottomLeft);

                // Curve: original (diagonal) and squashed path output level
                g.setColour (juce::Colours::black.withAlpha (0.25f));
                g.fillRoundedRectangle (transfer, 10.0f);
                const auto t = transfer.reduced (12.0f);
                const auto mapX = [&] (float db) { return juce::jmap (juce::jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, t.getX(), t.getRight()); };
                const auto mapY = [&] (float db) { return yFor (db, -60.0f, 0.0f, t); };
                g.setColour (colours::accent.withAlpha (0.12f));
                g.drawLine (t.getX(), t.getBottom(), t.getRight(), t.getY(), 1.0f);
                juce::Path curve;
                for (int i = 0; i <= 90; ++i)
                {
                    const float x = -60.0f + 60.0f * (float) i / 90.0f;
                    const auto pt = juce::Point<float> (mapX (x), mapY (x + dsp::SmashModule::wetGainDb (x, s.amount)));
                    if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
                }
                {
                    juce::Graphics::ScopedSaveState save (g);
                    g.reduceClipRegion (t.toNearestInt());
                    glowStroke (g, curve, t, 2.0f, alpha);
                }
                if (live->hasSignal)
                {
                    const float lv = level.current + 9.0f; // RMS meter -> detector peak, roughly
                    const auto dot = juce::Point<float> (mapX (lv), mapY (lv + dsp::SmashModule::wetGainDb (lv, s.amount)));
                    drawGlow (g, dot, 16.0f, 0.6f * alpha, colours::teal);
                    g.setColour (juce::Colours::white);
                    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
                }
                label (g, "Level in -> squashed out", transfer.reduced (12.0f, 10.0f));
                label (g, "Thr " + juce::String (juce::roundToInt (dsp::SmashModule::thresholdFor (s.amount))) + " dB  " + juce::String (dsp::SmashModule::ratioFor (s.amount), 1) + ":1",
                       transfer.reduced (12.0f, 10.0f), juce::Justification::bottomRight, colours::text);
            }

        private:
            VelvoxAudioProcessor& proc;
            const int instance;
            const LiveData* live = nullptr;
            dsp::SmashSettings s;
            Smoothed level, added { -100.0f, -100.0f };
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
                label (g, "Average spectrum of the voice", area.withHeight (12.0f));
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
                    label (g, on ? "Listening ..." : "Switch VOCAL CHECK on to analyse", plot, juce::Justification::centred, colours::textDim);
                }

                // Balance bars (relative to the 300 Hz - 2 kHz core) and key numbers
                label (g, "Balance vs. core", stats.withHeight (12.0f));
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
                line ("Voice analysed", juce::String (r.voicedSeconds, 1) + " s");
                line ("Peak", r.peakDb <= -99.0f ? juce::String ("-") : juce::String (r.peakDb, 1) + " dBFS");
                line ("Level range", r.loudnessRangeDb > 0.0f ? juce::String (r.loudnessRangeDb, 1) + " dB" : juce::String ("-"));
                line ("S vs vowels", r.sibilanceVsVowelDb > -99.0f ? juce::String (r.sibilanceVsVowelDb, 1) + " dB" : juce::String ("-"));

                if (on)
                    drawGlow (g, { stats.getRight() - 4.0f, stats.getY() + 5.0f }, 8.0f, 0.3f + 0.4f * pulse, colours::teal);
            }

        private:
            VelvoxAudioProcessor& proc;
            bool on = false;
            float pulse = 0.0f;
        };

        //======================================================================
        // LEVEL RIDE: detected level, the gain ride and the result over time
        class RideView final : public ModuleView
        {
        public:
            void update (const LiveData& d) override
            {
                live = &d;
                s = d.settings.ride;
                repaint();
            }

            void paint (juce::Graphics& g) override
            {
                if (live == nullptr)
                    return;
                const float alpha = s.enabled ? 1.0f : 0.4f;
                auto r = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
                auto readout = r.removeFromRight (110.0f);
                r.removeFromRight (16.0f);
                label (g, "Voice level and gain ride", r.withHeight (12.0f));
                auto plot = r.withTrimmedTop (18.0f);
                auto gainBand = plot.removeFromBottom (plot.getHeight() * 0.32f);
                plot.removeFromBottom (8.0f);

                // Level lanes (dB): detected, result, target, pause threshold
                hGrid (g, plot, { -12.0f, -24.0f, -36.0f, -48.0f }, -60.0f, 0.0f, " dB");
                const auto in = historyPath (live->rideLevel, plot, -60.0f, 0.0f);
                g.setColour (colours::accent.withAlpha (0.22f));
                g.strokePath (in, juce::PathStrokeType (1.0f));

                juce::Path out;
                for (int i = 0; i < LiveData::historySize; ++i)
                {
                    const float x = plot.getRight() - plot.getWidth() * (float) i / (float) (LiveData::historySize - 1);
                    const float y = yFor (live->rideLevel.ago (i) + live->rideGain.ago (i), -60.0f, 0.0f, plot);
                    if (i == 0) out.startNewSubPath (x, y); else out.lineTo (x, y);
                }
                glowStroke (g, out, plot, 1.5f, alpha);

                const float ty = yFor (s.targetDb, -60.0f, 0.0f, plot);
                const float dash[] { 4.0f, 4.0f };
                g.setColour (colours::teal.withAlpha (0.6f * alpha));
                g.drawDashedLine (juce::Line<float> (plot.getX(), ty, plot.getRight(), ty), dash, 2, 1.0f);
                label (g, "Target", juce::Rectangle<float> (plot.getX() + 4.0f, ty - 13.0f, 60.0f, 12.0f), juce::Justification::centredLeft, colours::teal);
                const float sy = yFor (s.silenceDb, -60.0f, 0.0f, plot);
                g.setColour (colours::accent.withAlpha (0.12f));
                g.drawDashedLine (juce::Line<float> (plot.getX(), sy, plot.getRight(), sy), dash, 2, 1.0f);
                label (g, "Pause below", juce::Rectangle<float> (plot.getX() + 4.0f, sy + 2.0f, 80.0f, 12.0f), juce::Justification::centredLeft, colours::textFaint);

                // Gain ride (+ above, - below the centre line)
                const float zero = gainBand.getCentreY();
                g.setColour (colours::accent.withAlpha (0.08f));
                g.fillRect (juce::Rectangle<float> (gainBand.getX(), zero, gainBand.getWidth(), 1.0f));
                const auto gainPath = historyPath (live->rideGain, gainBand, -18.0f, 18.0f);
                g.setGradientFill (auroraGradient (gainBand.getTopLeft(), gainBand.getTopRight(), 0.18f * alpha));
                g.fillPath (closedTo (gainPath, gainBand, zero));
                glowStroke (g, gainPath, gainBand, 1.4f, alpha);
                label (g, "Gain  +/-18 dB", gainBand.withHeight (12.0f));

                const float gNow = live->rideGain.ago (0);
                g.setColour (colours::text);
                g.setFont (display (28.0f));
                g.drawText ((gNow > 0.05f ? "+" : "") + juce::String (gNow, 1), readout.withHeight (34.0f).translated (0.0f, readout.getHeight() * 0.3f),
                            juce::Justification::centred, false);
                label (g, "dB ride", readout.withHeight (12.0f).translated (0.0f, readout.getHeight() * 0.3f + 36.0f), juce::Justification::centred);
                const bool voice = live->rideLevel.ago (0) > s.silenceDb;
                label (g, voice ? "Voice" : "Pause", readout.withHeight (12.0f).translated (0.0f, readout.getHeight() * 0.3f + 54.0f),
                       juce::Justification::centred, voice ? colours::teal : colours::textFaint);
            }

        private:
            const LiveData* live = nullptr;
            dsp::RideSettings s;
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
            case ModuleId::comp:    return std::make_unique<CompView>();
            case ModuleId::deess:   return std::make_unique<DeEssView> (spectrum);
            case ModuleId::sat:     return createSatView (p, instance);
            case ModuleId::doubler: return createVoicesModuleView (p, ModuleId::doubler, instance);
            case ModuleId::pitch:   return createPitchStage (p);
            case ModuleId::chop:    return createTimeModuleView (p, ModuleId::chop, instance);
            case ModuleId::delay:   return createTimeModuleView (p, ModuleId::delay, instance);
            case ModuleId::reverb:  return createReverbRoom (p, instance);
            case ModuleId::pulse:   return createTimeModuleView (p, ModuleId::pulse, instance);
            case ModuleId::smash:   return std::make_unique<SmashView> (p, instance);
            case ModuleId::choir:   return createVoicesModuleView (p, ModuleId::choir, instance);
            case ModuleId::smooth:  return createSmoothGraph (p, instance);
            case ModuleId::check:   return std::make_unique<CheckView> (p);
            case ModuleId::ride:    return std::make_unique<RideView>();
        }
        return std::make_unique<OutputView>();
    }
}
