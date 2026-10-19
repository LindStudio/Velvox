#include "AnalysisOverlay.h"

namespace velvox::ui
{
    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }
        using Phase = automix::AutoMixController::Phase;

        constexpr int cardW = 620, cardH = 470;

        juce::String seconds (float s) { return juce::String (s, 1).replaceCharacter ('.', ','); }

        void drawParagraph (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float size, juce::Colour c)
        {
            juce::AttributedString s;
            s.setJustification (juce::Justification::topLeft);
            s.setLineSpacing (3.0f);
            s.append (text, font (size), c);
            juce::TextLayout l;
            l.createLayout (s, r.getWidth());
            l.draw (g, r);
        }
    }

    AnalysisOverlay::AnalysisOverlay (automix::AutoMixController& c) : controller (c)
    {
        setWantsKeyboardFocus (true);
        setVisible (false);
        primary.getProperties().set ("primary", true);
        addAndMakeVisible (primary);
        addAndMakeVisible (secondary);

        primary.onClick = [this]
        {
            switch (view)
            {
                case View::intro:
                case View::failed:
                    controller.start();
                    setView (View::listening);
                    break;
                case View::capturing:
                    if (controller.analyseNow())
                        setView (View::analysing);
                    break;
                case View::done:
                    close();
                    break;
                case View::listening:
                case View::analysing:
                    break;
            }
        };

        secondary.onClick = [this]
        {
            switch (view)
            {
                case View::intro:
                    controller.dismissPrompt();
                    close();
                    break;
                case View::listening:
                case View::capturing:
                case View::analysing:
                    controller.cancel();
                    close();
                    break;
                case View::done:
                case View::failed:
                    close();
                    break;
            }
        };
    }

    juce::Rectangle<int> AnalysisOverlay::cardBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (cardW, cardH);
    }

    void AnalysisOverlay::showIntro()
    {
        setVisible (true);
        toFront (true);
        setView (View::intro);
        grabKeyboardFocus();
    }

    void AnalysisOverlay::showProgress()
    {
        setVisible (true);
        toFront (true);
        switch (controller.getPhase())
        {
            case Phase::listening: setView (View::listening); break;
            case Phase::capturing: setView (View::capturing); break;
            case Phase::analysing: setView (View::analysing); break;
            case Phase::failed:    setView (View::failed); break;
            case Phase::done:      setView (View::done); break;
            case Phase::idle:      setView (View::intro); break;
        }
        update();
    }

    void AnalysisOverlay::close()
    {
        setVisible (false);
    }

    void AnalysisOverlay::setView (View v)
    {
        view = v;
        viewStartMs = juce::Time::getMillisecondCounterHiRes();
        warning.clear();
        if (v == View::done)
            doneAtMs = viewStartMs;
        if (v == View::listening || v == View::intro)
            shownProgress.snap (0.0f);

        switch (v)
        {
            case View::intro:     primary.setButtonText ("Audio analysieren"); secondary.setButtonText (de ("Später")); break;
            case View::listening: primary.setButtonText ("Jetzt analysieren"); secondary.setButtonText ("Abbrechen"); break;
            case View::capturing: primary.setButtonText ("Jetzt analysieren"); secondary.setButtonText ("Abbrechen"); break;
            case View::analysing: primary.setButtonText ("Bitte warten"); secondary.setButtonText ("Abbrechen"); break;
            case View::done:      primary.setButtonText ("Ergebnis ansehen"); secondary.setButtonText (de ("Schließen")); break;
            case View::failed:    primary.setButtonText ("Erneut versuchen"); secondary.setButtonText (de ("Schließen")); break;
        }
        primary.setVisible (v != View::analysing);
        primary.setEnabled (v != View::listening);
        secondary.setVisible (v != View::done);
        layoutButtons();
        repaint();
    }

    void AnalysisOverlay::update()
    {
        if (! isVisible())
            return;

        const auto st = controller.getStatus();
        const double now = juce::Time::getMillisecondCounterHiRes();

        // Follow the controller (it also runs while this card is closed)
        if (view != View::intro)
        {
            View wanted = view;
            switch (st.phase)
            {
                case Phase::listening: wanted = View::listening; break;
                case Phase::capturing: wanted = View::capturing; break;
                case Phase::analysing: wanted = View::analysing; break;
                case Phase::failed:    wanted = View::failed; break;
                case Phase::done:      wanted = view == View::analysing || view == View::done ? View::done : view; break;
                case Phase::idle:      wanted = view; break;
            }
            if (wanted != view)
            {
                setView (wanted);
                if (wanted == View::done && onFinished)
                    onFinished();
            }
        }

        level = st.capture.recentRmsDb;
        juce::String newStatus, newWarning;
        float target = 0.0f;

        switch (view)
        {
            case View::intro:
                break;

            case View::listening:
            {
                newStatus = de ("Warte auf Signal am Eingang …");
                const double waited = (now - viewStartMs) * 0.001;
                if (level > -60.0f && level <= -50.0f)
                    newWarning = de ("Signal ist sehr leise. Velvox beginnt ab etwa -50 dBFS - Spurpegel oder Input-Gain anheben.");
                else if (waited > 6.0 && level <= -60.0f)
                    newWarning = de ("Kein Signal erkannt. Läuft die Wiedergabe, und ist die Vocal-Spur auf diesen Mixer-Kanal geroutet?");
                break;
            }

            case View::capturing:
            {
                const auto& c = st.capture;
                target = c.fraction();
                newStatus = seconds (c.voicedSeconds) + de (" von ") + seconds (c.targetSeconds) + de (" s Gesang erfasst");
                if (c.clipSamples > 0)
                    newWarning = de ("Übersteuerung im Eingang erkannt (") + juce::String (c.clipSamples)
                               + de (" Samples). Velvox kann Verzerrung nicht entfernen - besser den Pegel senken und neu starten.");
                else if (c.transportStopped && level < -60.0f)
                    newWarning = c.enough() ? de ("Wiedergabe gestoppt. Starte sie erneut, um weiter zu erfassen - oder analysiere die bisherigen ")
                                                  + seconds (c.voicedSeconds) + " s."
                                            : de ("Wiedergabe gestoppt - bisher zu wenig Gesang. Starte die Wiedergabe erneut, Velvox erfasst weiter.");
                else if (level < -60.0f && (now - viewStartMs) > 3000.0)
                    newWarning = de ("Gerade kein Signal - Pausen werden übersprungen.");
                primary.setEnabled (c.enough());
                break;
            }

            case View::analysing:
                target = juce::jlimit (0.0f, 1.0f, st.analysisFraction);
                newStatus = (st.stage.isNotEmpty() ? st.stage : de ("Analyse läuft")) + de (" …");
                break;

            case View::done:
            {
                const auto& r = controller.getReport();
                target = 1.0f;
                newStatus = juce::String ((int) r.changedModules.size()) + de (" Module eingestellt - ") + seconds (r.voicedSeconds)
                          + de (" s Gesang analysiert.");
                if (r.conservative)
                    newWarning = de ("Wenig Material: Velvox hat nur vorsichtige Einstellungen verwendet.");
                if (now - doneAtMs > 2600.0)
                {
                    close();
                    return;
                }
                break;
            }

            case View::failed:
                newStatus = st.error;
                break;
        }

        shownProgress.target = target;
        const bool moved = shownProgress.tick (0.18f);
        if (newStatus != status || newWarning != warning || moved || view == View::listening || view == View::capturing || view == View::analysing)
        {
            status = newStatus;
            warning = newWarning;
            repaint (cardBounds());
        }
    }

    void AnalysisOverlay::paint (juce::Graphics& g)
    {
        g.fillAll (colours::scrim.withMultipliedAlpha (0.85f));

        const auto card = cardBounds().toFloat();
        drawGlow (g, card.getTopLeft().translated (80.0f, 30.0f), 340.0f, 0.10f, colours::teal);
        drawGlow (g, card.getBottomRight().translated (-60.0f, -40.0f), 360.0f, 0.10f, colours::violet);
        drawGlassPanel (g, card, radius::panel + 6.0f, colours::violet, 0.07f);

        auto r = card.reduced (40.0f, 34.0f);
        const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;

        // ---- Visual: progress ring / waiting pulse / check mark
        auto visual = r.removeFromTop (120.0f);
        const auto c = visual.getCentre();
        const float ringR = 46.0f;
        juce::Path track;
        track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
        g.setColour (colours::accent.withAlpha (0.08f));
        g.strokePath (track, juce::PathStrokeType (6.0f));

        if (view == View::intro || view == View::listening)
        {
            // Breathing ring: idle while waiting (eight concentric "sound" arcs in the intro)
            const float pulse = view == View::listening ? 0.5f + 0.5f * (float) std::sin (t * 3.2) : 0.6f;
            g.setGradientFill (auroraGradient (c.translated (-ringR, -ringR), c.translated (ringR, ringR), 0.35f + 0.5f * pulse));
            g.strokePath (track, juce::PathStrokeType (3.0f));
            if (view == View::listening)
            {
                // Live input level as a filling inner disc
                const float lv = juce::jlimit (0.0f, 1.0f, juce::jmap (level, -70.0f, -12.0f, 0.0f, 1.0f));
                drawGlow (g, c, ringR * (0.4f + 0.6f * lv), 0.25f + 0.4f * lv, colours::teal);
            }
            // A simple wave mark in the centre
            juce::Path wave;
            for (int i = 0; i <= 40; ++i)
            {
                const float x = c.x - 24.0f + 48.0f * (float) i / 40.0f;
                const float env = std::sin (juce::MathConstants<float>::pi * (float) i / 40.0f);
                const float y = c.y + 10.0f * env * std::sin ((float) i * 0.9f + (float) t * (view == View::listening ? 4.0f : 0.0f));
                if (i == 0) wave.startNewSubPath (x, y); else wave.lineTo (x, y);
            }
            g.setColour (colours::text.withAlpha (0.85f));
            g.strokePath (wave, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (view == View::capturing || view == View::analysing || view == View::done)
        {
            const float p = shownProgress.current;
            if (view == View::analysing && p < 0.02f)
            {
                juce::Path spin;
                spin.addCentredArc (c.x, c.y, ringR, ringR, (float) t * 4.0f, 0.0f, 1.4f, true);
                g.setGradientFill (auroraGradient (c.translated (-ringR, 0.0f), c.translated (ringR, 0.0f)));
                g.strokePath (spin, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else if (p > 0.001f)
            {
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * p, true);
                g.setGradientFill (auroraGradient (c.translated (0.0f, -ringR), c.translated (0.0f, ringR), 0.25f));
                g.strokePath (arc, juce::PathStrokeType (14.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setGradientFill (auroraGradient (c.translated (0.0f, -ringR), c.translated (0.0f, ringR)));
                g.strokePath (arc, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            g.setColour (colours::text);
            if (view == View::done)
            {
                juce::Path tick;
                tick.startNewSubPath (c.x - 16.0f, c.y + 1.0f);
                tick.lineTo (c.x - 4.0f, c.y + 13.0f);
                tick.lineTo (c.x + 19.0f, c.y - 12.0f);
                g.strokePath (tick, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else
            {
                g.setFont (display (24.0f, true));
                g.drawText (juce::String (juce::roundToInt (p * 100.0f)) + "%", visual, juce::Justification::centred, false);
            }
        }
        else if (view == View::failed)
        {
            g.setColour (colours::warning.withAlpha (0.8f));
            g.strokePath (track, juce::PathStrokeType (3.0f));
            g.setFont (display (34.0f, true));
            g.drawText ("!", visual, juce::Justification::centred, false);
        }

        r.removeFromTop (18.0f);

        // ---- Title and text
        juce::String title, body;
        switch (view)
        {
            case View::intro:
                title = "Let's mix your vocal.";
                body = de ("Velvox analysiert zuerst deine Vocal und erstellt daraus eine passende Bearbeitung - Klang, Dynamik, S-Laute und Raum. "
                           "Starte die Wiedergabe deiner Vocal in der DAW. Velvox analysiert einen begrenzten Abschnitt und erstellt anschließend automatisch deinen Vocal-Mix.");
                break;
            case View::listening:
                title = de ("Spiel deine Vocal ab");
                body = de ("Starte jetzt die Wiedergabe in FL Studio. Velvox hört nur zu - der Klang wird erst nach der Analyse verändert.");
                break;
            case View::capturing:
                title = de ("Velvox hört zu");
                body = de ("Ein begrenzter Abschnitt wird erfasst; Pausen werden übersprungen. Die Erfassung stoppt automatisch, sobald genug Gesang da ist.");
                break;
            case View::analysing:
                title = de ("Dein Mix wird erstellt");
                body = de ("Analyse, Planung und Prüfung der ganzen Kette laufen im Hintergrund. Die Wiedergabe wird nicht unterbrochen.");
                break;
            case View::done:
                title = de ("Dein Vocal-Mix ist aktiv");
                body = de ("Hör dir das Ergebnis in der DAW an. Mit „Vorher / Mix“ vergleichst du in gleicher Lautheit, jedes Modul bleibt frei einstellbar.");
                break;
            case View::failed:
                title = de ("Analyse nicht möglich");
                body = de ("Es wurde nichts verändert.");
                break;
        }

        g.setColour (colours::text);
        g.setFont (font (view == View::intro ? 30.0f : 25.0f, true));
        g.drawText (title, r.removeFromTop (38.0f), juce::Justification::centred, false);
        r.removeFromTop (10.0f);
        auto bodyArea = r.removeFromTop (76.0f).reduced (8.0f, 0.0f);
        drawParagraph (g, body, bodyArea, 15.0f, colours::textDim);

        // ---- Intro: preparation tips; others: status and warning
        if (view == View::intro)
        {
            r.removeFromTop (4.0f);
            const char* tips[] {
                "Möglichst eine unbearbeitete oder nur leicht bearbeitete Vocal verwenden",
                "Ausreichend Pegel, aber keine Übersteuerung im Eingang",
                "Die Vocal-Spur auf diesen Mixer-Kanal routen",
            };
            for (auto* tip : tips)
            {
                auto row = r.removeFromTop (24.0f).reduced (24.0f, 0.0f);
                g.setGradientFill (auroraGradient (row.getTopLeft(), row.getTopLeft().translated (8.0f, 8.0f)));
                g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ row.getX() + 4.0f, row.getCentreY() }));
                g.setColour (colours::text.withAlpha (0.86f));
                g.setFont (font (14.0f));
                g.drawText (de (tip), row.withTrimmedLeft (18.0f), juce::Justification::centredLeft, true);
            }
        }
        else
        {
            g.setColour (view == View::failed ? colours::warning : colours::text);
            g.setFont (medium (16.0f));
            g.drawFittedText (status, r.removeFromTop (26.0f).toNearestInt(), juce::Justification::centred, 2);

            if (view == View::listening || view == View::capturing)
            {
                // Live input level bar
                auto bar = r.removeFromTop (22.0f).withSizeKeepingCentre (300.0f, 6.0f);
                g.setColour (colours::accent.withAlpha (0.08f));
                g.fillRoundedRectangle (bar, 3.0f);
                const float lv = juce::jlimit (0.0f, 1.0f, juce::jmap (level, -70.0f, 0.0f, 0.0f, 1.0f));
                if (lv > 0.0f)
                {
                    g.setGradientFill (auroraGradient (bar.getTopLeft(), bar.getTopRight()));
                    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * lv), 3.0f);
                }
                g.setColour (colours::textFaint);
                g.setFont (font (12.0f));
                g.drawText ("Eingang " + (level > -99.0f ? juce::String (juce::roundToInt (level)) + " dBFS RMS" : juce::String ("-")),
                            r.removeFromTop (16.0f), juce::Justification::centred, false);
            }

            if (warning.isNotEmpty())
            {
                r.removeFromTop (6.0f);
                auto w = r.removeFromTop (44.0f).reduced (20.0f, 0.0f);
                g.setColour (colours::bypass.withAlpha (0.10f));
                g.fillRoundedRectangle (w, 10.0f);
                g.setColour (colours::bypass.withAlpha (0.9f));
                g.setFont (font (13.5f));
                g.drawFittedText (warning, w.reduced (12.0f, 4.0f).toNearestInt(), juce::Justification::centred, 2);
            }
        }
    }

    void AnalysisOverlay::layoutButtons()
    {
        auto r = cardBounds().reduced (40, 30).removeFromBottom (44);
        const bool both = primary.isVisible() && secondary.isVisible();
        if (both)
        {
            auto row = r.withSizeKeepingCentre (420, 44);
            primary.setBounds (row.removeFromRight (240));
            row.removeFromRight (14);
            secondary.setBounds (row);
        }
        else
        {
            primary.setBounds (r.withSizeKeepingCentre (240, 44));
            secondary.setBounds (r.withSizeKeepingCentre (200, 44));
        }
    }

    void AnalysisOverlay::resized()
    {
        layoutButtons();
    }

    bool AnalysisOverlay::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            secondary.triggerClick();
            return true;
        }
        if (key == juce::KeyPress::returnKey && primary.isVisible() && primary.isEnabled())
        {
            primary.triggerClick();
            return true;
        }
        return false;
    }
}
