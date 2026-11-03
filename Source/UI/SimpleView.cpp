#include "SimpleView.h"
#include "../PluginProcessor.h"
#include "../Text/I18n.h"
#include "UserSettings.h"

namespace vocalypse::ui
{
    namespace
    {
        // "68 · 3" in the colour of the score (sad / okay / pleased like Memphy)
        class ScoreBadge final : public juce::TextButton
        {
        public:
            void paintButton (juce::Graphics& g, bool highlighted, bool) override
            {
                const int score = (int) getProperties().getWithDefault ("score", -1);
                const auto c = score < 45 ? colours::warning : score < 75 ? juce::Colour (0xfffbbf24) : colours::ok;
                const auto r = getLocalBounds().toFloat().reduced (0.5f);
                g.setColour (colours::panelInset);
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                g.setColour (c.withAlpha (highlighted ? 0.32f : 0.2f));
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                g.setColour (c.withAlpha (0.85f));
                g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
                g.setColour (c.interpolatedWith (juce::Colours::white, 0.35f));
                g.setFont (semiBold (11.0f));
                g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
            }
        };
    }

    namespace
    {
        using i18n::tr;

        void drawPanel (juce::Graphics& g, juce::Rectangle<int> area)
        {
            const auto r = area.toFloat();
            g.setColour (colours::panel);
            g.fillRoundedRectangle (r, 12.0f);
            g.setColour (colours::border);
            g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
        }

        // Memphy introduces himself in the tour (Jakob 2026-10-28: cheeky and playful, says what you can do with him)
        juce::StringArray introLines()
        {
            return { tr ("Autsch! Okay, bin da. Ich bin Memphy, dein kleiner Mix-Teufel.",
                         "Ouch! Okay, I'm here. I'm Memphy, your little mix devil."),
                     tr ("Spiel deinen Song, klick auf „Vocal analysieren“ – ich hör zu und mix dir die Stimme. Kaffee brauch ich keinen.",
                         "Play your song and hit “Analyze vocal” – I'll listen and mix your voice. No coffee needed."),
                     tr ("Klick auf mich, dann zeig ich dir im Vocal Check, was deiner Stimme fehlt. Ehrlich, aber nett.",
                         "Click me and I'll show you in Vocal Check what your voice is missing. Honest, but nice."),
                     tr ("Singst du richtig gut, tanz ich. Singst du schief … tja, das siehst du mir an.",
                         "Sing really well and I dance. Sing off-key … well, you'll see it on my face."),
                     tr ("Jetzt zeig ich dir den Rest. Keine Angst, ich beiß nur ein bisschen.",
                         "Now let me show you the rest. Don't worry, I only bite a little.") };
        }

        juce::StringArray byeLines()
        {
            return { tr ("Das war's! Danke fürs Zuhören – jetzt bist du dran. Ich wohn hier unten, falls was ist.",
                         "That's it! Thanks for listening – your turn now. I live down here if you need me.") };
        }

        // How long a line stays and how long he talks (reading pace)
        // (Jakob 2026-10-28: longer - about 7 s for a short line, 12 s for a long one)
        double lineSeconds (const juce::String& s) { return 3.0 + 0.08 * s.length(); }
        double talkSeconds (const juce::String& s) { return 0.6 + 0.05 * s.length(); }
    }

    void SimpleView::startIntro()
    {
        mascot.playIntro();
        lines = introLines();
        line = -1;
        lineTime = 0.0;
        setSay (tr ("Uiiiiiii…!", "Wheeeeee…!"));
    }

    void SimpleView::stopIntro()
    {
        if (lines.isEmpty())
            return;
        lines.clear();
        line = -1;
        lineTime = 0.0;
        mascot.stopScene();
        updateBubble (false, silentFor > mascot::Brain::silenceToIdle);   // the next frame knows if the mix is busy
    }

    void SimpleView::startOutro()
    {
        mascot.playBow();
        lines = byeLines();
        line = 0;   // he says it while he bows
        lineTime = 0.0;
        setSay (lines[0]);
    }

    void SimpleView::setSay (const juce::String& text)
    {
        if (text != say)
        {
            say = text;
            const auto old = bubble;
            layoutBubble();
            repaint (old.getUnion (bubble).expanded (10));
        }
    }

    bool SimpleView::advanceLines (double dt)
    {
        if (lines.isEmpty())
            return false;
        if (line < 0)
        {
            // The intro lines start once he has landed and waved
            if (mascot.getBrain().sceneRunning())
            {
                if (mascot.getBrain().mood() == mascot::Mood::wink)
                    setSay (tr ("Hey, hallo!", "Hey there!"));
                return true;
            }
            line = 0;
            lineTime = 0.0;
            setSay (lines[0]);
        }
        lineTime += dt;
        if (lineTime >= lineSeconds (lines[line]))
        {
            if (++line >= lines.size())
            {
                lines.clear();
                line = -1;
                return false;
            }
            lineTime = 0.0;
            setSay (lines[line]);
        }
        return true;
    }

    void SimpleView::tick (double dt)
    {
        mascot::Inputs in;
        advance (in, dt);
    }

    void SimpleView::advance (mascot::Inputs in, double dt)
    {
        const bool speaking = advanceLines (dt);
        in.talking = speaking && line >= 0 && lineTime < talkSeconds (lines[line]);
        updateScoreBadge();
        if (! speaking && line < 0 && lines.isEmpty())
            updateTip (in.autoMixBusy, dt);
        else if (tipId.isNotEmpty())
            endTip();
        if (const auto* f = tipId.isNotEmpty() ? proc.getVocalCheck().finding (tipId) : nullptr)
            in.tip = f->severity >= 0.66f ? 2 : 1;
        mascot.tick (in, dt);
        silentFor = in.inputDb > mascot::Brain::signalDb ? 0.0 : silentFor + dt;
        if (! speaking && line < 0 && lines.isEmpty() && tipId.isEmpty())
            updateBubble (in.autoMixBusy, silentFor > mascot::Brain::silenceToIdle);
    }

    juce::Rectangle<int> SimpleView::getMascotArea() const noexcept
    {
        return mascot.getBounds().getUnion (bubble);
    }

    const std::vector<SimpleView::MacroDef>& SimpleView::macroDefs()
    {
        static const std::vector<MacroDef> defs {
            // Klang
            { pid::macroPresence,   "Presence",    0xff00d1ff },
            { pid::macroWarmth,     "Warmth",      0xffd946ef },
            // Raum
            { pid::macroSpace,      "Space",       0xff3b82f6 },
            { pid::macroWidth,      "Width",       0xff6366ff },
            // Dynamik & Pegel
            { pid::macroAggression, "Punch",       0xff8b5cf6 },
            { pid::outputGain,      "Vocal Level", 0xfff472b6 },
        };
        return defs;
    }

    SimpleView::SimpleView (VocalypseAudioProcessor& p, const SpectrumSource* spectrum)
        : proc (p), spectrumView (spectrum)
    {
        spectrumView.setInputDashed (true);
        spectrumView.setRange (-84.0f, -6.0f);
        addAndMakeVisible (spectrumView);
        legend.setItems ({ { tr ("unbearbeitet", "unprocessed"), colours::textSoft.withAlpha (0.8f), LegendItem::Style::dashed },
                           { tr ("bearbeitet", "processed"), colours::blue.interpolatedWith (colours::teal, 0.4f), LegendItem::Style::fill } });
        addAndMakeVisible (legend);

        analyse.getProperties().set ("fire", true);   // the auto mix's action in the fire of the logo
        analyse.setTooltip (tr ("VOCALYPSE hört einen Abschnitt deiner Vocal während der Wiedergabe und stellt die Module ein. "
                                "Der Zustand davor bleibt über Vorher / Nachher erreichbar.",
                                "VOCALYPSE listens to a section of your vocal during playback and sets up the modules. "
                                "The state before stays available with Before / After."));
        analyse.onClick = [this] { if (onAnalyse) onAnalyse(); };

        for (auto* b : { &before, &after })
        {
            b->getProperties().set ("segment", true);
            b->setColour (juce::TextButton::buttonOnColourId, colours::cyan);
        }
        // Nachher = the auto mix: fire (Jakob 2026-11-03)
        after.setColour (juce::TextButton::buttonOnColourId, colours::fire);
        after.setColour (juce::TextButton::textColourOnId, colours::fireTop);
        before.setButtonText (tr ("Vorher", "Before"));
        after.setButtonText (tr ("Nachher", "After"));
        before.setTooltip (tr ("Die Einstellungen vor der Analyse (echter Parameterwechsel)", "The settings before the analysis (a real parameter switch)"));
        after.setTooltip (tr ("Der automatische Mix mit deinen Änderungen", "The automatic mix with your changes"));
        fair.setButtonText (tr ("Gleich laut", "Same loudness"));
        fair.setTooltip (tr ("Fairer Vergleich: Vorher, Nachher und Bypass gleich laut - du hörst den Klang, nicht die Lautheit",
                             "Fair comparison: before, after and bypass at the same loudness - you hear the sound, not the level"));
        fair.getProperties().set ("segment", true);
        fair.setColour (juce::TextButton::buttonOnColourId, colours::cyan);
        fair.onClick = [this]
        {
            if (auto* p = proc.getState().getParameter (pid::levelMatchMode))
            {
                const bool on = p->convertFrom0to1 (p->getValue()) > 0.5f;
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (on ? 0.0f : 1.0f));   // off / like bypass
                p->endChangeGesture();
            }
            fair.setToggleState (proc.getState().getRawParameterValue (pid::levelMatchMode)->load() > 0.5f, juce::dontSendNotification);
        };
        addChildComponent (fair);
        before.onClick = [this] { proc.getAutoMix().showPrevious (true); updateCompare(); };
        after.onClick = [this] { proc.getAutoMix().showPrevious (false); updateCompare(); };

        details.setTooltip (tr ("Was der Auto-Mix gemacht hat, Density und fairer Vergleich", "What the auto mix did, Density and fair comparison"));
        details.onClick = [this] { if (onShowDetails) onShowDetails(); };

        for (auto* b : { &analyse, &before, &after, &details })
            addAndMakeVisible (b);
        meters.setTooltip (tr ("Pegel und Lautheit (LUFS) am Eingang und Ausgang", "Level and loudness (LUFS) at the input and output"));
        addAndMakeVisible (meters);

        mascot.onClick = [this]
        {
            if (! settings::getBool (settings::mascotClicked))
            {
                settings::set (settings::mascotClicked, true);
                clickHint.setVisible (false);
            }
            if (onVocalCheck) onVocalCheck();
        };
        addAndMakeVisible (mascot);
        clickHint.onClick = mascot.onClick;
        addChildComponent (clickHint);
        scoreBadge = std::make_unique<ScoreBadge>();
        scoreBadge->onClick = [this] { if (onVocalCheck) onVocalCheck(); };
        addChildComponent (*scoreBadge);

        tipYes.setButtonText (tr ("Ja, mach", "Yes, do it"));
        tipShow.setButtonText (tr ("Zeig mir", "Show me"));
        tipYes.getProperties().set ("primary", true);
        tipYes.setTooltip (tr ("Memphy wendet den Vorschlag an (ein Rückgängig-Schritt)", "Memphy applies the suggestion (one undo step)"));
        tipShow.setTooltip (tr ("Öffnet den Vocal Check mit allen Vorschlägen", "Opens the Vocal Check with every suggestion"));
        tipYes.onClick = [this]
        {
            const auto id = tipId;
            endTip();
            proc.getVocalCheck().apply (id);
        };
        tipShow.onClick = [this]
        {
            endTip();
            if (onVocalCheck) onVocalCheck();
        };
        addChildComponent (tipYes);
        addChildComponent (tipShow);

        for (const auto& m : macroDefs())
        {
            auto s = std::make_unique<HSlider> (p.getState(), m.paramId, m.label, juce::Colour (m.colour));
            addAndMakeVisible (*s);
            macros.push_back (std::move (s));
        }

        proc.getAutoMix().addChangeListener (this);
        updateCompare();
        updateBubble (false, false);
    }

    SimpleView::~SimpleView()
    {
        proc.getAutoMix().removeChangeListener (this);
    }

    void SimpleView::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        updateCompare();
        repaint (statusRow);
    }

    void SimpleView::updateCompare()
    {
        // Vorher / Nachher only exists after an analysis; after one the main button analyses again
        auto& am = proc.getAutoMix();
        analyse.setButtonText (am.hasMix() ? tr ("Neu analysieren", "Analyze again") : tr ("Vocal analysieren", "Analyze vocal"));
        const bool can = am.hasMix() && am.hasPrevious();
        if (before.isVisible() != can || fair.isVisible() != can)
        {
            before.setVisible (can);
            after.setVisible (can);
            fair.setVisible (can);
            resized();
            repaint();
        }
        fair.setToggleState (proc.getState().getRawParameterValue (pid::levelMatchMode)->load() > 0.5f, juce::dontSendNotification);
        before.setToggleState (can && am.isShowingPrevious(), juce::dontSendNotification);
        after.setToggleState (can && ! am.isShowingPrevious(), juce::dontSendNotification);
    }

    void SimpleView::updateBubble (bool busy, bool silent)
    {
        // Texts of the approved assistant concept (cheeky, informal)
        juce::String text;
        if (busy && silent)
            text = tr ("Ich hör gar nichts. Ist die Spur scharf geschaltet und läuft der Song?",
                       "I can't hear anything. Is the track armed and is the song playing?");
        else if (busy)
            text = tr ("Spiel den Song ab, ich hör mir deine Stimme an. Danach stell ich alles ein.",
                       "Play the song, I'm listening to your voice. Then I'll set everything up.");
        else if (proc.getAutoMix().hasMix())
            text = tr ("Fertig! Dein Auto Mix steht. Hör dir Vorher und Nachher an.",
                       "Done! Your auto mix is set. Listen to Before and After.");
        else
            text = tr ("Spiel deinen Song ab und klick auf „Vocal analysieren“. Ich stell dann alles für dich ein.",
                       "Play your song and click “Analyze vocal”. I'll set everything up for you.");
        setSay (text);
    }

    void SimpleView::refresh (const LiveData& live)
    {
        spectrumView.refresh();
        meters.setLevels (live.inputDb.ago (0), live.outputDb.ago (0), live.inputLufs.ago (0), live.outputLufs.ago (0), live.clipped);
        updateFaderColours();
        if (before.isVisible() && (before.getToggleState() != proc.getAutoMix().isShowingPrevious()
                                   || fair.getToggleState() != (proc.getState().getRawParameterValue (pid::levelMatchMode)->load() > 0.5f)))
            updateCompare();

        // Memphy: real time between frames (the host may slow the timer down), score twice a second
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const double dt = lastTick > 0.0 ? juce::jlimit (0.0, 0.1, now - lastTick) : 1.0 / 60.0;
        lastTick = now;
        scoreAge += dt;
        if (scoreAge >= 0.5)
        {
            score = proc.getVocalCheck().score();
            scoreAge = 0.0;
        }
        using Phase = automix::AutoMixController::Phase;
        const auto phase = proc.getAutoMix().getPhase();
        mascot::Inputs in;
        in.inputDb = live.inputDb.ago (0);
        in.score = score;
        in.autoMixBusy = phase == Phase::listening || phase == Phase::capturing || phase == Phase::analysing;
        in.mixFinished = wasBusy && ! in.autoMixBusy && proc.getAutoMix().hasMix();   // thumbs up for a moment
        advance (in, dt);
        if (in.autoMixBusy != wasBusy)
        {
            wasBusy = in.autoMixBusy;
            repaint (statusRow);
        }
    }

    void SimpleView::updateFaderColours()
    {
        bool changed = false;
        for (auto& s : macros)
        {
            bool fire = false;
            if (const auto mixValue = proc.getAutoMix().autoMixValueOf (s->getParamId()))
                if (auto* p = proc.getState().getParameter (s->getParamId()))
                {
                    // the same value within a hair of the fader's range (the host rounds normalised values)
                    const auto range = p->getNormalisableRange();
                    const float now = p->convertFrom0to1 (p->getValue());
                    fire = std::abs (now - *mixValue) <= 0.002f * (range.end - range.start);
                }
            if (fire != s->isFromAutoMix())
            {
                s->setFromAutoMix (fire);
                changed = true;
            }
        }
        if (changed)
            for (auto& a : noteAreas)
                repaint (a);
    }

    juce::StringArray SimpleView::getMacroGroupTitles() const
    {
        return { tr ("Klang", "Tone"), tr ("Raum", "Space"), tr ("Dynamik & Pegel", "Dynamics & level") };
    }

    juce::String SimpleView::getMacroNote (int index) const
    {
        const auto& r = proc.getAutoMix().getReport();
        if (! r.valid || ! juce::isPositiveAndBelow (index, (int) macroDefs().size()))
            return {};
        const juce::String id (macroDefs()[(size_t) index].paramId);
        // Which steps of the mix belong to which fader
        const auto belongs = [&id] (const automix::MixReport::Line& l)
        {
            const auto title = i18n::show (l.title);
            const bool eq = l.moduleKey == "eq";
            const bool high = title.contains ("kHz");
            const bool lowCut = title.containsIgnoreCase ("low cut") || title.containsIgnoreCase ("lowcut");
            if (id == pid::macroPresence)   return (eq && high) || l.moduleKey == "deess" || l.moduleKey == "smooth";
            if (id == pid::macroWarmth)     return (eq && ! high && ! lowCut) || l.moduleKey == "sat";
            if (id == pid::macroSpace)      return l.moduleKey == "reverb" || l.moduleKey == "delay";
            if (id == pid::macroWidth)      return l.moduleKey == "doubler";
            if (id == pid::macroAggression) return l.moduleKey == "comp" || l.moduleKey == "smash" || l.moduleKey == "ride";
            return l.moduleKey == "output";
        };
        for (const auto& l : r.steps)
            if (belongs (l))
                return tr ("Auto Mix: ", "Auto mix: ") + i18n::show (l.title);
        return {};
    }

    void SimpleView::paint (juce::Graphics& g)
    {
        drawPanel (g, spectrumPanel);
        drawPanel (g, macroPanel);
        drawPanel (g, meterStrip);

        // Fader groups and what the auto mix did at each fader
        const auto titles = getMacroGroupTitles();
        for (int gi = 0; gi < 3; ++gi)
            drawCaption (g, i18n::upper (titles[gi]), groupHeads[(size_t) gi].toFloat(), juce::Justification::centredLeft, colours::textFaint, 11.0f);
        g.setFont (font (11.0f));
        for (int i = 0; i < (int) noteAreas.size(); ++i)
        {
            // The line under a fader glows while the fader still holds what the auto mix set
            const bool fire = i < (int) macros.size() && macros[(size_t) i]->isFromAutoMix();
            g.setColour (fire ? colours::fire.interpolatedWith (colours::fireTop, 0.25f) : colours::textFaint);
            g.drawText (getMacroNote (i), noteAreas[(size_t) i], juce::Justification::centredLeft, true);
        }

        // Spectrum inset
        const auto inset = spectrumView.getBounds().toFloat().expanded (1.0f);
        g.setColour (colours::panelInset);
        g.fillRoundedRectangle (inset, 8.0f);
        g.setColour (colours::borderInset);
        g.drawRoundedRectangle (inset.reduced (0.5f), 8.0f, 1.0f);

        // Segment group behind Vorher | Nachher
        if (before.isVisible())
        {
            g.setColour (colours::control);
            g.fillRoundedRectangle (before.getBounds().getUnion (after.getBounds()).toFloat().expanded (3.0f), 7.0f);
        }

        // Status of the auto mix (top left)
        {
            using Phase = automix::AutoMixController::Phase;
            const auto phase = proc.getAutoMix().getPhase();
            const bool busy = phase == Phase::listening || phase == Phase::capturing || phase == Phase::analysing;
            const bool mixed = proc.getAutoMix().hasMix();
            const auto dot = busy ? colours::cyan : mixed ? colours::fire : colours::textFaint;
            const auto title = busy ? tr ("Analysiere", "Analyzing") : mixed ? tr ("Auto Mix aktiv", "Auto mix active")
                                                                            : tr ("Auto Mix bereit", "Auto mix ready");
            const auto sub = busy ? tr ("Stimme wird gemessen", "measuring your voice")
                                  : mixed ? juce::String() : tr ("noch nicht analysiert", "not analyzed yet");
            auto r = statusRow.toFloat();
            g.setColour (dot);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + 4.0f, r.getCentreY() }));
            r.removeFromLeft (14.0f);
            g.setColour (colours::text);
            g.setFont (semiBold (12.0f));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title);
            g.drawText (title, r.removeFromLeft (w + 10.0f), juce::Justification::centredLeft, false);
            g.setColour (colours::textDim);
            g.setFont (font (12.0f));
            g.drawText (sub, r, juce::Justification::centredLeft, true);
        }

        // Speech bubble of the devil, its tip pointing at him
        const auto b = bubble.toFloat();
        juce::Path shape;
        shape.addRoundedRectangle (b, 12.0f);
        const float tipY = b.getBottom() - 32.0f;
        juce::Path tip;
        tip.addTriangle (b.getX() + 1.0f, tipY - 7.0f, b.getX() - 7.0f, tipY, b.getX() + 1.0f, tipY + 7.0f);
        g.setColour (colours::panelInset);
        g.fillPath (shape);
        g.fillPath (tip);
        g.setColour (colours::border);
        g.drawRoundedRectangle (b.reduced (0.5f), 12.0f, 1.0f);
        g.drawLine (b.getX() + 0.5f, tipY - 7.0f, b.getX() - 7.0f, tipY, 1.0f);
        g.drawLine (b.getX() - 7.0f, tipY, b.getX() + 0.5f, tipY + 7.0f, 1.0f);
        g.setColour (colours::panelInset);
        g.fillRect (b.getX() - 0.5f, tipY - 6.0f, 2.0f, 12.0f);

        g.setColour (colours::text);
        g.setFont (font (12.5f));
        g.drawFittedText (say, bubbleText, juce::Justification::topLeft, 3, 1.0f);
    }

    //==========================================================================
    namespace
    {
        constexpr int bubblePadX = 14, bubblePadY = 10, bubbleLineH = 16, bubbleActionsH = 30, bubbleGap = 6;
        juce::String clickHintText() { return tr ("Klick mich = Vocal Check", "Click me = Vocal Check"); }
    }

    void SimpleView::updateScoreBadge()
    {
        auto& vc = proc.getVocalCheck();
        const int s = vc.score(), tips = s >= 0 ? (int) vc.visibleFindings().size() : 0;
        if (s == badgeScore && tips == badgeTips)
            return;
        badgeScore = s;
        badgeTips = tips;
        const auto middot = juce::String::fromUTF8 (" \xc2\xb7 ");
        scoreBadge->setButtonText (juce::String (s) + middot + juce::String (tips));
        scoreBadge->setTooltip ("Vocal Check " + juce::String (s) + middot
                                + (tips == 1 ? tr ("1 Tipp", "1 tip") : i18n::format (tr ("{0} Tipps", "{0} tips"), { juce::String (tips) }))
                                + tr (" - Klick öffnet den Vocal Check", " - click to open the Vocal Check"));
        scoreBadge->getProperties().set ("score", s);
        scoreBadge->setVisible (s >= 0);
        resized();
    }

    SimpleView::ClickHint::ClickHint()
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip (tr ("Öffnet den Vocal Check", "Opens the Vocal Check"));
    }

    int SimpleView::ClickHint::getIdealWidth() const
    {
        return juce::GlyphArrangement::getStringWidthInt (font (11.0f), clickHintText()) + 20;
    }

    void SimpleView::ClickHint::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (colours::raisedHi);
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (colours::violet.withAlpha (isMouseOver() ? 0.9f : 0.55f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        g.setColour (colours::textSoft);
        g.setFont (font (11.0f));
        g.drawText (clickHintText(), getLocalBounds(), juce::Justification::centred, false);
    }

    void SimpleView::updateTip (bool busy, double dt)
    {
        auto& vc = proc.getVocalCheck();
        if (tipId.isNotEmpty())
        {
            // The finding went away or was fixed elsewhere (the tab, an undo): he stops
            if (busy || vc.finding (tipId) == nullptr || ! vc.canApply (tipId))
                endTip();
            return;
        }
        tipWait -= dt;
        if (busy || tipWait > 0.0 || vc.score() < 0)
            return;
        for (const auto* f : vc.visibleFindings())
            if (! tipsDone.contains (f->id) && vc.canApply (f->id))
            {
                showTip (f->id);
                return;
            }
    }

    void SimpleView::showTip (const juce::String& id)
    {
        const auto* f = proc.getVocalCheck().finding (id);
        if (f == nullptr)
            return;
        tipId = id;
        tipsDone.add (id);
        setSay (i18n::format (tr ("{0} - soll ich das richten?", "{0} - shall I fix it?"), { f->problem }));
        for (auto* b : { &analyse, &details })
            b->setVisible (false);
        for (auto* b : { &tipYes, &tipShow })
            b->setVisible (true);
        layoutBubble();
    }

    void SimpleView::endTip()
    {
        tipId.clear();
        tipWait = 2.5;   // one after the other, with a breath between
        for (auto* b : { &tipYes, &tipShow })
            b->setVisible (false);
        for (auto* b : { &analyse, &details })
            b->setVisible (true);
        updateBubble (false, silentFor > mascot::Brain::silenceToIdle);
        layoutBubble();
    }

    void SimpleView::layoutBubble()
    {
        if (bubbleRow.isEmpty())
            return;
        // Width: the text's longest line (at most the row), never narrower than the two buttons
        const int buttonsW = 168 + 8 + 86;
        const int maxTextW = bubbleRow.getWidth() - 2 * bubblePadX;
        juce::GlyphArrangement ga;
        ga.addJustifiedText (font (12.5f), say, 0.0f, 0.0f, (float) maxTextW, juce::Justification::topLeft);
        const auto box = ga.getBoundingBox (0, -1, true);
        const int lines = juce::jlimit (1, 3, juce::roundToInt (box.getHeight() / font (12.5f).getHeight()));
        const int textW = juce::jlimit (buttonsW, maxTextW, (int) std::ceil (box.getWidth()) + 2);
        const int h = 2 * bubblePadY + lines * bubbleLineH + bubbleGap + bubbleActionsH;
        bubble = bubbleRow.withTop (bubbleRow.getBottom() - h).withWidth (textW + 2 * bubblePadX);

        auto inner = bubble.reduced (bubblePadX, bubblePadY);
        bubbleText = inner.removeFromTop (lines * bubbleLineH);
        inner.removeFromTop (bubbleGap);
        auto actions = inner.removeFromTop (bubbleActionsH);
        analyse.setBounds (actions.removeFromLeft (168));
        tipYes.setBounds (analyse.getBounds());
        actions.removeFromLeft (8);
        details.setBounds (actions.removeFromLeft (86));
        tipShow.setBounds (details.getBounds());
    }

    void SimpleView::resized()
    {
        auto r = getLocalBounds();
        meterStrip = r.removeFromBottom (36);
        meters.setBounds (meterStrip.reduced (16, 0));
        r.removeFromBottom (8);
        macroPanel = r.removeFromRight (372);
        r.removeFromRight (8);
        spectrumPanel = r;

        // Vorher | Nachher on top, the spectrum, the devil with his speech bubble at the bottom
        auto inner = spectrumPanel.reduced (14, 14);
        auto top = inner.removeFromTop (26);
        statusRow = top;
        inner.removeFromTop (10);
        auto row = inner.removeFromBottom (104);
        inner.removeFromBottom (12);
        spectrumView.setBounds (inner.reduced (1));
        legend.setBounds (spectrumView.getBounds().reduced (8).removeFromTop (Legend::height).removeFromRight (legend.getIdealWidth()));

        auto seg = top.removeFromRight (164).reduced (3, 2);
        before.setBounds (seg.removeFromLeft (seg.getWidth() / 2));
        after.setBounds (seg);
        top.removeFromRight (8);
        fair.setBounds (top.removeFromRight (104).reduced (3, 2));

        // He stands on the panel's bottom padding, a little taller than the row (over the empty low end
        // of the spectrum); arms, the lying wake-up and the spin jump reach further, so his component
        // covers more and passes clicks beside the figure through
        const auto place = row.removeFromLeft (104).withTop (row.getY() - 28).withBottom (row.getBottom() + 6).toFloat();
        const auto reach = place.expanded (52.0f, 0.0f).withTop (place.getY() - 40.0f)
                                .getIntersection (spectrumPanel.reduced (2).toFloat()).getSmallestIntegerContainer();
        mascot.setBounds (reach);
        mascot.setStandArea (place - reach.getPosition().toFloat());
        row.removeFromLeft (8);
        bubbleRow = row.withTrimmedBottom (8);   // the click chip below
        layoutBubble();

        // The Vocal Check score at his head, top right of the figure
        if (scoreBadge != nullptr)
        {
            const int bw = juce::GlyphArrangement::getStringWidthInt (semiBold (11.0f), scoreBadge->getButtonText()) + 16;
            scoreBadge->setBounds (juce::Rectangle<int> (bw, 18).withPosition (juce::roundToInt (place.getRight()) - bw / 2,
                                                                                juce::roundToInt (place.getY()) + 34)
                                       .constrainedWithin (spectrumPanel.reduced (4)));
        }

        // "Klick mich = Vocal Check" under his feet, until the first click
        const auto standInView = place;
        const int chipW = clickHint.getIdealWidth();
        clickHint.setBounds (juce::Rectangle<int> (chipW, 16).withCentre ({ juce::roundToInt (standInView.getCentreX()), 0 })
                                 .withBottomY (spectrumPanel.getBottom() - 4)
                                 .constrainedWithin (spectrumPanel.reduced (4)));
        clickHint.setVisible (! settings::getBool (settings::mascotClicked));

        // Three groups of two faders: a heading, then each fader with its note line under it
        auto col = macroPanel.reduced (18, 18);
        const int groupH = col.getHeight() / 3;
        noteAreas.clear();
        for (int gi = 0; gi < 3; ++gi)
        {
            auto group = col.removeFromTop (groupH);
            groupHeads[(size_t) gi] = group.removeFromTop (20);
            const int rowH = group.getHeight() / 2;
            for (int k = 0; k < 2; ++k)
            {
                auto row = group.removeFromTop (rowH);
                auto& s = *macros[(size_t) (gi * 2 + k)];
                s.setBounds (row.removeFromTop (std::min (34, row.getHeight() - 16)));
                noteAreas.push_back (row.removeFromTop (16).withTrimmedLeft (14));
            }
        }
    }
}
