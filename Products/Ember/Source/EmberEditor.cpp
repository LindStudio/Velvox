#include "EmberEditor.h"
#include "UI/ValueText.h"
#include "Text/I18n.h"

namespace ember
{
    using namespace vocalypse;
    using i18n::tr;
    namespace c = ui::colours;

    // ---------------------------------------------------------------- meter
    void ReductionMeter::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        auto label = r.removeFromBottom (18.0f);
        auto bar = r.removeFromRight (14.0f);
        g.setColour (c::panelInset);
        g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (c::borderInset);
        g.drawRoundedRectangle (bar, 4.0f, 1.0f);

        if (value > peakHold || --holdTicks <= 0)
        {
            peakHold = std::max (value, peakHold - 0.25f);
            if (value >= peakHold)
                holdTicks = 30;
        }

        const float frac = juce::jlimit (0.0f, 1.0f, value / maxDb);
        auto lit = bar.reduced (2.0f).withHeight ((bar.getHeight() - 4.0f) * frac);
        g.setGradientFill (juce::ColourGradient (look::glow, lit.getX(), bar.getY(), look::accent, lit.getX(), bar.getBottom(), false));
        g.fillRoundedRectangle (lit, 3.0f);
        const float hy = bar.getY() + 2.0f + (bar.getHeight() - 4.0f) * juce::jlimit (0.0f, 1.0f, peakHold / maxDb);
        g.setColour (c::textStrong.withAlpha (0.8f));
        g.fillRect (bar.getX() + 1.0f, hy - 1.0f, bar.getWidth() - 2.0f, 2.0f);

        // Scale
        g.setFont (ui::monoFont (9.5f));
        g.setColour (c::textFaint);
        for (float db : { 0.0f, 6.0f, 12.0f, 18.0f })
        {
            const float y = bar.getY() + 2.0f + (bar.getHeight() - 4.0f) * db / maxDb;
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (bar.getX() - 24.0f, y - 6.0f, 20.0f, 12.0f), juce::Justification::centredRight);
        }
        g.setFont (ui::caps (11.0f));
        g.setColour (c::textMuted);
        g.drawText ("GR", label.withLeft (bar.getX() - 10.0f).withRight (bar.getRight() + 10.0f), juce::Justification::centred);
    }

    // --------------------------------------------------------------- display
    BandDisplay::BandDisplay (EmberAudioProcessor& p, ui::SpectrumSource& source) : processor (p), spectrum (&source)
    {
        spectrum.setRange (-90.0f, 0.0f);
        addAndMakeVisible (spectrum);
        history.fill (0.0f);
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    }

    float BandDisplay::xForHz (float hz) const { return ui::SpectrumView::xForFrequency (hz, getLocalBounds().toFloat()); }

    float BandDisplay::hzForX (float x) const
    {
        const auto r = getLocalBounds().toFloat();
        const float t = juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth());
        return ui::SpectrumSource::minHz * std::pow (ui::SpectrumSource::maxHz / ui::SpectrumSource::minHz, t);
    }

    void BandDisplay::pushReduction (float db)
    {
        history[(size_t) head] = db;
        head = (head + 1) % (int) history.size();
    }

    void BandDisplay::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (c::panelInset);
        g.fillRoundedRectangle (r, ui::radius::card);
        // the spectrum paints itself (child); the band and history are painted over it in paintOverChildren-like order:
        // JUCE paints children after paint(), so the band shading sits under the spectrum lines - intended
        const float hz = processor.state.getRawParameterValue (pid::freq)->load();
        const float x = xForHz (hz);
        const float gr = processor.engine.currentReductionDb.load();
        auto band = r.withLeft (x);
        g.setGradientFill (juce::ColourGradient (look::accent.withAlpha (0.10f + 0.03f * std::min (gr, 12.0f)), band.getX(), 0,
                                                 look::accent.withAlpha (0.02f), band.getRight(), 0, false));
        g.fillRect (band);
        g.setColour (look::accent.withAlpha (dragging ? 1.0f : 0.85f));
        g.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ x, r.getY() + 14.0f }));

        // Reduction history (top, hanging down: 0 dB at the top edge, 18 dB at 45 % height)
        juce::Path h;
        const float hTop = r.getY() + 1.0f, hSpan = r.getHeight() * 0.45f;
        const int n = (int) history.size();
        for (int i = 0; i < n; ++i)
        {
            const float v = history[(size_t) ((head + i) % n)];
            const float px = r.getX() + r.getWidth() * (float) i / (float) (n - 1);
            const float py = hTop + hSpan * juce::jlimit (0.0f, 1.0f, v / ReductionMeter::maxDb);
            if (i == 0) h.startNewSubPath (px, hTop);
            h.lineTo (px, py);
        }
        h.lineTo (r.getRight(), hTop);
        h.closeSubPath();
        g.setColour (look::glow.withAlpha (0.16f));
        g.fillPath (h);
        g.setColour (look::glow.withAlpha (0.65f));
        g.strokePath (h, juce::PathStrokeType (1.2f));

        // Labels
        g.setFont (ui::monoFont (11.0f));
        g.setColour (c::textStrong);
        const auto text = hz >= 1000.0f ? i18n::number (hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
        g.drawText (text, juce::Rectangle<float> (x + 10.0f, r.getY() + 6.0f, 90.0f, 16.0f), juce::Justification::centredLeft);
        g.setColour (look::glow);
        g.drawText ((gr >= 0.05f ? juce::String::fromUTF8 ("−") : juce::String()) + i18n::number (gr, 1) + " dB", juce::Rectangle<float> (r.getRight() - 96.0f, r.getBottom() - 22.0f, 88.0f, 16.0f),
                    juce::Justification::centredRight);
    }

    void BandDisplay::mouseDown (const juce::MouseEvent& e)
    {
        if (auto* p = processor.state.getParameter (pid::freq))
        {
            dragging = true;
            p->beginChangeGesture();
            mouseDrag (e);
        }
    }

    void BandDisplay::mouseDrag (const juce::MouseEvent& e)
    {
        if (auto* p = processor.state.getParameter (pid::freq); p != nullptr && dragging)
        {
            const float hz = juce::jlimit (2000.0f, 16000.0f, hzForX ((float) e.position.x));
            p->setValueNotifyingHost (p->convertTo0to1 (hz));
            repaint();
        }
    }

    void BandDisplay::mouseUp (const juce::MouseEvent&)
    {
        if (auto* p = processor.state.getParameter (pid::freq); p != nullptr && dragging)
            p->endChangeGesture();
        dragging = false;
    }

    void BandDisplay::mouseMove (const juce::MouseEvent&) {}

    void BandDisplay::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (auto* p = processor.state.getParameter (pid::freq))
            ui::gestures::resetToDefault (*p);
    }

    // ---------------------------------------------------------------- content
    class EmberEditor::Content final : public juce::Component
    {
    public:
        explicit Content (EmberEditor& e) : owner (e) {}
        void paint (juce::Graphics& g) override
        {
            g.fillAll (c::windowBg);
            auto r = getLocalBounds().toFloat();

            // Ember's colour: a warm light from the top left, the frame lines stay the VOCALYPSE ones
            g.setGradientFill (juce::ColourGradient (look::accent.withAlpha (0.13f), 0.0f, 0.0f,
                                                     look::accent.withAlpha (0.0f), r.getWidth() * 0.65f, r.getHeight() * 0.8f, true));
            g.fillRect (r);

            auto top = r.removeFromTop (60.0f);
            g.setColour (c::borderSoft);
            g.fillRect (top.getX(), top.getBottom() - 1.0f, top.getWidth(), 1.0f);

            // Brand: EMBER (fire gradient) + De-Esser
            auto brand = top.reduced (24.0f, 10.0f).removeFromLeft (360.0f);
            g.setFont (ui::display (30.0f, true).withExtraKerningFactor (0.06f));
            g.setGradientFill (juce::ColourGradient (look::glow, brand.getX(), brand.getY(), look::accent, brand.getX(), brand.getBottom(), false));
            g.drawText ("EMBER", brand.removeFromLeft (128.0f), juce::Justification::centredLeft);
            g.setFont (ui::semiBold (15.0f, 0.04f));
            g.setColour (c::textStrong);
            auto sub = brand.removeFromLeft (220.0f);
            g.drawText ("De-Esser", sub.removeFromTop (sub.getHeight() * 0.55f), juce::Justification::bottomLeft);
            g.setFont (ui::caps (10.5f));
            g.setColour (c::textFaint);
            g.drawText (juce::String::fromUTF8 ("LIND AUDIO  ·  VOCALYPSE"), sub, juce::Justification::topLeft);

            // Panel frame of the current view
            g.setColour (c::border);
            g.drawRoundedRectangle (getLocalBounds().toFloat().withTrimmedTop (68.0f).reduced (12.0f, 6.0f), ui::radius::panel, 1.0f);
        }
        EmberEditor& owner;
    };

    // ----------------------------------------------------------------- editor
    namespace
    {
        void styleSegment (juce::TextButton& b, const juce::String& text, int group)
        {
            b.setButtonText (text);
            b.setClickingTogglesState (false);
            b.getProperties().set (ui::VocalypseLookAndFeel::segmentProperty, true);
            b.setColour (juce::TextButton::buttonOnColourId, look::accent);
            b.setRadioGroupId (group);
        }

        void styleLabel (juce::Label& l, const juce::String& text)
        {
            l.setText (i18n::upper (text), juce::dontSendNotification);
            l.setFont (ui::caps (11.0f));
            l.setColour (juce::Label::textColourId, c::textMuted);
            l.setJustificationType (juce::Justification::centredLeft);
        }
    }

    EmberEditor::EmberEditor (EmberAudioProcessor& p)
        : AudioProcessorEditor (p), processor (p),
          spectrumSource (p.engine.inputTap, p.engine.outputTap),
          display (p, spectrumSource),
          tame (p.state, pid::amount, tr ("Zähmen", "Tame"), look::accent),
          freqK (p.state, pid::freq, tr ("Frequenz", "Frequency"), look::accent),
          threshK (p.state, pid::threshold, "Threshold", look::accent),
          rangeK (p.state, pid::range, "Range", look::accent),
          amountK (p.state, pid::amount, tr ("Zähmen", "Tame"), look::accent),
          attackK (p.state, pid::attack, "Attack", look::accent),
          releaseK (p.state, pid::release, "Release", look::accent),
          lookK (p.state, pid::lookahead, "Lookahead", look::accent),
          mixK (p.state, pid::mix, "Mix", look::accent),
          outK (p.state, pid::output, "Output", look::accent),
          modeSeg (p.state, pid::mode, { "Split", tr ("Breitband", "Wideband") }, look::accent),
          detectSeg (p.state, pid::detection, { tr ("Relativ", "Relative"), tr ("Absolut", "Absolute") }, look::accent),
          listenSeg (p.state, pid::listen, { tr ("Aus", "Off"), tr ("S-Band", "S band"), "Delta" }, look::accent)
    {
        i18n::setCurrent (i18n::systemLanguage());
        setLookAndFeel (&lnf);

        content = std::make_unique<Content> (*this);
        addAndMakeVisible (*content);
        auto& root = *content;

        styleSegment (simpleBtn, "SIMPLE", 1);
        styleSegment (advancedBtn, "ADVANCED", 1);
        simpleBtn.onClick = [this] { setAdvanced (false); };
        advancedBtn.onClick = [this] { setAdvanced (true); };

        autoBtn.getProperties().set ("fire", true);
        autoBtn.setColour (juce::TextButton::textColourOffId, c::fireInk);
        autoBtn.onClick = [this] { autoClicked(); };
        autoBtn.setTooltip (tr ("Ember hört deiner Stimme zu und stellt sich selbst ein.", "Ember listens to your voice and sets himself up."));

        bypassBtn.setButtonText ("BYPASS");
        bypassBtn.setClickingTogglesState (true);
        bypassBtn.setColour (juce::TextButton::buttonOnColourId, c::bypass);
        if (auto* bp = p.state.getParameter (pid::bypass))
            bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (*bp, bypassBtn);

        for (auto* b : { &simpleBtn, &advancedBtn, &autoBtn, &bypassBtn })
            root.addAndMakeVisible (*b);

        mascot.onClick = [this] { autoClicked(); };
        root.addAndMakeVisible (mascot);
        root.addAndMakeVisible (meter);
        root.addChildComponent (display);

        tame.setHero (true);
        tame.setDialSize (150);
        tame.setDescription (tr ("Wie stark Ember die S-Laute zähmt. 50 % = so, wie AUTO oder Threshold und Range es vorgeben.",
                                 "How hard Ember tames the S sounds. 50 % = what AUTO or threshold and range ask for."));
        root.addAndMakeVisible (tame);

        styleSegment (voiceLow, tr ("TIEF", "LOW"), 2);
        styleSegment (voiceMid, tr ("MITTEL", "MID"), 2);
        styleSegment (voiceHigh, tr ("HOCH", "HIGH"), 2);
        voiceLow.onClick = [this] { setVoice (voiceLowHz); };
        voiceMid.onClick = [this] { setVoice (voiceMidHz); };
        voiceHigh.onClick = [this] { setVoice (voiceHighHz); };
        voiceLow.setTooltip (tr ("Tiefe Stimme: S-Bereich ab 4,8 kHz", "Low voice: S range from 4.8 kHz"));
        voiceMid.setTooltip (tr ("Mittlere Stimme: ab 6,5 kHz", "Mid voice: from 6.5 kHz"));
        voiceHigh.setTooltip (tr ("Hohe Stimme: ab 8,2 kHz", "High voice: from 8.2 kHz"));
        listenBtn.setButtonText (tr ("S HÖREN", "HEAR S"));
        listenBtn.setTooltip (tr ("Hör nur, was Ember wegnimmt.", "Hear only what Ember removes."));
        listenBtn.onClick = [this]
        {
            if (auto* lp = processor.state.getParameter (pid::listen))
                ui::gestures::setReal (*lp, lp->convertFrom0to1 (lp->getValue()) > 0.5f ? 0.0f : 2.0f);
        };
        for (auto* b : { &voiceLow, &voiceMid, &voiceHigh, &listenBtn })
            root.addAndMakeVisible (*b);

        status.setFont (ui::font (13.5f));
        status.setColour (juce::Label::textColourId, c::textSoft);
        status.setJustificationType (juce::Justification::centredLeft);
        status.setMinimumHorizontalScale (0.8f);
        root.addAndMakeVisible (status);

        for (auto* k : { &freqK, &threshK, &rangeK, &amountK, &attackK, &releaseK, &lookK, &mixK, &outK })
        {
            k->setDialSize (ui::dialsize::secondary);
            root.addChildComponent (*k);
        }
        freqK.setDescription (tr ("Ab hier sucht Ember nach S-Lauten und senkt nur diesen Bereich ab.", "Ember looks for S sounds from here up and lowers only this range."));
        threshK.setDescription (tr ("Ab diesem Pegel greift Ember ein.", "Ember steps in above this level."));
        rangeK.setDescription (tr ("So viel nimmt Ember höchstens weg.", "The most Ember ever removes."));
        attackK.setDescription (tr ("Wie schnell Ember zupackt.", "How fast Ember grabs."));
        releaseK.setDescription (tr ("Wie schnell Ember wieder loslässt.", "How fast Ember lets go."));
        lookK.setDescription (tr ("Ember sieht das S so viel früher kommen. Die Latenz bleibt immer 5 ms.", "Ember sees the S coming this much earlier. The latency always stays 5 ms."));
        mixK.setDescription (tr ("Anteil des bearbeiteten Signals.", "Share of the processed signal."));
        outK.setDescription (tr ("Ausgangspegel.", "Output level."));

        styleLabel (modeLbl, tr ("Modus", "Mode"));
        styleLabel (detectLbl, tr ("Erkennung", "Detection"));
        styleLabel (listenLbl, tr ("Hören", "Listen"));
        for (juce::Component* comp : { (juce::Component*) &modeSeg, (juce::Component*) &detectSeg, (juce::Component*) &listenSeg,
                                       (juce::Component*) &modeLbl, (juce::Component*) &detectLbl, (juce::Component*) &listenLbl })
            root.addChildComponent (*comp);

        advanced = (bool) processor.state.state.getProperty ("advanced", false);
        setResizable (true, true);
        setResizeLimits (look::baseW * 3 / 4, look::baseH * 3 / 4, look::baseW * 2, look::baseH * 2);
        if (auto* cons = getConstrainer())
            cons->setFixedAspectRatio ((double) look::baseW / look::baseH);
        setSize (look::baseW, look::baseH);
        setAdvanced (advanced);
        startTimerHz (30);
    }

    EmberEditor::~EmberEditor()
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void EmberEditor::paint (juce::Graphics& g) { g.fillAll (c::windowBg); }

    void EmberEditor::resized()
    {
        const float scale = (float) getWidth() / (float) look::baseW;
        content->setBounds (0, 0, look::baseW, look::baseH);
        content->setTransform (juce::AffineTransform::scale (scale));
        layoutContent();
    }

    void EmberEditor::setAdvanced (bool a)
    {
        advanced = a;
        processor.state.state.setProperty ("advanced", a, nullptr);
        simpleBtn.setToggleState (! a, juce::dontSendNotification);
        advancedBtn.setToggleState (a, juce::dontSendNotification);

        for (juce::Component* comp : { (juce::Component*) &tame, (juce::Component*) &voiceLow, (juce::Component*) &voiceMid,
                                       (juce::Component*) &voiceHigh, (juce::Component*) &listenBtn, (juce::Component*) &meter })
            comp->setVisible (! a);
        for (juce::Component* comp : { (juce::Component*) &display, (juce::Component*) &freqK, (juce::Component*) &threshK,
                                       (juce::Component*) &rangeK, (juce::Component*) &amountK, (juce::Component*) &attackK,
                                       (juce::Component*) &releaseK, (juce::Component*) &lookK, (juce::Component*) &mixK,
                                       (juce::Component*) &outK, (juce::Component*) &modeSeg, (juce::Component*) &detectSeg,
                                       (juce::Component*) &listenSeg, (juce::Component*) &modeLbl, (juce::Component*) &detectLbl,
                                       (juce::Component*) &listenLbl })
            comp->setVisible (a);
        mascot.setCompact (a);
        layoutContent();
        refreshButtons();
    }

    void EmberEditor::layoutContent()
    {
        using R = juce::Rectangle<int>;
        // Top bar
        advancedBtn.setBounds (R (530, 16, 96, 28));
        simpleBtn.setBounds (R (434, 16, 96, 28));
        autoBtn.setBounds (R (650, 16, 110, 28));
        bypassBtn.setBounds (R (772, 16, 104, 28));

        if (! advanced)
        {
            mascot.setBounds (R (28, 80, 500, 400));
            meter.setBounds (R (540, 96, 44, 372));
            tame.setBounds (R (612, 82, 250, 230));
            voiceLow.setBounds (R (612, 342, 82, 32));
            voiceMid.setBounds (R (696, 342, 82, 32));
            voiceHigh.setBounds (R (780, 342, 82, 32));
            listenBtn.setBounds (R (612, 392, 250, 32));
            status.setBounds (R (40, 490, 820, 44));
        }
        else
        {
            display.setBounds (R (28, 80, 664, 238));
            mascot.setBounds (R (700, 80, 176, 238));
            int x = 28;
            for (auto* k : { &freqK, &threshK, &rangeK, &amountK, &attackK, &releaseK, &lookK, &mixK, &outK })
            {
                k->setBounds (R (x, 330, 94, 108));
                x += 94;
            }
            modeLbl.setBounds (R (40, 448, 120, 18));
            modeSeg.setBounds (R (40, 466, 200, 30));
            detectLbl.setBounds (R (290, 448, 120, 18));
            detectSeg.setBounds (R (290, 466, 220, 30));
            listenLbl.setBounds (R (560, 448, 120, 18));
            listenSeg.setBounds (R (560, 466, 300, 30));
            status.setBounds (R (40, 504, 820, 34));
        }
    }

    void EmberEditor::setVoice (float hz)
    {
        if (auto* p = processor.state.getParameter (pid::freq))
            ui::gestures::setReal (*p, hz);
        refreshButtons();
    }

    void EmberEditor::autoClicked()
    {
        using Phase = EmberAudioProcessor::AutoPhase;
        if (processor.getAutoPhase() == Phase::analysing)
            return;
        if (processor.getAutoPhase() == Phase::listening)
        {
            if (processor.getCapture().getSeconds() >= autoMinSeconds)
                processor.finishAuto();
            else
                processor.cancelAuto();
        }
        else
        {
            processor.startAuto();
        }
        refreshButtons();
    }

    void EmberEditor::refreshButtons()
    {
        using Phase = EmberAudioProcessor::AutoPhase;
        const auto phase = processor.getAutoPhase();
        autoBtn.setEnabled (phase != Phase::analysing);
        if (phase == Phase::analysing)
            autoBtn.setButtonText (juce::String::fromUTF8 ("…"));
        else if (phase == Phase::listening)
            autoBtn.setButtonText (processor.getCapture().getSeconds() >= autoMinSeconds ? tr ("FERTIG", "DONE") : tr ("ABBRECHEN", "CANCEL"));
        else
            autoBtn.setButtonText ("AUTO");

        const float hz = processor.state.getRawParameterValue (pid::freq)->load();
        voiceLow.setToggleState (std::abs (hz - voiceLowHz) < 1.0f, juce::dontSendNotification);
        voiceMid.setToggleState (std::abs (hz - voiceMidHz) < 1.0f, juce::dontSendNotification);
        voiceHigh.setToggleState (std::abs (hz - voiceHighHz) < 1.0f, juce::dontSendNotification);
        const bool hearing = processor.state.getRawParameterValue (pid::listen)->load() > 0.5f;
        listenBtn.setToggleState (hearing, juce::dontSendNotification);
        listenBtn.setColour (juce::TextButton::buttonOnColourId, look::accent);
    }

    void EmberEditor::timerCallback()
    {
        using Phase = EmberAudioProcessor::AutoPhase;
        const float gr = processor.engine.gainReductionDb.pull();
        const float in = processor.engine.inputPeak.pull();
        processor.engine.outputPeak.pull();
        shownGr = gr > shownGr ? gr : shownGr + (gr - shownGr) * 0.25f;

        const auto phase = processor.getAutoPhase();
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        MascotInput mi;
        mi.bypass = processor.state.getRawParameterValue (pid::bypass)->load() > 0.5f;
        mi.hasSignal = in > 0.00316f;
        mi.autoListening = phase == Phase::listening || phase == Phase::analysing;
        mi.autoJustFinished = phase == Phase::done && lastPhase != Phase::done;
        mi.reductionDb = gr;
        mascot.setInput (mi);

        if (phase != lastPhase && (phase == Phase::done || phase == Phase::failed))
            messageUntil = now + 8.0;
        lastPhase = phase;

        // Status line + Ember's speech bubble
        juce::String text;
        if (phase == Phase::listening)
        {
            const int pct = juce::roundToInt (processor.getAutoProgress() * 100.0);
            text = processor.getAutoMessage() + "  " + juce::String (pct) + " %";
            mascot.setBubble (tr ("Ich hör zu …", "Listening …"));
        }
        else if (phase == Phase::analysing)
        {
            text = processor.getAutoMessage();
            mascot.setBubble (tr ("Moment …", "One moment …"));
        }
        else if (now < messageUntil)
        {
            text = processor.getAutoMessage();
            mascot.setBubble (phase == Phase::done ? tr ("Passt!", "Got it!") : tr ("Hmm …", "Hmm …"));
        }
        else
        {
            mascot.setBubble ({});
            text = mi.bypass ? tr ("Bypass: Ember schläft. Das Signal läuft unbearbeitet durch.", "Bypass: Ember is asleep. The signal passes unprocessed.")
                             : tr ("Klick auf Ember oder AUTO: Er hört zu und stellt sich auf deine Stimme ein.",
                                   "Click Ember or AUTO: he listens and sets himself up for your voice.");
        }
        status.setText (text, juce::dontSendNotification);

        meter.setValue (shownGr);
        if (advanced)
        {
            spectrumSource.update (processor.getCurrentSampleRate());
            display.pushReduction (gr);
            display.refresh();
        }
        refreshButtons();
    }
}
