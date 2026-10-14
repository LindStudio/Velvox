#include "BeginnerView.h"
#include "../PluginProcessor.h"

namespace velvox::ui
{
    BeginnerView::BeginnerView (VelvoxAudioProcessor& p)
    {
        struct Def { const char* id; const char* name; juce::Colour colour; const char* text; };
        const Def defs[6] {
            { pid::macroPresence,   "Presence",   colours::cyan,    "Brings the voice to the front: clearer top end, harsh S sounds kept in check." },
            { pid::macroWarmth,     "Warmth",     colours::comp,    "Rounder and fuller: more body, softer highs, a little analog-style colour." },
            { pid::macroAggression, "Aggression", colours::magenta, "Harder and louder-feeling: heavier, faster compression and drive." },
            { pid::macroWidth,      "Width",      colours::violet,  "Wider vocal: doubled takes left and right. The centre stays solid." },
            { pid::macroSpace,      "Space",      colours::reverb,  "Reverb and echo that step back while you sing and bloom in the gaps." },
            { pid::macroDensity,    "Density",    colours::deess,   "Thicker and steadier: every word arrives at a similar level." },
        };

        for (size_t i = 0; i < macros.size(); ++i)
        {
            macros[i].knob = std::make_unique<Knob> (p.getState(), defs[i].id, defs[i].name, defs[i].colour);
            macros[i].description = defs[i].text;
            addAndMakeVisible (*macros[i].knob);
        }

        levelMatch = std::make_unique<Choice> (p.getState(), pid::levelMatchMode, "Fair compare");
        addAndMakeVisible (*levelMatch);
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
        const auto r = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 12.0f);
        g.setGradientFill (juce::ColourGradient (colours::violet.withAlpha (0.10f), r.getX(), r.getY(),
                                                 colours::magenta.withAlpha (0.04f), r.getRight(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (colours::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

        g.setColour (colours::text);
        g.setFont (display (22.0f));
        g.drawText ("Shape your vocal", getLocalBounds().reduced (20, 0).withHeight (54), juce::Justification::centredLeft, false);
        g.setColour (colours::textDim);
        g.setFont (font (13.0f));
        g.drawText ("Pick a preset, then move these six controls. Switch to Pro for every detail.",
                    getLocalBounds().reduced (20, 0).withHeight (54).withTrimmedLeft (200), juce::Justification::centredLeft, true);

        for (const auto& m : macros)
        {
            g.setColour (colours::textDim);
            g.setFont (font (12.5f));
            g.drawFittedText (m.description, m.textArea, juce::Justification::centredTop, 3);
        }

        // Hints
        const auto h = hintArea.toFloat();
        g.setColour (colours::background.withAlpha (0.6f));
        g.fillRoundedRectangle (h, 8.0f);
        auto line = hintArea.reduced (14, 10);
        g.setFont (font (13.0f));
        if (currentHints.empty())
        {
            g.setColour (colours::textDim);
            g.drawText ("Play your vocal to see live tips here.", line.removeFromTop (22), juce::Justification::centredLeft, false);
        }
        for (const auto& hint : currentHints)
        {
            auto row = line.removeFromTop (22);
            const auto c = hint.level == Hint::Level::warning ? colours::warning : (hint.level == Hint::Level::good ? colours::ok : colours::cyan);
            g.setColour (c);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ (float) row.getX() + 4.0f, (float) row.getCentreY() }));
            g.setColour (colours::text.withAlpha (0.9f));
            g.drawText (hint.text, row.withTrimmedLeft (16), juce::Justification::centredLeft, true);
        }
    }

    void BeginnerView::resized()
    {
        auto r = getLocalBounds().reduced (20, 0);
        r.removeFromTop (58);

        hintArea = r.removeFromBottom (128);
        r.removeFromBottom (12);
        auto compare = hintArea.removeFromRight (190);
        hintArea.removeFromRight (12);
        levelMatch->setBounds (compare.reduced (6, 12));

        const int perRow = 3;
        const int rowH = r.getHeight() / 2;
        for (size_t i = 0; i < macros.size(); ++i)
        {
            const int row = (int) i / perRow, col = (int) i % perRow;
            auto cell = juce::Rectangle<int> (r.getX() + col * r.getWidth() / perRow, r.getY() + row * rowH, r.getWidth() / perRow, rowH);
            auto text = cell.removeFromBottom (48).reduced (14, 0);
            macros[i].textArea = text;
            macros[i].knob->setBounds (cell.reduced (8, 2));
        }
    }
}
