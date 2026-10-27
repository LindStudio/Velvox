#include "TutorialOverlay.h"

namespace velvox::ui
{
    namespace
    {
        constexpr int welcomeW = 540, welcomeH = 392;
        constexpr int cardW = 380;
        constexpr float holePad = 6.0f;

        juce::TextLayout layoutBody (const juce::String& text, float width, float size)
        {
            juce::AttributedString s;
            s.setJustification (juce::Justification::topLeft);
            s.setLineSpacing (2.5f);
            s.append (text, font (size), colours::text.withAlpha (0.78f));
            juce::TextLayout l;
            l.createLayout (s, width);
            return l;
        }

        int stepCardHeight (const TutorialOverlay::Step& s)
        {
            const auto body = layoutBody (s.text, (float) cardW - 2.0f * space::xl, typesize::body + 0.5f);
            return space::xl + 16 + 24 + space::s + (int) std::ceil (body.getHeight()) + space::l + 32 + space::l;
        }
    }

    const std::vector<TutorialOverlay::Step>& TutorialOverlay::steps()
    {
        static const std::vector<Step> s {
            { "modes", "SIMPEL and ADVANCED",
              "SIMPEL shows the spectrum of your vocal and six macro faders - enough for most mixes. ADVANCED shows the module chain with the full panel of the selected module. Switching never changes a value." },
            { "analyse", "Vocal analysieren",
              "Play your vocal in the DAW and click Vocal analysieren: Headliner listens to a section and sets up the modules. Afterwards Vorher / Nachher compares with the state before, Details shows what was done." },
            { "macros", "The six macros",
              "Presence, Space, Width, Punch, Warmth and Vocal Level move several modules at once. Drag a fader, hold Shift for fine moves, double-click to reset, click the value to type it." },
            { "presets", "Presets",
              "The arrows step through the presets; click the name for the list with search, categories, favourites and your own presets (save, rename, delete)." },
            { "tabs", "Your mix and VOCAL CHECK",
              "The first tab is your mix: AUTO MIX in simple mode, MODULE (the chain) in advanced mode. VOCAL CHECK is a diagnosis of the recording with suggestions; it never changes the sound by itself." },
            { "chain", "The chain (ADVANCED)",
              "Each tile is one module in processing order: name, icon, on/off. Click a tile to open its panel, drag it to reorder, right-click for duplicate, remove, reset, Als Send / Als Insert and undo. The + tile adds a module." },
            { "body", "Module panels",
              "A large live picture on top, the main controls below, everything else behind Mehr. Rest the mouse on a control to read what it does. Ctrl+Z undoes any change." },
            { "global", "Bypass",
              "Bypass plays the unprocessed vocal; an amber frame shows it is active. Set the fair compare (Details) to Bypass match so loudness does not fool you." },
            { "help", "Help",
              "The complete guide, offline. Inside every module the ? button opens the page for that module." },
        };
        return s;
    }

    TutorialOverlay::TutorialOverlay()
    {
        setVisible (false);
        setWantsKeyboardFocus (true);

        for (auto* b : { &germanButton, &englishButton, &analyseButton, &startButton, &pluginButton, &backButton, &nextButton, &skipButton })
            addChildComponent (b);

        using i18n::tr;
        analyseButton.setButtonText (tr ("Vocal analysieren", "Analyze vocal"));
        startButton.setButtonText (tr ("Kurze Tour (1 Minute)", "Short tour (1 minute)"));
        pluginButton.setButtonText (tr ("Direkt zum Plugin", "Go to the plugin"));
        backButton.setButtonText (tr ("Zurück", "Back"));
        nextButton.setButtonText (tr ("Weiter", "Next"));
        skipButton.setButtonText (tr ("Beenden", "Skip"));

        // Language: two large segments, the current language lit
        germanButton.setConnectedEdges (juce::Button::ConnectedOnRight);
        englishButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
        germanButton.setToggleState (i18n::isGerman(), juce::dontSendNotification);
        englishButton.setToggleState (! i18n::isGerman(), juce::dontSendNotification);
        germanButton.onClick = [this] { if (onLanguage) onLanguage (i18n::Language::german); };
        englishButton.onClick = [this] { if (onLanguage) onLanguage (i18n::Language::english); };

        analyseButton.getProperties().set ("primary", true);
        nextButton.setToggleState (true, juce::dontSendNotification);   // lit = primary action
        analyseButton.onClick = [this] { finish (Outcome::analyse); };
        startButton.onClick = [this] { goTo (0); };
        pluginButton.onClick = [this] { finish (Outcome::skipped); };
        backButton.onClick = [this] { goTo (step - 1); };
        nextButton.onClick = [this]
        {
            if (step + 1 >= (int) steps().size())
                finish (Outcome::finished);
            else
                goTo (step + 1);
        };
        skipButton.onClick = [this] { finish (Outcome::skipped); };

        analyseButton.setTooltip (tr ("Spiel die Vocal in der DAW ab - Headliner hört zu und stellt die Module ein",
                                      "Play the vocal in your DAW - Headliner listens and sets up the modules"));
        startButton.setTooltip (tr ("Ein kurzer Rundgang durch die Oberfläche",
                                    "A short tour of the interface"));
        pluginButton.setTooltip (tr ("Schließen - die Tour startest du jederzeit in den Einstellungen oder der Hilfe",
                                     "Close - start the tour any time from the settings or Help"));
        skipButton.setTooltip (tr ("Tour beenden - du kannst sie jederzeit neu starten",
                                   "End the tour - you can start it again any time"));
    }

    void TutorialOverlay::start (bool withWelcome)
    {
        spotValid = false;
        setVisible (true);
        toFront (true);
        goTo (withWelcome ? -1 : 0);
    }

    void TutorialOverlay::goTo (int index)
    {
        step = juce::jlimit (-1, (int) steps().size() - 1, index);
        target = step >= 0 && targetFor ? targetFor (steps()[(size_t) step].target) : juce::Rectangle<int>();

        if (! target.isEmpty())
        {
            const float v[] { (float) target.getX(), (float) target.getY(), (float) target.getWidth(), (float) target.getHeight() };
            for (size_t i = 0; i < 4; ++i)
            {
                if (spotValid && isShowing()) spot[i].target = v[i];   // glide while on screen
                else spot[i].snap (v[i]);
            }
            spotValid = true;
        }

        layoutButtons();
        repaint();
        if (isShowing())
            grabKeyboardFocus();
    }

    void TutorialOverlay::finish (Outcome outcome)
    {
        setVisible (false);
        step = -1;
        if (onClose)
            onClose (outcome);
    }

    juce::Rectangle<float> TutorialOverlay::spotlight() const
    {
        return juce::Rectangle<float> (spot[0].current, spot[1].current, spot[2].current, spot[3].current).expanded (holePad);
    }

    juce::Rectangle<int> TutorialOverlay::cardBounds() const
    {
        const auto area = getLocalBounds().reduced (space::l);
        if (step < 0)
            return getLocalBounds().withSizeKeepingCentre (welcomeW, welcomeH);

        const int h = stepCardHeight (steps()[(size_t) step]);
        const auto t = target.expanded ((int) holePad);
        juce::Rectangle<int> c (cardW, h);

        if (t.getBottom() + space::l + h <= area.getBottom())
            c.setPosition (t.getCentreX() - cardW / 2, t.getBottom() + space::l);
        else if (t.getY() - space::l - h >= area.getY())
            c.setPosition (t.getCentreX() - cardW / 2, t.getY() - space::l - h);
        else   // tall target: inside it, near its top
            c.setPosition (t.getCentreX() - cardW / 2, t.getY() + space::xl);

        return c.constrainedWithin (area);
    }

    void TutorialOverlay::layoutButtons()
    {
        const bool welcome = step < 0;
        for (auto* b : { &germanButton, &englishButton, &analyseButton, &startButton, &pluginButton })
            b->setVisible (welcome && isVisible());
        for (auto* b : { &backButton, &nextButton, &skipButton })
            b->setVisible (! welcome && isVisible());

        const auto card = cardBounds();
        if (welcome)
        {
            // Bottom up: secondary row, primary action, divider, language
            auto r = card.reduced (space::xxl, space::xl);
            auto secondary = r.removeFromBottom (34);
            startButton.setBounds (secondary.removeFromLeft ((secondary.getWidth() - space::s) / 2));
            secondary.removeFromLeft (space::s);
            pluginButton.setBounds (secondary);
            r.removeFromBottom (space::s);
            analyseButton.setBounds (r.removeFromBottom (42));
            r.removeFromBottom (space::xl + 1);
            auto language = r.removeFromBottom (44);
            germanButton.setBounds (language.removeFromLeft (language.getWidth() / 2));
            englishButton.setBounds (language);
            return;
        }

        backButton.setEnabled (step > 0);
        nextButton.setButtonText (step + 1 >= (int) steps().size() ? i18n::tr ("Fertig", "Finish") : i18n::tr ("Weiter", "Next"));
        auto row = card.reduced (space::xl, space::l).removeFromBottom (32);
        nextButton.setBounds (row.removeFromRight (84));
        row.removeFromRight (space::s);
        backButton.setBounds (row.removeFromRight (70));
        skipButton.setBounds (row.removeFromLeft (64));
    }

    void TutorialOverlay::resized()
    {
        if (step >= 0)
            goTo (step);
        else
            layoutButtons();
    }

    bool TutorialOverlay::hitTest (int x, int y)
    {
        // Welcome is a real choice; during the tour only the card catches the mouse
        return step < 0 || cardBounds().contains (x, y);
    }

    bool TutorialOverlay::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            finish (step < 0 ? Outcome::later : Outcome::skipped);
            return true;
        }
        if (step >= 0 && (key == juce::KeyPress::rightKey || key == juce::KeyPress::returnKey))
        {
            nextButton.triggerClick();
            return true;
        }
        if (step > 0 && key == juce::KeyPress::leftKey)
        {
            goTo (step - 1);
            return true;
        }
        return false;
    }

    void TutorialOverlay::tick()
    {
        if (! isVisible() || step < 0)
            return;
        bool moving = false;
        for (auto& s : spot)
            moving = s.tick (0.22f) || moving;
        if (moving)
            repaint();
    }

    void TutorialOverlay::paint (juce::Graphics& g)
    {
        const auto card = cardBounds().toFloat();

        if (step < 0)
        {
            g.fillAll (colours::scrim);
            drawGlow (g, { card.getX() + 120.0f, card.getY() }, 380.0f, 0.10f, colours::teal);
            drawGlow (g, { card.getRight() - 120.0f, card.getY() }, 380.0f, 0.08f, colours::blue);
            g.setColour (colours::panel);
            g.fillRoundedRectangle (card, radius::panel);
            drawGlass (g, card, radius::panel, 0.03f);

            auto r = card.reduced ((float) space::xxl, (float) space::xl);
            drawCaption (g, i18n::tr ("Willkommen", "Welcome"), r.removeFromTop (20.0f), juce::Justification::centredLeft, colours::teal, typesize::caption);
            drawBrand (g, Brand::wordmark, r.removeFromTop (40.0f), juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
            g.setGradientFill (auroraGradient (r.getTopLeft(), r.getTopLeft().translated (48.0f, 0.0f)));
            g.fillRoundedRectangle (r.getX(), r.getY() + 6.0f, 48.0f, 2.0f, 1.0f);
            r.removeFromTop (22.0f);

            // Text above the language buttons; a divider between language and the start options
            const auto language = germanButton.getBounds().toFloat();
            layoutBody (i18n::tr ("Headliner mischt deine Vocal: Spiel sie ab, Headliner hört zu und stellt die Module ein. "
                                  "Danach ist alles frei einstellbar.\n\n"
                                  "Wähle deine Sprache - du kannst sie jederzeit in den Einstellungen ändern.",
                                  "Headliner mixes your vocal: play it, Headliner listens and sets up the modules. "
                                  "Everything stays adjustable afterwards.\n\n"
                                  "Choose your language - you can change it any time in the settings."),
                        r.getWidth(), typesize::lead).draw (g, r.withBottom (language.getY() - (float) space::m));
            g.setColour (colours::borderSoft);
            g.drawHorizontalLine (juce::roundToInt (language.getBottom() + (float) space::m), r.getX(), r.getRight());
            return;
        }

        // Dim everything except the highlighted area
        const auto hole = spotlight();
        juce::Path dim;
        dim.addRectangle (getLocalBounds().toFloat());
        dim.addRoundedRectangle (hole, radius::card);
        dim.setUsingNonZeroWinding (false);
        g.setColour (colours::background.withAlpha (0.62f));
        g.fillPath (dim);

        g.setGradientFill (auroraGradient (hole.getTopLeft(), hole.getBottomRight(), 0.95f));
        g.drawRoundedRectangle (hole, radius::card, 1.6f);

        // Explanation card
        const auto& s = steps()[(size_t) step];
        drawGlow (g, { card.getX() + 60.0f, card.getY() }, 200.0f, 0.10f, colours::teal);
        g.setColour (colours::raised);
        g.fillRoundedRectangle (card, radius::card);
        drawGlass (g, card, radius::card, 0.03f);

        auto r = card.reduced ((float) space::xl, (float) space::xl);
        drawCaption (g, i18n::format (i18n::tr ("Schritt {0} von {1}", "Step {0} of {1}"), { juce::String (step + 1), juce::String ((int) steps().size()) }),
                     r.removeFromTop (16.0f), juce::Justification::centredLeft, colours::teal, typesize::caption);
        g.setColour (colours::text);
        g.setFont (font (typesize::title - 2.0f, true));
        g.drawText (s.title, r.removeFromTop (24.0f), juce::Justification::centredLeft, true);
        r.removeFromTop ((float) space::s);
        layoutBody (s.text, r.getWidth(), typesize::body + 0.5f).draw (g, r);

        // Progress dots, right of the skip button
        const int n = (int) steps().size();
        const float dotsW = (float) n * 9.0f;
        const auto skip = skipButton.getBounds().toFloat();
        const float x0 = juce::jmax (skip.getRight() + 10.0f, (float) backButton.getX() - 12.0f - dotsW);
        for (int i = 0; i < n; ++i)
        {
            const auto c = juce::Point<float> (x0 + (float) i * 9.0f + 3.0f, skip.getCentreY());
            g.setColour (i == step ? colours::teal : colours::textFaint.withAlpha (i < step ? 0.9f : 0.45f));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c));
        }
    }
}
