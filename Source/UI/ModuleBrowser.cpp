#include "ModuleBrowser.h"
#include "../PluginProcessor.h"
#include "HelpContent.h"
#include "Icons.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        constexpr int numCategories = 7;
        constexpr int columns = 3;
        constexpr int cardH = 112;
        constexpr int cardGap = 14;

        // Display order inside the browser: the usual order of a vocal chain
        constexpr ModuleId browserOrder[dsp::numModules] {
            ModuleId::eq, ModuleId::deess, ModuleId::smooth,
            ModuleId::ride, ModuleId::comp, ModuleId::smash,
            ModuleId::sat, ModuleId::doubler, ModuleId::choir,
            ModuleId::delay, ModuleId::reverb,
            ModuleId::pitch, ModuleId::chop, ModuleId::pulse,
            ModuleId::check
        };

        // A few extra words people search for
        const char* keywordsFor (ModuleId id)
        {
            switch (id)
            {
                case ModuleId::eq:      return "equalizer filter low cut high cut tone presence air mud";
                case ModuleId::comp:    return "compressor dynamics level punch";
                case ModuleId::deess:   return "deesser sibilance s t harsh";
                case ModuleId::sat:     return "saturation drive distortion warmth tape tube harmonics";
                case ModuleId::doubler: return "double width stereo thick";
                case ModuleId::pitch:   return "pitch shift transpose formant";
                case ModuleId::chop:    return "chop gate stutter rhythm trance";
                case ModuleId::delay:   return "delay echo ping pong tempo space";
                case ModuleId::reverb:  return "reverb hall room plate space depth atmosphere";
                case ModuleId::pulse:   return "pulse pump volume shaper sidechain rhythm";
                case ModuleId::smash:   return "smash parallel compression density loud";
                case ModuleId::choir:   return "choir voices unison wide";
                case ModuleId::smooth:  return "smooth resonance harsh soothe";
                case ModuleId::check:   return "check analysis diagnose meter";
                case ModuleId::ride:    return "ride level rider gain automation";
            }
            return "";
        }
    }

    ModuleBrowser::Category ModuleBrowser::categoryOf (ModuleId id) noexcept
    {
        switch (id)
        {
            case ModuleId::eq: case ModuleId::deess: case ModuleId::smooth:       return Category::cleanTone;
            case ModuleId::ride: case ModuleId::comp: case ModuleId::smash:       return Category::dynamics;
            case ModuleId::sat: case ModuleId::doubler: case ModuleId::choir:     return Category::colourWidth;
            case ModuleId::delay: case ModuleId::reverb:                          return Category::space;
            case ModuleId::pitch: case ModuleId::chop: case ModuleId::pulse:      return Category::creative;
            case ModuleId::check:                                                 return Category::analysis;
        }
        return Category::all;
    }

    const char* ModuleBrowser::categoryName (Category c) noexcept
    {
        switch (c)
        {
            case Category::all:         return "All";
            case Category::cleanTone:   return "Clean & Tone";
            case Category::dynamics:    return "Dynamics";
            case Category::colourWidth: return "Colour & Width";
            case Category::space:       return "Space";
            case Category::creative:    return "Creative";
            case Category::analysis:    return "Analysis";
        }
        return "";
    }

    ModuleBrowser::ModuleBrowser (VelvoxAudioProcessor& p) : proc (p)
    {
        setWantsKeyboardFocus (true);
        search.setTextToShowWhenEmpty ("Search modules - e.g. reverb, harsh, width", colours::textFaint);
        search.setFont (font (15.0f));
        search.setIndents (14, 0);
        search.setJustification (juce::Justification::centredLeft);
        search.addListener (this);
        addAndMakeVisible (search);
        closeButton.onClick = [this] { close(); };
        addAndMakeVisible (closeButton);
    }

    void ModuleBrowser::open (int index, bool end)
    {
        insertIndex = index;
        atEnd = end;
        search.clear();
        category = Category::all;
        setVisible (true);
        toFront (true);
        relayout();
        search.grabKeyboardFocus();
    }

    void ModuleBrowser::close()
    {
        setVisible (false);
    }

    int ModuleBrowser::freeInstances (ModuleId id) const
    {
        const auto r = proc.getRouting();
        return dsp::maxInstances (id) - r.countOf (id);
    }

    void ModuleBrowser::setCategory (Category c)
    {
        category = c;
        relayout();
    }

    void ModuleBrowser::setSearchText (const juce::String& text)
    {
        search.setText (text, juce::dontSendNotification);
        relayout();
    }

    std::vector<ModuleId> ModuleBrowser::visibleModules() const
    {
        std::vector<ModuleId> out;
        const auto words = juce::StringArray::fromTokens (search.getText().toLowerCase(), " ", "");
        for (auto id : browserOrder)
        {
            if (category != Category::all && categoryOf (id) != category)
                continue;
            const auto haystack = (juce::String (dsp::moduleDisplayName (id)) + " " + help::shortDescription ((int) id) + " "
                                   + keywordsFor (id) + " " + categoryName (categoryOf (id))).toLowerCase();
            bool match = true;
            for (const auto& w : words)
                match = match && haystack.contains (w);
            if (match)
                out.push_back (id);
        }
        return out;
    }

    juce::Rectangle<int> ModuleBrowser::panelBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (std::min (1040, getWidth() - 80), std::min (700, getHeight() - 60));
    }

    juce::Rectangle<int> ModuleBrowser::chipBounds (int index) const
    {
        const auto p = panelBounds().reduced (space::xxl, 0);
        int x = p.getX();
        const int y = p.getY() + 132;
        for (int i = 0; i < index; ++i)
            x += juce::GlyphArrangement::getStringWidthInt (medium (13.5f), categoryName ((Category) i)) + 36 + space::s;
        const int w = juce::GlyphArrangement::getStringWidthInt (medium (13.5f), categoryName ((Category) index)) + 36;
        return { x, y, w, 32 };
    }

    juce::Rectangle<int> ModuleBrowser::cardBoundsFor (ModuleId id) const
    {
        for (const auto& c : cards)
            if (c.id == id)
                return c.bounds;
        return {};
    }

    void ModuleBrowser::relayout()
    {
        cards.clear();
        const auto p = panelBounds().reduced (space::xxl, 0);
        auto grid = p.withTrimmedTop (184).withTrimmedBottom (space::xl);
        const int w = (grid.getWidth() - cardGap * (columns - 1)) / columns;
        int i = 0;
        for (auto id : visibleModules())
        {
            const int col = i % columns, row = i / columns;
            cards.push_back ({ id, { grid.getX() + col * (w + cardGap), grid.getY() + row * (cardH + cardGap), w, cardH } });
            ++i;
        }
        hoveredCard = -1;
        repaint();
    }

    void ModuleBrowser::resized()
    {
        const auto p = panelBounds().reduced (space::xxl, 0);
        closeButton.setBounds (p.getRight() - 96, p.getY() + 28, 96, 34);
        search.setBounds (p.getX(), p.getY() + 84, p.getWidth(), 38);
        relayout();
    }

    void ModuleBrowser::paint (juce::Graphics& g)
    {
        g.fillAll (colours::scrim);
        const auto panel = panelBounds().toFloat();
        drawGlow (g, { panel.getX() + 140.0f, panel.getY() }, 420.0f, 0.10f, colours::teal);
        drawGlow (g, { panel.getRight() - 140.0f, panel.getY() }, 420.0f, 0.08f, colours::violet);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (panel, radius::panel);
        drawGlass (g, panel, radius::panel, 0.03f);

        auto p = panelBounds().reduced (space::xxl, 0);
        drawCaption (g, "Module browser", p.withY (p.getY() + 22).withHeight (16).toFloat(), juce::Justification::centredLeft, colours::teal, typesize::caption);
        g.setColour (colours::text);
        g.setFont (font (typesize::title + 4.0f, true));
        g.drawText ("Add a module", p.withY (p.getY() + 38).withHeight (32), juce::Justification::centredLeft, false);
        g.setColour (colours::textDim);
        g.setFont (font (typesize::body));
        const auto where = atEnd ? juce::String ("at the end of the chain") : "at position " + juce::String (insertIndex + 1) + " of the chain";
        g.drawText ("Inserted " + where + ". Every module can be used more than once - each instance has its own settings.",
                    p.withY (p.getY() + 38).withHeight (32).withTrimmedLeft (220).withTrimmedRight (110), juce::Justification::centredLeft, true);

        // Category chips (text plus a filled state - never colour alone)
        for (int i = 0; i < numCategories; ++i)
        {
            const auto r = chipBounds (i).toFloat();
            const bool active = (int) category == i, hov = hoveredChip == i;
            g.setColour (active ? colours::cyan.withAlpha (0.18f) : colours::accent.withAlpha (hov ? 0.06f : 0.025f));
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (active ? colours::cyan.withAlpha (0.8f) : colours::line.brighter (hov ? 0.6f : 0.3f));
            g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, active ? 1.4f : 1.0f);
            g.setColour (active ? colours::text : colours::textDim);
            g.setFont (medium (13.5f));
            g.drawText (categoryName ((Category) i), r, juce::Justification::centred, false);
        }

        if (cards.empty())
        {
            g.setColour (colours::textDim);
            g.setFont (font (typesize::body));
            g.drawText ("No module matches \"" + search.getText() + "\".", p.withTrimmedTop (200).withHeight (40), juce::Justification::centred, false);
        }

        const auto routing = proc.getRouting();
        for (int i = 0; i < (int) cards.size(); ++i)
        {
            const auto& c = cards[(size_t) i];
            const auto r = c.bounds.toFloat();
            const bool analysis = categoryOf (c.id) == Category::analysis;
            const int used = routing.countOf (c.id), limit = dsp::maxInstances (c.id);
            const bool full = used >= limit;
            const bool hov = hoveredCard == i && ! full;

            // Body: processing modules are filled glass cards, the analysis tool is outlined
            if (analysis)
            {
                juce::Path outline;
                outline.addRoundedRectangle (r.reduced (0.75f), radius::tile);
                juce::Path dashed;
                const float dashes[] { 6.0f, 4.0f };
                juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
                g.setColour (colours::accent.withAlpha (hov ? 0.05f : 0.015f));
                g.fillRoundedRectangle (r, radius::tile);
                g.setColour (colours::line.brighter (hov ? 0.8f : 0.45f));
                g.fillPath (dashed);
            }
            else
            {
                g.setColour (full ? colours::panel : (hov ? colours::raisedHi : colours::raised));
                g.fillRoundedRectangle (r, radius::tile);
                if (! full)
                {
                    const float t = (float) ((int) categoryOf (c.id) - 1) / 5.0f;
                    g.setGradientFill (juce::ColourGradient (aurora (t, hov ? 0.16f : 0.08f), r.getX(), r.getY(),
                                                             aurora (t, 0.0f), r.getRight(), r.getBottom(), false));
                    g.fillRoundedRectangle (r, radius::tile);
                }
                g.setColour (hov ? colours::cyan.withAlpha (0.8f) : colours::line.brighter (full ? 0.05f : 0.35f));
                g.drawRoundedRectangle (r.reduced (0.5f), radius::tile, hov ? 1.5f : 1.0f);
            }

            const auto iconBox = juce::Rectangle<float> (40.0f, 40.0f).withPosition (r.getX() + 16.0f, r.getY() + 16.0f);
            g.setColour (colours::background.withAlpha (0.6f));
            g.fillRoundedRectangle (iconBox, 10.0f);
            drawModuleIcon (g, (int) c.id, iconBox.reduced (9.0f), full ? colours::textFaint : colours::cyan.interpolatedWith (colours::text, 0.2f));

            const float tx = iconBox.getRight() + 14.0f;
            g.setColour (full ? colours::textDim : colours::text);
            g.setFont (font (16.0f, true));
            g.drawText (dsp::moduleDisplayName (c.id), juce::Rectangle<float> (tx, r.getY() + 14.0f, r.getRight() - tx - 12.0f, 22.0f),
                        juce::Justification::centredLeft, false);
            drawCaption (g, analysis ? "Analysis tool" : categoryName (categoryOf (c.id)),
                         juce::Rectangle<float> (tx, r.getY() + 38.0f, r.getRight() - tx - 12.0f, 14.0f),
                         juce::Justification::centredLeft, analysis ? colours::teal : colours::textFaint, 9.0f);

            g.setColour (colours::textDim);
            g.setFont (font (13.0f));
            const auto text = analysis ? help::shortDescription ((int) c.id) + ". Never changes the sound."
                                       : help::shortDescription ((int) c.id);
            g.drawFittedText (text, juce::Rectangle<float> (r.getX() + 16.0f, r.getY() + 62.0f, r.getWidth() - 32.0f, 34.0f).toNearestInt(),
                              juce::Justification::topLeft, 2, 0.95f);

            // Instance status (top right)
            juce::String status;
            if (full)
                status = limit == 1 ? "In the chain" : "All " + juce::String (limit) + " in use";
            else if (used > 0)
                status = juce::String (used) + " of " + juce::String (limit) + " in chain";
            if (status.isNotEmpty())
            {
                g.setFont (medium (11.0f));
                const float w = (float) juce::GlyphArrangement::getStringWidthInt (medium (11.0f), status) + 16.0f;
                const auto badge = juce::Rectangle<float> (w, 20.0f).withPosition (r.getRight() - w - 12.0f, r.getY() + 14.0f);
                g.setColour (full ? colours::accent.withAlpha (0.04f) : colours::cyan.withAlpha (0.12f));
                g.fillRoundedRectangle (badge, 10.0f);
                g.setColour (full ? colours::textFaint : colours::text);
                g.drawText (status, badge, juce::Justification::centred, false);
            }
            if (hov)
                drawCaption (g, used > 0 ? "Click to add another instance" : "Click to add",
                             juce::Rectangle<float> (r.getX() + 16.0f, r.getBottom() - 20.0f, r.getWidth() - 32.0f, 14.0f),
                             juce::Justification::centredRight, colours::cyan, 9.0f);
        }
    }

    void ModuleBrowser::mouseMove (const juce::MouseEvent& e)
    {
        int card = -1, chip = -1;
        for (int i = 0; i < (int) cards.size(); ++i)
            if (cards[(size_t) i].bounds.contains (e.getPosition()))
                card = i;
        for (int i = 0; i < numCategories; ++i)
            if (chipBounds (i).contains (e.getPosition()))
                chip = i;
        if (card != hoveredCard || chip != hoveredChip)
        {
            hoveredCard = card;
            hoveredChip = chip;
            const bool clickable = chip >= 0 || (card >= 0 && freeInstances (cards[(size_t) card].id) > 0);
            setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void ModuleBrowser::mouseExit (const juce::MouseEvent&)
    {
        hoveredCard = hoveredChip = -1;
        repaint();
    }

    void ModuleBrowser::mouseDown (const juce::MouseEvent& e)
    {
        if (! panelBounds().contains (e.getPosition()))
        {
            close();
            return;
        }
        for (int i = 0; i < numCategories; ++i)
            if (chipBounds (i).contains (e.getPosition()))
            {
                setCategory ((Category) i);
                return;
            }
        for (const auto& c : cards)
            if (c.bounds.contains (e.getPosition()))
            {
                choose (c.id);
                return;
            }
    }

    void ModuleBrowser::choose (ModuleId id)
    {
        if (freeInstances (id) <= 0)
            return;
        close();
        if (onChoose)
            onChoose (id, insertIndex, atEnd);
    }

    void ModuleBrowser::textEditorReturnKeyPressed (juce::TextEditor&)
    {
        for (auto id : visibleModules())
            if (freeInstances (id) > 0)
            {
                choose (id);
                return;
            }
    }

    bool ModuleBrowser::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            close();
            return true;
        }
        return false;
    }
}
