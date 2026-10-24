#include "VocalControlBar.h"
#include "../PluginProcessor.h"

namespace velvox::ui
{
    namespace
    {
        struct MacroDef { const char* paramId; const char* label; juce::uint32 colour; };
        const MacroDef macros[] {
            { pid::macroPresence,   "Presence",    0xff00d1ff },
            { pid::macroSpace,      "Space",       0xff3b82f6 },
            { pid::macroWidth,      "Width",       0xff6366ff },
            { pid::macroAggression, "Punch",       0xff8b5cf6 },
            { pid::macroWarmth,     "Warmth",      0xffd946ef },
            { pid::outputGain,      "Vocal Level", 0xfff472b6 },
        };
    }

    VocalControlBar::VocalControlBar (VelvoxAudioProcessor& p, const LiveData& liveData) : live (liveData)
    {
        for (const auto& m : macros)
        {
            auto k = std::make_unique<Knob> (p.getState(), m.paramId, m.label, juce::Colour (m.colour));
            k->setDialSize (dialsize::macro);
            addAndMakeVisible (*k);
            knobs.push_back (std::move (k));
        }
    }

    void VocalControlBar::refresh()
    {
        const float gr = live.compGr.ago (0);
        if (std::abs (gr - shownGr) > 0.05f)
        {
            shownGr = gr;
            repaint (grArea);
        }
    }

    void VocalControlBar::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().reduced (24, 14);
        g.setColour (colours::textStrong);
        g.setFont (semiBold (13.0f, 0.18f));
        g.drawText ("VOCAL CONTROL", r.removeFromTop (18), juce::Justification::centredLeft, false);

        // Gain reduction: value and a horizontal bar (0 .. 12 dB)
        const auto a = grArea.toFloat();
        g.setColour (colours::textFaint);
        g.setFont (semiBold (10.0f, 0.15f));
        g.drawText ("GAIN REDUCTION", a.withHeight (16.0f), juce::Justification::centredLeft, false);
        g.setColour (colours::text);
        g.setFont (monoFont (22.0f));
        g.drawText ((shownGr > 0.05f ? "-" : "") + juce::String (shownGr, 1) + " dB", a.withTrimmedTop (20.0f).withHeight (30.0f),
                    juce::Justification::centredLeft, false);
        const auto track = juce::Rectangle<float> (a.getX(), a.getY() + 60.0f, a.getWidth(), 5.0f);
        g.setColour (colours::control);
        g.fillRoundedRectangle (track, 2.5f);
        const float frac = juce::jlimit (0.0f, 1.0f, shownGr / 12.0f);
        if (frac > 0.0f)
        {
            juce::ColourGradient grad (juce::Colour (0xff00d1ff), track.getX(), 0.0f, juce::Colour (0xfff472b6), track.getRight(), 0.0f, false);
            grad.addColour (0.5, juce::Colour (0xff8b5cf6));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (track.withWidth (track.getWidth() * frac), 2.5f);
        }
    }

    void VocalControlBar::resized()
    {
        auto r = getLocalBounds().reduced (24, 14);
        r.removeFromTop (22);
        grArea = r.removeFromRight (220).withSizeKeepingCentre (220, 70);
        r.removeFromRight (24);
        const int w = r.getWidth() / (int) knobs.size();
        for (auto& k : knobs)
            k->setBounds (r.removeFromLeft (w).withSizeKeepingCentre (w, 96));
    }
}
