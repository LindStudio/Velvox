#include "EmberEditor.h"
#include "UI/ValueText.h"
#include "Text/I18n.h"

namespace ember
{
    using namespace vocalypse;
    using i18n::tr;
    namespace c = ui::colours;

    namespace
    {
        // Jakob 2026-11-13 (next to FabFilter Pro-DS at the same window size): "die Woerter kann man nicht lesen".
        // EMBER's panel uses larger type than the VOCALYPSE design; set when the plugin binary loads, before
        // any component makes a font.
        constexpr float emberTextScale = 1.35f;
        const bool textScaleSet = (ui::setTextScale (emberTextScale), true);

        juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

        juce::Path triangle (bool right)
        {
            juce::Path p;
            if (right) p.addTriangle (0.0f, 0.0f, 10.0f, 6.0f, 0.0f, 12.0f);
            else       p.addTriangle (10.0f, 0.0f, 0.0f, 6.0f, 10.0f, 12.0f);
            return p;
        }

        juce::Path hamburger()
        {
            juce::Path p;
            for (int i = 0; i < 3; ++i)
                p.addRoundedRectangle (0.0f, (float) i * 6.0f, 18.0f, 2.2f, 1.1f);
            return p;
        }

        void styleShape (juce::ShapeButton& b, const juce::Path& path)
        {
            b.setShape (path, false, true, false);
            b.setColours (c::textMuted, c::textStrong, look::accent);
            b.setBorderSize (juce::BorderSize<int> (7));
        }

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
            l.setInterceptsMouseClicks (false, false);
        }

        juce::String hzText (float hz)
        {
            return hz >= 1000.0f ? i18n::number (hz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
        }
    }

    // ================================================================ meters
    void ReductionMeter::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        auto readout = r.removeFromTop (26.0f);
        auto label = r.removeFromBottom (20.0f);
        auto bar = r.removeFromRight (14.0f);
        g.setColour (c::panelInset);
        g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (c::borderInset);
        g.drawRoundedRectangle (bar, 4.0f, 1.0f);

        if (value > peakHold || --holdTicks <= 0)
        {
            peakHold = std::max (value, peakHold - 0.15f);
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

        g.setFont (ui::monoFont (9.5f));
        g.setColour (c::textFaint);
        for (float db : { 0.0f, 3.0f, 6.0f, 9.0f, 12.0f })
        {
            const float y = bar.getY() + 2.0f + (bar.getHeight() - 4.0f) * db / maxDb;
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (bar.getX() - 28.0f, y - 8.0f, 24.0f, 16.0f), juce::Justification::centredRight);
        }
        g.setFont (ui::monoFont (11.0f));
        g.setColour (peakHold >= 0.05f ? look::glow : c::textMuted);
        g.drawText ((peakHold >= 0.05f ? utf8 ("−") : juce::String()) + i18n::number (peakHold, 1), readout, juce::Justification::centredRight);
        g.setFont (ui::caps (11.0f));
        g.setColour (c::textMuted);
        g.drawText ("GR dB", label, juce::Justification::centredRight);
    }

    DetectorMeter::DetectorMeter (EmberAudioProcessor& p) : processor (p)
    {
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    juce::Rectangle<float> DetectorMeter::barArea() const
    {
        auto r = getLocalBounds().toFloat();
        r.removeFromTop (8.0f);
        r.removeFromBottom (22.0f);
        return r.withSizeKeepingCentre (16.0f, r.getHeight()).withX (r.getRight() - 22.0f);
    }

    float DetectorMeter::yFor (float db) const
    {
        const auto b = barArea();
        return b.getBottom() - b.getHeight() * juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
    }

    float DetectorMeter::dbFor (float y) const
    {
        const auto b = barArea();
        return minDb + (maxDb - minDb) * juce::jlimit (0.0f, 1.0f, (b.getBottom() - y) / b.getHeight());
    }

    void DetectorMeter::setLevel (float db, float smartWeight)
    {
        level = db;
        weight = smartWeight;
        hold = db > hold ? db : hold - 0.6f;
        repaint();
    }

    void DetectorMeter::paint (juce::Graphics& g)
    {
        const auto b = barArea();
        g.setColour (c::panelInset);
        g.fillRoundedRectangle (b, 4.0f);
        g.setColour (c::borderInset);
        g.drawRoundedRectangle (b, 4.0f, 1.0f);

        const float thr = processor.state.getRawParameterValue (pid::threshold)->load();
        const float ty = yFor (thr);
        if (hold > minDb)
        {
            // above the threshold the bar burns (if smart detection says it is an S), below it stays calm
            auto bar = b.reduced (2.0f).withTop (yFor (hold));
            const auto over = bar.withBottom (std::max (bar.getY(), std::min (ty, bar.getBottom())));
            const auto under = bar.withTop (std::max (bar.getY(), ty));
            g.setColour (c::textMuted.withAlpha (0.55f));
            g.fillRoundedRectangle (under, 3.0f);
            g.setColour (weight > 0.5f ? look::accent : c::textMuted);
            g.fillRoundedRectangle (over, 3.0f);
        }

        // Threshold handle
        g.setColour (dragging ? look::glow : look::accent);
        g.fillRect (b.getX() - 18.0f, ty - 1.0f, b.getWidth() + 22.0f, 2.0f);
        juce::Path tri;
        tri.addTriangle (b.getX() - 20.0f, ty - 6.0f, b.getX() - 20.0f, ty + 6.0f, b.getX() - 11.0f, ty);
        g.fillPath (tri);

        g.setFont (ui::monoFont (9.5f));
        g.setColour (c::textFaint);
        for (float db : { 0.0f, -20.0f, -40.0f, -60.0f })
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (b.getX() - 46.0f, yFor (db) - 6.0f, 24.0f, 12.0f), juce::Justification::centredRight);
        g.setFont (ui::caps (10.5f));
        g.setColour (c::textMuted);
        g.drawText (tr ("S-PEGEL", "S LEVEL"), getLocalBounds().toFloat().removeFromBottom (18.0f), juce::Justification::centredRight);
    }

    void DetectorMeter::mouseDown (const juce::MouseEvent& e)
    {
        if (auto* p = processor.state.getParameter (pid::threshold))
        {
            dragging = true;
            p->beginChangeGesture();
            mouseDrag (e);
        }
    }

    void DetectorMeter::mouseDrag (const juce::MouseEvent& e)
    {
        if (auto* p = processor.state.getParameter (pid::threshold); p != nullptr && dragging)
        {
            p->setValueNotifyingHost (p->convertTo0to1 (dbFor (e.position.y)));
            repaint();
        }
    }

    void DetectorMeter::mouseUp (const juce::MouseEvent&)
    {
        if (auto* p = processor.state.getParameter (pid::threshold); p != nullptr && dragging)
            p->endChangeGesture();
        dragging = false;
    }

    void DetectorMeter::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (auto* p = processor.state.getParameter (pid::threshold))
            ui::gestures::resetToDefault (*p);
    }

    // ================================================================ display
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

        // Reduction history (top, hanging down: 0 dB at the top edge, 12 dB at 45 % height)
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

        g.setFont (ui::monoFont (11.0f));
        g.setColour (c::textStrong);
        g.drawText (hzText (hz), juce::Rectangle<float> (x + 10.0f, r.getY() + 6.0f, 90.0f, 16.0f), juce::Justification::centredLeft);
        g.setColour (look::glow);
        g.drawText ((gr >= 0.05f ? utf8 ("−") : juce::String()) + i18n::number (gr, 1) + " dB",
                    juce::Rectangle<float> (r.getRight() - 96.0f, r.getBottom() - 22.0f, 88.0f, 16.0f), juce::Justification::centredRight);
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
            p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (2000.0f, 16000.0f, hzForX (e.position.x))));
            repaint();
        }
    }

    void BandDisplay::mouseUp (const juce::MouseEvent&)
    {
        if (auto* p = processor.state.getParameter (pid::freq); p != nullptr && dragging)
            p->endChangeGesture();
        dragging = false;
    }

    void BandDisplay::mouseDoubleClick (const juce::MouseEvent&)
    {
        if (auto* p = processor.state.getParameter (pid::freq))
            ui::gestures::resetToDefault (*p);
    }

    // ================================================================ about
    class EmberPanel::AboutOverlay final : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            g.fillAll (c::scrim);
            auto card = getLocalBounds().toFloat().withSizeKeepingCentre (460.0f, 300.0f);
            g.setColour (c::raisedHi);
            g.fillRoundedRectangle (card, ui::radius::panel);
            g.setColour (look::accent.withAlpha (0.6f));
            g.drawRoundedRectangle (card, ui::radius::panel, 1.0f);
            auto r = card.reduced (28.0f, 22.0f);
            g.setFont (ui::display (30.0f / emberTextScale, true).withExtraKerningFactor (0.06f));   // the wordmark keeps its size
            g.setGradientFill (juce::ColourGradient (look::glow, r.getX(), r.getY(), look::accent, r.getX(), r.getY() + 34.0f, false));
            g.drawText ("EMBER", r.removeFromTop (36.0f), juce::Justification::centredLeft);
            g.setColour (c::textStrong);
            g.setFont (ui::semiBold (15.0f));
            g.drawText ("De-Esser  " + juce::String (EMBER_VERSION_STRING), r.removeFromTop (22.0f), juce::Justification::centredLeft);
            g.setColour (c::textMuted);
            g.setFont (ui::font (13.0f));
            g.drawText (utf8 ("© 2026 Lind Audio · lind-audio.com"), r.removeFromTop (20.0f), juce::Justification::centredLeft);
            r.removeFromTop (14.0f);
            g.setColour (c::textSoft);
            g.setFont (ui::font (12.5f));
            g.drawFittedText (tr ("Mit JUCE gebaut. Schriften: Montserrat (SIL Open Font License 1.1), Roboto Mono (Apache License 2.0).\n"
                                  "Danke an alle Sängerinnen und Sänger, deren Aufnahmen beim Abstimmen geholfen haben.",
                                  "Built with JUCE. Fonts: Montserrat (SIL Open Font License 1.1), Roboto Mono (Apache License 2.0).\n"
                                  "Thanks to all singers whose recordings helped tune it."),
                              r.removeFromTop (90.0f).toNearestInt(), juce::Justification::topLeft, 6);
            g.setColour (c::textFaint);
            g.setFont (ui::font (12.0f));
            g.drawText (tr ("Klicken zum Schließen", "Click to close"), r, juce::Justification::bottomLeft);
        }
        void mouseUp (const juce::MouseEvent&) override { setVisible (false); }
    };

    // ================================================================ panel
    EmberPanel::EmberPanel (EmberAudioProcessor& p, UserSettings& s, std::function<void (juce::String)> lang)
        : processor (p), settings (s), onLanguage (std::move (lang)),
          spectrumSource (p.engine.inputTap, p.engine.outputTap),
          display (p, spectrumSource),
          detector (p),
          amountBig (p.state, pid::amount, "Amount", look::accent),
          freqK (p.state, pid::freq, tr ("Frequenz", "Frequency"), look::accent),
          threshK (p.state, pid::threshold, "Threshold", look::accent),
          rangeK (p.state, pid::range, "Range", look::accent),
          amountK (p.state, pid::amount, "Amount", look::accent),
          attackK (p.state, pid::attack, "Attack", look::accent),
          releaseK (p.state, pid::release, "Release", look::accent),
          lookK (p.state, pid::lookahead, "Lookahead", look::accent),
          mixK (p.state, pid::mix, "Mix", look::accent),
          outK (p.state, pid::output, "Output", look::accent),
          modeSeg (p.state, pid::mode, { "Split", tr ("Breitband", "Wideband") }, look::accent),
          detectSeg (p.state, pid::detection, { "Smart", tr ("Relativ", "Relative"), tr ("Absolut", "Absolute") }, look::accent),
          listenSeg (p.state, pid::listen, { tr ("Aus", "Off"), tr ("S-Band", "S band"), "Delta" }, look::accent)
    {

        // ---- top bar
        styleSegment (simpleBtn, "SIMPLE", 1);
        styleSegment (advancedBtn, "ADVANCED", 1);
        simpleBtn.onClick = [this] { setAdvanced (false); };
        advancedBtn.onClick = [this] { setAdvanced (true); };
        autoBtn.getProperties().set ("fire", true);
        autoBtn.setColour (juce::TextButton::textColourOffId, c::fireInk);
        autoBtn.onClick = [this] { autoClicked(); };
        bypassBtn.setButtonText ("BYPASS");
        bypassBtn.setClickingTogglesState (true);
        bypassBtn.setColour (juce::TextButton::buttonOnColourId, c::bypass);
        if (auto* bp = p.state.getParameter (pid::bypass))
            bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (*bp, bypassBtn);
        styleShape (menuBtn, hamburger());
        menuBtn.onClick = [this] { showMainMenu(); };

        // ---- preset bar
        styleShape (prevBtn, triangle (false));
        styleShape (nextBtn, triangle (true));
        prevBtn.onClick = [this] { processor.stepPreset (-1); };
        nextBtn.onClick = [this] { processor.stepPreset (1); };
        presetBtn.onClick = [this] { showPresetMenu(); };
        presetBtn.setColour (juce::TextButton::textColourOffId, c::textStrong);
        saveBtn.setButtonText (tr ("SPEICHERN", "SAVE"));
        saveBtn.onClick = [this] { savePresetAs(); };
        styleSegment (slotA, "A", 3);
        styleSegment (slotB, "B", 3);
        slotA.onClick = [this] { processor.switchSlot (0); refreshButtons(); };
        slotB.onClick = [this] { processor.switchSlot (1); refreshButtons(); };
        slotCopy.setButtonText (utf8 ("A → B"));
        slotCopy.onClick = [this] { processor.copyActiveToOther(); };

        for (juce::Component* comp : { (juce::Component*) &simpleBtn, (juce::Component*) &advancedBtn, (juce::Component*) &autoBtn,
                                       (juce::Component*) &bypassBtn, (juce::Component*) &menuBtn, (juce::Component*) &prevBtn,
                                       (juce::Component*) &nextBtn, (juce::Component*) &presetBtn, (juce::Component*) &saveBtn,
                                       (juce::Component*) &slotA, (juce::Component*) &slotB, (juce::Component*) &slotCopy })
            addAndMakeVisible (*comp);

        // ---- SIMPLE
        mascot.onClick = [this] { autoClicked(); };
        addChildComponent (mascot);
        addChildComponent (meter);
        amountBig.setHero (true);
        amountBig.setDialSize (150);
        addChildComponent (amountBig);
        styleLabel (voiceLbl, tr ("Stimmlage", "Voice"));
        addChildComponent (voiceLbl);
        styleSegment (voiceLow, tr ("TIEF", "LOW"), 2);
        styleSegment (voiceMid, tr ("MITTEL", "MID"), 2);
        styleSegment (voiceHigh, tr ("HOCH", "HIGH"), 2);
        voiceLow.onClick = [this] { setVoice (voiceLowHz); };
        voiceMid.onClick = [this] { setVoice (voiceMidHz); };
        voiceHigh.onClick = [this] { setVoice (voiceHighHz); };
        listenBtn.setButtonText ("DELTA");
        listenBtn.onClick = [this]
        {
            if (auto* lp = processor.state.getParameter (pid::listen))
                ui::gestures::setReal (*lp, lp->convertFrom0to1 (lp->getValue()) > 0.5f ? 0.0f : 2.0f);
        };
        for (auto* b : { &voiceLow, &voiceMid, &voiceHigh, &listenBtn })
            addChildComponent (*b);

        status.setFont (ui::font (13.5f));
        status.setColour (juce::Label::textColourId, c::textSoft);
        status.setJustificationType (juce::Justification::centredLeft);
        status.setMinimumHorizontalScale (0.8f);
        status.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (status);

        // ---- ADVANCED
        addChildComponent (display);
        addChildComponent (detector);
        for (auto* k : { &freqK, &threshK, &rangeK, &amountK, &attackK, &releaseK, &lookK, &mixK, &outK })
        {
            k->setDialSize (ui::dialsize::secondary);
            addChildComponent (*k);
        }
        styleLabel (modeLbl, tr ("Modus", "Mode"));
        styleLabel (detectLbl, tr ("Erkennung", "Detection"));
        styleLabel (listenLbl, tr ("Abhören", "Listen"));
        for (juce::Component* comp : { (juce::Component*) &modeSeg, (juce::Component*) &detectSeg, (juce::Component*) &listenSeg,
                                       (juce::Component*) &modeLbl, (juce::Component*) &detectLbl, (juce::Component*) &listenLbl })
            addChildComponent (*comp);

        // ---- explanations (info card on hover)
        const auto amountText = tr ("Wie stark die S-Laute reduziert werden. 50 % = genau das, was Threshold und Range (oder AUTO) vorgeben, "
                                    "100 % = doppelt so viel, 0 % = aus.",
                                    "How hard the S sounds are reduced. 50 % = exactly what threshold and range (or AUTO) set, "
                                    "100 % = twice as much, 0 % = off.");
        amountBig.setDescription (amountText);
        amountK.setDescription (amountText);
        freqK.setDescription (tr ("Ab dieser Frequenz sucht der De-Esser nach S-Lauten und senkt nur diesen Bereich ab. "
                                  "Tiefe Stimmen um 5 kHz, hohe um 7–8 kHz. Auch im Spektrum mit der Maus ziehbar.",
                                  "From this frequency up the de-esser looks for S sounds and lowers only this range. "
                                  "Low voices around 5 kHz, high voices around 7-8 kHz. Also draggable in the spectrum."));
        threshK.setDescription (tr ("Ab diesem S-Pegel greift der De-Esser ein. Tiefer = mehr S-Laute werden erfasst. "
                                    "Auch an der S-Pegel-Anzeige rechts ziehbar.",
                                    "The S level above which the de-esser steps in. Lower = more S sounds are caught. "
                                    "Also draggable on the S level meter on the right."));
        rangeK.setDescription (tr ("So viel nimmt der De-Esser höchstens weg. 3–6 dB klingen natürlich, mehr kann lispeln lassen.",
                                   "The most the de-esser ever removes. 3-6 dB sound natural, more can make the voice lisp."));
        attackK.setDescription (tr ("Wie schnell der De-Esser zupackt, wenn ein S beginnt.", "How fast the de-esser grabs when an S starts."));
        releaseK.setDescription (tr ("Wie schnell er nach dem S wieder loslässt. Kurz (30–40 ms) lässt den nächsten Vokal unberührt.",
                                     "How fast it lets go after the S. Short (30-40 ms) leaves the next vowel untouched."));
        lookK.setDescription (tr ("Der De-Esser sieht das S so viel früher kommen und erwischt auch den Anfang. "
                                  "Die Latenz bleibt immer 5 ms.",
                                  "The de-esser sees the S coming this much earlier and also catches its start. "
                                  "The latency always stays 5 ms."));
        mixK.setDescription (tr ("Anteil des bearbeiteten Signals. Unter 100 % bleibt etwas vom Original-S erhalten.",
                                 "Share of the processed signal. Below 100 % some of the original S remains."));
        outK.setDescription (tr ("Ausgangspegel nach dem De-Esser.", "Output level after the de-esser."));
        modeSeg.setInfo (tr ("Modus", "Mode"),
                         tr ("Split: nur der Bereich über der Frequenz wird leiser, die Stimme darunter bleibt voll. "
                             "Breitband: die ganze Stimme taucht beim S kurz ab (natürlicher bei Sprache).",
                             "Split: only the range above the frequency gets quieter, the voice below stays full. "
                             "Wideband: the whole voice dips briefly on each S (more natural for speech)."));
        detectSeg.setInfo (tr ("Erkennung", "Detection"),
                           tr ("Smart (empfohlen): reagiert nur, wenn wirklich ein Zischlaut klingt. Helle Vokale, Luft und Atem "
                               "bleiben unberührt. Relativ: misst das S im Verhältnis zur Lautstärke der Stimme, leise und laute "
                               "Stellen gleich. Absolut: fester Pegel wie ein klassischer De-Esser.",
                               "Smart (recommended): reacts only when a real sibilant sounds. Bright vowels, air and breaths "
                               "stay untouched. Relative: measures the S against the loudness of the voice, quiet and loud "
                               "parts alike. Absolute: fixed level like a classic de-esser."));
        listenSeg.setInfo (tr ("Abhören", "Listen"),
                           tr ("S-Band: nur den Bereich hören, in dem gesucht wird (zum Einstellen der Frequenz). "
                               "Delta: nur das hören, was entfernt wird. Es sollten nur Zischlaute zu hören sein.",
                               "S band: hear only the range being searched (to set the frequency). "
                               "Delta: hear only what is removed. You should hear nothing but sibilants."));

        addInfo (simpleBtn, "SIMPLE", tr ("Die einfache Ansicht: Ember, Amount, Stimmlage und Delta. Reicht für die meisten Stimmen.",
                                          "The simple view: Ember, Amount, voice and Delta. Enough for most vocals."));
        addInfo (advancedBtn, "ADVANCED", tr ("Alle Regler, das Spektrum mit dem S-Bereich und die S-Pegel-Anzeige mit dem Threshold.",
                                              "Every control, the spectrum with the S range and the S level meter with the threshold."));
        addInfo (autoBtn, "AUTO", tr ("Spiel deinen Gesang ab und klick auf AUTO. Ember hört etwa 12 Sekunden zu und stellt Frequenz, "
                                      "Threshold und Range so ein, dass die S-Laute dezent leiser werden und die Vokale unberührt bleiben.",
                                      "Play your vocal and click AUTO. Ember listens for about 12 seconds and sets frequency, "
                                      "threshold and range so the S sounds get subtly quieter and the vowels stay untouched."));
        addInfo (bypassBtn, "BYPASS", tr ("Schaltet den De-Esser aus, um mit dem Original zu vergleichen. Die Latenz bleibt gleich.",
                                          "Switches the de-esser off to compare with the original. The latency stays the same."));
        addInfo (menuBtn, tr ("Menü", "Menu"), tr ("Sprache, Handbuch und Informationen zu EMBER.", "Language, manual and about EMBER."));
        addInfo (prevBtn, tr ("Voriges Preset", "Previous preset"), tr ("Lädt das vorige Preset.", "Loads the previous preset."));
        addInfo (nextBtn, tr ("Nächstes Preset", "Next preset"), tr ("Lädt das nächste Preset.", "Loads the next preset."));
        addInfo (presetBtn, "Preset", tr ("Werks-Presets und deine eigenen Presets. Ein * zeigt, dass du seit dem Laden etwas geändert hast.",
                                          "Factory presets and your own presets. A * shows you changed something since loading."));
        addInfo (saveBtn, tr ("Preset speichern", "Save preset"), tr ("Speichert die aktuelle Einstellung als eigenes Preset.",
                                                                      "Saves the current setting as your own preset."));
        addInfo (slotA, tr ("Vergleich A", "Compare A"), tr ("Zwei Einstellungen vergleichen: A und B merken sich je eine Einstellung. "
                                                             "Umschalten, hören, die bessere behalten.",
                                                             "Compare two settings: A and B each keep one setting. Switch, listen, keep the better one."));
        addInfo (slotB, tr ("Vergleich B", "Compare B"), tr ("Beim ersten Klick startet B als Kopie von A.", "On the first click B starts as a copy of A."));
        addInfo (slotCopy, tr ("Kopieren", "Copy"), tr ("Kopiert die aktive Einstellung in den anderen Speicher.", "Copies the active setting into the other slot."));
        addInfo (mascot, "Ember", tr ("Ember zeigt, was der De-Esser tut: Feuer = er nimmt S-Laute weg. Klick auf ihn startet AUTO.",
                                      "Ember shows what the de-esser does: fire = he is removing S sounds. Click him to start AUTO."));
        addInfo (meter, "Gain Reduction", tr ("Wie viel gerade weggenommen wird, in dB.", "How much is being removed right now, in dB."));
        addInfo (voiceLow, tr ("Tiefe Stimme", "Low voice"), tr ("S-Bereich ab 5 kHz, für tiefe und männliche Stimmen.", "S range from 5 kHz, for low and male voices."));
        addInfo (voiceMid, tr ("Mittlere Stimme", "Mid voice"), tr ("S-Bereich ab 6,5 kHz, passt für die meisten Stimmen.", "S range from 6.5 kHz, fits most voices."));
        addInfo (voiceHigh, tr ("Hohe Stimme", "High voice"), tr ("S-Bereich ab 8 kHz, für hohe und weibliche Stimmen.", "S range from 8 kHz, for high and female voices."));
        addInfo (listenBtn, "Delta", tr ("Nur das hören, was entfernt wird. Hörst du Vokale, ist es zu viel.",
                                         "Hear only what is removed. If you hear vowels, it is too much."));
        addInfo (display, tr ("Spektrum", "Spectrum"), tr ("Rot markiert: der Bereich, der bei S-Lauten leiser wird. Die Kante ziehen = Frequenz. "
                                                           "Oben: wie viel in den letzten Sekunden weggenommen wurde.",
                                                           "Red: the range that gets quieter on S sounds. Drag the edge = frequency. "
                                                           "Top: how much was removed over the last seconds."));
        addInfo (detector, tr ("S-Pegel", "S level"), tr ("Wie laut die S-Laute gerade sind. Über der roten Linie (Threshold) wird reduziert. "
                                                          "Grau = Smart erkennt keinen Zischlaut. Linie ziehen = Threshold.",
                                                          "How loud the S sounds are right now. Above the red line (threshold) they are reduced. "
                                                          "Grey = Smart hears no sibilant. Drag the line = threshold."));

        addChildComponent (infoPopup);
        about = std::make_unique<AboutOverlay>();
        addChildComponent (*about);
        addChildComponent (licenceLock);
        licenceLock.setLicence (&processor.getLicence());

        setSize (look::baseW, look::baseH);
        advanced = (bool) processor.state.state.getProperty ("advanced", false);
        setAdvanced (advanced);
        startTimerHz (30);
    }

    EmberPanel::~EmberPanel()
    {
        stopTimer();
    }

    void EmberPanel::addInfo (juce::Component& comp, const juce::String& title, const juce::String& text)
    {
        infos.push_back (std::make_unique<ui::HoverInfo> (comp, [title] { return title; }, nullptr, [text] { return text; }));
    }

    void EmberPanel::showInfo (juce::Component& anchor, const juce::String& title, const juce::String& value, const juce::String& text)
    {
        infoPopup.toFront (false);
        infoPopup.show (getLocalArea (&anchor, anchor.getLocalBounds()), title, value, text);
    }

    void EmberPanel::hideInfo (juce::Component&)
    {
        infoPopup.hide();
    }

    void EmberPanel::paint (juce::Graphics& g)
    {
        g.fillAll (c::windowBg);
        auto r = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (look::accent.withAlpha (0.13f), 0.0f, 0.0f,
                                                 look::accent.withAlpha (0.0f), r.getWidth() * 0.65f, r.getHeight() * 0.8f, true));
        g.fillRect (r);

        auto top = r.removeFromTop (60.0f);
        g.setColour (c::borderSoft);
        g.fillRect (top.getX(), top.getBottom() - 1.0f, top.getWidth(), 1.0f);

        auto brand = top.reduced (24.0f, 10.0f).removeFromLeft (360.0f);
        g.setFont (ui::display (30.0f / emberTextScale, true).withExtraKerningFactor (0.06f));   // the wordmark keeps its size
        g.setGradientFill (juce::ColourGradient (look::glow, brand.getX(), brand.getY(), look::accent, brand.getX(), brand.getBottom(), false));
        g.drawText ("EMBER", brand.removeFromLeft (128.0f), juce::Justification::centredLeft);
        g.setFont (ui::semiBold (15.0f, 0.04f));
        g.setColour (c::textStrong);
        auto sub = brand.removeFromLeft (220.0f);
        g.drawText ("De-Esser", sub.removeFromTop (sub.getHeight() * 0.55f), juce::Justification::bottomLeft);
        g.setFont (ui::caps (10.5f));
        g.setColour (c::textFaint);
        g.drawText (utf8 ("LIND AUDIO"), sub, juce::Justification::topLeft);

        // preset bar backing
        g.setColour (c::control);
        g.fillRoundedRectangle (juce::Rectangle<float> (24.0f, 68.0f, 852.0f, 32.0f), 7.0f);

        g.setColour (c::border);
        g.drawRoundedRectangle (juce::Rectangle<float> (12.0f, 108.0f, 876.0f, 484.0f), ui::radius::panel, 1.0f);
    }

    void EmberPanel::resized() { layout(); }

    void EmberPanel::setAdvanced (bool a)
    {
        advanced = a;
        processor.state.state.setProperty ("advanced", a, nullptr);
        simpleBtn.setToggleState (! a, juce::dontSendNotification);
        advancedBtn.setToggleState (a, juce::dontSendNotification);

        // Ember lives in SIMPLE only (Jakob 2026-11-10)
        for (juce::Component* comp : { (juce::Component*) &mascot, (juce::Component*) &meter, (juce::Component*) &amountBig,
                                       (juce::Component*) &voiceLbl, (juce::Component*) &voiceLow, (juce::Component*) &voiceMid,
                                       (juce::Component*) &voiceHigh, (juce::Component*) &listenBtn })
            comp->setVisible (! a);
        for (juce::Component* comp : { (juce::Component*) &display, (juce::Component*) &detector, (juce::Component*) &freqK,
                                       (juce::Component*) &threshK, (juce::Component*) &rangeK, (juce::Component*) &amountK,
                                       (juce::Component*) &attackK, (juce::Component*) &releaseK, (juce::Component*) &lookK,
                                       (juce::Component*) &mixK, (juce::Component*) &outK, (juce::Component*) &modeSeg,
                                       (juce::Component*) &detectSeg, (juce::Component*) &listenSeg, (juce::Component*) &modeLbl,
                                       (juce::Component*) &detectLbl, (juce::Component*) &listenLbl })
            comp->setVisible (a);
        infoPopup.hide();
        layout();
        refreshButtons();
    }

    void EmberPanel::layout()
    {
        using R = juce::Rectangle<int>;
        simpleBtn.setBounds (R (420, 16, 92, 28));
        advancedBtn.setBounds (R (512, 16, 100, 28));
        autoBtn.setBounds (R (630, 16, 100, 28));
        bypassBtn.setBounds (R (740, 16, 92, 28));
        menuBtn.setBounds (R (842, 14, 34, 32));

        prevBtn.setBounds (R (30, 70, 28, 28));
        presetBtn.setBounds (R (60, 71, 300, 26));
        nextBtn.setBounds (R (362, 70, 28, 28));
        saveBtn.setBounds (R (400, 71, 110, 26));
        slotA.setBounds (R (700, 71, 40, 26));
        slotB.setBounds (R (742, 71, 40, 26));
        slotCopy.setBounds (R (790, 71, 80, 26));

        if (! advanced)
        {
            mascot.setBounds (R (28, 116, 520, 402));
            meter.setBounds (R (556, 126, 44, 386));
            amountBig.setBounds (R (624, 118, 248, 230));
            voiceLbl.setBounds (R (624, 362, 248, 18));
            voiceLow.setBounds (R (624, 382, 82, 32));
            voiceMid.setBounds (R (707, 382, 82, 32));
            voiceHigh.setBounds (R (790, 382, 82, 32));
            listenBtn.setBounds (R (624, 430, 248, 32));
            status.setBounds (R (40, 530, 820, 50));
        }
        else
        {
            display.setBounds (R (28, 118, 760, 222));
            detector.setBounds (R (796, 114, 76, 230));
            int x = 28;
            for (auto* k : { &freqK, &threshK, &rangeK, &amountK, &attackK, &releaseK, &lookK, &mixK, &outK })
            {
                k->setBounds (R (x, 352, 94, 108));
                x += 94;
            }
            modeLbl.setBounds (R (40, 466, 120, 18));
            modeSeg.setBounds (R (40, 484, 200, 30));
            detectLbl.setBounds (R (284, 466, 120, 18));
            detectSeg.setBounds (R (284, 484, 270, 30));
            listenLbl.setBounds (R (598, 466, 120, 18));
            listenSeg.setBounds (R (598, 484, 262, 30));
            status.setBounds (R (40, 528, 820, 50));
        }
        infoPopup.setBounds (getLocalBounds());
        if (about != nullptr)
            about->setBounds (getLocalBounds());
        licenceLock.setBounds (getLocalBounds());
    }

    void EmberPanel::setVoice (float hz)
    {
        if (auto* p = processor.state.getParameter (pid::freq))
            ui::gestures::setReal (*p, hz);
        refreshButtons();
    }

    void EmberPanel::autoClicked()
    {
        using Phase = EmberAudioProcessor::AutoPhase;
        settings.setAutoUsed();
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

    void EmberPanel::showPresetMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader (tr ("Werks-Presets", "Factory presets"));
        const auto& factory = factoryPresets();
        for (int i = 0; i < (int) factory.size(); ++i)
            menu.addItem (1 + i, presetName (factory[(size_t) i]), true, processor.getFactoryIndex() == i);
        const auto users = userPresets();
        menu.addSectionHeader (tr ("Eigene Presets", "Your presets"));
        if (users.isEmpty())
            menu.addItem (-1, tr ("(noch keine – mit SPEICHERN anlegen)", "(none yet - create one with SAVE)"), false);
        for (int i = 0; i < users.size(); ++i)
            menu.addItem (100 + i, users[i].getFileNameWithoutExtension(), true,
                          processor.getFactoryIndex() < 0 && processor.getPresetName() == users[i].getFileNameWithoutExtension());
        menu.addSeparator();
        menu.addItem (1000, tr ("Preset speichern …", "Save preset …"));
        menu.addItem (1001, tr ("Preset-Ordner öffnen", "Open preset folder"));
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetBtn),
                            [this, users] (int id)
                            {
                                if (id >= 1 && id < 100)
                                    processor.loadFactoryPreset (id - 1);
                                else if (id >= 100 && id < 1000)
                                    processor.loadUserPreset (users[id - 100]);
                                else if (id == 1000)
                                    savePresetAs();
                                else if (id == 1001)
                                {
                                    userPresetFolder().createDirectory();
                                    userPresetFolder().startAsProcess();
                                }
                            });
    }

    void EmberPanel::savePresetAs()
    {
        userPresetFolder().createDirectory();
        chooser = std::make_unique<juce::FileChooser> (tr ("Preset speichern", "Save preset"),
                                                       userPresetFolder().getChildFile (tr ("Mein Preset", "My preset") + presetExtension),
                                                       juce::String ("*") + presetExtension);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file != juce::File())
                                      processor.saveUserPreset (file);
                              });
    }

    void EmberPanel::showMainMenu()
    {
        juce::PopupMenu lang;
        const auto current = settings.getLanguage();
        lang.addItem (11, tr ("Wie das System", "Same as the system"), true, current == "system");
        lang.addItem (12, "Deutsch", true, current == "de");
        lang.addItem (13, "English", true, current == "en");
        juce::PopupMenu menu;
        menu.addSubMenu (tr ("Sprache", "Language"), lang);
        menu.addItem (20, tr ("Handbuch öffnen", "Open the manual"));
        menu.addItem (21, tr ("Ember-Tipp wieder zeigen", "Show Ember's tip again"));
        auto& licence = processor.getLicence();
        if (licence.isOn())
        {
            // Licence > status line, enter key, release this computer, buy
            const bool licensed = licence.status().access == vocalypse::licensing::Access::licensed;
            juce::PopupMenu lic;
            lic.addItem (-1, licence.describe(), false);
            lic.addSeparator();
            lic.addItem (40, tr ("Lizenzschlüssel eingeben …", "Enter licence key …"), ! licensed);
            lic.addItem (41, tr ("Diesen Computer freigeben", "Release this computer"), licensed);
            lic.addItem (42, tr ("EMBER kaufen", "Buy EMBER"), ! licensed && licence.buyUrl().isNotEmpty());
            menu.addSubMenu (tr ("Lizenz", "Licence"), lic);
        }
        menu.addSeparator();
        menu.addItem (30, tr ("Über EMBER …", "About EMBER …"));
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuBtn),
                            [this] (int id)
                            {
                                auto& lic = processor.getLicence();
                                if (id == 40)
                                    vocalypse::ui::showActivationDialog (lic, getTopLevelComponent());
                                else if (id == 41)
                                    lic.deactivate ([] (bool ok, const juce::String& message)
                                    {
                                        if (message.isNotEmpty())
                                            juce::AlertWindow::showMessageBoxAsync (ok ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon,
                                                                                    tr ("Lizenz", "Licence"), message);
                                    });
                                else if (id == 42)
                                    juce::URL (lic.buyUrl()).launchInDefaultBrowser();
                                if (id >= 11 && id <= 13)
                                {
                                    const juce::String pref = id == 11 ? "system" : (id == 12 ? "de" : "en");
                                    if (onLanguage)
                                        juce::MessageManager::callAsync ([cb = onLanguage, pref] { cb (pref); });
                                }
                                else if (id == 20)
                                {
                                    // the installer puts the manual into ProgramData; fall back to the website
                                    const auto local = juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                                                           .getChildFile ("Lind Audio").getChildFile ("EMBER")
                                                           .getChildFile (i18n::isGerman() ? "EMBER_Handbuch.html" : "EMBER_Manual.html");
                                    if (local.existsAsFile())
                                        local.startAsProcess();
                                    else
                                        juce::URL ("https://lind-audio.com/ember").launchInDefaultBrowser();
                                }
                                else if (id == 21)
                                {
                                    settings.file().setValue ("autoUsed", false);
                                    settings.file().saveIfNeeded();
                                }
                                else if (id == 30)
                                {
                                    about->setVisible (true);
                                    about->toFront (false);
                                }
                            });
    }

    void EmberPanel::refreshButtons()
    {
        using Phase = EmberAudioProcessor::AutoPhase;
        const auto phase = processor.getAutoPhase();
        autoBtn.setEnabled (phase != Phase::analysing);
        if (phase == Phase::analysing)
            autoBtn.setButtonText (utf8 ("…"));
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

        const auto name = processor.getPresetName() + (processor.isPresetModified() ? " *" : "");
        if (presetBtn.getButtonText() != name)
            presetBtn.setButtonText (name);
        slotA.setToggleState (processor.getActiveSlot() == 0, juce::dontSendNotification);
        slotB.setToggleState (processor.getActiveSlot() == 1, juce::dontSendNotification);
        slotCopy.setButtonText (processor.getActiveSlot() == 0 ? utf8 ("A → B") : utf8 ("B → A"));
    }

    void EmberPanel::timerCallback()
    {
        using Phase = EmberAudioProcessor::AutoPhase;
        const float gr = processor.engine.gainReductionDb.pull();
        const float in = processor.engine.inputPeak.pull();
        processor.engine.outputPeak.pull();
        shownGr = gr > shownGr ? gr : shownGr + (gr - shownGr) * 0.25f;

        const auto phase = processor.getAutoPhase();
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const bool bypassed = processor.state.getRawParameterValue (pid::bypass)->load() > 0.5f;
        MascotInput mi;
        mi.bypass = bypassed;
        mi.hasSignal = in > 0.00316f;
        mi.autoListening = phase == Phase::listening || phase == Phase::analysing;
        mi.autoJustFinished = phase == Phase::done && lastPhase != Phase::done;
        mi.reductionDb = shownGr;
        mascot.setInput (mi);

        if (phase != lastPhase && (phase == Phase::done || phase == Phase::failed))
            messageUntil = now + 8.0;
        lastPhase = phase;

        juce::String text;
        if (phase == Phase::listening)
        {
            text = processor.getAutoMessage() + "  " + juce::String (juce::roundToInt (processor.getAutoProgress() * 100.0)) + " %";
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
            mascot.setBubble (settings.wasAutoUsed() || bypassed ? juce::String() : tr ("Klick mich für AUTO!", "Click me for AUTO!"));
            if (bypassed)
                text = tr ("Bypass: der De-Esser ist aus, das Signal läuft unbearbeitet durch.", "Bypass: the de-esser is off, the signal passes unprocessed.");
            else if (advanced)
                text = tr ("Fahr mit der Maus über einen Regler, um zu sehen, was er macht. Doppelklick setzt ihn zurück.",
                           "Hover over a control to see what it does. Double-click resets it.");
            else
                text = tr ("Spiel deinen Gesang ab und klick auf Ember oder AUTO: Er stellt sich auf deine Stimme ein.",
                           "Play your vocal and click Ember or AUTO: he sets himself up for your voice.");
        }
        if (status.getText() != text)
            status.setText (text, juce::dontSendNotification);

        meter.setValue (shownGr);
        if (advanced)
        {
            spectrumSource.update (processor.getCurrentSampleRate());
            display.pushReduction (gr);
            display.refresh();
            detector.setLevel (bypassed ? -100.0f : processor.engine.detectorDb.load(), processor.engine.detectorWeight.load());
        }
        refreshButtons();
    }

    // ================================================================ editor
    void EmberEditor::applyLanguage (const juce::String& preference)
    {
        i18n::setCurrent (i18n::resolve (i18n::preferenceFromString (preference)));
    }

    EmberEditor::EmberEditor (EmberAudioProcessor& p) : AudioProcessorEditor (p), processor (p)
    {
        applyLanguage (settings->getLanguage());
        setLookAndFeel (&lnf);
        rebuildPanel();
        setResizable (true, true);
        setResizeLimits (look::baseW * 3 / 4, look::baseH * 3 / 4, look::baseW * 2, look::baseH * 2);
        if (auto* cons = getConstrainer())
            cons->setFixedAspectRatio ((double) look::baseW / look::baseH);
        const int w = juce::jlimit (look::baseW * 3 / 4, look::baseW * 2, settings->getEditorWidth (look::baseW));
        setSize (w, w * look::baseH / look::baseW);
    }

    EmberEditor::~EmberEditor()
    {
        panel.reset();
        setLookAndFeel (nullptr);
    }

    void EmberEditor::rebuildPanel()
    {
        panel.reset();
        juce::Component::SafePointer<EmberEditor> safe (this);
        panel = std::make_unique<EmberPanel> (processor, *settings, [safe] (juce::String pref) { if (safe != nullptr) safe->setLanguage (pref); });
        addAndMakeVisible (*panel);
        resized();
    }

    void EmberEditor::setLanguage (const juce::String& preference)
    {
        settings->setLanguage (preference);
        applyLanguage (preference);
        rebuildPanel();
    }

    void EmberEditor::paint (juce::Graphics& g) { g.fillAll (c::windowBg); }

    void EmberEditor::resized()
    {
        if (panel == nullptr)
            return;
        const float scale = (float) getWidth() / (float) look::baseW;
        panel->setBounds (0, 0, look::baseW, look::baseH);
        panel->setTransform (juce::AffineTransform::scale (scale));
        if (getWidth() > 0)
            settings->setEditorWidth (getWidth());
    }

    bool EmberEditor::isAdvanced() const { return panel != nullptr && panel->isAdvanced(); }
    void EmberEditor::setAdvanced (bool a) { if (panel != nullptr) panel->setAdvanced (a); }
    void EmberEditor::tickForTest() { if (panel != nullptr) panel->tick(); }
}
