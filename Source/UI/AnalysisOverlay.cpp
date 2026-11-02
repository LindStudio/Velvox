#include "AnalysisOverlay.h"
#include "../Text/I18n.h"

namespace velvox::ui
{
    namespace
    {
        using i18n::tr;
        using Phase = automix::AutoMixController::Phase;

        constexpr int cardW = 620, cardH = 470;
        constexpr int introW = 760, introH = 560;   // 0.8: genre, vibe and the beat box

        // A pill to choose one of a group (genre / vibe)
        class Chip final : public juce::Button
        {
        public:
            explicit Chip (const juce::String& text) : juce::Button (text)
            {
                setClickingTogglesState (true);
                setMouseCursor (juce::MouseCursor::PointingHandCursor);
            }

            void paintButton (juce::Graphics& g, bool over, bool down) override
            {
                const auto r = getLocalBounds().toFloat().reduced (1.0f);
                const float corner = r.getHeight() * 0.5f;
                if (getToggleState())
                {
                    g.setGradientFill (auroraGradient (r.getTopLeft(), r.getBottomRight()));
                    g.fillRoundedRectangle (r, corner);
                    g.setColour (colours::background);
                }
                else
                {
                    g.setColour (colours::accent.withAlpha (over ? 0.14f : 0.07f));
                    g.fillRoundedRectangle (r, corner);
                    g.setColour (colours::accent.withAlpha (down ? 0.5f : 0.25f));
                    g.drawRoundedRectangle (r, corner, 1.0f);
                    g.setColour (colours::text.withAlpha (over ? 1.0f : 0.85f));
                }
                g.setFont (medium (13.5f));
                g.drawText (getButtonText(), r, juce::Justification::centred, false);
            }
        };

        juce::String seconds (float s) { return i18n::number (s, 1); }

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

        // 0.8: genre and vibe (one of each), written to the controller when chosen
        for (int g = 0; g < automix::numGenres; ++g)
        {
            auto chip = std::make_unique<Chip> (i18n::show (automix::genreName ((automix::Genre) g)));
            chip->setRadioGroupId (8801);
            chip->onClick = [this, g]
            {
                auto intent = controller.getIntent();
                intent.genre = (automix::Genre) g;
                controller.setIntent (intent);
            };
            addChildComponent (*chip);
            genreChips.push_back (std::move (chip));
        }
        for (int v = 0; v < automix::numVibes; ++v)
        {
            auto chip = std::make_unique<Chip> (i18n::show (automix::vibeName ((automix::Vibe) v)));
            chip->setRadioGroupId (8802);
            chip->onClick = [this, v]
            {
                auto intent = controller.getIntent();
                intent.vibe = (automix::Vibe) v;
                controller.setIntent (intent);
            };
            addChildComponent (*chip);
            vibeChips.push_back (std::move (chip));
        }

        primary.onClick = [this]
        {
            switch (view)
            {
                case View::intro:
                case View::failed:
                    controller.start();
                    setView (View::listening);
                    break;
                case View::capturing:   // 0.8: no early analysis - the button is hidden
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
        return view == View::intro ? getLocalBounds().withSizeKeepingCentre (introW, introH)
                                   : getLocalBounds().withSizeKeepingCentre (cardW, cardH);
    }

    AnalysisOverlay::IntroLayout AnalysisOverlay::introLayout() const
    {
        IntroLayout l;
        auto r = cardBounds().toFloat().reduced (40.0f, 30.0f);
        l.title = r.removeFromTop (36.0f);
        r.removeFromTop (4.0f);
        l.body = r.removeFromTop (42.0f);
        r.removeFromTop (10.0f);
        l.genreLabel = r.removeFromTop (20.0f);
        l.genreRow = r.removeFromTop (34.0f);
        r.removeFromTop (12.0f);
        l.vibeLabel = r.removeFromTop (20.0f);
        l.vibeRow = r.removeFromTop (34.0f);
        r.removeFromTop (18.0f);
        l.beatBox = r.removeFromTop (104.0f);
        return l;
    }

    void AnalysisOverlay::layoutChips()
    {
        const auto l = introLayout();
        const auto place = [] (std::vector<std::unique_ptr<juce::Button>>& chips, juce::Rectangle<float> row)
        {
            const float gap = 8.0f;
            std::vector<float> widths;
            float total = 0.0f;
            for (auto& c : chips)
            {
                widths.push_back ((float) juce::GlyphArrangement::getStringWidthInt (medium (13.5f), c->getButtonText()) + 30.0f);
                total += widths.back();
            }
            total += gap * (float) (chips.size() - 1);
            float x = row.getCentreX() - 0.5f * total;
            for (size_t i = 0; i < chips.size(); ++i)
            {
                chips[i]->setBounds (juce::Rectangle<float> (x, row.getY() + 1.0f, widths[i], row.getHeight() - 2.0f).toNearestInt());
                x += widths[i] + gap;
            }
        };
        place (genreChips, l.genreRow);
        place (vibeChips, l.vibeRow);
    }

    void AnalysisOverlay::syncChips()
    {
        const auto intent = controller.getIntent();
        for (int g = 0; g < (int) genreChips.size(); ++g)
            genreChips[(size_t) g]->setToggleState (g == (int) intent.genre, juce::dontSendNotification);
        for (int v = 0; v < (int) vibeChips.size(); ++v)
            vibeChips[(size_t) v]->setToggleState (v == (int) intent.vibe, juce::dontSendNotification);
    }

    void AnalysisOverlay::showIntro()
    {
        setVisible (true);
        toFront (true);
        setView (View::intro);
        syncChips();
        update();
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
        if (v == View::listening || v == View::intro)
            shownProgress.snap (0.0f);

        switch (v)
        {
            case View::intro:     primary.setButtonText (tr ("Auto Mix starten", "Start auto mix")); secondary.setButtonText (tr ("Später", "Later")); break;
            case View::listening: secondary.setButtonText (tr ("Abbrechen", "Cancel")); break;
            case View::capturing: secondary.setButtonText (tr ("Abbrechen", "Cancel")); break;
            case View::analysing: primary.setButtonText (tr ("Bitte warten", "Please wait")); secondary.setButtonText (tr ("Abbrechen", "Cancel")); break;
            case View::done:      primary.setButtonText (tr ("Ergebnis ansehen", "View result")); secondary.setButtonText (tr ("Schließen", "Close")); break;
            case View::failed:    primary.setButtonText (tr ("Erneut versuchen", "Try again")); secondary.setButtonText (tr ("Schließen", "Close")); break;
        }
        // 0.8: while listening there is no button to analyse early - the full listening time is needed
        primary.setVisible (v == View::intro || v == View::done || v == View::failed);
        primary.setEnabled (true);
        secondary.setVisible (v != View::done);
        for (auto* chips : { &genreChips, &vibeChips })
            for (auto& c : *chips)
                c->setVisible (v == View::intro);
        layoutButtons();
        layoutChips();
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
        const bool beat = (beatPresent && beatPresent()) || ((view == View::listening || view == View::capturing) && st.capture.beatSeen);
        const auto newBeat = beat ? juce::String::fromUTF8 ("\xe2\x97\x8f ") + tr ("Beat erkannt", "Beat detected")
                                  : juce::String::fromUTF8 ("\xe2\x97\x8b ") + tr ("Kein Beat am Sidechain", "No beat on the sidechain");

        switch (view)
        {
            case View::intro:
                break;

            case View::listening:
            {
                newStatus = tr ("Warte auf Signal am Eingang …", "Waiting for a signal at the input …");
                const double waited = (now - viewStartMs) * 0.001;
                if (level > -60.0f && level <= -50.0f)
                    newWarning = tr ("Signal ist sehr leise. Headliner beginnt ab etwa -50 dBFS - Spurpegel oder Input-Gain anheben.",
                                     "The signal is very quiet. Headliner starts at about -50 dBFS - raise the track level or input gain.");
                else if (waited > 6.0 && level <= -60.0f)
                    newWarning = tr ("Kein Signal erkannt. Läuft die Wiedergabe, und liegt Headliner auf der Spur mit deiner Vocal?",
                                     "No signal detected. Is playback running, and is Headliner on the track with your vocal?");
                break;
            }

            case View::capturing:
            {
                const auto& c = st.capture;
                target = c.fraction();
                const int missing = std::max (1, (int) std::ceil (c.targetSeconds - c.voicedSeconds - 1.0e-3f));
                newStatus = i18n::format (tr ("Noch {0} s Gesang", "{0} s of singing to go"), { juce::String (missing) });
                if (c.clipSamples > 0)
                    newWarning = i18n::format (tr ("Übersteuerung im Eingang erkannt ({0} Samples). Headliner kann Verzerrung nicht entfernen - besser den Pegel senken und neu starten.",
                                                   "Clipping detected at the input ({0} samples). Headliner can't remove distortion - better lower the level and start again."),
                                               { juce::String (c.clipSamples) });
                else if (c.transportStopped && level < -60.0f)
                    newWarning = i18n::format (tr ("Weiter abspielen - noch {0} s nötig. Headliner hört weiter zu, sobald der Song wieder läuft.",
                                                   "Keep playing - {0} s still needed. Headliner keeps listening as soon as the song plays again."),
                                               { juce::String (missing) });
                else if (level < -60.0f && (now - viewStartMs) > 3000.0)
                    newWarning = tr ("Gerade kein Signal - Pausen werden übersprungen.", "No signal right now - gaps are skipped.");
                break;
            }

            case View::analysing:
                target = juce::jlimit (0.0f, 1.0f, st.analysisFraction);
                newStatus = (st.stage.isNotEmpty() ? i18n::show (st.stage) : tr ("Analyse läuft", "Analyzing")) + juce::String::fromUTF8 (" …");
                break;

            case View::done:
            {
                const auto& r = controller.getReport();
                target = 1.0f;
                newStatus = i18n::format (tr ("{0} Module eingestellt - {1} s Gesang analysiert.", "{0} modules set - {1} s of singing analyzed."),
                                          { juce::String ((int) r.changedModules.size()), seconds (r.voicedSeconds) });
                if (r.conservative)
                    newWarning = tr ("Wenig Material: Headliner hat nur vorsichtige Einstellungen verwendet.",
                                     "Little material: Headliner used only cautious settings.");
                // Stays until clicked: the confirmation is not missed while listening to the DAW
                break;
            }

            case View::failed:
                newStatus = i18n::show (st.error);
                break;
        }

        shownProgress.target = target;
        const bool moved = shownProgress.tick (0.18f);
        if (newStatus != status || newWarning != warning || newBeat != beatStatus || moved || view == View::listening || view == View::capturing || view == View::analysing)
        {
            status = newStatus;
            warning = newWarning;
            beatStatus = newBeat;
            repaint (cardBounds());
        }
    }

    juce::String AnalysisOverlay::introText()
    {
        return i18n::format (tr ("Spiel deinen Song ab, am besten mit der unbearbeiteten Vocal. Headliner hört {0} s Gesang zu und stellt dann jedes Modul auf deine Stimme ein.",
                                 "Play your song, ideally with the unprocessed vocal. Headliner listens to {0} s of singing and then sets every module for your voice."),
                             { juce::String (juce::roundToInt (automix::CaptureConfig {}.targetVoicedSeconds)) });
    }

    juce::String AnalysisOverlay::beatHelpText()
    {
        return tr ("Dann passt der Auto Mix die Stimme an den Beat an und stellt sie über den Beat. FL Studio: Beat-Kanal im Mixer auswählen "
                   "\xe2\x86\x92 Rechtsklick auf den Pfeil unter dem Headliner-Kanal \xe2\x86\x92 \xe2\x80\x9eSidechain to this track\xe2\x80\x9c "
                   "\xe2\x86\x92 im Plugin-Fenster (Zahnrad) den Sidechain-Eingang wählen.",
                   "Then the auto mix fits the vocal to the beat and sets it above the beat. FL Studio: select the beat channel in the mixer "
                   "\xe2\x86\x92 right-click the arrow below the Headliner channel \xe2\x86\x92 \"Sidechain to this track\" "
                   "\xe2\x86\x92 choose the sidechain input in the plugin window (gear).");
    }

    void AnalysisOverlay::paint (juce::Graphics& g)
    {
        g.fillAll (colours::scrim.withMultipliedAlpha (0.85f));

        const auto card = cardBounds().toFloat();
        drawGlow (g, card.getTopLeft().translated (80.0f, 30.0f), 340.0f, 0.10f, colours::teal);
        drawGlow (g, card.getBottomRight().translated (-60.0f, -40.0f), 360.0f, 0.10f, colours::violet);
        drawGlassPanel (g, card, radius::panel + 6.0f, colours::violet, 0.07f);

        // ---- 0.8 intro: genre, vibe and the beat box
        if (view == View::intro)
        {
            const auto l = introLayout();
            g.setColour (colours::text);
            g.setFont (font (28.0f, true));
            g.drawFittedText (tr ("Lass uns deine Vocal mixen.", "Let's mix your vocal."), l.title.toNearestInt(), juce::Justification::centred, 1, 0.85f);
            drawParagraph (g, introText(), l.body.reduced (30.0f, 0.0f), 14.5f, colours::textDim);
            g.setColour (colours::textFaint);
            g.setFont (semiBold (12.0f, 0.08f));
            g.drawText (i18n::upper (tr ("Was mischst du?", "What are you mixing?")), l.genreLabel, juce::Justification::centred, false);
            g.drawText (i18n::upper (tr ("Wie soll die Stimme klingen?", "How should the vocal sound?")), l.vibeLabel, juce::Justification::centred, false);

            const auto box = l.beatBox.reduced (10.0f, 0.0f);
            const bool beat = beatStatus.startsWith (juce::String::fromUTF8 ("\xe2\x97\x8f"));
            g.setColour ((beat ? colours::ok : colours::accent).withAlpha (0.08f));
            g.fillRoundedRectangle (box, 12.0f);
            g.setColour ((beat ? colours::ok : colours::accent).withAlpha (0.3f));
            g.drawRoundedRectangle (box, 12.0f, 1.0f);
            auto inner = box.reduced (18.0f, 12.0f);
            auto head = inner.removeFromTop (22.0f);
            g.setColour (beat ? colours::ok : colours::textDim);
            g.setFont (medium (13.5f));
            g.drawText (beatStatus, head.removeFromRight (230.0f), juce::Justification::centredRight, false);
            g.setColour (colours::text);
            g.setFont (semiBold (14.5f));
            g.drawText (tr ("Tipp: Beat als Sidechain (empfohlen)", "Tip: beat as sidechain (recommended)"), head, juce::Justification::centredLeft, false);
            inner.removeFromTop (6.0f);
            drawParagraph (g, beatHelpText(), inner, 13.0f, colours::textDim);
            return;
        }

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
                title = tr ("Lass uns deine Vocal mixen.", "Let's mix your vocal.");
                body = introText();
                break;
            case View::listening:
                title = tr ("Spiel deine Vocal ab", "Play your vocal");
                body = tr ("Starte jetzt die Wiedergabe in deiner DAW. Headliner hört nur zu - der Klang wird erst nach der Analyse verändert.",
                           "Start playback in your DAW now. Headliner only listens - the sound changes only after the analysis.");
                break;
            case View::capturing:
                title = tr ("Headliner hört zu", "Headliner is listening");
                body = tr ("Ein begrenzter Abschnitt wird erfasst; Pausen werden übersprungen. Die Erfassung stoppt automatisch, sobald genug Gesang da ist.",
                           "A limited section is captured; gaps are skipped. Capturing stops by itself as soon as there is enough singing.");
                break;
            case View::analysing:
                title = tr ("Dein Mix wird erstellt", "Creating your mix");
                body = tr ("Analyse, Planung und Prüfung der ganzen Kette laufen im Hintergrund. Die Wiedergabe wird nicht unterbrochen.",
                           "Analysis, planning and checking of the whole chain run in the background. Playback is not interrupted.");
                break;
            case View::done:
                title = tr ("Dein Vocal-Mix ist aktiv", "Your vocal mix is active");
                body = tr ("Hör dir das Ergebnis in der DAW an. Mit „Vorher / Nachher“ vergleichst du in gleicher Lautheit, jedes Modul bleibt frei einstellbar.",
                           "Listen to the result in your DAW. \"Before / After\" compares at the same loudness, and every module stays fully adjustable.");
                break;
            case View::failed:
                title = tr ("Analyse nicht möglich", "Analysis not possible");
                body = tr ("Es wurde nichts verändert.", "Nothing was changed.");
                break;
        }

        g.setColour (colours::text);
        g.setFont (font (view == View::intro ? 30.0f : 25.0f, true));
        g.drawFittedText (title, r.removeFromTop (38.0f).toNearestInt(), juce::Justification::centred, 1, 0.85f);
        r.removeFromTop (10.0f);
        auto bodyArea = r.removeFromTop (view == View::intro ? 44.0f : 76.0f).reduced (8.0f, 0.0f);
        drawParagraph (g, body, bodyArea, 15.0f, colours::textDim);

        // ---- Status and warning
        {
            g.setColour (view == View::failed ? colours::warning : colours::text);
            g.setFont (medium (16.0f));
            g.drawFittedText (status, r.removeFromTop (26.0f).toNearestInt(), juce::Justification::centred, 2);

            if (view == View::listening || view == View::capturing)
            {
                // Live input level as a segmented level meter (label, segments, value) - in the
                // colours of a level meter, not the gradient of the progress ring, so it never
                // reads as a second progress bar
                auto row = r.removeFromTop (38.0f).withSizeKeepingCentre (380.0f, 16.0f);
                g.setColour (colours::textFaint);
                g.setFont (semiBold (11.0f, 0.12f));
                g.drawText (tr ("PEGEL", "LEVEL"), row.removeFromLeft (64.0f), juce::Justification::centredLeft, false);
                g.setColour (colours::textDim);
                g.setFont (font (12.5f));
                g.drawText (level > -99.0f ? juce::String (juce::roundToInt (level)) + " dBFS" : juce::String ("-"),
                            row.removeFromRight (76.0f), juce::Justification::centredRight, false);
                row.removeFromRight (10.0f);

                constexpr int segments = 30;
                const float lowDb = -70.0f, gap = 2.0f;
                const float segW = (row.getWidth() - gap * (segments - 1)) / (float) segments;
                const auto meter = row.withSizeKeepingCentre (row.getWidth(), 10.0f);
                for (int i = 0; i < segments; ++i)
                {
                    const float segDb = lowDb - lowDb * (float) (i + 1) / (float) segments;   // top of this segment
                    const auto seg = juce::Rectangle<float> (meter.getX() + (float) i * (segW + gap), meter.getY(), segW, meter.getHeight());
                    const juce::Colour hue = segDb > -6.0f ? colours::warning : segDb > -18.0f ? colours::bypass : colours::ok;
                    const bool lit = level >= segDb - (-lowDb / (float) segments);
                    g.setColour (lit ? hue.withAlpha (0.9f) : colours::accent.withAlpha (0.07f));
                    g.fillRoundedRectangle (seg, 1.5f);
                }
                // 0.8: whether the beat comes in on the sidechain
                g.setColour (beatStatus.startsWith (juce::String::fromUTF8 ("\xe2\x97\x8f")) ? colours::ok : colours::textFaint);
                g.setFont (font (12.5f));
                g.drawText (beatStatus, r.removeFromTop (18.0f), juce::Justification::centred, false);
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
        layoutChips();
    }

    void AnalysisOverlay::mouseUp (const juce::MouseEvent& e)
    {
        // The confirmation stays until it is clicked (anywhere on the card or around it)
        if (view == View::done && ! e.mouseWasDraggedSinceMouseDown())
            close();
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
