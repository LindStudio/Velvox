#include "ModuleBrowser.h"
#include "../PluginProcessor.h"
#include "HelpContent.h"
#include "Icons.h"
#include "../Text/I18n.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        constexpr int cardW = 108, cardH = 76, cardGap = 8, groupGap = 22, captionH = 18, lineGap = 14;

        // Display order: by category (spec 2.7), the usual order of a vocal chain inside each
        constexpr ModuleId browserOrder[dsp::numModules] {
            ModuleId::eq, ModuleId::deess, ModuleId::smooth,          // Clean-up
            ModuleId::comp, ModuleId::smash, ModuleId::ride,          // Dynamics
            ModuleId::sat,                                            // Colour
            ModuleId::doubler,                                        // Width / Voices
            ModuleId::delay, ModuleId::reverb,                        // Space
            ModuleId::check                                           // Analysis
        };

        // A few extra words people search for, in English and German (the search finds both languages)
        const char* keywordsFor (ModuleId id)
        {
            switch (id)
            {
                case ModuleId::eq:      return "equalizer filter low cut high cut tone presence air mud entzerrer klang höhen tiefen präsenz luft mulmig";
                case ModuleId::comp:    return "compressor dynamics level punch kompressor dynamik pegel";
                case ModuleId::deess:   return "deesser sibilance s t harsh zischlaute s-laute scharf";
                case ModuleId::sat:     return "saturation drive distortion warmth tape tube harmonics sättigung verzerrung wärme band röhre obertöne";
                case ModuleId::doubler: return "double width stereo thick dopplung breite dick";
                case ModuleId::delay:   return "delay echo ping pong tempo space raum";
                case ModuleId::reverb:  return "reverb hall room plate space depth atmosphere raum tiefe atmosphäre";
                case ModuleId::smash:   return "smash parallel compression density loud kompression dichte laut";
                case ModuleId::smooth:  return "smooth resonance harsh soothe resonanz scharf glätten";
                case ModuleId::check:   return "check analysis diagnose meter analyse messen";
                case ModuleId::ride:    return "ride level rider gain automation pegel ausgleich";
            }
            return "";
        }
    }

    // Search field that passes Up / Down (and Left / Right while empty) to the browser
    class ModuleBrowser::SearchField final : public juce::TextEditor
    {
    public:
        explicit SearchField (ModuleBrowser& b) : owner (b) {}

        bool keyPressed (const juce::KeyPress& key) override
        {
            const bool empty = getText().isEmpty();
            if (key == juce::KeyPress::downKey || (empty && key == juce::KeyPress::rightKey)) { owner.moveHighlight (1); return true; }
            if (key == juce::KeyPress::upKey || (empty && key == juce::KeyPress::leftKey))    { owner.moveHighlight (-1); return true; }
            return juce::TextEditor::keyPressed (key);
        }

    private:
        ModuleBrowser& owner;
    };

    ModuleBrowser::Category ModuleBrowser::categoryOf (ModuleId id) noexcept
    {
        switch (id)
        {
            case ModuleId::eq: case ModuleId::deess: case ModuleId::smooth:       return Category::cleanUp;
            case ModuleId::comp: case ModuleId::smash: case ModuleId::ride:       return Category::dynamics;
            case ModuleId::sat:                                                   return Category::colour;
            case ModuleId::doubler:                                               return Category::widthVoices;
            case ModuleId::delay: case ModuleId::reverb:                          return Category::space;
            case ModuleId::check:                                                 return Category::analysis;
        }
        return Category::cleanUp;
    }

    // German and English name of a category
    static std::pair<const char*, const char*> categoryTexts (ModuleBrowser::Category c)
    {
        using Category = ModuleBrowser::Category;
        switch (c)
        {
            case Category::cleanUp:     return { "Aufräumen", "Clean-up" };
            case Category::dynamics:    return { "Dynamik", "Dynamics" };
            case Category::colour:      return { "Farbe", "Color" };
            case Category::widthVoices: return { "Breite / Stimmen", "Width / Voices" };
            case Category::space:       return { "Raum", "Space" };
            case Category::analysis:    return { "Analyse", "Analysis" };
        }
        return { "", "" };
    }

    juce::String ModuleBrowser::categoryName (Category c)
    {
        const auto [german, english] = categoryTexts (c);
        return i18n::tr (german, english);
    }

    ModuleBrowser::ModuleBrowser (VelvoxAudioProcessor& p) : proc (p)
    {
        setWantsKeyboardFocus (true);
        search = std::make_unique<SearchField> (*this);
        search->setTextToShowWhenEmpty (i18n::tr ("Suchen - z. B. Hall, scharf, Breite", "Search - e.g. reverb, harsh, width"), colours::textFaint);
        search->setFont (font (14.0f));
        search->setIndents (12, 0);
        search->setJustification (juce::Justification::centredLeft);
        search->addListener (this);
        addAndMakeVisible (*search);
        setVisible (false);
    }

    ModuleBrowser::~ModuleBrowser() = default;

    void ModuleBrowser::open (int index, bool end)
    {
        insertIndex = index;
        atEnd = end;
        search->clear();
        setVisible (true);
        toFront (true);
        highlighted = -1;
        relayout();
        search->grabKeyboardFocus();
    }

    void ModuleBrowser::close()
    {
        setVisible (false);
    }

    int ModuleBrowser::freeInstances (ModuleId id) const
    {
        return dsp::maxInstances (id) - proc.getRouting().countOf (id);
    }

    juce::String ModuleBrowser::reasonUnavailable (ModuleId id) const
    {
        if (freeInstances (id) > 0)
            return {};
        const int limit = dsp::maxInstances (id);
        return limit == 1 ? i18n::tr ("Schon in der Kette (nur einmal möglich)", "Already in the chain (only once possible)")
                          : i18n::format (i18n::tr ("Alle {0} sind in der Kette", "All {0} are in the chain"), { juce::String (limit) });
    }

    void ModuleBrowser::setSearchText (const juce::String& text)
    {
        search->setText (text, juce::dontSendNotification);
        relayout();
    }

    std::vector<ModuleId> ModuleBrowser::visibleModules() const
    {
        std::vector<ModuleId> out;
        const auto words = juce::StringArray::fromTokens (search->getText().toLowerCase(), " ", "");
        for (auto id : browserOrder)
        {
            // The category in both languages (the search finds German and English words)
            const auto [categoryDe, categoryEn] = categoryTexts (categoryOf (id));
            const auto category = juce::String::fromUTF8 (categoryDe) + " " + categoryEn;
            const auto haystack = (juce::String (dsp::moduleDisplayName (id)) + " " + help::shortDescription ((int) id) + " "
                                   + juce::String::fromUTF8 (keywordsFor (id)) + " " + category).toLowerCase();
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
        return getLocalBounds().withSizeKeepingCentre (std::min (760, getWidth() - 40), std::min (500, getHeight() - 40));
    }

    juce::Rectangle<int> ModuleBrowser::cardBoundsFor (ModuleId id) const
    {
        for (const auto& c : cards)
            if (c.id == id)
                return c.bounds;
        return {};
    }

    juce::Rectangle<int> ModuleBrowser::groupBoundsFor (Category c) const
    {
        for (const auto& g : groups)
            if (g.category == c)
                return g.bounds;
        return {};
    }

    std::optional<ModuleId> ModuleBrowser::getHighlighted() const
    {
        if (juce::isPositiveAndBelow (highlighted, (int) cards.size()))
            return cards[(size_t) highlighted].id;
        return std::nullopt;
    }

    void ModuleBrowser::moveHighlight (int steps)
    {
        if (cards.empty())
            return;
        const int n = (int) cards.size();
        int i = highlighted < 0 ? (steps > 0 ? -1 : n) : highlighted;
        // Skip modules that cannot be added
        for (int tries = 0; tries < n; ++tries)
        {
            i = (i + (steps > 0 ? 1 : -1) + n) % n;
            if (freeInstances (cards[(size_t) i].id) > 0)
                break;
        }
        highlighted = i;
        updateInfo();
        repaint();
    }

    bool ModuleBrowser::chooseHighlighted()
    {
        if (const auto id = getHighlighted(); id.has_value() && freeInstances (*id) > 0)
        {
            choose (*id);
            return true;
        }
        return false;
    }

    void ModuleBrowser::updateInfo()
    {
        const int shown = hoveredCard >= 0 ? hoveredCard : highlighted;
        if (! juce::isPositiveAndBelow (shown, (int) cards.size()))
        {
            infoText = cards.empty() ? i18n::format (i18n::tr ("Kein Modul passt zu „{0}“.", "No module matches \"{0}\"."), { search->getText() }) : juce::String();
            return;
        }
        const auto id = cards[(size_t) shown].id;
        infoText = juce::String (dsp::moduleDisplayName (id)) + " - " + help::shortDescription ((int) id);
        if (const auto reason = reasonUnavailable (id); reason.isNotEmpty())
            infoText << "  (" << reason << ")";
        else if (categoryOf (id) == Category::analysis)
            infoText << i18n::tr (". Verändert den Klang nie.", ". Never changes the sound.");
    }

    void ModuleBrowser::relayout()
    {
        cards.clear();
        groups.clear();
        const auto p = panelBounds().reduced (space::xl, 0);
        auto area = p.withTrimmedTop (98).withTrimmedBottom (44);

        // Groups flow left to right and wrap as whole blocks
        const auto visible = visibleModules();
        int x = area.getX(), y = area.getY();
        for (int c = 0; c < numCategories; ++c)
        {
            std::vector<ModuleId> inGroup;
            for (auto id : visible)
                if ((int) categoryOf (id) == c)
                    inGroup.push_back (id);
            if (inGroup.empty())
                continue;
            const int w = (int) inGroup.size() * cardW + ((int) inGroup.size() - 1) * cardGap;
            if (x > area.getX() && x + w > area.getRight())
            {
                x = area.getX();
                y += captionH + cardH + lineGap;
            }
            groups.push_back ({ (Category) c, { x, y, w, captionH + cardH } });
            int cx = x;
            for (auto id : inGroup)
            {
                cards.push_back ({ id, { cx, y + captionH, cardW, cardH } });
                cx += cardW + cardGap;
            }
            x += w + groupGap;
        }

        // Highlight the first module that can be added (Enter takes it)
        if (! juce::isPositiveAndBelow (highlighted, (int) cards.size()) || freeInstances (cards[(size_t) highlighted].id) <= 0)
        {
            highlighted = -1;
            for (int i = 0; i < (int) cards.size() && highlighted < 0; ++i)
                if (freeInstances (cards[(size_t) i].id) > 0)
                    highlighted = i;
        }
        hoveredCard = -1;
        updateInfo();
        repaint();
    }

    void ModuleBrowser::resized()
    {
        const auto p = panelBounds().reduced (space::xl, 0);
        search->setBounds (p.getX(), p.getY() + 52, p.getWidth(), 32);
        relayout();
    }

    void ModuleBrowser::paint (juce::Graphics& g)
    {
        g.fillAll (colours::scrim);
        const auto panel = panelBounds().toFloat();
        drawGlow (g, { panel.getX() + 120.0f, panel.getY() }, 340.0f, 0.10f, colours::teal);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (panel, radius::panel);
        drawGlass (g, panel, radius::panel, 0.03f);

        auto p = panelBounds().reduced (space::xl, 0);
        g.setColour (colours::text);
        g.setFont (font (18.0f, true));
        const auto where = atEnd ? i18n::tr ("am Ende der Kette", "at the end of the chain")
                                 : i18n::format (i18n::tr ("an Position {0}", "at position {0}"), { juce::String (insertIndex + 1) });
        g.drawText (i18n::tr ("Modul hinzufügen", "Add module"), p.withY (p.getY() + 14).withHeight (28), juce::Justification::centredLeft, false);
        drawCaption (g, where, p.withY (p.getY() + 14).withHeight (28).toFloat(), juce::Justification::centredRight, colours::textFaint, 9.5f);

        // Category captions
        for (const auto& gr : groups)
            drawCaption (g, categoryName (gr.category), gr.bounds.withHeight (captionH).toFloat(), juce::Justification::centredLeft,
                         gr.category == Category::analysis ? colours::teal : colours::textFaint, 9.0f);

        for (int i = 0; i < (int) cards.size(); ++i)
        {
            const auto& c = cards[(size_t) i];
            const auto r = c.bounds.toFloat();
            const bool full = freeInstances (c.id) <= 0;
            const bool hot = ! full && (hoveredCard == i || highlighted == i);
            const auto hue = moduleColour (c.id);

            g.setColour (full ? colours::panel : (hot ? colours::raisedHi : colours::raised));
            g.fillRoundedRectangle (r, radius::tile);
            if (hot)
            {
                g.setGradientFill (juce::ColourGradient (hue.withAlpha (0.14f), r.getX(), r.getY(), hue.withAlpha (0.0f), r.getX(), r.getBottom(), false));
                g.fillRoundedRectangle (r, radius::tile);
            }
            g.setColour (highlighted == i && ! full ? hue.withAlpha (0.9f) : colours::line.brighter (hoveredCard == i ? 0.6f : 0.3f));
            g.drawRoundedRectangle (r.reduced (0.5f), radius::tile, highlighted == i ? 1.4f : 1.0f);

            const float alpha = full ? 0.35f : 1.0f;
            const auto iconBox = juce::Rectangle<float> (30.0f, 30.0f).withCentre ({ r.getCentreX(), r.getY() + 26.0f });
            drawModuleIcon (g, (int) c.id, iconBox, (full ? colours::textFaint : hue).withAlpha (alpha));
            g.setColour ((full ? colours::textDim : colours::text).withAlpha (full ? 0.6f : 1.0f));
            g.setFont (semiBold (10.0f, 0.08f));
            g.drawFittedText (juce::String (dsp::moduleDisplayName (c.id)).toUpperCase(),
                              juce::Rectangle<float> (r.getX() + 4.0f, r.getY() + 46.0f, r.getWidth() - 8.0f, 14.0f).toNearestInt(),
                              juce::Justification::centred, 1, 0.75f);
            const int used = proc.getRouting().countOf (c.id);
            if (full || used > 0)
            {
                g.setColour (full ? colours::textFaint : hue.withAlpha (0.8f));
                g.setFont (font (9.5f));
                g.drawText (full ? i18n::tr ("voll", "full") : juce::String (used) + " / " + juce::String (dsp::maxInstances (c.id)),
                            juce::Rectangle<float> (r.getX(), r.getBottom() - 15.0f, r.getWidth(), 12.0f), juce::Justification::centred, false);
            }
        }

        // Description of the hovered / highlighted module
        if (infoText.isNotEmpty())
        {
            g.setColour (colours::textDim);
            g.setFont (font (12.5f));
            g.drawFittedText (infoText, p.withY (panelBounds().getBottom() - 38).withHeight (26), juce::Justification::centredLeft, 1, 0.85f);
        }
    }

    void ModuleBrowser::mouseMove (const juce::MouseEvent& e)
    {
        int card = -1;
        for (int i = 0; i < (int) cards.size(); ++i)
            if (cards[(size_t) i].bounds.contains (e.getPosition()))
                card = i;
        if (card != hoveredCard)
        {
            hoveredCard = card;
            const bool clickable = card >= 0 && freeInstances (cards[(size_t) card].id) > 0;
            setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            updateInfo();
            repaint();
        }
    }

    void ModuleBrowser::mouseExit (const juce::MouseEvent&)
    {
        hoveredCard = -1;
        updateInfo();
        repaint();
    }

    void ModuleBrowser::mouseDown (const juce::MouseEvent& e)
    {
        if (! panelBounds().contains (e.getPosition()))
        {
            close();
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

    bool ModuleBrowser::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey) { close(); return true; }
        if (key == juce::KeyPress::returnKey) { chooseHighlighted(); return true; }
        if (key == juce::KeyPress::downKey || key == juce::KeyPress::rightKey) { moveHighlight (1); return true; }
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::leftKey)    { moveHighlight (-1); return true; }
        return false;
    }
}
