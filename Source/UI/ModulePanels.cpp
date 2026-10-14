#include "ModulePanels.h"
#include "../PluginProcessor.h"

namespace velvox::ui
{
    namespace
    {
        constexpr int headerHeight = 40;
        constexpr int padding = 12;
        constexpr int stripHeight = 24;
    }

    //==========================================================================
    ModulePanel::ModulePanel (APVTS& state, juce::String t, juce::String s, juce::Colour c, const char* onId)
        : apvts (state), accent (c), title (std::move (t)), subtitle (std::move (s)), onParamId (onId)
    {
        if (onParamId != nullptr)
        {
            power = std::make_unique<PowerButton> (state, onParamId, accent);
            addAndMakeVisible (*power);
        }
    }

    bool ModulePanel::isOn() const
    {
        return onParamId == nullptr || apvts.getRawParameterValue (onParamId)->load() > 0.5f;
    }

    void ModulePanel::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();
        const bool on = isOn();

        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (colours::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

        // Accent edge: the module's position colour on the signal path
        g.setColour (on ? accent : accent.withAlpha (0.25f));
        g.fillRoundedRectangle (r.getX() + 12.0f, r.getY(), r.getWidth() - 24.0f, 2.0f, 1.0f);

        auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 0);
        header.removeFromLeft (power != nullptr ? 30 : 0);

        g.setColour (on ? colours::text : colours::textDim);
        g.setFont (font (16.0f, true));
        const int titleW = juce::GlyphArrangement::getStringWidthInt (font (16.0f, true), title) + 10;
        g.drawText (title, header.removeFromLeft (titleW), juce::Justification::centredLeft, false);

        g.setColour (colours::textDim);
        g.setFont (font (12.0f));
        g.drawText (subtitle, header, juce::Justification::centredLeft, true);
    }

    void ModulePanel::resized()
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (headerHeight).reduced (padding, 0);
        if (power != nullptr)
            power->setBounds (header.removeFromLeft (22).withSizeKeepingCentre (22, 22));

        layoutContent (r.reduced (padding, 0).withTrimmedBottom (padding));
    }

    void ModulePanel::refresh (VelvoxAudioProcessor&)
    {
        const int on = isOn() ? 1 : 0;
        if (on == lastOn)
            return;
        lastOn = on;

        for (auto* c : getChildren())
            if (c != power.get())
                c->setAlpha (on ? 1.0f : 0.45f);
        repaint();
    }

    void ModulePanel::layoutGrid (std::initializer_list<juce::Component*> items, juce::Rectangle<int> area, int columns, int rowHeight)
    {
        const int cellW = area.getWidth() / columns;
        int i = 0;
        for (auto* c : items)
        {
            const int col = i % columns, row = i / columns;
            if (c != nullptr)
                c->setBounds (area.getX() + col * cellW, area.getY() + row * rowHeight, cellW, rowHeight);
            ++i;
        }
    }

    //==========================================================================
    EqPanel::EqPanel (APVTS& s)
        : ModulePanel (s, "Equalizer", "Clean up lows, shape presence and air", colours::eq, pid::eqOn),
          curve (s),
          lowCutOn (s, pid::hpfOn, "Low cut", colours::eq),
          slope (s, pid::hpfSlope),
          lowCut (s, pid::hpfFreq, "Low cut", colours::eq),
          lowFreq (s, pid::lowFreq, "Low freq", colours::eq),
          lowGain (s, pid::lowGain, "Low", colours::eq),
          midFreq (s, pid::midFreq, "Mid freq", colours::eq),
          midGain (s, pid::midGain, "Mid", colours::eq),
          midQ (s, pid::midQ, "Mid Q", colours::eq),
          highFreq (s, pid::highFreq, "High freq", colours::eq),
          highGain (s, pid::highGain, "High", colours::eq),
          mix (s, pid::eqMix, "Mix", colours::eq)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &curve, &lowCutOn, &slope, &lowCut, &lowFreq, &lowGain,
                                                                  &midFreq, &midGain, &midQ, &highFreq, &highGain, &mix })
            addAndMakeVisible (c);
    }

    void EqPanel::layoutContent (juce::Rectangle<int> r)
    {
        curve.setBounds (r.removeFromTop ((int) (r.getHeight() * 0.48f)));
        r.removeFromTop (8);

        auto strip = r.removeFromTop (stripHeight);
        lowCutOn.setBounds (strip.removeFromLeft (96));
        strip.removeFromLeft (8);
        slope.setBounds (strip.removeFromLeft (110));
        r.removeFromTop (4);

        layoutGrid ({ &lowCut, &lowFreq, &lowGain, &midFreq, &midGain, &midQ, &highFreq, &highGain, &mix }, r, 9, r.getHeight());
    }

    void EqPanel::refresh (VelvoxAudioProcessor& p)
    {
        ModulePanel::refresh (p);
        curve.refresh();
    }

    //==========================================================================
    CompressorPanel::CompressorPanel (APVTS& s)
        : ModulePanel (s, "Compressor", "Even out level", colours::comp, pid::compOn),
          threshold (s, pid::compThreshold, "Threshold", colours::comp),
          ratio (s, pid::compRatio, "Ratio", colours::comp),
          attack (s, pid::compAttack, "Attack", colours::comp),
          release (s, pid::compRelease, "Release", colours::comp),
          knee (s, pid::compKnee, "Knee", colours::comp),
          makeup (s, pid::compMakeup, "Makeup", colours::comp),
          mix (s, pid::compMix, "Mix", colours::comp)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &threshold, &ratio, &attack, &release, &knee, &makeup, &mix, &gr })
            addAndMakeVisible (c);
        gr.setTooltip ("Gain reduction in dB");
    }

    void CompressorPanel::layoutContent (juce::Rectangle<int> r)
    {
        gr.setBounds (r.removeFromRight (26));
        r.removeFromRight (8);
        layoutGrid ({ &threshold, &ratio, &attack, &release, &knee, &makeup, &mix }, r, 4, r.getHeight() / 2);
    }

    void CompressorPanel::refresh (VelvoxAudioProcessor& p)
    {
        ModulePanel::refresh (p);
        gr.push (p.getChain().getCompressor().gainReductionDb.pull());
    }

    //==========================================================================
    DeEsserPanel::DeEsserPanel (APVTS& s)
        : ModulePanel (s, "De-esser", "Tame harsh S sounds", colours::deess, pid::deessOn),
          freq (s, pid::deessFreq, "Frequency", colours::deess),
          threshold (s, pid::deessThreshold, "Threshold", colours::deess),
          range (s, pid::deessRange, "Range", colours::deess),
          listen (s, pid::deessListen, "Listen", colours::deess)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &freq, &threshold, &range, &listen, &gr })
            addAndMakeVisible (c);
        gr.setTooltip ("Reduction of the sibilance band in dB");
    }

    void DeEsserPanel::layoutContent (juce::Rectangle<int> r)
    {
        gr.setBounds (r.removeFromRight (22));
        r.removeFromRight (8);
        listen.setBounds (r.removeFromTop (stripHeight).removeFromLeft (90));
        r.removeFromTop (6);
        const int rowH = r.getHeight() / 2;
        layoutGrid ({ &freq, &threshold }, r.removeFromTop (rowH), 2, rowH);
        range.setBounds (r.withSizeKeepingCentre (r.getWidth() / 2, r.getHeight()));
    }

    void DeEsserPanel::refresh (VelvoxAudioProcessor& p)
    {
        ModulePanel::refresh (p);
        gr.push (p.getChain().getDeEsser().gainReductionDb.pull());
    }

    //==========================================================================
    SaturationPanel::SaturationPanel (APVTS& s)
        : ModulePanel (s, "Saturation", "Warmth, grit", colours::sat, pid::satOn),
          curve (s),
          drive (s, pid::satDrive, "Drive", colours::sat),
          output (s, pid::satOutput, "Output", colours::sat),
          mix (s, pid::satMix, "Mix", colours::sat)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &curve, &drive, &output, &mix })
            addAndMakeVisible (c);
        curve.setTooltip ("Transfer curve: input level (horizontal) to output level (vertical)");
    }

    void SaturationPanel::layoutContent (juce::Rectangle<int> r)
    {
        curve.setBounds (r.removeFromTop ((int) (r.getHeight() * 0.52f)).reduced (18, 0));
        r.removeFromTop (8);
        layoutGrid ({ &drive, &output, &mix }, r, 3, r.getHeight());
    }

    void SaturationPanel::refresh (VelvoxAudioProcessor& p)
    {
        ModulePanel::refresh (p);
        curve.refresh();
    }

    //==========================================================================
    DelayPanel::DelayPanel (APVTS& s)
        : ModulePanel (s, "Delay", "Echoes beside the voice", colours::delay, pid::delayOn),
          sync (s, pid::delaySync, "Sync", colours::delay),
          pingPong (s, pid::delayPingPong, "Ping-pong", colours::delay),
          time (s, pid::delayTime, "Time", colours::delay),
          division (s, pid::delayDivision),
          feedback (s, pid::delayFeedback, "Feedback", colours::delay),
          mix (s, pid::delayMix, "Mix", colours::delay),
          lowCut (s, pid::delayLowCut, "Low cut", colours::delay),
          highCut (s, pid::delayHighCut, "High cut", colours::delay)
    {
        divisionLabel.setText ("Note", juce::dontSendNotification);
        divisionLabel.setFont (font (13.0f));
        divisionLabel.setJustificationType (juce::Justification::centred);
        divisionLabel.setColour (juce::Label::textColourId, colours::text.withAlpha (0.85f));
        divisionSlot.addAndMakeVisible (divisionLabel);
        divisionSlot.addAndMakeVisible (division);

        info.setFont (font (11.0f));
        info.setJustificationType (juce::Justification::centred);
        info.setColour (juce::Label::textColourId, colours::textDim);

        for (auto* c : std::initializer_list<juce::Component*> { &sync, &pingPong, &time, &divisionSlot, &feedback, &mix, &lowCut, &highCut, &info })
            addAndMakeVisible (c);
    }

    void DelayPanel::layoutContent (juce::Rectangle<int> r)
    {
        auto strip = r.removeFromTop (stripHeight);
        sync.setBounds (strip.removeFromLeft (76));
        strip.removeFromLeft (8);
        pingPong.setBounds (strip.removeFromLeft (104));
        r.removeFromTop (6);

        const int rowH = r.getHeight() / 2;
        auto row1 = r.removeFromTop (rowH);
        layoutGrid ({ &time, &feedback, &mix }, row1, 3, rowH);
        divisionSlot.setBounds (time.getBounds());
        auto slot = divisionSlot.getLocalBounds();
        divisionLabel.setBounds (slot.removeFromTop (16));
        division.setBounds (slot.withSizeKeepingCentre (std::min (slot.getWidth() - 4, 76), 24).translated (0, -6));

        layoutGrid ({ &lowCut, &highCut, nullptr }, r, 3, r.getHeight());
        info.setBounds (r.getX() + 2 * (r.getWidth() / 3), r.getY(), r.getWidth() / 3, r.getHeight());
    }

    void DelayPanel::refresh (VelvoxAudioProcessor& p)
    {
        ModulePanel::refresh (p);

        const bool syncOn = apvts.getRawParameterValue (pid::delaySync)->load() > 0.5f;
        time.setVisible (! syncOn);
        divisionSlot.setVisible (syncOn);

        const auto& meters = p.getChain().meters;
        const int ms = juce::roundToInt (meters.effectiveDelayMs.load());
        juce::String text;
        if (syncOn)
            text = meters.tempoSynced.load() ? juce::String (ms) + " ms\nat host tempo" : "No host tempo:\nusing " + juce::String (ms) + " ms";
        if (info.getText() != text)
            info.setText (text, juce::dontSendNotification);
    }

    //==========================================================================
    ReverbPanel::ReverbPanel (APVTS& s)
        : ModulePanel (s, "Reverb", "Space and depth", colours::reverb, pid::reverbOn),
          type (s, pid::reverbType),
          mix (s, pid::reverbMix, "Mix", colours::reverb),
          decay (s, pid::reverbDecay, "Decay", colours::reverb),
          preDelay (s, pid::reverbPreDelay, "Pre-delay", colours::reverb),
          size (s, pid::reverbSize, "Size", colours::reverb),
          tone (s, pid::reverbDamping, "Tone", colours::reverb),
          lowCut (s, pid::reverbLowCut, "Low cut", colours::reverb),
          width (s, pid::reverbWidth, "Width", colours::reverb)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &type, &mix, &decay, &preDelay, &size, &tone, &lowCut, &width })
            addAndMakeVisible (c);
    }

    void ReverbPanel::layoutContent (juce::Rectangle<int> r)
    {
        type.setBounds (r.removeFromTop (stripHeight).removeFromLeft (110));
        r.removeFromTop (6);
        const int rowH = r.getHeight() / 3;
        layoutGrid ({ &mix, &decay, &preDelay, &size, &tone, &lowCut, &width }, r, 3, rowH);
        width.setBounds (width.getBounds().withX (r.getX() + r.getWidth() / 3));
    }
}
