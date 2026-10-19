#include "BeginnerView.h"
#include "../PluginProcessor.h"
#include "HelpContent.h"

namespace velvox::ui
{
    namespace
    {
        constexpr int heroH = 244;
        constexpr int macroH = 236;
        constexpr int gapY = 14;
        constexpr int compareW = 236;   // "Fair compare" column of the hint strip
        constexpr int hintRowH = 26;
    }

    BeginnerView::BeginnerView (VelvoxAudioProcessor& p)
    {
        struct Def { const char* id; const char* name; const char* text; };
        const Def defs[6] {
            { pid::macroPresence,   "Presence",   "Brings the voice to the front: clearer top end, harsh S sounds kept in check." },
            { pid::macroWarmth,     "Warmth",     "Rounder and fuller: more body, softer highs, a little analog-style colour." },
            { pid::macroAggression, "Aggression", "Harder and louder-feeling: heavier, faster compression and drive." },
            { pid::macroWidth,      "Width",      "Wider vocal: doubled takes left and right. The centre stays solid." },
            { pid::macroSpace,      "Space",      "Reverb and echo that step back while you sing and bloom in the gaps." },
            { pid::macroDensity,    "Density",    "Thicker and steadier: every word arrives at a similar level." },
        };

        addAndMakeVisible (spectrum);
        spectrum.setRange (-90.0f, 0.0f);

        for (size_t i = 0; i < macros.size(); ++i)
        {
            macros[i].knob = std::make_unique<Knob> (p.getState(), defs[i].id, defs[i].name);
            macros[i].knob->setHero (true);
            macros[i].knob->setMaxDialSize (122);
            macros[i].knob->setDescription (defs[i].text);
            addAndMakeVisible (*macros[i].knob);
        }

        levelMatch = std::make_unique<Choice> (p.getState(), pid::levelMatchMode, "Fair compare");
        addAndMakeVisible (*levelMatch);

        addChildComponent (editInPro);
        addChildComponent (focusHelp);
        editInPro.setTooltip ("Switch to Pro and open this module with all of its controls");
        focusHelp.setTooltip ("Help for this module");
        editInPro.onClick = [this] { if (onEditInPro) onEditInPro (focusModule); };
        focusHelp.onClick = [this] { if (onHelp) onHelp (focusModule); };
    }

    void BeginnerView::setHints (const std::vector<Hint>& hints)
    {
        bool same = hints.size() == currentHints.size();
        for (size_t i = 0; same && i < hints.size(); ++i)
            same = hints[i].text == currentHints[i].text && hints[i].level == currentHints[i].level;
        if (! same)
        {
            currentHints = hints;
            repaint (hintArea);
        }
    }

    void BeginnerView::paint (juce::Graphics& g)
    {
        // Hero: live spectrum on a glass stage, lit from below
        const auto hero = heroArea.toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (hero, 18.0f);
        {
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clip;
            clip.addRoundedRectangle (hero, 18.0f);
            g.reduceClipRegion (clip);
            drawGlow (g, { hero.getX() + hero.getWidth() * 0.3f, hero.getBottom() + 60.0f }, hero.getWidth() * 0.45f, 0.10f, colours::teal);
            drawGlow (g, { hero.getX() + hero.getWidth() * 0.75f, hero.getBottom() + 60.0f }, hero.getWidth() * 0.45f, 0.09f, colours::blue);
        }
        drawGlass (g, hero, 18.0f, 0.012f);

        auto title = heroArea.reduced (26, 20).removeFromTop (54);
        g.setColour (colours::text);
        g.setFont (font (typesize::title, true));
        g.drawText ("Shape your vocal", title.removeFromTop (30), juce::Justification::centredLeft, false);
        g.setColour (colours::textDim);
        g.setFont (font (typesize::body));
        g.drawText ("Pick a preset, then shape it with the six controls. Rest the mouse on one to see what it does.",
                    title, juce::Justification::centredLeft, true);

        // Hints strip
        const auto h = hintArea.toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (h, 14.0f);
        drawGlass (g, h, 14.0f, 0.016f);
        auto line = hintArea.reduced (20, 16).withTrimmedRight (compareW);
        int maxHints = 4;
        if (focusModule != -2)
        {
            // Clicked module first: what it is, with its actions on the right
            auto row = line.removeFromTop (hintRowH + 6).withTrimmedRight (editInPro.getWidth() + focusHelp.getWidth() + 18);
            const auto dot = juce::Point<float> ((float) row.getX() + 4.0f, (float) row.getCentreY());
            g.setColour (colours::cyan);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (dot));
            row.removeFromLeft (20);
            g.setColour (colours::text);
            g.setFont (font (typesize::body, true));
            const int titleW = juce::GlyphArrangement::getStringWidthInt (font (typesize::body, true), focusTitle) + 10;
            g.drawText (focusTitle, row.removeFromLeft (titleW), juce::Justification::centredLeft, false);
            g.setColour (colours::textDim);
            g.setFont (font (typesize::body));
            g.drawText (focusText, row, juce::Justification::centredLeft, true);
            maxHints = 3;
        }
        g.setFont (font (typesize::body));
        if (currentHints.empty())
        {
            g.setColour (colours::textDim);
            g.drawText ("Play your vocal to see live tips here.", line.removeFromTop (hintRowH), juce::Justification::centredLeft, false);
        }
        for (const auto& hint : currentHints)
        {
            if (maxHints-- <= 0)
                break;
            auto row = line.removeFromTop (hintRowH);
            const auto dot = juce::Point<float> ((float) row.getX() + 4.0f, (float) row.getCentreY());
            const auto c = hint.level == Hint::Level::warning ? colours::warning : colours::teal;
            g.setColour (hint.level == Hint::Level::info ? colours::textFaint : c);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (dot));
            g.setColour (hint.level == Hint::Level::info ? colours::textDim : colours::text);
            g.drawFittedText (hint.text, row.withTrimmedLeft (20), juce::Justification::centredLeft, 1, 0.92f);
        }

        // Divider before the compare control
        g.setColour (colours::line);
        g.fillRect (juce::Rectangle<float> (h.getRight() - (float) compareW, h.getY() + 18.0f, 1.0f, h.getHeight() - 36.0f));
    }

    void BeginnerView::resized()
    {
        auto r = getLocalBounds();
        heroArea = r.removeFromTop (heroH);
        r.removeFromTop (gapY);
        macroArea = r.removeFromTop (macroH);
        r.removeFromTop (gapY);
        hintArea = r;

        spectrum.setBounds (heroArea.reduced (1).withTrimmedTop (78).withTrimmedBottom (1));
        levelMatch->setBounds (hintArea.withTrimmedLeft (hintArea.getWidth() - compareW).reduced (22, 14));

        auto actions = focusRow();
        focusHelp.setBounds (actions.removeFromRight (34));
        actions.removeFromRight (8);
        editInPro.setBounds (actions.removeFromRight (116));

        const int n = (int) macros.size();
        for (int i = 0; i < n; ++i)
        {
            auto cell = juce::Rectangle<int> (macroArea.getX() + i * macroArea.getWidth() / n, macroArea.getY(),
                                              macroArea.getWidth() / n, macroArea.getHeight());
            macros[(size_t) i].knob->setBounds (cell.reduced (4, 2));
        }
    }
}

namespace velvox::ui
{
    juce::Rectangle<int> BeginnerView::focusRow() const
    {
        return hintArea.reduced (20, 16).withTrimmedRight (compareW + 12).removeFromTop (hintRowH + 6);
    }

    void BeginnerView::showModule (int id)
    {
        focusModule = id;
        focusTitle = id >= 0 && id < dsp::numModules ? juce::String (dsp::moduleDisplayName ((dsp::ModuleId) id)) : juce::String ("Output");
        focusText = help::shortDescription (id) + ".";
        editInPro.setVisible (true);
        focusHelp.setVisible (true);
        repaint (hintArea);
    }
}
