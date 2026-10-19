#include "ModuleViews.h"
#include "../PluginProcessor.h"
#include "../DSP/SaturationModule.h"
#include "../DSP/ChopModule.h"

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
        // EQ: live spectrum, response curve and draggable band nodes
        class EqView final : public ModuleView
        {
        public:
            EqView (VelvoxAudioProcessor& p, const SpectrumSource* s) : proc (p), spectrum (s)
            {
                addAndMakeVisible (spectrum);
                addAndMakeVisible (curve);
                spectrum.setRange (-90.0f, 0.0f);
                spectrum.setIntensity (0.75f);
                auto* procPtr = &p;
                curve.provider = [procPtr] (dsp::EqSettings& es, bool& on, float& mix)
                {
                    const auto e = procPtr->readEffectiveSettings();
                    es = e.eq;
                    on = e.eqOn;
                    mix = e.eqMix;
                };
            }

            void resized() override
            {
                spectrum.setBounds (getLocalBounds());
                curve.setBounds (getLocalBounds());
            }

            void update (const LiveData&) override
            {
                spectrum.refresh();
                curve.refresh();
                bool moved = false;
                for (int i = 0; i < (int) nodes.size(); ++i)
                {
                    nodes[(size_t) i].glow.target = (i == hovered || i == dragged) ? 1.0f : 0.0f;
                    moved = nodes[(size_t) i].glow.tick (0.2f) || moved;
                }
                if (moved || dragged >= 0)
                    repaint();
            }

            void paintOverChildren (juce::Graphics& g) override
            {
                label (g, "Drag the points  -  double-click resets", getLocalBounds().toFloat().reduced (14.0f, 10.0f),
                       juce::Justification::topRight, colours::textFaint);

                for (int i = 0; i < (int) nodes.size(); ++i)
                {
                    const auto& n = nodes[(size_t) i];
                    const auto p = position (n);
                    const bool on = n.onId == nullptr || raw (proc, n.onId) > 0.5f;
                    const float gl = n.glow.current;
                    const auto c = aurora (n.t);

                    drawGlow (g, p, 14.0f + 12.0f * gl, (on ? 0.25f : 0.08f) + 0.35f * gl, c);
                    g.setColour (colours::background);
                    g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (p));
                    g.setColour (on ? c : c.withAlpha (0.4f));
                    g.drawEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (p), 1.6f);
                    g.setColour (on ? c.interpolatedWith (juce::Colours::white, 0.5f) : colours::textFaint);
                    g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (p));

                    if (gl > 0.02f)
                    {
                        juce::String text = juce::String (n.name).toUpperCase() + "   " + hz (raw (proc, n.freqId));
                        if (n.gainId != nullptr)
                        {
                            const float gain = raw (proc, n.gainId);
                            text << "   " << (gain > 0.0f ? "+" : "") << juce::String (gain, 1) << " dB";
                        }
                        g.setFont (medium (11.0f));
                        const float w = (float) juce::GlyphArrangement::getStringWidthInt (medium (11.0f), text) + 22.0f;
                        auto box = juce::Rectangle<float> (w, 24.0f).withCentre (p.translated (0.0f, -26.0f))
                                       .constrainedWithin (getLocalBounds().toFloat().reduced (4.0f));
                        g.setColour (colours::raised.withAlpha (0.92f * gl));
                        g.fillRoundedRectangle (box, 12.0f);
                        g.setColour (c.withAlpha (0.5f * gl));
                        g.drawRoundedRectangle (box.reduced (0.5f), 12.0f, 1.0f);
                        g.setColour (colours::text.withAlpha (gl));
                        g.drawText (text, box, juce::Justification::centred, false);
                    }
                }
            }

            void mouseMove (const juce::MouseEvent& e) override
            {
                const int i = nodeAt (e.position);
                if (i != hovered)
                {
                    hovered = i;
                    setMouseCursor (i >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
                }
            }

            void mouseExit (const juce::MouseEvent&) override { hovered = -1; }

            void mouseDown (const juce::MouseEvent& e) override
            {
                dragged = nodeAt (e.position);
                if (dragged < 0)
                    return;
                for (auto* id : { nodes[(size_t) dragged].freqId, nodes[(size_t) dragged].gainId })
                    if (id != nullptr)
                        if (auto* prm = proc.getState().getParameter (id))
                            prm->beginChangeGesture();
            }

            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (dragged < 0)
                    return;
                const auto& n = nodes[(size_t) dragged];
                const auto plot = EqCurve::plotArea (curve.getBounds().toFloat());
                const float t = juce::jlimit (0.0f, 1.0f, (e.position.x - plot.getX()) / plot.getWidth());
                setParam (proc, n.freqId, SpectrumSource::minHz * std::pow (SpectrumSource::maxHz / SpectrumSource::minHz, t));
                if (n.gainId != nullptr)
                    setParam (proc, n.gainId, juce::jmap (e.position.y, plot.getBottom(), plot.getY(), -EqCurve::rangeDb, EqCurve::rangeDb));
            }

            void mouseUp (const juce::MouseEvent&) override
            {
                if (dragged < 0)
                    return;
                for (auto* id : { nodes[(size_t) dragged].freqId, nodes[(size_t) dragged].gainId })
                    if (id != nullptr)
                        if (auto* prm = proc.getState().getParameter (id))
                            prm->endChangeGesture();
                dragged = -1;
            }

            void mouseDoubleClick (const juce::MouseEvent& e) override
            {
                const int i = nodeAt (e.position);
                if (i < 0)
                    return;
                const auto& n = nodes[(size_t) i];
                // Gain bands reset to 0 dB; the cut filters switch on / off
                const char* id = n.gainId != nullptr ? n.gainId : n.onId;
                if (auto* prm = proc.getState().getParameter (id))
                {
                    prm->beginChangeGesture();
                    prm->setValueNotifyingHost (n.gainId != nullptr ? prm->getDefaultValue() : (prm->getValue() > 0.5f ? 0.0f : 1.0f));
                    prm->endChangeGesture();
                }
            }

        private:
            struct Node
            {
                const char* freqId;
                const char* gainId;
                const char* onId;
                const char* name;
                float t;
                Smoothed glow {};
            };

            juce::Point<float> position (const Node& n) const
            {
                const auto plot = EqCurve::plotArea (curve.getBounds().toFloat());
                const float x = SpectrumView::xForFrequency (raw (proc, n.freqId), plot);
                const float y = yFor (n.gainId != nullptr ? raw (proc, n.gainId) : 0.0f, -EqCurve::rangeDb, EqCurve::rangeDb, plot);
                return { x, y };
            }

            int nodeAt (juce::Point<float> p) const
            {
                int best = -1;
                float bestD = 14.0f;
                for (int i = 0; i < (int) nodes.size(); ++i)
                {
                    const float d = position (nodes[(size_t) i]).getDistanceFrom (p);
                    if (d < bestD)
                    {
                        bestD = d;
                        best = i;
                    }
                }
                return best;
            }

            VelvoxAudioProcessor& proc;
            SpectrumView spectrum;
            EqCurve curve;
            std::array<Node, 5> nodes { { { pid::hpfFreq, nullptr, pid::hpfOn, "Low cut", 0.0f },
                                          { pid::lowFreq, pid::lowGain, nullptr, "Low", 0.25f },
                                          { pid::midFreq, pid::midGain, nullptr, "Mid", 0.5f },
                                          { pid::highFreq, pid::highGain, nullptr, "High", 0.75f },
                                          { pid::lpfFreq, nullptr, pid::lpfOn, "High cut", 1.0f } } };
            int hovered = -1, dragged = -1;
        };

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
        // Chop: 16-step sequencer with a glowing playhead
        class ChopView final : public ModuleView
        {
        public:
            void update (const LiveData& d) override
            {
                const auto& pats = dsp::ChopModule::patterns();
                const auto& pat = pats[(size_t) juce::jlimit (0, (int) pats.size() - 1, d.settings.chop.pattern)];
                steps = pat.steps;
                name = pat.name;
                on = d.settings.chop.enabled;
                current = d.chopStep;
                for (int i = 0; i < 16; ++i)
                    flash[(size_t) i] = (on && i == current) ? 1.0f : flash[(size_t) i] * 0.9f;
                repaint();
            }

            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat().reduced (20.0f, 14.0f);
                label (g, "Pattern", r.withHeight (12.0f));
                label (g, name, r.withHeight (12.0f), juce::Justification::topRight, colours::text);
                auto cells = r.withSizeKeepingCentre (r.getWidth(), std::min (r.getHeight() - 50.0f, 96.0f)).translated (0.0f, 4.0f);

                const int n = juce::jmax (1, steps.length());
                const float gap = 6.0f;
                const float w = (cells.getWidth() - gap * (float) (n - 1)) / (float) n;
                const float alpha = on ? 1.0f : 0.4f;

                for (int i = 0; i < n; ++i)
                {
                    const auto cell = juce::Rectangle<float> (cells.getX() + (float) i * (w + gap), cells.getY(), w, cells.getHeight());
                    const auto type = steps[i];
                    const float f = flash[(size_t) juce::jlimit (0, 15, i)];
                    const auto c = aurora ((float) i / (float) (n - 1));

                    if (f > 0.02f)
                        drawGlow (g, cell.getCentre(), cell.getHeight() * 0.9f, 0.35f * f, c);

                    if (type == 'x')
                    {
                        g.setGradientFill (juce::ColourGradient (c.withAlpha ((0.55f + 0.45f * f) * alpha), 0.0f, cell.getY(),
                                                                 c.withAlpha ((0.15f + 0.3f * f) * alpha), 0.0f, cell.getBottom(), false));
                        g.fillRoundedRectangle (cell, 5.0f);
                        g.setColour (juce::Colours::white.withAlpha ((0.12f + 0.4f * f) * alpha));
                        g.fillRoundedRectangle (cell.withHeight (2.0f).reduced (3.0f, 0.0f), 1.0f);
                    }
                    else if (type == 'r')
                    {
                        g.setColour (c.withAlpha ((0.08f + 0.2f * f) * alpha));
                        g.fillRoundedRectangle (cell, 5.0f);
                        g.setColour (c.withAlpha ((0.6f + 0.4f * f) * alpha));
                        g.drawRoundedRectangle (cell.reduced (0.5f), 5.0f, 1.2f);
                        g.setFont (caps (11.5f));
                        g.drawText ("R", cell, juce::Justification::centred, false);
                    }
                    else
                    {
                        g.setColour (juce::Colours::black.withAlpha (0.35f));
                        g.fillRoundedRectangle (cell, 5.0f);
                        g.setColour (colours::accent.withAlpha (0.05f + 0.15f * f));
                        g.drawRoundedRectangle (cell.reduced (0.5f), 5.0f, 1.0f);
                    }

                    if (i % 4 == 0)
                    {
                        g.setColour (colours::textFaint);
                        g.setFont (medium (10.5f));
                        g.drawText (juce::String (i / 4 + 1), juce::Rectangle<float> (cell.getX(), cell.getBottom() + 8.0f, w, 12.0f),
                                    juce::Justification::centredLeft, false);
                    }
                }

                if (on && current >= 0 && current < n)
                {
                    const float x = cells.getX() + (float) current * (w + gap) + w * 0.5f;
                    g.setColour (juce::Colours::white.withAlpha (0.7f));
                    g.fillRoundedRectangle (x - w * 0.5f, cells.getY() - 9.0f, w, 3.0f, 1.5f);
                }
            }

        private:
            juce::String steps, name;
            bool on = false;
            int current = -1;
            std::array<float, 16> flash {};
        };

        //======================================================================
        // Delay: the echoes on a time line (ping-pong alternates left / right),
        // lit as a scan passes and pushed down live by ducking
        class DelayView final : public ModuleView
        {
        public:
            void update (const LiveData& d) override
            {
                const auto& cs = d.settings;
                s = cs.delay;
                on = cs.delayOn;
                mix = cs.delayMix;
                ms = cs.delaySync ? d.effectiveDelayMs : cs.delayTimeMs;
                duck.target = juce::Decibels::decibelsToGain (-d.delayDuck.ago (0));
                duck.tick (0.25f);
                time = d.time;
                level = d.level();
                repaint();
            }

            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat().reduced (20.0f, 14.0f);
                const float alpha = on ? 1.0f : 0.4f;
                label (g, "Echoes", r.withHeight (12.0f));
                label (g, juce::String (juce::roundToInt (ms)) + " ms" + (s.pingPong ? "   ping-pong" : ""), r.withHeight (12.0f),
                       juce::Justification::topRight, colours::text);
                auto plot = r.withTrimmedTop (18.0f).withTrimmedBottom (16.0f);
                const float cy = plot.getCentreY();
                const float windowMs = juce::jlimit (800.0f, 8000.0f, ms * 7.0f);
                const auto xFor = [&] (float t) { return plot.getX() + plot.getWidth() * t / windowMs; };

                g.setColour (colours::accent.withAlpha (0.06f));
                g.fillRect (juce::Rectangle<float> (plot.getX(), cy - 0.5f, plot.getWidth(), 1.0f));

                // Time axis
                const float stepMs = windowMs > 4000.0f ? 1000.0f : (windowMs > 1600.0f ? 500.0f : 250.0f);
                g.setFont (medium (10.5f));
                for (float t = stepMs; t < windowMs; t += stepMs)
                {
                    const float x = xFor (t);
                    g.setColour (colours::accent.withAlpha (0.03f));
                    g.fillRect (juce::Rectangle<float> (x, plot.getY(), 1.0f, plot.getHeight()));
                    g.setColour (colours::textFaint);
                    g.drawText (juce::String ((int) t) + " ms", juce::Rectangle<float> (x - 30.0f, plot.getBottom() + 3.0f, 60.0f, 11.0f),
                                juce::Justification::centred, false);
                }

                // Scan line loops through the window, with a soft trail
                const float scan = (float) std::fmod (time * 1000.0, (double) windowMs);
                const float sx = xFor (scan);
                g.setGradientFill (juce::ColourGradient (colours::teal.withAlpha (0.0f), sx - 140.0f, 0.0f,
                                                         colours::teal.withAlpha (0.035f * alpha), sx, 0.0f, false));
                g.fillRect (juce::Rectangle<float> (sx - 140.0f, plot.getY(), 140.0f, plot.getHeight()).getIntersection (plot));
                g.setColour (colours::teal.withAlpha (0.25f * alpha));
                g.fillRect (juce::Rectangle<float> (sx, plot.getY(), 1.0f, plot.getHeight()));

                const float half = plot.getHeight() * 0.46f;

                // Dry voice
                drawGlow (g, { xFor (0.0f), cy }, 20.0f, 0.3f);
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, half * 2.0f).withCentre ({ xFor (0.0f) + 2.0f, cy }), 1.5f);

                // Echo taps
                // Height = level of each repeat (feedback, live ducking); brightness = mix
                float amp = 0.9f * duck.current;
                const float strength = 0.45f + 0.55f * juce::jlimit (0.0f, 1.0f, mix * 2.5f);
                for (int k = 1; k < 64 && amp > 0.02f; ++k)
                {
                    const float t = ms * (float) k;
                    if (t > windowMs)
                        break;
                    const float x = xFor (t);
                    const float near = std::exp (-std::pow ((scan - t) / 70.0f, 2.0f));
                    const auto c = aurora (t / windowMs);
                    const float h = half * amp;
                    const bool up = ! s.pingPong || (k % 2 == 1);
                    const bool down = ! s.pingPong || (k % 2 == 0);
                    const auto bar = juce::Rectangle<float> (x - 2.0f, up ? cy - h : cy, 4.0f, (up && down) ? h * 2.0f : h);

                    drawGlow (g, bar.getCentre(), 14.0f + h * 0.5f, (0.18f + 0.45f * near) * strength * alpha, c);
                    g.setGradientFill (juce::ColourGradient (c.interpolatedWith (juce::Colours::white, 0.2f + 0.5f * near).withAlpha (strength * alpha),
                                                             0.0f, up ? bar.getY() : bar.getBottom(), c.withAlpha (0.35f * strength * alpha), 0.0f, cy, false));
                    g.fillRoundedRectangle (bar, 2.0f);
                    amp *= s.feedback;
                }

                if (s.pingPong)
                {
                    label (g, "L", plot.withWidth (14.0f).withHeight (12.0f).translated (-12.0f, 4.0f));
                    label (g, "R", plot.withWidth (14.0f).withY (plot.getBottom() - 12.0f).translated (-12.0f, 0.0f));
                }
                label (g, hz (s.lowCutHz) + "  -  " + hz (s.highCutHz), plot.withY (plot.getY() + 2.0f).withHeight (12.0f),
                       juce::Justification::topRight, colours::textFaint);
            }

        private:
            dsp::DelaySettings s;
            bool on = false;
            float mix = 0.2f, ms = 375.0f, level = 0.0f;
            double time = 0.0;
            Smoothed duck { 1.0f, 1.0f };
        };

        //======================================================================
        // Reverb: the tail - pre-delay gap, early reflections, diffuse decay
        class ReverbView final : public ModuleView
        {
        public:
            void update (const LiveData& d) override
            {
                s = d.settings.reverb;
                on = d.settings.reverbOn;
                mix = d.settings.reverbMix;
                duck.target = juce::Decibels::decibelsToGain (-d.reverbDuck.ago (0));
                duck.tick (0.2f);
                time = d.time;
                repaint();
            }

            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat().reduced (20.0f, 14.0f);
                const float alpha = on ? 1.0f : 0.4f;
                label (g, s.type == 0 ? "Room" : "Plate", r.withHeight (12.0f));
                label (g, "RT60 " + juce::String (s.decaySeconds, 2) + " s   pre " + juce::String (juce::roundToInt (s.preDelayMs)) + " ms",
                       r.withHeight (12.0f), juce::Justification::topRight, colours::text);
                auto plot = r.withTrimmedTop (18.0f).withTrimmedBottom (16.0f);

                const float pre = s.preDelayMs / 1000.0f;
                const float total = juce::jmax (0.6f, pre + s.decaySeconds * 1.05f);
                const auto xFor = [&] (float t) { return plot.getX() + plot.getWidth() * t / total; };
                const float cy = plot.getY() + plot.getHeight() * 0.62f;
                const float up = cy - plot.getY(), downScale = (plot.getBottom() - cy) * s.width;
                const auto env = [&] (float t) { return t < pre ? 0.0f : std::pow (10.0f, -3.0f * (t - pre) / std::max (0.1f, s.decaySeconds)); };
                const float gain = duck.current * (0.5f + 0.5f * juce::jlimit (0.0f, 1.0f, mix * 3.0f));

                // Time axis
                g.setFont (medium (10.5f));
                const float step = total > 4.0f ? 1.0f : 0.5f;
                for (float t = step; t < total; t += step)
                {
                    g.setColour (colours::accent.withAlpha (0.03f));
                    g.fillRect (juce::Rectangle<float> (xFor (t), plot.getY(), 1.0f, plot.getHeight()));
                    g.setColour (colours::textFaint);
                    g.drawText (juce::String (t, 1) + " s", juce::Rectangle<float> (xFor (t) - 20.0f, plot.getBottom() + 3.0f, 40.0f, 11.0f),
                                juce::Justification::centred, false);
                }

                // Envelope (top = left channel feel, bottom scaled by width)
                juce::Path top, shape;
                for (int i = 0; i <= 200; ++i)
                {
                    const float t = total * (float) i / 200.0f;
                    const float y = cy - env (t) * up * gain;
                    if (i == 0) { top.startNewSubPath (xFor (t), y); shape.startNewSubPath (xFor (t), y); }
                    else { top.lineTo (xFor (t), y); shape.lineTo (xFor (t), y); }
                }
                for (int i = 200; i >= 0; --i)
                {
                    const float t = total * (float) i / 200.0f;
                    shape.lineTo (xFor (t), cy + env (t) * downScale * gain);
                }
                shape.closeSubPath();
                g.setGradientFill (auroraGradient (plot.getTopLeft(), plot.getTopRight(), 0.16f * alpha));
                g.fillPath (shape);

                // Diffuse texture: denser with size, darker with damping
                juce::Random rnd (11);
                const int lines = 120 + (int) (s.size * 260.0f);
                for (int i = 0; i < lines; ++i)
                {
                    const float t = pre + rnd.nextFloat() * (total - pre);
                    const float e = env (t) * gain * (0.25f + 0.75f * rnd.nextFloat());
                    const float shimmer = 0.6f + 0.4f * std::sin ((float) time * 3.0f + (float) i);
                    g.setColour (aurora (t / total, 0.28f * alpha * shimmer));
                    g.fillRect (juce::Rectangle<float> (xFor (t), cy - e * up, 1.0f, e * (up + downScale)));
                }

                // Early reflections
                const int er = s.type == 0 ? 6 : 12;
                for (int i = 0; i < er; ++i)
                {
                    const float t = pre + (0.004f + 0.07f * (float) i / (float) er) * (0.5f + s.size);
                    const float h = (0.95f - 0.06f * (float) i) * up * gain;
                    g.setColour (colours::teal.interpolatedWith (juce::Colours::white, 0.4f).withAlpha (0.8f * alpha));
                    g.fillRect (juce::Rectangle<float> (xFor (t) - 0.75f, cy - h, 1.5f, h));
                }

                glowStroke (g, top, plot, 1.6f, alpha);

                // Damping darkens the far end of the tail
                const float damp = 1.0f - juce::jlimit (0.0f, 1.0f, (s.dampingHz - 1000.0f) / 19000.0f);
                g.setGradientFill (juce::ColourGradient (colours::background.withAlpha (0.0f), plot.getCentreX(), 0.0f,
                                                         colours::background.withAlpha (0.65f * damp), plot.getRight(), 0.0f, false));
                g.fillRect (plot.withLeft (plot.getCentreX()));

                // Pre-delay bracket
                if (pre > 0.002f)
                {
                    g.setColour (colours::textDim);
                    g.drawLine (xFor (0.0f), cy + 6.0f, xFor (pre), cy + 6.0f, 1.0f);
                }
                drawGlow (g, { xFor (0.0f), cy }, 16.0f, 0.3f);
                g.setColour (juce::Colours::white);
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ xFor (0.0f), cy }));
            }

        private:
            dsp::ReverbSettings s;
            bool on = false;
            float mix = 0.2f;
            double time = 0.0;
            Smoothed duck { 1.0f, 1.0f };
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
        // PULSE: the volume curve as an editor. Drag the six points; the
        // playhead and the gain meter show what is applied right now.
        class PulseView final : public ModuleView
        {
        public:
            explicit PulseView (VelvoxAudioProcessor& p) : proc (p) {}

            void update (const LiveData& d) override
            {
                s = d.settings.pulse;
                const auto& m = proc.getChain().getPulse();
                phase = m.displayPhase.load();
                gain.target = m.displayGain.load();
                gain.tick (0.4f);
                bpm = m.displayBpm.load();
                hostTempo = m.displayHostTempo.load();
                for (int i = 0; i < 6; ++i)
                {
                    glow[(size_t) i].target = (i == hovered || i == dragged) ? 1.0f : 0.0f;
                    glow[(size_t) i].tick (0.2f);
                }
                repaint();
            }

            void paint (juce::Graphics& g) override
            {
                const float alpha = s.enabled ? 1.0f : 0.45f;
                auto r = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
                label (g, "Volume curve  -  drag the points   (fill = applied gain at this depth)", r.withHeight (12.0f));
                label (g, dsp::PulseModule::rateNames()[s.rate] + " cycle   " + juce::String (bpm, 1) + " BPM" + (hostTempo ? " (host)" : " (manual)"),
                       r.withHeight (12.0f), juce::Justification::topRight, colours::text);

                auto meter = r.withTrimmedTop (20.0f).removeFromRight (16.0f);
                r.removeFromRight (30.0f);
                const auto plot = curveArea();

                // Grid: beats / sixteenths of the cycle and quarter levels
                for (int i = 1; i < 16; ++i)
                {
                    g.setColour (colours::accent.withAlpha (i % 4 == 0 ? 0.06f : 0.025f));
                    g.fillRect (juce::Rectangle<float> (plot.getX() + plot.getWidth() * (float) i / 16.0f, plot.getY(), 1.0f, plot.getHeight()));
                }
                for (int i = 1; i < 4; ++i)
                {
                    g.setColour (colours::accent.withAlpha (0.03f));
                    g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getY() + plot.getHeight() * (float) i / 4.0f, plot.getWidth(), 1.0f));
                }

                // Curve as drawn (0..1) and the gain that is applied (depth)
                juce::Path curve, applied;
                const int n = 240;
                for (int i = 0; i <= n; ++i)
                {
                    const float ph = (float) i / (float) n;
                    const float c = dsp::PulseModule::evaluate (s, ph);
                    const float a = 1.0f - s.depth * (1.0f - c);
                    const float x = plot.getX() + plot.getWidth() * ph;
                    if (i == 0) { curve.startNewSubPath (x, yFor (c, 0.0f, 1.0f, plot)); applied.startNewSubPath (x, yFor (a, 0.0f, 1.0f, plot)); }
                    else        { curve.lineTo (x, yFor (c, 0.0f, 1.0f, plot)); applied.lineTo (x, yFor (a, 0.0f, 1.0f, plot)); }
                }
                g.setGradientFill (auroraGradient (plot.getTopLeft(), plot.getTopRight(), 0.13f * alpha));
                g.fillPath (closedTo (applied, plot, plot.getBottom()));
                g.setColour (colours::accent.withAlpha (0.35f * alpha));
                const float dash[] { 3.0f, 3.0f };
                juce::Path dashed;
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, applied, dash, 2);   // applied gain (depth): fill + fine line
                g.fillPath (dashed);
                glowStroke (g, curve, plot, 2.0f, alpha);         // the editable curve with its points

                // Playhead
                if (phase >= 0.0f)
                {
                    const float x = plot.getX() + plot.getWidth() * phase;
                    const float y = yFor (1.0f - s.depth * (1.0f - dsp::PulseModule::evaluate (s, phase)), 0.0f, 1.0f, plot);
                    g.setGradientFill (juce::ColourGradient (colours::teal.withAlpha (0.0f), x - 80.0f, 0.0f, colours::teal.withAlpha (0.08f), x, 0.0f, false));
                    g.fillRect (juce::Rectangle<float> (x - 80.0f, plot.getY(), 80.0f, plot.getHeight()).getIntersection (plot));
                    g.setColour (colours::teal.withAlpha (0.5f));
                    g.fillRect (juce::Rectangle<float> (x, plot.getY(), 1.0f, plot.getHeight()));
                    drawGlow (g, { x, y }, 16.0f, 0.7f, colours::teal);
                    g.setColour (juce::Colours::white);
                    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ x, y }));
                }

                // Points
                for (int i = 0; i < 6; ++i)
                {
                    const auto pt = pointPosition (i);
                    const float gl = glow[(size_t) i].current;
                    const auto c = aurora (s.x[(size_t) i]);
                    drawGlow (g, pt, 12.0f + 10.0f * gl, (0.25f + 0.4f * gl) * alpha, c);
                    g.setColour (colours::background);
                    g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (pt));
                    g.setColour (c.withAlpha (alpha));
                    g.drawEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (pt), 1.6f);
                    if (gl > 0.02f)
                    {
                        g.setColour (colours::text.withAlpha (gl));
                        g.setFont (medium (10.5f));
                        g.drawText (juce::String (juce::roundToInt (s.y[(size_t) i] * 100.0f)) + " %",
                                    juce::Rectangle<float> (60.0f, 14.0f).withCentre (pt.translated (0.0f, -16.0f)), juce::Justification::centred, false);
                    }
                }

                // Gain applied right now
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                g.fillRoundedRectangle (meter.withSizeKeepingCentre (6.0f, meter.getHeight()), 3.0f);
                const auto fill = meter.withSizeKeepingCentre (6.0f, meter.getHeight()).withTop (yFor (gain.current, 0.0f, 1.0f, meter));
                g.setGradientFill (auroraGradient (fill.getBottomLeft(), fill.getTopLeft(), alpha));
                g.fillRoundedRectangle (fill, 3.0f);
                label (g, "Gain", meter.withY (meter.getBottom() + 2.0f).withHeight (12.0f).expanded (14.0f, 0.0f), juce::Justification::centred);
            }

            void mouseMove (const juce::MouseEvent& e) override
            {
                hovered = pointAt (e.position);
                setMouseCursor (hovered >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
            }
            void mouseExit (const juce::MouseEvent&) override { hovered = -1; }

            void mouseDown (const juce::MouseEvent& e) override
            {
                dragged = pointAt (e.position);
                if (dragged >= 0)
                    for (auto* id : { pid::pulseX[dragged], pid::pulseY[dragged] })
                        if (auto* prm = proc.getState().getParameter (id))
                            prm->beginChangeGesture();
            }

            void mouseDrag (const juce::MouseEvent& e) override
            {
                if (dragged < 0)
                    return;
                const auto plot = curveArea();
                setParam (proc, pid::pulseX[dragged], juce::jlimit (0.0f, 1.0f, (e.position.x - plot.getX()) / plot.getWidth()));
                setParam (proc, pid::pulseY[dragged], juce::jlimit (0.0f, 1.0f, (plot.getBottom() - e.position.y) / plot.getHeight()));
            }

            void mouseUp (const juce::MouseEvent&) override
            {
                if (dragged >= 0)
                    for (auto* id : { pid::pulseX[dragged], pid::pulseY[dragged] })
                        if (auto* prm = proc.getState().getParameter (id))
                            prm->endChangeGesture();
                dragged = -1;
            }

        private:
            juce::Rectangle<float> curveArea() const
            {
                auto r = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
                r.removeFromRight (46.0f);
                return r.withTrimmedTop (24.0f).withTrimmedBottom (14.0f).reduced (6.0f, 6.0f);
            }

            juce::Point<float> pointPosition (int i) const
            {
                const auto plot = curveArea();
                const float x = raw (proc, pid::pulseX[i]), y = raw (proc, pid::pulseY[i]);
                return { plot.getX() + plot.getWidth() * x, yFor (y, 0.0f, 1.0f, plot) };
            }

            int pointAt (juce::Point<float> p) const
            {
                int best = -1;
                float bestD = 14.0f;
                for (int i = 0; i < 6; ++i)
                {
                    const float d = pointPosition (i).getDistanceFrom (p);
                    if (d < bestD) { bestD = d; best = i; }
                }
                return best;
            }

            VelvoxAudioProcessor& proc;
            dsp::PulseSettings s;
            float phase = -1.0f, bpm = 150.0f;
            bool hostTempo = false;
            Smoothed gain { 1.0f, 1.0f };
            std::array<Smoothed, 6> glow {};
            int hovered = -1, dragged = -1;
        };

        //======================================================================
        // VOCAL SMASH: reduction over time, and the curve of the squashed path
        // (input level -> level of the added path) against the original
        class SmashView final : public ModuleView
        {
        public:
            explicit SmashView (VelvoxAudioProcessor& p) : proc (p) {}

            void update (const LiveData& d) override
            {
                live = &d;
                s = d.settings.smash;
                level.target = d.inputDb.ago (0);
                level.tick (0.35f);
                added.target = proc.getChain().getSmash().blendLevelDb.load();
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
            const LiveData* live = nullptr;
            dsp::SmashSettings s;
            Smoothed level, added { -100.0f, -100.0f };
        };

        //======================================================================
        // CHOIR: every voice on the stage with its real pitch offset
        class ChoirView final : public ModuleView
        {
        public:
            explicit ChoirView (VelvoxAudioProcessor& p) : proc (p) {}

            void update (const LiveData& d) override
            {
                s = d.settings.choir;
                time = d.time;
                level.target = d.level();
                level.tick (0.15f);
                spread.target = s.width;
                spread.tick (0.12f);
                auto& m = proc.getChain().getChoir();
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
            SmoothView (VelvoxAudioProcessor& p, const SpectrumSource* src) : proc (p), spectrum (src)
            {
                addAndMakeVisible (spectrum);
                spectrum.setRange (-90.0f, 0.0f);
                spectrum.setIntensity (0.6f);
            }

            void resized() override { spectrum.setBounds (getLocalBounds().withTrimmedTop (22)); }

            void update (const LiveData& d) override
            {
                s = d.settings.smooth;
                auto& m = proc.getChain().getSmooth();
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

    std::unique_ptr<ModuleView> createModuleView (VelvoxAudioProcessor& p, int id, const SpectrumSource* spectrum)
    {
        if (id < 0)
            return std::make_unique<OutputView>();
        switch ((ModuleId) id)
        {
            case ModuleId::eq:      return std::make_unique<EqView> (p, spectrum);
            case ModuleId::comp:    return std::make_unique<CompView>();
            case ModuleId::deess:   return std::make_unique<DeEssView> (spectrum);
            case ModuleId::sat:     return std::make_unique<SatView>();
            case ModuleId::doubler: return std::make_unique<DoublerView>();
            case ModuleId::pitch:   return std::make_unique<PitchView>();
            case ModuleId::chop:    return std::make_unique<ChopView>();
            case ModuleId::delay:   return std::make_unique<DelayView>();
            case ModuleId::reverb:  return std::make_unique<ReverbView>();
            case ModuleId::pulse:   return std::make_unique<PulseView> (p);
            case ModuleId::smash:   return std::make_unique<SmashView> (p);
            case ModuleId::choir:   return std::make_unique<ChoirView> (p);
            case ModuleId::smooth:  return std::make_unique<SmoothView> (p, spectrum);
            case ModuleId::check:   return std::make_unique<CheckView> (p);
            case ModuleId::ride:    return std::make_unique<RideView>();
        }
        return std::make_unique<OutputView>();
    }
}
