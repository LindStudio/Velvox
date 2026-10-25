#include "TutorialOverlay.h"

namespace velvox::ui
{
    namespace
    {
        constexpr int welcomeW = 560, welcomeH = 330;
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
            { "tabs", "AUTO MIX and VOCAL CHECK",
              "AUTO MIX is your mix (simple or advanced). VOCAL CHECK is a diagnosis of the recording with suggestions; it never changes the sound by itself." },
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

        for (auto* b : { &startButton, &pluginButton, &laterButton, &backButton, &nextButton, &skipButton })
            addChildComponent (b);

        startButton.setToggleState (true, juce::dontSendNotification);   // lit = primary action
        nextButton.setToggleState (true, juce::dontSendNotification);
        startButton.onClick = [this] { goTo (0); };
        pluginButton.onClick = [this] { finish (Outcome::skipped); };
        laterButton.onClick = [this] { finish (Outcome::later); };
        backButton.onClick = [this] { goTo (step - 1); };
        nextButton.onClick = [this]
        {
            if (step + 1 >= (int) steps().size())
                finish (Outcome::finished);
            else
                goTo (step + 1);
        };
        skipButton.onClick = [this] { finish (Outcome::skipped); };

        startButton.setTooltip ("A short tour of the interface (about one minute)");
        pluginButton.setTooltip ("Close and do not show this again - you can start it from Help any time");
        laterButton.setTooltip ("Close for now and show it again the next time Headliner opens");
        skipButton.setTooltip ("End the tour - you can start it again from Help");
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
        for (auto* b : { &startButton, &pluginButton, &laterButton })
            b->setVisible (welcome && isVisible());
        for (auto* b : { &backButton, &nextButton, &skipButton })
            b->setVisible (! welcome && isVisible());

        const auto card = cardBounds();
        if (welcome)
        {
            auto row = card.reduced (space::xxl, space::xl).removeFromBottom (36);
            startButton.setBounds (row.removeFromRight (150));
            row.removeFromRight (space::s);
            pluginButton.setBounds (row.removeFromRight (150));
            laterButton.setBounds (row.removeFromLeft (150));
            return;
        }

        backButton.setEnabled (step > 0);
        nextButton.setButtonText (step + 1 >= (int) steps().size() ? "Finish" : "Next");
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
            drawCaption (g, "Welcome", r.removeFromTop (20.0f), juce::Justification::centredLeft, colours::teal, typesize::caption);
            drawBrand (g, Brand::wordmark, r.removeFromTop (40.0f), juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid);
            g.setGradientFill (auroraGradient (r.getTopLeft(), r.getTopLeft().translated (48.0f, 0.0f)));
            g.fillRoundedRectangle (r.getX(), r.getY() + 6.0f, 48.0f, 2.0f, 1.0f);
            r.removeFromTop (22.0f);
            layoutBody ("Headliner is your vocal processing system for modern EDM, hypertechno and hard dance vocals. "
                        "Shape your voice with individual modules, or find the sound you are looking for in seconds.\n\n"
                        "Would you like a one-minute tour of the interface?",
                        r.getWidth(), typesize::lead).draw (g, r.withTrimmedBottom (52.0f));
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
        drawCaption (g, "Step " + juce::String (step + 1) + " of " + juce::String ((int) steps().size()),
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
