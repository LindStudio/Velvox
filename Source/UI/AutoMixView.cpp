#include "AutoMixView.h"
#include "../PluginProcessor.h"
#include "../Text/I18n.h"

namespace velvox::ui
{
    namespace
    {
        using i18n::tr;
        // The report texts are packed in both languages (or plain German from older projects)
        using i18n::show;
        juce::String num (float v, int decimals = 1) { return i18n::number (v, decimals); }

        constexpr int headerH = 44, gapV = 10, statusW = 300, tuneH = 92;

        // Design panel: #07080D, border #161925, radius 12
        void drawPanel (juce::Graphics& g, juce::Rectangle<int> area)
        {
            const auto r = area.toFloat();
            g.setColour (colours::panel);
            g.fillRoundedRectangle (r, 12.0f);
            g.setColour (colours::border);
            g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
        }

        int moduleIdForKey (const juce::String& key)
        {
            if (key == "output")
                return -1;
            if (auto m = dsp::moduleFromKey (key))
                return (int) *m;
            return -1;
        }

        juce::Colour confidenceColour (automix::Confidence c)
        {
            switch (c)
            {
                case automix::Confidence::measured: return colours::teal;
                case automix::Confidence::likely:   return colours::blue;
                case automix::Confidence::possible: return colours::textFaint;
            }
            return colours::textFaint;
        }

        juce::String confidenceText (automix::Confidence c)
        {
            switch (c)
            {
                case automix::Confidence::measured: return tr ("gemessen", "measured");
                case automix::Confidence::likely:   return tr ("wahrscheinlich", "likely");
                case automix::Confidence::possible: return tr ("möglich", "possible");
            }
            return {};
        }
    }

    //==========================================================================
    // The processing steps as clickable rows, followed by notes
    class AutoMixView::StepList final : public juce::Component
    {
    public:
        struct Row
        {
            juce::String chip, title, reason;
            int module = -2;          // -2 = not clickable (note)
            bool measured = false;
            bool note = false;
        };

        void setRows (std::vector<Row> r)
        {
            rows = std::move (r);
            hovered = -1;
            setSize (getWidth(), heightFor());
            repaint();
        }

        int heightFor() const
        {
            int h = 4;
            for (auto& r : rows)
                h += r.note ? noteH : rowH;
            return std::max (h, 40);
        }

        int getNumRows() const noexcept { return (int) rows.size(); }
        std::function<void (int)> onClick;

        void paint (juce::Graphics& g) override
        {
            int y = 2;
            for (int i = 0; i < (int) rows.size(); ++i)
            {
                const auto& row = rows[(size_t) i];
                const int h = row.note ? noteH : rowH;
                auto r = juce::Rectangle<int> (0, y, getWidth(), h).toFloat().reduced (2.0f, 3.0f);
                y += h;

                if (row.note)
                {
                    g.setColour (colours::textFaint);
                    g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ r.getX() + 10.0f, r.getCentreY() }));
                    g.setColour (colours::textDim);
                    g.setFont (font (13.0f));
                    g.drawFittedText (row.title, r.withTrimmedLeft (22.0f).toNearestInt(), juce::Justification::centredLeft, 2);
                    continue;
                }

                if (i == hovered && row.module > -2)
                {
                    g.setColour (colours::accent.withAlpha (0.05f));
                    g.fillRoundedRectangle (r, radius::control);
                }

                // Module chip
                const auto chip = juce::Rectangle<float> (r.getX() + 4.0f, r.getY() + 8.0f, 88.0f, 24.0f);
                r.removeFromLeft (100.0f);
                if (row.module >= 0 && row.module < dsp::numModules)
                {
                    const auto hue = moduleColour ((dsp::ModuleId) row.module);
                    g.setColour (hue.withAlpha (0.16f));
                    g.fillRoundedRectangle (chip, 12.0f);
                    g.setColour (hue.withAlpha (0.65f));
                }
                else
                {
                    g.setGradientFill (auroraGradient (chip.getTopLeft(), chip.getBottomRight(), 0.18f));
                    g.fillRoundedRectangle (chip, 12.0f);
                    g.setColour (colours::teal.withAlpha (0.5f));
                }
                g.drawRoundedRectangle (chip.reduced (0.5f), 12.0f, 1.0f);
                g.setColour (colours::text);
                g.setFont (medium (12.5f));
                g.drawFittedText (row.chip, chip.toNearestInt(), juce::Justification::centred, 1, 0.8f);

                auto top = r.removeFromTop (34.0f).withTrimmedTop (6.0f);
                g.setColour (colours::text);
                g.setFont (font (15.0f, true));
                g.drawText (row.title, top.withTrimmedRight (110.0f), juce::Justification::centredLeft, true);
                g.setColour (row.measured ? colours::teal : colours::textFaint);
                g.setFont (font (11.5f));
                g.drawText (row.measured ? tr ("Messung", "Measured") : tr ("Regel", "Rule"), top.removeFromRight (100.0f).withTrimmedRight (8.0f),
                            juce::Justification::centredRight, false);

                g.setColour (colours::textDim);
                g.setFont (font (13.0f));
                g.drawFittedText (row.reason, r.withTrimmedRight (8.0f).toNearestInt(), juce::Justification::topLeft, 2, 0.95f);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int i = rowAt (e.y);
            const bool clickable = i >= 0 && rows[(size_t) i].module > -2;
            if (i != hovered)
            {
                hovered = clickable ? i : -1;
                setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            hovered = -1;
            repaint();
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            const int i = rowAt (e.y);
            if (i >= 0 && rows[(size_t) i].module > -2 && onClick && ! e.mouseWasDraggedSinceMouseDown())
                onClick (rows[(size_t) i].module);
        }

    private:
        int rowAt (int yPos) const
        {
            int y = 2;
            for (int i = 0; i < (int) rows.size(); ++i)
            {
                const int h = rows[(size_t) i].note ? noteH : rowH;
                if (yPos >= y && yPos < y + h)
                    return i;
                y += h;
            }
            return -1;
        }

        static constexpr int rowH = 70, noteH = 30;
        std::vector<Row> rows;
        int hovered = -1;
    };

    //==========================================================================
    AutoMixView::AutoMixView (VelvoxAudioProcessor& p)
        : proc (p), autoMix (p.getAutoMix())
    {
        steps = std::make_unique<StepList>();
        steps->onClick = [this] (int id) { if (onOpenModule) onOpenModule (id); };
        stepView.setViewedComponent (steps.get(), false);
        stepView.setScrollBarsShown (true, false);
        stepView.setScrollBarThickness (6);
        addAndMakeVisible (stepView);

        analyse.getProperties().set ("primary", true);
        analyse.setTooltip (tr ("Headliner erfasst einen Abschnitt deiner Vocal während der Wiedergabe und erstellt daraus den Mix. Der bisherige Zustand bleibt wiederherstellbar.",
                                "Headliner captures a section of your vocal during playback and builds the mix from it. The previous state can be restored."));
        analyse.onClick = [this] { if (onAnalyse) onAnalyse(); };

        restore.setButtonText (tr ("Vorherigen Zustand wiederherstellen", "Restore previous state"));
        restore.setTooltip (tr ("Verwirft den Auto-Mix und stellt die Einstellungen von davor wieder her", "Discards the auto mix and brings back the settings from before"));
        restore.onClick = [this] { autoMix.restorePrevious(); };

        for (auto* b : { &analyse, &restore })
            addAndMakeVisible (b);

        density = std::make_unique<Knob> (p.getState(), pid::macroDensity, "Density", colours::blue);
        density->setDialSize (dialsize::macro);
        addAndMakeVisible (*density);
        // Off / Bypass match / Auto gain (the host sees the English choice names)
        const juce::StringArray matchLabels { tr ("Aus", "Off"), tr ("Wie Bypass", "Like bypass"), tr ("Auto-Gain", "Auto gain") };
        levelMatch = std::make_unique<Segments> (p.getState(), pid::levelMatchMode, matchLabels, colours::blue);
        addAndMakeVisible (*levelMatch);

        autoMix.addChangeListener (this);
        rebuild();
    }

    AutoMixView::~AutoMixView()
    {
        autoMix.removeChangeListener (this);
    }

    int AutoMixView::getNumStepRows() const noexcept { return steps->getNumRows(); }

    void AutoMixView::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        rebuild();
    }

    void AutoMixView::rebuild()
    {
        const auto& r = autoMix.getReport();
        std::vector<StepList::Row> rows;
        if (r.valid)
        {
            for (auto& s : r.steps)
            {
                const int id = moduleIdForKey (s.moduleKey);
                const juce::String chip = s.moduleKey == "output" ? juce::String ("Output")
                                        : (id >= 0 ? juce::String (dsp::moduleShortName ((dsp::ModuleId) id)) : s.moduleKey);
                rows.push_back ({ chip, show (s.title), show (s.reason), id, s.measured, false });
            }
            for (auto& a : r.adjustments)
                rows.push_back ({ {}, i18n::format (tr ("Nachgeprüft: {0}", "Checked: {0}"), { show (a) }), {}, -2, false, true });
            for (auto& n : r.notes)
                rows.push_back ({ {}, show (n), {}, -2, false, true });
        }
        steps->setRows (std::move (rows));
        stepView.setVisible (r.valid);
        updateButtons();
        resized();
        repaint();
    }

    void AutoMixView::updateButtons()
    {
        const bool has = autoMix.hasMix();
        analyse.setButtonText (has ? tr ("Neu analysieren", "Analyze again") : tr ("Vocal analysieren", "Analyze vocal"));

        restore.setVisible (has && autoMix.hasPrevious());
    }

    void AutoMixView::refresh()
    {
        // Live input status from the chain meters
        const auto& m = proc.getChain().meters;
        const float lufs = m.inputLufs.load();
        juce::String in;
        juce::Colour c = colours::textDim;
        if (lufs < -70.0f)          { in = tr ("Kein Signal", "No signal"); c = colours::textFaint; }
        else if (lufs < -36.0f)     { in = num (lufs) + tr (" LUFS · sehr leise", " LUFS · very quiet"); c = colours::bypass; }
        else                        { in = num (lufs) + " LUFS"; c = colours::text; }

        const auto sc = proc.getSidechainStatus();
        juce::String side = ! sc.connected ? tr ("Nicht verbunden (optional)", "Not connected (optional)")
                          : (sc.hasSignal() ? i18n::format (tr ("Verbunden · Beat {0} dB RMS", "Connected · beat {0} dB RMS"),
                                                            { juce::String (juce::roundToInt (sc.beatRmsDb)) })
                                            : tr ("Verbunden · kein Signal", "Connected · no signal"));

        if (in != inputStatus || side != sidechainStatus || c != inputColour)
        {
            inputStatus = in;
            sidechainStatus = side;
            inputColour = c;
            repaint (statusArea);
        }

    }

    void AutoMixView::paint (juce::Graphics& g)
    {
        const auto& r = autoMix.getReport();

        // ---- Header
        {
            auto h = headerArea.toFloat().withTrimmedLeft (4.0f);
            g.setColour (colours::textStrong);
            g.setFont (semiBold (15.0f, 2.5f / 15.0f));
            g.drawText ("AUTO MIX", h.removeFromLeft (124.0f), juce::Justification::centredLeft, false);

            // Status chip
            const juce::String chipText = r.valid ? (autoMix.isShowingPrevious() ? tr ("Vorher-Zustand aktiv", "Before state active")
                                                                                 : tr ("Analyse abgeschlossen", "Analysis complete"))
                                                  : tr ("Noch nicht analysiert", "Not analyzed yet");
            const auto chipFont = medium (13.0f);
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (chipFont, chipText) + 34.0f;
            auto chip = h.removeFromLeft (w).withSizeKeepingCentre (w, 28.0f);
            const auto chipColour = r.valid ? (autoMix.isShowingPrevious() ? colours::bypass : colours::ok) : colours::textFaint;
            g.setColour (chipColour.withAlpha (0.12f));
            g.fillRoundedRectangle (chip, 14.0f);
            g.setColour (chipColour.withAlpha (0.6f));
            g.drawRoundedRectangle (chip.reduced (0.5f), 14.0f, 1.0f);
            g.setColour (chipColour);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ chip.getX() + 14.0f, chip.getCentreY() }));
            g.setColour (colours::text);
            g.setFont (chipFont);
            g.drawText (chipText, chip.withTrimmedLeft (24.0f), juce::Justification::centredLeft, false);
        }

        // ---- A: vocal status
        drawPanel (g, statusArea);
        {
            auto a = statusArea.reduced (20, 16);
            g.setColour (colours::text);
            g.setFont (caps (16.0f));
            g.drawText (tr ("VOCAL-STATUS", "VOCAL STATUS"), a.removeFromTop (26), juce::Justification::centredLeft, false);
            a.removeFromTop (6);

            const auto line = [&g, &a] (const juce::String& label, const juce::String& value, juce::Colour c)
            {
                auto row = a.removeFromTop (27);
                g.setColour (colours::textDim);
                g.setFont (font (13.5f));
                g.drawText (label, row.removeFromLeft (128), juce::Justification::centredLeft, false);
                g.setColour (c);
                g.setFont (medium (13.5f));
                g.drawFittedText (value, row, juce::Justification::centredLeft, 1, 0.85f);
            };

            if (r.valid)
            {
                line (tr ("Analyse", "Analysis"), i18n::format (tr ("{0} s Gesang (Abschnitt)", "{0} s of singing (section)"), { num (r.voicedSeconds) }), colours::text);
                line ("Material", r.conservative ? tr ("Wenig - vorsichtige Werte", "Little - cautious values") : tr ("Ausreichend", "Enough"),
                      r.conservative ? colours::bypass : colours::ok);
                line (tr ("Bearbeitet", "Processed"), i18n::format (tr ("{0} Module", "{0} modules"), { juce::String (r.changedModules.size()) }), colours::text);
                line (tr ("Ausgang", "Output"), i18n::format (tr ("{0} LUFS · Spitze {1} dB", "{0} LUFS · peak {1} dB"), { num (r.outputLufs), num (r.outputPeakDb) }),
                      colours::text);
            }
            else
            {
                line (tr ("Analyse", "Analysis"), tr ("Noch keine", "None yet"), colours::textFaint);
            }
            line (tr ("Eingang (live)", "Input (live)"), inputStatus, inputColour);
            line (tr ("Beat-Sidechain", "Beat sidechain"), sidechainStatus + (r.valid && r.beatAnalysed ? tr (" · analysiert", " · analyzed") : juce::String()),
                  colours::textDim);

            a.removeFromTop (10);
            g.setColour (colours::text);
            g.setFont (font (14.0f, true));
            g.drawText (tr ("Hinweise zur Aufnahme", "Notes on the recording"), a.removeFromTop (22), juce::Justification::centredLeft, false);

            if (! r.valid)
            {
                g.setColour (colours::textDim);
                g.setFont (font (13.0f));
                g.drawFittedText (tr ("Erscheinen nach der Analyse - nur was gemessen wurde, mit Angabe der Sicherheit.",
                                      "Shown after the analysis - only what was measured, with how certain it is."),
                                  a.removeFromTop (40), juce::Justification::topLeft, 2, 1.0f);
            }
            else if (r.findings.empty())
            {
                g.setColour (colours::ok);
                g.setFont (font (13.0f));
                g.drawText (tr ("Keine Auffälligkeiten gemessen.", "Nothing unusual measured."), a.removeFromTop (22), juce::Justification::centredLeft, false);
            }
            else
            {
                for (size_t i = 0; i < std::min<size_t> (4, r.findings.size()) && a.getHeight() >= 24; ++i)
                {
                    const auto& f = r.findings[i];
                    auto row = a.removeFromTop (24);
                    const auto cc = confidenceColour (f.confidence);
                    g.setColour (cc);
                    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ (float) row.getX() + 4.0f, (float) row.getCentreY() }));
                    g.setFont (font (11.5f));
                    g.drawText (confidenceText (f.confidence), row.removeFromRight (100), juce::Justification::centredRight, false);
                    g.setColour (colours::text.withAlpha (0.9f));
                    g.setFont (font (13.0f));
                    g.drawFittedText (show (f.title), row.withTrimmedLeft (16), juce::Justification::centredLeft, 1, 0.85f);
                }
            }
        }

        // ---- B: automatic mix
        drawPanel (g, stepsArea);
        {
            auto a = stepsArea.reduced (20, 16);
            auto head = a.removeFromTop (26);
            g.setColour (colours::text);
            g.setFont (caps (16.0f));
            g.drawText (tr ("AUTOMATISCH ERSTELLTER MIX", "AUTOMATICALLY CREATED MIX"), head, juce::Justification::centredLeft, false);
            if (! r.valid)
            {
                a.removeFromTop (30);
                g.setColour (colours::textDim);
                g.setFont (font (15.0f, true));
                g.drawText (tr ("Noch kein Auto-Mix.", "No auto mix yet."), a.removeFromTop (30), juce::Justification::centred, false);
            }
        }

        // ---- D: fine-tuning (density + fair compare; the other macros are VOCAL CONTROL)
        drawPanel (g, macroArea);
        {
            auto a = macroArea.reduced (20, 14);
            g.setColour (colours::textStrong);
            g.setFont (semiBold (13.0f, 0.18f));
            g.drawText (tr ("FEINSCHLIFF", "FINE-TUNING"), a.removeFromTop (18), juce::Justification::centredLeft, false);
            g.setColour (colours::textFaint);
            g.setFont (semiBold (10.0f, 0.15f));
            g.drawText (tr ("FAIRER VERGLEICH", "FAIR COMPARISON"), levelMatch->getBounds().translated (0, -20).withHeight (16), juce::Justification::centredLeft, false);
        }
    }

    void AutoMixView::resized()
    {
        auto r = getLocalBounds();
        headerArea = r.removeFromTop (headerH);

        // Header buttons (right to left)
        auto hb = headerArea.withSizeKeepingCentre (headerArea.getWidth(), 32);
        analyse.setBounds (hb.removeFromRight (176));

        r.removeFromTop (8);
        macroArea = r.removeFromBottom (tuneH);
        r.removeFromBottom (gapV);
        statusArea = r.removeFromLeft (statusW);
        r.removeFromLeft (gapV);
        stepsArea = r;

        auto list = stepsArea.reduced (16, 14).withTrimmedTop (34);
        if (restore.isVisible())
        {
            restore.setBounds (list.removeFromBottom (32).removeFromRight (290));
            list.removeFromBottom (8);
        }
        stepView.setBounds (list);
        steps->setSize (list.getWidth() - 8, steps->heightFor());

        auto m = macroArea.reduced (20, 10);
        density->setBounds (m.removeFromRight (96).withSizeKeepingCentre (96, 78));
        m.removeFromRight (24);
        levelMatch->setBounds (m.removeFromRight (360).withSizeKeepingCentre (360, 28).translated (0, 10));
    }
}
