#include "SimpleView.h"
#include "../PluginProcessor.h"

namespace velvox::ui
{
    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }

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
        analyse.setTooltip (de ("Headliner hört einen Abschnitt deiner Vocal während der Wiedergabe und stellt die Module ein. "
                                "Der Zustand davor bleibt über Vorher / Nachher erreichbar."));
        analyse.onClick = [this] { if (onAnalyse) onAnalyse(); };

        for (auto* b : { &before, &after })
        {
            b->getProperties().set ("segment", true);
            b->setColour (juce::TextButton::buttonOnColourId, colours::cyan);
        }
        before.setTooltip (de ("Die Einstellungen vor der Analyse (echter Parameterwechsel)"));
        after.setTooltip (de ("Der automatische Mix mit deinen Änderungen"));
        before.onClick = [this] { proc.getAutoMix().showPrevious (true); updateCompare(); };
        after.onClick = [this] { proc.getAutoMix().showPrevious (false); updateCompare(); };

        details.setTooltip (de ("Was der Auto-Mix gemacht hat, Density und fairer Vergleich"));
        details.onClick = [this] { if (onShowDetails) onShowDetails(); };

        for (auto* b : { &analyse, &before, &after, &details })
            addAndMakeVisible (b);
        meters.setTooltip (juce::String::fromUTF8 ("Pegel und Lautheit (LUFS) am Eingang und Ausgang"));
        addAndMakeVisible (meters);

        for (const auto& m : macroDefs())
        {
            auto s = std::make_unique<HSlider> (p.getState(), m.paramId, m.label, juce::Colour (m.colour));
            addAndMakeVisible (*s);
            macros.push_back (std::move (s));
        }

        proc.getAutoMix().addChangeListener (this);
        updateCompare();
    }

    SimpleView::~SimpleView()
    {
        proc.getAutoMix().removeChangeListener (this);
    }

    void SimpleView::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        updateCompare();
    }

    void SimpleView::updateCompare()
    {
        // Vorher / Nachher only exists after an analysis
        auto& am = proc.getAutoMix();
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

    void SimpleView::refresh (const LiveData& live)
    {
        spectrumView.refresh();
        meters.setLevels (live.inputDb.ago (0), live.outputDb.ago (0), live.inputLufs.ago (0), live.outputLufs.ago (0), live.clipped);
        if (before.isVisible() && before.getToggleState() != proc.getAutoMix().isShowingPrevious())
            updateCompare();
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

        // Spectrum with the button row under it
        auto inner = spectrumPanel.reduced (14, 14);
        buttonRow = inner.removeFromBottom (32);
        inner.removeFromBottom (12);
        spectrumView.setBounds (inner.reduced (1));

        auto row = buttonRow;
        analyse.setBounds (row.removeFromLeft (176));
        details.setBounds (row.removeFromRight (86));
        if (before.isVisible())
        {
            row.removeFromLeft (18);
            auto seg = row.removeFromLeft (164).reduced (3, 2);
            before.setBounds (seg.removeFromLeft (seg.getWidth() / 2));
            after.setBounds (seg);
        }

        // Six faders, evenly spaced
        auto col = macroPanel.reduced (18, 22);
        const int rowH = col.getHeight() / (int) macros.size();
        for (auto& s : macros)
            s->setBounds (col.removeFromTop (rowH).withSizeKeepingCentre (col.getWidth(), std::min (rowH, 34)));
    }
}
