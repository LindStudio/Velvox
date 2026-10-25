#include "ModuleViews.h"
#include "EqPanel.h"
#include "ReverbPanel.h"
#include "../PluginProcessor.h"
#include "../DSP/SaturationModule.h"
#include "../DSP/ChopModule.h"
#include "TimePanels.h"

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
        // Saturation: transfer curve with a travelling signal, and the harmonics it adds
        class SatView final : public ModuleView
        {
        public:
            void update (const LiveData& d) override
            {
                const auto& st = d.settings.sat;
                if (st.driveDb != drive || st.mix != mix || st.character != character || st.enabled != on)
                {
                    drive = st.driveDb;
                    mix = st.mix;
                    character = st.character;
                    on = st.enabled;
                    computeHarmonics();
                }
                time = d.time;
                level = d.level();
                repaint();
            }

            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
                const float alpha = on ? 1.0f : 0.4f;
                auto curveArea = r.removeFromLeft (r.getHeight() * 1.25f);
                r.removeFromLeft (24.0f);

                // Transfer curve
                label (g, "Transfer", curveArea.withHeight (12.0f));
                const auto t = curveArea.withTrimmedTop (18.0f);
                g.setColour (juce::Colours::black.withAlpha (0.25f));
                g.fillRoundedRectangle (t, 10.0f);
                const auto plot = t.reduced (12.0f);
                g.setColour (colours::accent.withAlpha (0.05f));
                g.fillRect (juce::Rectangle<float> (plot.getCentreX(), plot.getY(), 1.0f, plot.getHeight()));
                g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getCentreY(), plot.getWidth(), 1.0f));
                g.setColour (colours::accent.withAlpha (0.08f));
                g.drawLine (plot.getX(), plot.getBottom(), plot.getRight(), plot.getY(), 1.0f);

                const auto map = [&] (float x) { return juce::Point<float> (juce::jmap (x, -1.0f, 1.0f, plot.getX(), plot.getRight()),
                                                                            juce::jmap (juce::jlimit (-1.2f, 1.2f, transfer (x)), -1.2f, 1.2f, plot.getBottom(), plot.getY())); };
                juce::Path p;
                for (int i = 0; i <= 96; ++i)
                {
                    const auto pt = map (-1.0f + 2.0f * (float) i / 96.0f);
                    if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
                }
                glowStroke (g, p, plot, 2.0f, alpha);

                // A signal riding the curve, its swing follows the output level
                const float swing = 0.35f + 0.6f * level;
                for (int k = 6; k >= 0; --k)
                {
                    const float x = swing * std::sin ((float) (time - k * 0.03) * twoPi * 0.4f);
                    const auto pt = map (x);
                    drawGlow (g, pt, 10.0f - (float) k, (k == 0 ? 0.7f : 0.12f) * alpha, colours::teal);
                }
                g.setColour (juce::Colours::white.withAlpha (alpha));
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (map (swing * std::sin ((float) time * twoPi * 0.4f))));

                // Harmonics
                label (g, "Harmonics (sine at -6 dBFS)", r.withHeight (12.0f));
                label (g, "THD " + juce::String (thd, thd < 1.0f ? 2 : 1) + " %", r.withHeight (12.0f), juce::Justification::topRight, colours::text);
                auto bars = r.withTrimmedTop (22.0f).withTrimmedBottom (16.0f);
                hGrid (g, bars, { -20.0f, -40.0f, -60.0f }, -80.0f, 0.0f, " dB");
                const int n = (int) harmonics.size();
                const float slot = (bars.getWidth() - 44.0f) / (float) n;
                for (int i = 0; i < n; ++i)
                {
                    const float x = bars.getX() + slot * (float) i + slot * 0.2f;
                    const float top = yFor (harmonics[(size_t) i], -80.0f, 0.0f, bars);
                    const auto bar = juce::Rectangle<float> (x, top, slot * 0.6f, bars.getBottom() - top);
                    const auto c = aurora ((float) i / (float) (n - 1));
                    if (bar.getHeight() > 1.0f)
                    {
                        drawGlow (g, { bar.getCentreX(), top }, slot * 0.7f, 0.25f * alpha, c);
                        g.setGradientFill (juce::ColourGradient (c.withAlpha (0.9f * alpha), 0.0f, bar.getY(), c.withAlpha (0.15f * alpha), 0.0f, bar.getBottom(), false));
                        g.fillRoundedRectangle (bar, 3.0f);
                    }
                    g.setColour (colours::textFaint);
                    g.setFont (medium (10.5f));
                    g.drawText (juce::String (i + 1), juce::Rectangle<float> (x - 4.0f, bars.getBottom() + 3.0f, slot * 0.6f + 8.0f, 12.0f), juce::Justification::centred, false);
                }
            }

        private:
            float transfer (float x) const
            {
                const float driveLin = juce::Decibels::decibelsToGain (drive);
                const float wet = dsp::SaturationModule::shape (x, driveLin, character) * dsp::SaturationModule::compensationFor (driveLin, character);
                return x + (wet - x) * mix;
            }

            void computeHarmonics()
            {
                constexpr int N = 512;
                std::array<float, N> y {};
                for (int i = 0; i < N; ++i)
                    y[(size_t) i] = transfer (0.5f * std::sin (twoPi * (float) i / (float) N));

                std::array<float, 12> mags {};
                for (int k = 1; k <= 12; ++k)
                {
                    double re = 0.0, im = 0.0;
                    for (int i = 0; i < N; ++i)
                    {
                        const double ph = twoPi * k * i / (double) N;
                        re += y[(size_t) i] * std::cos (ph);
                        im += y[(size_t) i] * std::sin (ph);
                    }
                    mags[(size_t) k - 1] = (float) (2.0 * std::sqrt (re * re + im * im) / N);
                }
                double distortion = 0.0;
                for (int k = 0; k < 12; ++k)
                {
                    harmonics[(size_t) k] = k == 0 ? 0.0f : juce::Decibels::gainToDecibels (mags[(size_t) k] / std::max (1.0e-9f, mags[0]), -100.0f);
                    if (k > 0)
                        distortion += mags[(size_t) k] * mags[(size_t) k];
                }
                thd = (float) (100.0 * std::sqrt (distortion) / std::max (1.0e-9f, mags[0]));
            }

            float drive = -100.0f, mix = -1.0f, thd = 0.0f, level = 0.0f;
            int character = -1;
            bool on = false;
            double time = 0.0;
            std::array<float, 12> harmonics {};
        };

        //======================================================================
        // Doubler: the stereo stage - lead in the centre, two voices drifting at the sides
        class DoublerView final : public ModuleView
        {
        public:
            void update (const LiveData& d) override
            {
                s = d.settings.doubler;
                time = d.time;
                level.target = d.level();
                level.tick (0.15f);
                spread.target = s.width;
                spread.tick (0.12f);
                repaint();
            }

            void paint (juce::Graphics& g) override
            {
                const auto r = getLocalBounds().toFloat().reduced (24.0f, 16.0f);
                const float alpha = s.enabled ? 1.0f : 0.35f;
                const float cy = r.getCentreY() - 6.0f;
                const auto centre = juce::Point<float> (r.getCentreX(), cy);

                // Stage
                juce::Path arc;
                arc.startNewSubPath (r.getX(), cy + 30.0f);
                arc.quadraticTo (r.getCentreX(), cy - 50.0f, r.getRight(), cy + 30.0f);
                g.setColour (colours::accent.withAlpha (0.05f));
                g.strokePath (arc, juce::PathStrokeType (1.0f));
                g.setColour (colours::accent.withAlpha (0.04f));
                g.fillRect (juce::Rectangle<float> (r.getCentreX() - 0.5f, r.getY(), 1.0f, r.getHeight() - 22.0f));
                label (g, "L", r.withWidth (20.0f), juce::Justification::centredLeft);
                label (g, "R", r.withLeft (r.getRight() - 20.0f), juce::Justification::centredRight);
                label (g, "Stereo field", r.withHeight (12.0f));

                // Lead vocal
                drawGlow (g, centre, 46.0f + 20.0f * level.current, 0.18f + 0.25f * level.current);
                g.setColour (juce::Colours::white);
                g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (centre));

                // Two voices: placed by width, wobbling with detune, echo rings from timing
                const float detune = juce::jlimit (0.0f, 1.0f, s.detuneCents / 30.0f);
                for (int v : { -1, 1 })
                {
                    const float ph = (float) time * twoPi;
                    const float dx = std::sin (ph * 0.23f + (float) v) * 10.0f * detune;
                    const float dy = std::cos (ph * 0.31f + (float) v * 1.7f) * 8.0f * detune;
                    const auto p = juce::Point<float> (centre.x + (float) v * spread.current * r.getWidth() * 0.42f + dx, cy + dy + 6.0f);
                    const auto c = aurora (v < 0 ? 0.05f : 0.95f);
                    const float strength = (0.3f + 0.7f * s.mix) * alpha;

                    g.setColour (c.withAlpha (0.18f * strength));
                    g.drawLine (juce::Line<float> (centre, p), 1.0f);

                    // Timing: rings expanding out of the voice
                    const float period = 0.6f + s.delayMs / 40.0f;
                    for (int k = 0; k < 3; ++k)
                    {
                        const float phase = std::fmod ((float) time / period + (float) k / 3.0f + (v > 0 ? 0.37f : 0.0f), 1.0f);
                        const float rad = 10.0f + phase * (16.0f + s.delayMs * 1.2f);
                        g.setColour (c.withAlpha ((1.0f - phase) * 0.35f * strength));
                        g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (p), 1.0f);
                    }

                    drawGlow (g, p, 30.0f + 14.0f * level.current, 0.55f * strength, c);
                    g.setColour (c.interpolatedWith (juce::Colours::white, 0.5f).withAlpha (alpha));
                    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (p));
                }

                // Mono safe
                auto meter = r.withTop (r.getBottom() - 14.0f).withSizeKeepingCentre (220.0f, 14.0f);
                label (g, "Mono safe", meter.withWidth (70.0f), juce::Justification::centredLeft);
                auto bar = meter.withTrimmedLeft (78.0f).withSizeKeepingCentre (meter.getWidth() - 78.0f, 3.0f);
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                g.fillRoundedRectangle (bar, 1.5f);
                g.setGradientFill (auroraGradient (bar.getTopLeft(), bar.getTopRight(), alpha));
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * s.monoSafe), 1.5f);
            }

        private:
            dsp::DoublerSettings s;
            double time = 0.0;
            Smoothed level, spread;
        };

        //======================================================================
        // Pitch: keyboard with the transposition drawn as a glowing arc
        class PitchView final : public ModuleView
        {
        public:
            void update (const LiveData& d) override
            {
                s = d.settings.pitch;
                shift.target = s.semitones + s.cents / 100.0f;
                shift.tick (0.14f);
                formant.target = s.formantShift;
                formant.tick (0.14f);
                level = d.level();
                repaint();
            }

            void paint (juce::Graphics& g) override
            {
                const auto r = getLocalBounds().toFloat().reduced (20.0f, 14.0f);
                const float alpha = s.enabled ? 1.0f : 0.35f;
                auto keys = r.withTop (r.getBottom() - r.getHeight() * 0.38f);

                // Read-out
                const juce::String st = (shift.current > 0.05f ? "+" : "") + juce::String (juce::roundToInt (s.semitones)) + " st";
                g.setColour (colours::text.withAlpha (0.4f + 0.6f * alpha));
                g.setFont (display (30.0f));
                g.drawText (st, r.withHeight (34.0f), juce::Justification::topLeft, false);
                label (g, juce::String (juce::roundToInt (s.cents)) + " ct fine   " + juce::String (juce::roundToInt (s.mix * 100.0f)) + " % mix",
                       r.withHeight (12.0f).translated (0.0f, 40.0f));
                label (g, s.preserveFormants ? "Formants kept  " + juce::String (s.formantShift, 1) + " st" : "Formants follow the pitch",
                       r.withHeight (12.0f), juce::Justification::topRight);

                // Keyboard: -24 .. +24 semitones around the sung note (0)
                const int whiteCount = 29;
                const float ww = keys.getWidth() / (float) whiteCount;
                int w = 0;
                std::array<float, 49> centres {};
                for (int k = -24; k <= 24; ++k)
                {
                    const int note = ((k % 12) + 12) % 12;
                    const bool black = note == 1 || note == 3 || note == 6 || note == 8 || note == 10;
                    centres[(size_t) (k + 24)] = black ? keys.getX() + ww * (float) w : keys.getX() + ww * ((float) w + 0.5f);
                    if (! black)
                    {
                        const auto key = juce::Rectangle<float> (keys.getX() + ww * (float) w, keys.getY(), ww - 2.0f, keys.getHeight());
                        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff23252b), 0.0f, key.getY(), juce::Colour (0xff17181c), 0.0f, key.getBottom(), false));
                        g.fillRoundedRectangle (key, 3.0f);
                        ++w;
                    }
                }
                for (int k = -24; k <= 24; ++k)
                {
                    const int note = ((k % 12) + 12) % 12;
                    if (note == 1 || note == 3 || note == 6 || note == 8 || note == 10)
                    {
                        const auto key = juce::Rectangle<float> (ww * 0.62f, keys.getHeight() * 0.6f).withCentre ({ centres[(size_t) (k + 24)], keys.getY() + keys.getHeight() * 0.3f });
                        g.setColour (juce::Colour (0xff08090b));
                        g.fillRoundedRectangle (key, 2.0f);
                    }
                }

                const auto keyX = [&] (float semis)
                {
                    const float c = juce::jlimit (-24.0f, 24.0f, semis) + 24.0f;
                    const int i0 = (int) std::floor (c);
                    const int i1 = std::min (48, i0 + 1);
                    return centres[(size_t) i0] + (centres[(size_t) i1] - centres[(size_t) i0]) * (c - (float) i0);
                };

                // Octave labels
                g.setFont (medium (10.5f));
                for (int o : { -24, -12, 0, 12, 24 })
                {
                    g.setColour (o == 0 ? colours::text : colours::textFaint);
                    g.drawText ((o > 0 ? "+" : "") + juce::String (o), juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ keyX ((float) o), keys.getBottom() - 9.0f }),
                                juce::Justification::centred, false);
                }

                // Sung note and its target, joined by an arc
                const float x0 = keyX (0.0f), x1 = keyX (shift.current);
                const float baseY = keys.getY() - 6.0f;
                drawGlow (g, { x0, baseY }, 14.0f, 0.3f);
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x0, baseY }));

                if (std::abs (x1 - x0) > 1.0f)
                {
                    juce::Path arc;
                    const float lift = 18.0f + std::abs (shift.current) * 2.2f;
                    arc.startNewSubPath (x0, baseY);
                    arc.quadraticTo ((x0 + x1) * 0.5f, baseY - lift, x1, baseY);
                    glowStroke (g, arc, juce::Rectangle<float> (std::min (x0, x1), baseY - lift, std::abs (x1 - x0), lift), 2.0f, alpha * (0.4f + 0.6f * s.mix));
                }
                const auto target = juce::Point<float> (x1, baseY);
                drawGlow (g, target, 22.0f + 10.0f * level, 0.6f * alpha, colours::teal);
                g.setColour (colours::teal.interpolatedWith (juce::Colours::white, 0.5f).withAlpha (alpha));
                g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (target));

                // Formant marker (diamond under the keys' top edge)
                const float fx = keyX (s.preserveFormants ? formant.current : shift.current);
                juce::Path diamond;
                diamond.addStar ({ fx, keys.getY() + keys.getHeight() * 0.78f }, 2, 3.0f, 6.0f, juce::MathConstants<float>::halfPi * 0.5f);
                g.setColour (colours::blue.withAlpha (0.9f * alpha));
                g.fillPath (diamond);
            }

        private:
            dsp::PitchSettings s;
            Smoothed shift, formant;
            float level = 0.0f;
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
        // CHOIR: every voice on the stage with its real pitch offset
        class ChoirView final : public ModuleView
        {
        public:
            ChoirView (VelvoxAudioProcessor& p, int k) : proc (p), instance (k) {}

            void update (const LiveData& d) override
            {
                s = d.settings.choir;
                time = d.time;
                level.target = d.level();
                level.tick (0.15f);
                spread.target = s.width;
                spread.tick (0.12f);
                auto& m = proc.getChain().getChoir (instance);
                corr.target = s.enabled ? m.outputCorrelation.load() : 1.0f;
                corr.tick (0.1f);
                for (int i = 0; i < dsp::ChoirModule::maxVoices; ++i)
                    cents[(size_t) i] = m.voiceCents[(size_t) i].load();
                repaint();
            }

            void paint (juce::Graphics& g) override
            {
                const float alpha = s.enabled ? 1.0f : 0.35f;
                auto r = getLocalBounds().toFloat().reduced (24.0f, 14.0f);
                label (g, juce::String (s.voices) + " voices", r.withHeight (12.0f));
                auto meter = r.removeFromBottom (16.0f);
                const float cy = r.getCentreY() + 4.0f;
                const auto centre = juce::Point<float> (r.getCentreX(), cy);

                g.setColour (colours::accent.withAlpha (0.04f));
                g.fillRect (juce::Rectangle<float> (r.getCentreX() - 0.5f, r.getY() + 14.0f, 1.0f, r.getHeight() - 14.0f));
                label (g, "L", r.withWidth (20.0f), juce::Justification::centredLeft);
                label (g, "R", r.withLeft (r.getRight() - 20.0f), juce::Justification::centredRight);

                drawGlow (g, centre, 40.0f + 18.0f * level.current, 0.18f + 0.25f * level.current);
                g.setColour (juce::Colours::white);
                g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (centre));

                const auto& sh = dsp::ChoirModule::shapes();
                for (int v = 0; v < s.voices; ++v)
                {
                    const auto& shape = sh[(size_t) v];
                    const float pan = shape.pan * spread.current;
                    const float x = centre.x + pan * r.getWidth() * 0.44f;
                    // Later voices stand further back (higher on the stage)
                    const float y = cy - (r.getHeight() * 0.32f) * (s.timingMs / dsp::ChoirModule::maxTimingMs) * shape.timing
                                    + std::sin ((float) time * (0.7f + 0.13f * (float) v) + (float) v) * 3.0f * s.humanize;
                    const auto p = juce::Point<float> (x, y);
                    const auto c = aurora ((pan + 1.0f) * 0.5f);
                    const float size = 6.0f + 4.0f * s.density;

                    g.setColour (c.withAlpha (0.12f * alpha));
                    g.drawLine (juce::Line<float> (centre, p), 1.0f);
                    drawGlow (g, p, 20.0f + 12.0f * level.current * s.density, (0.25f + 0.4f * s.mix) * alpha, c);
                    g.setColour (c.interpolatedWith (juce::Colours::white, 0.5f).withAlpha (alpha));
                    g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (p));

                    const float ct = cents[(size_t) v];
                    g.setFont (medium (10.5f));
                    g.setColour (colours::textDim.withAlpha (alpha));
                    g.drawText ((ct >= 0.0f ? "+" : "") + juce::String (juce::roundToInt (ct)) + " ct", juce::Rectangle<float> (50.0f, 12.0f).withCentre (p.translated (0.0f, 14.0f)),
                                juce::Justification::centred, false);
                }

                // Mono compatibility of the module output
                // Only real cancellation (negative correlation) is a mono problem
                const bool risk = s.enabled && corr.current < 0.0f;
                label (g, risk ? "Mono risk - reduce Width" : "Mono compatibility", meter.withWidth (150.0f), juce::Justification::centredLeft,
                       risk ? colours::warning : colours::textDim);
                auto bar = meter.withTrimmedLeft (160.0f).withTrimmedRight (60.0f).withSizeKeepingCentre (meter.getWidth() - 220.0f, 3.0f);
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                g.fillRoundedRectangle (bar, 1.5f);
                const float x = juce::jmap (juce::jlimit (-1.0f, 1.0f, corr.current), -1.0f, 1.0f, bar.getX(), bar.getRight());
                const auto c = risk ? colours::warning : colours::teal;
                drawGlow (g, { x, bar.getCentreY() }, 10.0f, 0.45f, c);
                g.setColour (c);
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x, bar.getCentreY() }));
                label (g, juce::String (corr.current, 2), meter.withLeft (bar.getRight() + 8.0f), juce::Justification::centredLeft, colours::text);
            }

        private:
            VelvoxAudioProcessor& proc;
            const int instance;
            dsp::ChoirSettings s;
            double time = 0.0;
            Smoothed level, spread, corr { 1.0f, 1.0f };
            std::array<float, dsp::ChoirModule::maxVoices> cents {};
        };

        //======================================================================
        // SMOOTH: spectrum with the reductions that are applied right now
        class SmoothView final : public ModuleView
        {
        public:
            SmoothView (VelvoxAudioProcessor& p, const SpectrumSource* src, int k) : proc (p), instance (k), spectrum (src)
            {
                addAndMakeVisible (spectrum);
                spectrum.setRange (-90.0f, 0.0f);
                spectrum.setIntensity (0.6f);
            }

            void resized() override { spectrum.setBounds (getLocalBounds().withTrimmedTop (22)); }

            void update (const LiveData& d) override
            {
                s = d.settings.smooth;
                auto& m = proc.getChain().getSmooth (instance);
                for (int b = 0; b < dsp::SmoothModule::numBands; ++b)
                {
                    cut[(size_t) b].target = m.bandCutDb[(size_t) b].load();
                    cut[(size_t) b].tick (0.35f);
                }
                spectrum.refresh();
                repaint();
            }

            void paintOverChildren (juce::Graphics& g) override
            {
                const float alpha = s.enabled ? 1.0f : 0.4f;
                const auto plot = spectrum.getBounds().toFloat();
                auto head = getLocalBounds().toFloat().reduced (16.0f, 6.0f).withHeight (12.0f);

                // Untreated range is shaded
                const float lo = SpectrumView::xForFrequency (std::min (s.lowHz, s.highHz), plot);
                const float hi = SpectrumView::xForFrequency (std::max (s.lowHz, s.highHz), plot);
                g.setColour (colours::background.withAlpha (0.55f));
                g.fillRect (plot.withRight (lo));
                g.fillRect (plot.withLeft (hi));
                g.setColour (colours::teal.withAlpha (0.35f * alpha));
                g.fillRect (juce::Rectangle<float> (lo, plot.getY(), 1.0f, plot.getHeight()));
                g.fillRect (juce::Rectangle<float> (hi, plot.getY(), 1.0f, plot.getHeight()));

                // Reductions hang from the top, one bar per band
                float maxCut = 0.0f, maxHz = 0.0f;
                for (int b = 0; b < dsp::SmoothModule::numBands; ++b)
                {
                    const float c = cut[(size_t) b].current;
                    if (c < 0.05f)
                        continue;
                    const float f = dsp::SmoothModule::bandFrequency (b);
                    const float x = SpectrumView::xForFrequency (f, plot);
                    const float h = plot.getHeight() * 0.75f * juce::jlimit (0.0f, 1.0f, c / 18.0f);
                    const auto bar = juce::Rectangle<float> (x - 3.0f, plot.getY(), 6.0f, h);
                    const auto col = aurora (juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth()));
                    drawGlow (g, { x, bar.getBottom() }, 10.0f + h * 0.3f, 0.35f * alpha, col);
                    g.setGradientFill (juce::ColourGradient (col.withAlpha (0.15f * alpha), 0.0f, bar.getY(), col.withAlpha (0.9f * alpha), 0.0f, bar.getBottom(), false));
                    g.fillRoundedRectangle (bar, 3.0f);
                    if (c > maxCut) { maxCut = c; maxHz = f; }
                }

                label (g, "Live reductions", head);
                label (g, maxCut > 0.3f ? "Strongest  -" + juce::String (maxCut, 1) + " dB at " + hz (maxHz) : juce::String ("Nothing to tame right now"),
                       head, juce::Justification::topRight, colours::text);
            }

        private:
            VelvoxAudioProcessor& proc;
            const int instance;
            SpectrumView spectrum;
            dsp::SmoothSettings s;
            std::array<Smoothed, dsp::SmoothModule::numBands> cut {};
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
            case ModuleId::sat:     return std::make_unique<SatView>();
            case ModuleId::doubler: return std::make_unique<DoublerView>();
            case ModuleId::pitch:   return std::make_unique<PitchView>();
            case ModuleId::chop:    return createTimeModuleView (p, ModuleId::chop, instance);
            case ModuleId::delay:   return createTimeModuleView (p, ModuleId::delay, instance);
            case ModuleId::reverb:  return createReverbRoom (p, instance);
            case ModuleId::pulse:   return createTimeModuleView (p, ModuleId::pulse, instance);
            case ModuleId::smash:   return std::make_unique<SmashView> (p, instance);
            case ModuleId::choir:   return std::make_unique<ChoirView> (p, instance);
            case ModuleId::smooth:  return std::make_unique<SmoothView> (p, spectrum, instance);
            case ModuleId::check:   return std::make_unique<CheckView> (p);
            case ModuleId::ride:    return std::make_unique<RideView>();
        }
        return std::make_unique<OutputView>();
    }
}
