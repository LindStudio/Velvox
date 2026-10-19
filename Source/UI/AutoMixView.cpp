#include "AutoMixView.h"
#include "../PluginProcessor.h"

namespace velvox::ui
{
    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }
        juce::String num (float v, int decimals = 1) { return juce::String (v, decimals).replaceCharacter ('.', ','); }

        constexpr int headerH = 56, gapV = 14, statusW = 360;

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
                case automix::Confidence::measured: return "gemessen";
                case automix::Confidence::likely:   return "wahrscheinlich";
                case automix::Confidence::possible: return de ("möglich");
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
                g.setGradientFill (auroraGradient (chip.getTopLeft(), chip.getBottomRight(), 0.18f));
                g.fillRoundedRectangle (chip, 12.0f);
                g.setColour (colours::teal.withAlpha (0.5f));
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
                g.drawText (row.measured ? "Messung" : "Regel", top.removeFromRight (100.0f).withTrimmedRight (8.0f),
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
        analyse.setTooltip (de ("Velvox erfasst einen Abschnitt deiner Vocal während der Wiedergabe und erstellt daraus den Mix. Der bisherige Zustand bleibt wiederherstellbar."));
        analyse.onClick = [this] { if (onAnalyse) onAnalyse(); };

        previous.setConnectedEdges (juce::Button::ConnectedOnRight);
        mix.setConnectedEdges (juce::Button::ConnectedOnLeft);
        previous.setTooltip (de ("Die Einstellungen vor dem Auto-Mix - echter Parameterwechsel, hörbar in der DAW"));
        mix.setTooltip (de ("Der automatische Mix (inklusive deiner Änderungen daran)"));
        previous.onClick = [this] { autoMix.showPrevious (true); updateButtons(); };
        mix.onClick = [this] { autoMix.showPrevious (false); updateButtons(); };

        original.setClickingTogglesState (false);
        original.setTooltip (de ("Unbearbeitete Vocal hören (Bypass, latenzkompensiert). Mit „Fairer Vergleich = Bypass match“ in gleicher Lautheit wie der Mix."));
        original.onClick = [this]
        {
            if (auto* b = proc.getState().getParameter (pid::bypass))
            {
                b->beginChangeGesture();
                b->setValueNotifyingHost (b->getValue() > 0.5f ? 0.0f : 1.0f);
                b->endChangeGesture();
            }
            updateButtons();
        };

        restore.setButtonText (de ("Vorherigen Zustand wiederherstellen"));
        restore.setTooltip (de ("Verwirft den Auto-Mix und stellt die Einstellungen von davor wieder her"));
        restore.onClick = [this] { autoMix.restorePrevious(); };

        for (auto* b : { &analyse, &previous, &mix, &original, &restore })
            addAndMakeVisible (b);

        struct Def { const char* id; const char* name; };
        const Def defs[6] { { pid::macroPresence, "Presence" }, { pid::macroWarmth, "Warmth" }, { pid::macroAggression, "Punch" },
                            { pid::macroWidth, "Width" }, { pid::macroSpace, "Space" }, { pid::macroDensity, "Density" } };
        for (size_t i = 0; i < macros.size(); ++i)
        {
            macros[i] = std::make_unique<Knob> (p.getState(), defs[i].id, defs[i].name);
            macros[i]->setProminence (Knob::Prominence::primary);
            macros[i]->setMaxDialSize (96);
            addAndMakeVisible (*macros[i]);
        }
        levelMatch = std::make_unique<Choice> (p.getState(), pid::levelMatchMode, "Fairer Vergleich");
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
                rows.push_back ({ chip, s.title, s.reason, id, s.measured, false });
            }
            for (auto& a : r.adjustments)
                rows.push_back ({ {}, de ("Nachgeprüft: ") + a, {}, -2, false, true });
            for (auto& n : r.notes)
                rows.push_back ({ {}, n, {}, -2, false, true });
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
        analyse.setButtonText (has ? "Neu analysieren" : "Audio analysieren");

        const bool canCompare = has && autoMix.hasPrevious();
        previous.setVisible (canCompare);
        mix.setVisible (canCompare);
        restore.setVisible (canCompare);
        previous.setToggleState (autoMix.isShowingPrevious(), juce::dontSendNotification);
        mix.setToggleState (! autoMix.isShowingPrevious(), juce::dontSendNotification);

        const bool bypassed = proc.getState().getRawParameterValue (pid::bypass)->load() > 0.5f;
        original.setToggleState (bypassed, juce::dontSendNotification);
        original.setButtonText (bypassed ? "Original (aktiv)" : "Original");
    }

    void AutoMixView::refresh()
    {
        // Live input status from the chain meters
        const auto& m = proc.getChain().meters;
        const float lufs = m.inputLufs.load();
        juce::String in;
        juce::Colour c = colours::textDim;
        if (lufs < -70.0f)          { in = "Kein Signal"; c = colours::textFaint; }
        else if (lufs < -36.0f)     { in = num (lufs) + de (" LUFS · sehr leise"); c = colours::bypass; }
        else                        { in = num (lufs) + " LUFS"; c = colours::text; }

        const auto sc = proc.getSidechainStatus();
        juce::String side = ! sc.connected ? de ("Nicht verbunden (optional)")
                          : (sc.hasSignal() ? de ("Verbunden · Beat ") + juce::String (juce::roundToInt (sc.beatRmsDb)) + " dB RMS"
                                            : de ("Verbunden · kein Signal"));

        if (in != inputStatus || side != sidechainStatus || c != inputColour)
        {
            inputStatus = in;
            sidechainStatus = side;
            inputColour = c;
            repaint (statusArea);
        }

        const bool bypassed = proc.getState().getRawParameterValue (pid::bypass)->load() > 0.5f;
        if (bypassed != original.getToggleState() || autoMix.isShowingPrevious() != previous.getToggleState())
            updateButtons();
    }

    void AutoMixView::paint (juce::Graphics& g)
    {
        const auto& r = autoMix.getReport();

        // ---- Header
        {
            auto h = headerArea.toFloat();
            g.setColour (colours::text);
            g.setFont (font (typesize::hero, true));
            g.drawText ("Auto Mix", h.removeFromLeft (150.0f), juce::Justification::centredLeft, false);

            // Status chip
            const juce::String chipText = r.valid ? (autoMix.isShowingPrevious() ? de ("Vorher-Zustand aktiv") : de ("Analyse abgeschlossen"))
                                                  : de ("Noch nicht analysiert");
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
        drawGlassPanel (g, statusArea.toFloat(), radius::panel, colours::teal, 0.06f);
        {
            auto a = statusArea.reduced (20, 16);
            g.setColour (colours::text);
            g.setFont (font (17.0f, true));
            g.drawText ("Vocal-Status", a.removeFromTop (26), juce::Justification::centredLeft, false);
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
                line ("Analyse", num (r.voicedSeconds) + de (" s Gesang (Abschnitt)"), colours::text);
                line ("Material", r.conservative ? de ("Wenig - vorsichtige Werte") : juce::String ("Ausreichend"),
                      r.conservative ? colours::bypass : colours::ok);
                line ("Bearbeitet", juce::String (r.changedModules.size()) + " Module", colours::text);
                line ("Ausgang", num (r.outputLufs) + " LUFS · Spitze " + num (r.outputPeakDb) + " dB", colours::text);
            }
            else
            {
                line ("Analyse", "Noch keine", colours::textFaint);
            }
            line ("Eingang (live)", inputStatus, inputColour);
            line ("Beat-Sidechain", sidechainStatus + (r.valid && r.beatAnalysed ? de (" · analysiert") : juce::String()),
                  colours::textDim);

            a.removeFromTop (10);
            g.setColour (colours::text);
            g.setFont (font (14.0f, true));
            g.drawText (de ("Hinweise zur Aufnahme"), a.removeFromTop (22), juce::Justification::centredLeft, false);

            if (! r.valid)
            {
                g.setColour (colours::textDim);
                g.setFont (font (13.0f));
                g.drawFittedText (de ("Erscheinen nach der Analyse - nur was gemessen wurde, mit Angabe der Sicherheit."),
                                  a.removeFromTop (40), juce::Justification::topLeft, 2);
            }
            else if (r.findings.empty())
            {
                g.setColour (colours::ok);
                g.setFont (font (13.0f));
                g.drawText (de ("Keine Auffälligkeiten gemessen."), a.removeFromTop (22), juce::Justification::centredLeft, false);
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
                    g.drawFittedText (f.title, row.withTrimmedLeft (16), juce::Justification::centredLeft, 1, 0.85f);
                }
            }
        }

        // ---- B: automatic mix
        drawGlassPanel (g, stepsArea.toFloat(), radius::panel, colours::violet, 0.06f);
        {
            auto a = stepsArea.reduced (20, 16);
            auto head = a.removeFromTop (26);
            g.setColour (colours::text);
            g.setFont (font (17.0f, true));
            g.drawText ("Automatisch erstellter Mix", head, juce::Justification::centredLeft, false);
            if (r.valid)
            {
                g.setColour (colours::textFaint);
                g.setFont (font (12.0f));
                g.drawText (de ("Klick öffnet das Modul"), head, juce::Justification::centredRight, false);
            }
            else
            {
                a.removeFromTop (30);
                g.setColour (colours::text);
                g.setFont (font (22.0f, true));
                g.drawText ("Let's mix your vocal.", a.removeFromTop (34), juce::Justification::centred, false);
                g.setColour (colours::textDim);
                g.setFont (font (14.5f));
                g.drawFittedText (de ("Starte die Wiedergabe deiner Vocal in der DAW und klicke auf „Audio analysieren“. "
                                      "Velvox analysiert einen begrenzten Abschnitt, wählt die passenden Module und stellt sie ein - "
                                      "danach bleibt alles frei veränderbar."),
                                  a.removeFromTop (70).reduced (30, 0), juce::Justification::centredTop, 3);
            }
        }

        // ---- D: fine-tuning
        drawGlassPanel (g, macroArea.toFloat(), radius::panel, colours::blue, 0.05f);
        {
            auto head = macroArea.reduced (20, 14).removeFromTop (24);
            g.setColour (colours::text);
            g.setFont (font (17.0f, true));
            g.drawText ("Feinschliff", head, juce::Justification::centredLeft, false);
            g.setColour (colours::textFaint);
            g.setFont (font (12.5f));
            g.drawText (de ("Makros wirken auf die echten Modulparameter - zusätzlich zum Auto-Mix"),
                        head.withTrimmedLeft (110), juce::Justification::centredLeft, false);
        }
    }

    void AutoMixView::resized()
    {
        auto r = getLocalBounds();
        headerArea = r.removeFromTop (headerH);

        // Header buttons (right to left)
        auto hb = headerArea.withSizeKeepingCentre (headerArea.getWidth(), 38);
        analyse.setBounds (hb.removeFromRight (190));
        hb.removeFromRight (16);
        original.setBounds (hb.removeFromRight (140));
        hb.removeFromRight (10);
        if (mix.isVisible())
        {
            mix.setBounds (hb.removeFromRight (110));
            previous.setBounds (hb.removeFromRight (90));
        }

        r.removeFromTop (8);
        macroArea = r.removeFromBottom (222);
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

        auto m = macroArea.reduced (16, 12).withTrimmedTop (30);
        auto choice = m.removeFromRight (180);
        levelMatch->setBounds (choice.withSizeKeepingCentre (170, 64));
        const int w = m.getWidth() / (int) macros.size();
        for (size_t i = 0; i < macros.size(); ++i)
            macros[i]->setBounds (m.getX() + (int) i * w, m.getY(), w, m.getHeight());
    }
}
