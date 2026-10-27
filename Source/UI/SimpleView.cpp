#include "SimpleView.h"
#include "../PluginProcessor.h"
#include "../Text/I18n.h"

namespace velvox::ui
{
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

    }

    const std::vector<SimpleView::MacroDef>& SimpleView::macroDefs()
    {
        static const std::vector<MacroDef> defs {
            { pid::macroPresence,   "Presence",    0xff00d1ff },
            { pid::macroSpace,      "Space",       0xff3b82f6 },
            { pid::macroWidth,      "Width",       0xff6366ff },
            { pid::macroAggression, "Punch",       0xff8b5cf6 },
            { pid::macroWarmth,     "Warmth",      0xffd946ef },
            { pid::outputGain,      "Vocal Level", 0xfff472b6 },
        };
        return defs;
    }

    SimpleView::SimpleView (VelvoxAudioProcessor& p, const SpectrumSource* spectrum)
        : proc (p), spectrumView (spectrum)
    {
        spectrumView.setInputDashed (true);
        spectrumView.setRange (-84.0f, -6.0f);
        addAndMakeVisible (spectrumView);

        analyse.getProperties().set ("primary", true);
        analyse.setTooltip (tr ("Headliner hört einen Abschnitt deiner Vocal während der Wiedergabe und stellt die Module ein. "
                                "Der Zustand davor bleibt über Vorher / Nachher erreichbar.",
                                "Headliner listens to a section of your vocal during playback and sets up the modules. "
                                "The state before stays available with Before / After."));
        analyse.onClick = [this] { if (onAnalyse) onAnalyse(); };

        for (auto* b : { &before, &after })
        {
            b->getProperties().set ("segment", true);
            b->setColour (juce::TextButton::buttonOnColourId, colours::cyan);
        }
        before.setButtonText (tr ("Vorher", "Before"));
        after.setButtonText (tr ("Nachher", "After"));
        before.setTooltip (tr ("Die Einstellungen vor der Analyse (echter Parameterwechsel)", "The settings before the analysis (a real parameter switch)"));
        after.setTooltip (tr ("Der automatische Mix mit deinen Änderungen", "The automatic mix with your changes"));
        before.onClick = [this] { proc.getAutoMix().showPrevious (true); updateCompare(); };
        after.onClick = [this] { proc.getAutoMix().showPrevious (false); updateCompare(); };

        details.setTooltip (tr ("Was der Auto-Mix gemacht hat, Density und fairer Vergleich", "What the auto mix did, Density and fair comparison"));
        details.onClick = [this] { if (onShowDetails) onShowDetails(); };

        for (auto* b : { &analyse, &before, &after, &details })
            addAndMakeVisible (b);
        meters.setTooltip (tr ("Pegel und Lautheit (LUFS) am Eingang und Ausgang", "Level and loudness (LUFS) at the input and output"));
        addAndMakeVisible (meters);

        mascot.onClick = [this] { if (onVocalCheck) onVocalCheck(); };
        addAndMakeVisible (mascot);

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
        if (before.isVisible() != can)
        {
            before.setVisible (can);
            after.setVisible (can);
            resized();
            repaint();
        }
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
        if (text != say)
        {
            say = text;
            repaint (bubble);
        }
    }

    void SimpleView::refresh (const LiveData& live)
    {
        spectrumView.refresh();
        meters.setLevels (live.inputDb.ago (0), live.outputDb.ago (0), live.inputLufs.ago (0), live.outputLufs.ago (0), live.clipped);
        if (before.isVisible() && before.getToggleState() != proc.getAutoMix().isShowingPrevious())
            updateCompare();

        // The devil: real time between frames (the host may slow the timer down), score twice a second
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
        mascot.tick (in, dt);
        silentFor = in.inputDb > mascot::Brain::signalDb ? 0.0 : silentFor + dt;
        updateBubble (in.autoMixBusy, silentFor > mascot::Brain::silenceToIdle);
        if (in.autoMixBusy != wasBusy)
        {
            wasBusy = in.autoMixBusy;
            repaint (statusRow);
        }
    }

    void SimpleView::paint (juce::Graphics& g)
    {
        drawPanel (g, spectrumPanel);
        drawPanel (g, macroPanel);
        drawPanel (g, meterStrip);

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
            const auto dot = busy ? colours::cyan : mixed ? colours::ok : colours::textFaint;
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

        auto text = bubble.reduced (14, 10);
        const auto actions = text.removeFromBottom (30);
        g.setColour (colours::text);
        g.setFont (font (12.5f));
        g.drawFittedText (say, text, juce::Justification::topLeft, 3, 1.0f);
        g.setColour (colours::textDim);
        g.setFont (font (10.0f));
        g.drawText (tr ("Klick auf mich = Vocal Check", "Click me = Vocal Check"),
                    actions.withLeft (details.getRight() + 8), juce::Justification::centredRight, true);
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

        auto seg = top.removeFromRight (164).reduced (3, 2);
        before.setBounds (seg.removeFromLeft (seg.getWidth() / 2));
        after.setBounds (seg);

        // He stands on the panel's bottom padding, a little taller than the row (over the empty low end
        // of the spectrum); arms, the lying wake-up and the spin jump reach further, so his component
        // covers more and passes clicks beside the figure through
        const auto place = row.removeFromLeft (104).withTop (row.getY() - 28).withBottom (row.getBottom() + 6).toFloat();
        const auto reach = place.expanded (52.0f, 0.0f).withTop (place.getY() - 40.0f)
                                .getIntersection (spectrumPanel.reduced (2).toFloat()).getSmallestIntegerContainer();
        mascot.setBounds (reach);
        mascot.setStandArea (place - reach.getPosition().toFloat());
        row.removeFromLeft (8);
        bubble = row;

        auto actions = bubble.reduced (14, 10).removeFromBottom (30);
        analyse.setBounds (actions.removeFromLeft (168));
        actions.removeFromLeft (8);
        details.setBounds (actions.removeFromLeft (86));

        // Six faders, evenly spaced
        auto col = macroPanel.reduced (18, 22);
        const int rowH = col.getHeight() / (int) macros.size();
        for (auto& s : macros)
            s->setBounds (col.removeFromTop (rowH).withSizeKeepingCentre (col.getWidth(), std::min (rowH, 34)));
    }
}
