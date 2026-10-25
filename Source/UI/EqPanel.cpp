#include "EqPanel.h"
#include "HelpContent.h"
#include "../PluginProcessor.h"

namespace velvox::ui
{
    using dsp::EqBandType;
    using dsp::ModuleId;

    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }
    }

    //==========================================================================
    namespace eqedit
    {
        namespace
        {
            eqbands::Access access (VelvoxAudioProcessor& proc) { return eqbands::accessFor (proc.getState()); }

            // Runs `fn` as one undo step; a macro-only band gets its parameters first (same step)
            template <typename Fn>
            void edit (VelvoxAudioProcessor& proc, int instance, int band, const juce::String& name, Fn&& fn)
            {
                if (band < 0 || band >= dsp::maxEqBands)
                    return;
                proc.getUndo().perform (name, [&]
                {
                    materialise (proc, instance, band);
                    fn (access (proc));
                });
            }
        }

        juce::String paramId (int instance, int band, const char* field) { return eqbands::bandParamId (instance, band, field); }

        dsp::EqBand parameterBand (VelvoxAudioProcessor& proc, int instance, int band)
        {
            return eqbands::readBand (access (proc), instance, band);
        }

        ShownBands shownBands (VelvoxAudioProcessor& proc, int instance)
        {
            ShownBands out;
            const auto eff = proc.readEffectiveSettings().instance (instance).eq;
            const auto a = access (proc);
            for (int i = 0; i < dsp::maxEqBands; ++i)
            {
                auto& s = out[(size_t) i];
                const auto param = eqbands::readBand (a, instance, i);
                s.band = eff.bands[(size_t) i];
                s.used = ! s.band.isFree();
                s.macroOnly = s.used && param.isFree();
                s.gainOffset = s.band.gain - (s.macroOnly ? 0.0f : param.gain);
            }
            return out;
        }

        bool materialise (VelvoxAudioProcessor& proc, int instance, int band)
        {
            if (band < 0 || band >= dsp::maxEqBands)
                return false;
            const auto s = shownBands (proc, instance)[(size_t) band];
            if (! s.macroOnly)
                return false;
            auto b = s.band;
            b.gain = 0.0f;   // the macro adds its share on top (Macros.cpp, roleBand)
            b.on = true;
            eqbands::writeBand (access (proc), instance, band, b);
            return true;
        }

        int addBand (VelvoxAudioProcessor& proc, int instance, float freqHz, float gainDb)
        {
            // A slot free in the parameters AND in the effective settings, so a macro band keeps its place
            const auto shown = shownBands (proc, instance);
            const auto a = access (proc);
            int slot = -1;
            for (int i = 0; i < dsp::maxEqBands && slot < 0; ++i)
                if (! shown[(size_t) i].used && eqbands::readBand (a, instance, i).isFree())
                    slot = i;
            if (slot < 0)
                slot = eqbands::findFreeBand (a, instance);
            if (slot < 0)
                return -1;

            dsp::EqBand b;
            b.on = true;
            b.type = (int) EqBandType::bell;
            b.freq = juce::jlimit (dsp::eqdesign::minFreq, dsp::eqdesign::maxFreq, freqHz);
            b.gain = juce::jlimit (-dsp::eqdesign::maxGainDb, dsp::eqdesign::maxGainDb, gainDb);
            b.q = 1.0f;
            proc.getUndo().perform (de ("EQ-Band hinzufügen"), [&] { eqbands::writeBand (a, instance, slot, b); });
            return slot;
        }

        void deleteBand (VelvoxAudioProcessor& proc, int instance, int band)
        {
            if (band < 0 || band >= dsp::maxEqBands)
                return;
            proc.getUndo().perform (de ("EQ-Band löschen"), [&] { eqbands::writeBand (access (proc), instance, band, dsp::EqBand {}); });
        }

        void setType (VelvoxAudioProcessor& proc, int instance, int band, EqBandType type)
        {
            edit (proc, instance, band, "EQ-Band: Typ", [&] (const eqbands::Access& a)
            {
                a.set (paramId (instance, band, pid::eqband::type), (float) type);
            });
        }

        void setSlope (VelvoxAudioProcessor& proc, int instance, int band, int slopeIndex)
        {
            edit (proc, instance, band, "EQ-Band: Steilheit", [&] (const eqbands::Access& a)
            {
                a.set (paramId (instance, band, pid::eqband::slope), (float) juce::jlimit (0, dsp::numEqSlopes - 1, slopeIndex));
            });
        }

        void toggleOn (VelvoxAudioProcessor& proc, int instance, int band)
        {
            edit (proc, instance, band, "EQ-Band ein/aus", [&] (const eqbands::Access& a)
            {
                const auto id = paramId (instance, band, pid::eqband::on);
                a.set (id, a.get (id) > 0.5f ? 0.0f : 1.0f);
            });
        }

        void resetBand (VelvoxAudioProcessor& proc, int instance, int band)
        {
            edit (proc, instance, band, de ("EQ-Band zurücksetzen"), [&] (const eqbands::Access& a)
            {
                const dsp::EqBand defaults;
                a.set (paramId (instance, band, pid::eqband::gain), defaults.gain);
                a.set (paramId (instance, band, pid::eqband::q), defaults.q);
                a.set (paramId (instance, band, pid::eqband::slope), (float) defaults.slope);
            });
        }

        juce::String typeName (int type)
        {
            switch ((EqBandType) juce::jlimit (0, dsp::numEqBandTypes - 1, type))
            {
                case EqBandType::bell:      return "Bell";
                case EqBandType::lowShelf:  return "Low Shelf";
                case EqBandType::highShelf: return "High Shelf";
                case EqBandType::lowCut:    return "Low Cut";
                case EqBandType::highCut:   return "High Cut";
                case EqBandType::notch:     return "Notch";
                case EqBandType::bandPass:  return "Band Pass";
                case EqBandType::tilt:      return "Tilt";
            }
            return "Bell";
        }

        juce::String roleTag (int role)
        {
            switch ((dsp::EqRole) juce::jlimit (0, dsp::numEqRoles - 1, role))
            {
                case dsp::EqRole::none:     return {};
                case dsp::EqRole::presence: return "PRES";
                case dsp::EqRole::warmth:   return "WARM";
                case dsp::EqRole::air:      return "AIR";
                case dsp::EqRole::lowCut:   return "LC";
                case dsp::EqRole::mud:      return "MUD";
                case dsp::EqRole::autoMix:  return "AUTO";
                case dsp::EqRole::highCut:  return "HC";
            }
            return {};
        }

        bool hasGain (int type) noexcept
        {
            const auto t = (EqBandType) type;
            return t == EqBandType::bell || t == EqBandType::lowShelf || t == EqBandType::highShelf || t == EqBandType::tilt;
        }

        bool hasQ (int type) noexcept
        {
            const auto t = (EqBandType) type;
            return t == EqBandType::bell || t == EqBandType::lowShelf || t == EqBandType::highShelf || t == EqBandType::notch
                || t == EqBandType::bandPass;
        }

        juce::Colour bandColour (int band)
        {
            // 16 distinct hues, starting warm so band 1 differs from the teal sum curve
            static const juce::uint32 palette[dsp::maxEqBands] {
                0xfff59e0b, 0xfff472b6, 0xffa78bfa, 0xff60a5fa, 0xff34d399, 0xfffacc15, 0xfffb7185, 0xffc084fc,
                0xff38bdf8, 0xffa3e635, 0xfffb923c, 0xffe879f9, 0xff818cf8, 0xff22d3ee, 0xff4ade80, 0xfffda4af };
            return juce::Colour (palette[(size_t) juce::jlimit (0, dsp::maxEqBands - 1, band)]);
        }
    }

    //==========================================================================
    namespace
    {
        const juce::Colour eqColour { 0xff2dd4bf };   // spec 7: EQ module colour, the sum curve

        juce::String hzText (float f)
        {
            return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
        }

        juce::String dbText (float db) { return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB"; }

        juce::RangedAudioParameter* paramFor (VelvoxAudioProcessor& proc, int instance, int band, const char* field)
        {
            return dynamic_cast<juce::RangedAudioParameter*> (proc.getState().getParameter (eqedit::paramId (instance, band, field)));
        }

        void setReal (juce::RangedAudioParameter* p, float v)
        {
            if (p != nullptr && std::isfinite (v))
                p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (v)));
        }
    }

    EqGraph::EqGraph (VelvoxAudioProcessor& p, const SpectrumSource* spectrum, int eqInstance)
        : proc (p), instance (eqInstance), spectrumView (spectrum)
    {
        spectrumView.setRange (-90.0f, 0.0f);
        spectrumView.setIntensity (0.55f);
        addAndMakeVisible (spectrumView);
        setWantsKeyboardFocus (true);
        for (int i = 0; i < numPoints; ++i)
            freqs[(size_t) i] = SpectrumSource::minHz * std::pow (SpectrumSource::maxHz / SpectrumSource::minHz, (float) i / (float) (numPoints - 1));
        refreshCurves();
    }

    EqGraph::~EqGraph()
    {
        endWheelGesture();
        endDrag();
    }

    std::unique_ptr<ModuleView> createEqGraph (VelvoxAudioProcessor& p, const SpectrumSource* spectrum, int instance)
    {
        return std::make_unique<EqGraph> (p, spectrum, instance);
    }

    //==========================================================================
    // Geometry

    juce::Rectangle<float> EqGraph::plotArea() const noexcept
    {
        // Room for the frequency labels below and the dB labels inside on the right
        return getLocalBounds().toFloat().withTrimmedTop (12.0f).withTrimmedBottom (24.0f).reduced (10.0f, 0.0f);
    }

    float EqGraph::xForFreq (float hz) const noexcept
    {
        const auto r = plotArea();
        return r.getX() + r.getWidth() * std::log (std::max (1.0f, hz) / SpectrumSource::minHz) / std::log (SpectrumSource::maxHz / SpectrumSource::minHz);
    }

    float EqGraph::freqForX (float x) const noexcept
    {
        const auto r = plotArea();
        const float t = (x - r.getX()) / std::max (1.0f, r.getWidth());
        return juce::jlimit (dsp::eqdesign::minFreq, dsp::eqdesign::maxFreq,
                             SpectrumSource::minHz * std::pow (SpectrumSource::maxHz / SpectrumSource::minHz, t));
    }

    float EqGraph::yForGain (float db) const noexcept
    {
        const auto r = plotArea();
        return juce::jmap (db, -rangeDb, rangeDb, r.getBottom(), r.getY());
    }

    float EqGraph::gainForY (float y) const noexcept
    {
        const auto r = plotArea();
        return juce::jmap (y, r.getBottom(), r.getY(), -rangeDb, rangeDb);
    }

    juce::Point<float> EqGraph::pointFor (int band) const
    {
        if (band < 0 || band >= dsp::maxEqBands)
            return {};
        const auto& b = shown[(size_t) band].band;
        const auto r = plotArea();
        const float x = juce::jlimit (r.getX(), r.getRight(), xForFreq (b.freq));
        const float gain = eqedit::hasGain (b.type) ? juce::jlimit (-rangeDb, rangeDb, b.gain) : 0.0f;
        return { x, yForGain (gain) };
    }

    int EqGraph::bandAt (juce::Point<float> p) const
    {
        int best = -1;
        float bestD = 13.0f;
        for (int i = 0; i < dsp::maxEqBands; ++i)
        {
            if (! shown[(size_t) i].used)
                continue;
            // The selected point wins a tie, so stacked points stay reachable
            const float d = pointFor (i).getDistanceFrom (p) - (i == selected ? 3.0f : 0.0f);
            if (d < bestD)
            {
                bestD = d;
                best = i;
            }
        }
        return best;
    }

    void EqGraph::resized()
    {
        const auto r = plotArea();
        // The spectrum shares the frequency axis; its labels sit in the strip below the plot
        spectrumView.setBounds (juce::Rectangle<float> (r.getX(), r.getY(), r.getWidth(), (float) getHeight() - r.getY() - 4.0f).toNearestInt());
        haveCurves = false;
        refreshCurves();
    }

    //==========================================================================
    // Curves

    void EqGraph::refreshCurves()
    {
        const auto eff = proc.readEffectiveSettings().instance (instance);
        const bool changed = ! haveCurves || eff.eqOn != eqOn || std::abs (eff.eqMix - mix) > 1.0e-4f
                          || eff.eq.legacyResponse != lastSettings.legacyResponse || eff.eq.bands != lastSettings.bands
                          || getLocalBounds() != lastSize;
        shown = eqedit::shownBands (proc, instance);   // macro-only state can change without a new curve
        if (! changed)
            return;

        eqOn = eff.eqOn;
        mix = eff.eqMix;
        lastSettings = eff.eq;
        lastSize = getLocalBounds();
        haveCurves = true;
        if (selected >= 0 && ! shown[(size_t) selected].used)
            setSelected (-1);

        const double fs = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
        const auto r = plotArea();
        const float zeroY = yForGain (0.0f);
        const auto build = [&] (const std::array<float, numPoints>& db, juce::Path& stroke, juce::Path& fill)
        {
            stroke.clear();
            fill.clear();
            fill.startNewSubPath (r.getX(), zeroY);
            for (int i = 0; i < numPoints; ++i)
            {
                const float x = r.getX() + r.getWidth() * (float) i / (float) (numPoints - 1);
                const float y = yForGain (juce::jlimit (-rangeDb - 4.0f, rangeDb + 4.0f, std::isfinite (db[(size_t) i]) ? db[(size_t) i] : -100.0f));
                if (i == 0)
                    stroke.startNewSubPath (x, y);
                else
                    stroke.lineTo (x, y);
                fill.lineTo (x, y);
            }
            fill.lineTo (r.getRight(), zeroY);
            fill.closeSubPath();
        };

        for (int b = 0; b < dsp::maxEqBands; ++b)
        {
            if (! shown[(size_t) b].used)
            {
                bandStroke[(size_t) b].clear();
                bandFill[(size_t) b].clear();
                continue;
            }
            dsp::EqualizerModule::responseCurveDb (eff.eq, fs, freqs.data(), bandDb[(size_t) b].data(), numPoints, b);
            build (bandDb[(size_t) b], bandStroke[(size_t) b], bandFill[(size_t) b]);
        }

        dsp::EqualizerModule::responseCurveDb (eff.eq, fs, freqs.data(), sumDb.data(), numPoints, -1);
        for (auto& db : sumDb)
        {
            // Dry / wet: magnitude approximation of the blend (as the 0.5 curve did)
            const double lin = 1.0 + (std::pow (10.0, (double) db / 20.0) - 1.0) * (double) mix;
            db = (float) (20.0 * std::log10 (std::max (lin, 1.0e-6)));
        }
        build (sumDb, sumStroke, sumFill);
        repaint();
    }

    float EqGraph::responseAt (int band, float hz) const
    {
        const auto r = plotArea();
        const float t = juce::jlimit (0.0f, 1.0f, (xForFreq (hz) - r.getX()) / std::max (1.0f, r.getWidth()));
        const auto& db = band < 0 ? sumDb : bandDb[(size_t) band];
        return db[(size_t) juce::roundToInt (t * (float) (numPoints - 1))];
    }

    //==========================================================================
    void EqGraph::update (const LiveData&)
    {
        spectrumView.refresh();
        refreshCurves();

        // Hover card: after 600 ms on a point, at once while dragging
        int card = dragged >= 0 ? dragged : -1;
        if (card < 0 && hovered >= 0 && juce::Time::getMillisecondCounter() - hoverSince >= (juce::uint32) motion::hoverInfoDelayMs)
            card = hovered;
        if (card >= 0)
        {
            const auto& s = shown[(size_t) card];
            const auto& b = s.band;
            juce::String title = "BAND " + juce::String (card + 1) + "  " + eqedit::typeName (b.type).toUpperCase();
            if (const auto tag = eqedit::roleTag (b.role); tag.isNotEmpty())
                title << "  " << juce::String::charToString ((juce::juce_wchar) 0x00b7) << "  " << tag;
            juce::String values = hzText (b.freq);
            if (eqedit::hasGain (b.type))
                values << "   " << dbText (b.gain);
            if (eqedit::hasQ (b.type))
                values << "   Q " << juce::String (b.q, 2);
            if (b.isCut())
                values << "   " << dsp::eqSlopeDbPerOct (b.slope) << " dB/oct";
            if (! b.on)
                values << "   AUS";
            if (card != cardBand || title != cardTitle || values != cardValues)
            {
                cardTitle = title;
                cardValues = values;
                cardWidth = (float) std::max (juce::GlyphArrangement::getStringWidthInt (semiBold (9.0f, 0.1f), cardTitle),
                                              juce::GlyphArrangement::getStringWidthInt (monoFont (11.0f), cardValues)) + 24.0f;
                cardBand = card;
                repaint();
            }
        }
        else if (cardBand >= 0)
        {
            cardBand = -1;
            repaint();
        }
    }

    //==========================================================================
    // Drawing

    void EqGraph::paint (juce::Graphics&) {}

    void EqGraph::paintOverChildren (juce::Graphics& g)
    {
        const auto r = plotArea();

        // dB guides and labels (+12 / +6 / 0 / -6 / -12)
        static const juce::String dbLabels[] { "+12", "+6", "0", "-6", "-12" };
        static const float dbValues[] { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f };
        g.setFont (medium (10.0f));
        for (int i = 0; i < 5; ++i)
        {
            const float y = yForGain (dbValues[i]);
            g.setColour (colours::accent.withAlpha (dbValues[i] == 0.0f ? 0.10f : 0.035f));
            g.fillRect (juce::Rectangle<float> (r.getX(), y, r.getWidth(), 1.0f));
            g.setColour (colours::textFaint);
            g.drawText (dbLabels[i], juce::Rectangle<float> (r.getRight() - 30.0f, y - 13.0f, 28.0f, 12.0f), juce::Justification::right, false);
        }

        const float active = eqOn ? 1.0f : 0.4f;
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (r.expanded (0.0f, 2.0f).toNearestInt());

            // Each band faint in its colour, the selected one a little brighter
            for (int b = 0; b < dsp::maxEqBands; ++b)
            {
                if (! shown[(size_t) b].used)
                    continue;
                const auto c = eqedit::bandColour (b);
                const bool sel = b == selected;
                const float on = shown[(size_t) b].band.on ? 1.0f : 0.35f;
                g.setColour (c.withAlpha ((sel ? 0.13f : 0.06f) * on * active));
                g.fillPath (bandFill[(size_t) b]);
                g.setColour (c.withAlpha ((sel ? 0.75f : 0.32f) * on * active));
                g.strokePath (bandStroke[(size_t) b], juce::PathStrokeType (sel ? 1.4f : 1.0f));
            }

            // The sum: bright, with a soft glow
            g.setColour (eqColour.withAlpha (0.05f * active));
            g.fillPath (sumFill);
            g.setColour (eqColour.withAlpha (0.16f * active));
            g.strokePath (sumStroke, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (eqColour.interpolatedWith (juce::Colours::white, 0.15f).withAlpha (active));
            g.strokePath (sumStroke, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Points: band colour, number inside; role bands carry a small tag
        for (int b = 0; b < dsp::maxEqBands; ++b)
        {
            const auto& s = shown[(size_t) b];
            if (! s.used)
                continue;
            const auto p = pointFor (b);
            const auto c = eqedit::bandColour (b);
            const bool sel = b == selected, hot = b == hovered || b == dragged;
            const float on = s.band.on ? 1.0f : 0.4f;
            const float size = sel ? 17.0f : 15.0f;
            if (sel || hot)
                drawGlow (g, p, 20.0f, 0.35f * on, c);
            g.setColour (colours::panelInset);
            g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (p));
            g.setColour (c.withAlpha (s.band.on ? (sel ? 1.0f : 0.85f) : 0.35f));
            g.fillEllipse (juce::Rectangle<float> (size - 3.0f, size - 3.0f).withCentre (p));
            if (sel)
            {
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawEllipse (juce::Rectangle<float> (size, size).withCentre (p), 1.4f);
            }
            g.setColour (colours::panelInset.withAlpha (0.9f));
            g.setFont (semiBold (9.0f));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (size, size).withCentre (p), juce::Justification::centred, false);

            if (s.band.role != (int) dsp::EqRole::none)
            {
                g.setColour (c.withAlpha (0.9f * on));
                g.setFont (semiBold (8.5f, 0.12f));
                const auto tagArea = juce::Rectangle<float> (40.0f, 11.0f).withCentre (p.translated (0.0f, size * 0.5f + 8.0f));
                g.drawText (eqedit::roleTag (s.band.role), tagArea, juce::Justification::centred, false);
            }
        }

        // Hover / drag card with the band's values
        if (cardBand >= 0 && shown[(size_t) cardBand].used)
        {
            const auto p = pointFor (cardBand);
            const float h = 40.0f;
            auto box = juce::Rectangle<float> (cardWidth, h).withCentre (p.translated (0.0f, -(h * 0.5f + 16.0f)));
            if (box.getY() < 2.0f)
                box.setY (p.y + 22.0f);
            box = box.constrainedWithin (getLocalBounds().toFloat().reduced (3.0f));
            g.setColour (colours::raised.withAlpha (0.95f));
            g.fillRoundedRectangle (box, 8.0f);
            g.setColour (eqedit::bandColour (cardBand).withAlpha (0.6f));
            g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);
            auto inner = box.reduced (12.0f, 5.0f);
            g.setColour (colours::textMuted);
            g.setFont (semiBold (9.0f, 0.1f));
            g.drawText (cardTitle, inner.removeFromTop (14.0f), juce::Justification::centredLeft, false);
            g.setColour (colours::text);
            g.setFont (monoFont (11.0f));
            g.drawText (cardValues, inner, juce::Justification::centredLeft, false);
        }
    }

    //==========================================================================
    // Selection and menu

    void EqGraph::setSelected (int band)
    {
        band = band >= 0 && band < dsp::maxEqBands ? band : -1;
        if (band == selected)
            return;
        selected = band;
        if (onSelect)
            onSelect (band);
        repaint();
    }

    juce::PopupMenu EqGraph::buildPointMenu (int band)
    {
        juce::PopupMenu menu;
        if (band < 0 || band >= dsp::maxEqBands)
            return menu;
        const auto& b = shown[(size_t) band].band;
        menu.addSectionHeader ("Band " + juce::String (band + 1));
        for (int t = 0; t < dsp::numEqBandTypes; ++t)
            menu.addItem (menuTypeBase + t, eqedit::typeName (t), true, b.type == t);
        if (b.isCut())
        {
            juce::PopupMenu slopes;
            for (int s = 0; s < dsp::numEqSlopes; ++s)
                slopes.addItem (menuSlopeBase + s, juce::String (dsp::eqSlopeDbPerOct (s)) + " dB/oct", true, b.slope == s);
            menu.addSeparator();
            menu.addSubMenu ("Steilheit", slopes);
        }
        menu.addSeparator();
        menu.addItem (menuToggle, "Ein", true, b.on);
        menu.addItem (menuDelete, juce::String::fromUTF8 ("Löschen"));
        return menu;
    }

    void EqGraph::performMenu (int band, int id)
    {
        if (band < 0 || band >= dsp::maxEqBands || id == 0)
            return;
        if (id >= menuTypeBase && id < menuTypeBase + dsp::numEqBandTypes)
            eqedit::setType (proc, instance, band, (EqBandType) (id - menuTypeBase));
        else if (id >= menuSlopeBase && id < menuSlopeBase + dsp::numEqSlopes)
            eqedit::setSlope (proc, instance, band, id - menuSlopeBase);
        else if (id == menuToggle)
            eqedit::toggleOn (proc, instance, band);
        else if (id == menuDelete)
        {
            eqedit::deleteBand (proc, instance, band);
            haveCurves = false;
            refreshCurves();
            if (selected == band || selected < 0)
            {
                // Next band up, else the nearest one below, else none
                int next = -1;
                for (int i = band + 1; i < dsp::maxEqBands && next < 0; ++i)
                    if (shown[(size_t) i].used)
                        next = i;
                for (int i = band; --i >= 0 && next < 0;)
                    if (shown[(size_t) i].used)
                        next = i;
                setSelected (next);
            }
        }
        haveCurves = false;
        refreshCurves();
    }

    //==========================================================================
    // Mouse

    void EqGraph::mouseMove (const juce::MouseEvent& e)
    {
        const int b = bandAt (e.position);
        if (b != hovered)
        {
            hovered = b;
            hoverSince = juce::Time::getMillisecondCounter();
            setMouseCursor (b >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
            if (cardBand >= 0 && dragged < 0)
                cardBand = -1;
            repaint();
        }
    }

    void EqGraph::mouseExit (const juce::MouseEvent&)
    {
        hovered = -1;
        if (dragged < 0 && cardBand >= 0)
        {
            cardBand = -1;
            repaint();
        }
    }

    void EqGraph::mouseDown (const juce::MouseEvent& e)
    {
        endWheelGesture();
        endDrag();
        if (isShowing())
            grabKeyboardFocus();
        const int b = bandAt (e.position);

        if (e.mods.isPopupMenu())
        {
            if (b < 0)
            {
                // Empty space: the panel's own menu (reset module, presets ...)
                if (auto* parent = getParentComponent())
                    parent->mouseDown (e.getEventRelativeTo (parent));
                return;
            }
            setSelected (b);
            juce::Component::SafePointer<EqGraph> safe (this);
            buildPointMenu (b).showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> (1, 1).withCentre (pointFor (b).toInt()))),
                                              [safe, b] (int id) { if (safe != nullptr) safe->performMenu (b, id); });
            return;
        }

        setSelected (b);
        if (b < 0)
            return;
        if (e.mods.isAltDown())
        {
            eqedit::resetBand (proc, instance, b);
            haveCurves = false;
            refreshCurves();
            return;
        }
        beginDrag (b);
        dragLast = e.position;
    }

    void EqGraph::beginDrag (int band)
    {
        dragged = band;
        const auto& s = shown[(size_t) band];
        dragFreq = s.band.freq;
        dragGain = s.band.gain;
        dragOffset = s.gainOffset;
        materialised = ! s.macroOnly;
        if (auto* f = paramFor (proc, instance, band, pid::eqband::freq))
            f->beginChangeGesture();
        if (auto* g = paramFor (proc, instance, band, pid::eqband::gain))
            g->beginChangeGesture();
    }

    void EqGraph::endDrag()
    {
        if (dragged < 0)
            return;
        const int band = dragged;
        dragged = -1;
        // In reverse order: the outermost gesture (frequency) ends last and records the step
        if (auto* g = paramFor (proc, instance, band, pid::eqband::gain))
            g->endChangeGesture();
        if (auto* f = paramFor (proc, instance, band, pid::eqband::freq))
            f->endChangeGesture();
    }

    void EqGraph::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragged < 0)
            return;
        // Relative movement: Shift / Ctrl switch to fine (1/10) at any moment without a jump
        const auto delta = e.position - dragLast;
        dragLast = e.position;
        if (delta.isOrigin())
            return;
        const float scale = e.mods.isShiftDown() || e.mods.isCtrlDown() ? 0.1f : 1.0f;

        if (! materialised)
        {
            eqedit::materialise (proc, instance, dragged);   // inside the open gesture: same undo step
            materialised = true;
        }

        const int type = shown[(size_t) dragged].band.type;
        dragFreq = freqForX (xForFreq (dragFreq) + delta.x * scale);
        setReal (paramFor (proc, instance, dragged, pid::eqband::freq), dragFreq);
        if (eqedit::hasGain (type))
        {
            dragGain += (gainForY (dragLast.y) - gainForY (dragLast.y - delta.y)) * scale;
            dragGain = juce::jlimit (-dsp::eqdesign::maxGainDb, dsp::eqdesign::maxGainDb, dragGain);
            setReal (paramFor (proc, instance, dragged, pid::eqband::gain), dragGain - dragOffset);
        }
        refreshCurves();
    }

    void EqGraph::mouseUp (const juce::MouseEvent&)
    {
        endDrag();
        refreshCurves();
    }

    void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu() || e.mods.isAltDown())
            return;
        endDrag();
        const int b = bandAt (e.position);
        if (b >= 0)
        {
            eqedit::toggleOn (proc, instance, b);   // Pro-Q: double-click a band = enable / disable
        }
        else
        {
            const auto r = plotArea();
            if (! r.expanded (4.0f).contains (e.position))
                return;
            const int added = eqedit::addBand (proc, instance, freqForX (e.position.x),
                                               juce::jlimit (-rangeDb, rangeDb, gainForY (e.position.y)));
            haveCurves = false;
            refreshCurves();
            if (added >= 0)
                setSelected (added);
        }
        haveCurves = false;
        refreshCurves();
    }

    void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        const int b = bandAt (e.position);
        const float dy = wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f);
        const int type = b >= 0 ? shown[(size_t) b].band.type : -1;
        const bool usable = b >= 0 && (eqedit::hasQ (type) || shown[(size_t) b].band.isCut());
        if (! usable || std::abs (wheel.deltaX) > std::abs (wheel.deltaY) || dy == 0.0f)
        {
            endWheelGesture();
            ModuleView::mouseWheelMove (e, wheel);
            return;
        }

        if (shown[(size_t) b].band.isCut())
        {
            // Cuts have no Q: the wheel steps the slope, one step per notch
            endWheelGesture();
            if (wheelAccum * dy < 0.0f)
                wheelAccum = 0.0f;
            wheelAccum += dy;
            if (std::abs (wheelAccum) >= 0.08f)
            {
                const int slope = juce::jlimit (0, dsp::numEqSlopes - 1, shown[(size_t) b].band.slope + (wheelAccum > 0.0f ? 1 : -1));
                wheelAccum = 0.0f;
                if (slope != shown[(size_t) b].band.slope)
                    eqedit::setSlope (proc, instance, b, slope);
                haveCurves = false;
                refreshCurves();
            }
            return;
        }

        auto* q = paramFor (proc, instance, b, pid::eqband::q);
        if (q == nullptr)
            return;
        if (wheelBand != b)
        {
            endWheelGesture();
            wheelBand = b;
            q->beginChangeGesture();
            eqedit::materialise (proc, instance, b);   // inside the gesture: one step
        }
        const auto range = q->getNormalisableRange();
        setReal (q, eqWheelQ (shown[(size_t) b].band.q, dy, e.mods.isShiftDown() || e.mods.isCtrlDown(), range.start, range.end));
        setSelected (b);
        refreshCurves();
        startTimer (450);   // the gesture ends when the wheel rests
    }

    void EqGraph::timerCallback() { endWheelGesture(); }

    void EqGraph::endWheelGesture()
    {
        stopTimer();
        if (wheelBand >= 0)
            if (auto* q = paramFor (proc, instance, wheelBand, pid::eqband::q))
                q->endChangeGesture();
        wheelBand = -1;
    }

    bool EqGraph::keyPressed (const juce::KeyPress& key)
    {
        if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selected >= 0)
        {
            performMenu (selected, menuDelete);
            return true;
        }
        return false;
    }

    //==========================================================================
    // One cell of the selected-band row: the control bound to one parameter of the selected band.
    // Rebinding replaces the control (selection changes are rare, never per frame).
    class EqPanel::Slot final : public juce::Component
    {
    public:
        enum class Kind { none, knob, choice, toggle };

        void bind (APVTS& state, Kind k, const juce::String& id, const juce::String& label, juce::Colour hue)
        {
            if (k == kind && id == boundId)
                return;
            child.reset();
            kind = k;
            boundId = id;
            if (k == Kind::knob)
            {
                auto kn = std::make_unique<Knob> (state, id.toRawUTF8(), label, hue);
                kn->setProminence (Knob::Prominence::primary);
                child = std::move (kn);
            }
            else if (k == Kind::choice)
            {
                child = std::make_unique<Choice> (state, id.toRawUTF8(), label);
            }
            else if (k == Kind::toggle)
            {
                child = std::make_unique<Toggle> (state, id.toRawUTF8(), label, hue);
            }
            if (child != nullptr)
                addAndMakeVisible (*child);
            resized();
        }

        void setUsable (bool usable)
        {
            if (child != nullptr)
            {
                child->setEnabled (usable);
                child->setAlpha (usable ? 1.0f : 0.3f);
            }
        }

        void resized() override
        {
            if (child == nullptr)
                return;
            auto r = getLocalBounds();
            if (kind == Kind::knob)
                child->setBounds (r);
            else if (kind == Kind::choice)
                child->setBounds (r.withSizeKeepingCentre (r.getWidth(), std::min (r.getHeight(), 62)).translated (0, -6));
            else
                child->setBounds (r.withSizeKeepingCentre (std::min (r.getWidth(), 84), 26).translated (0, -8));
        }

    private:
        std::unique_ptr<juce::Component> child;
        Kind kind = Kind::none;
        juce::String boundId;
    };

    //==========================================================================
    EqPanel::EqPanel (VelvoxAudioProcessor& p)
        : DetailPanel (p, "Equalizer", help::shortDescription ((int) ModuleId::eq), pid::eqOn)
    {
        knob (pid::eqMix, "Mix");
        legacyResponse = &own (std::make_unique<juce::TextButton> (de ("Alte EQ-Kurve – umstellen")));
        legacyResponse->setTooltip (de ("Dieses EQ nutzt noch die Filterkurven von 0.5 (klingt wie das alte Projekt). "
                                        "Umstellen: analog genaue Kurven ohne Stauchung nahe 20 kHz."));
        legacyResponse->onClick = [this]
        {
            proc.getUndo().perform (de ("EQ: neue Filterkurven"), [this] { eqbands::clearLegacyResponse (apvts, getInstance()); });
            refresh();
        };
        lastLegacyShown = apvts.getRawParameterValue (pidFor (pid::eqLegacyResponse))->load() > 0.5f;
        legacyResponse->setVisible (lastLegacyShown);

        for (auto& s : slots)
            s = &own (std::make_unique<Slot>());
        setMainControls ({ slots[0], slots[1], slots[2], slots[3], slots[4] });

        noBand.setText ("Kein Band", juce::dontSendNotification);
        noBand.setFont (semiBold (10.0f, 0.12f));
        noBand.setColour (juce::Label::textColourId, colours::textFaint);
        noBand.setJustificationType (juce::Justification::centred);
        noBand.setInterceptsMouseClicks (false, false);
        addChildComponent (noBand);

        // Opening: the band selected last time in this EQ, else the first band there is
        const int k = juce::jlimit (0, 3, getInstance());
        const auto shown = eqedit::shownBands (proc, k);
        int first = -1;
        if (lastSelection[(size_t) k] >= 0 && lastSelection[(size_t) k] < dsp::maxEqBands && shown[(size_t) lastSelection[(size_t) k]].used)
            first = lastSelection[(size_t) k];
        for (int i = 0; i < dsp::maxEqBands && first < 0; ++i)
            if (shown[(size_t) i].used)
                first = i;
        selectBand (first);
    }

    EqPanel::~EqPanel()
    {
        if (auto* g = getGraph())
            g->onSelect = nullptr;
    }

    std::unique_ptr<DetailPanel> createEqPanel (VelvoxAudioProcessor& p) { return std::make_unique<EqPanel> (p); }

    EqGraph* EqPanel::getGraph() noexcept { return dynamic_cast<EqGraph*> (getVisual()); }

    void EqPanel::connectGraph()
    {
        auto* g = getGraph();
        if (graphConnected || g == nullptr)
            return;
        graphConnected = true;
        g->onSelect = [this] (int band) { selectBand (band); };
        g->setSelected (selectedBand);
    }

    void EqPanel::layoutVisual (juce::Rectangle<int>)
    {
        connectGraph();
    }

    void EqPanel::selectBand (int band)
    {
        band = band >= 0 && band < dsp::maxEqBands ? band : -1;
        if (band == selectedBand && boundBand == band)
            return;
        selectedBand = band;
        const int k = getInstance();
        lastSelection[(size_t) juce::jlimit (0, 3, k)] = band;

        // A band a macro created has no parameters yet: give it its own (same sound, not an edit)
        if (band >= 0 && eqedit::shownBands (proc, k)[(size_t) band].macroOnly)
        {
            proc.getUndo().beginSuppress();
            eqedit::materialise (proc, k, band);
            proc.getUndo().endSuppress();
        }
        if (auto* g = getGraph(); g != nullptr && g->getSelected() != band)
            g->setSelected (band);
        bindSlots();
    }

    void EqPanel::bindSlots()
    {
        using Kind = Slot::Kind;
        const int k = getInstance();
        const auto hue = moduleColour (ModuleId::eq);
        if (selectedBand < 0)
        {
            for (auto* s : slots)
            {
                s->bind (apvts, Kind::none, {}, {}, hue);
                s->setVisible (false);
            }
            noBand.setVisible (true);
            boundBand = -1;
            boundType = -1;
            layoutBandRow();
            repaint();
            return;
        }

        const auto b = eqedit::parameterBand (proc, k, selectedBand);
        const auto id = [&] (const char* field) { return eqedit::paramId (k, selectedBand, field); };
        slots[0]->bind (apvts, Kind::toggle, id (pid::eqband::on), "Ein", hue);
        slots[1]->bind (apvts, Kind::choice, id (pid::eqband::type), "Typ", hue);
        slots[2]->bind (apvts, Kind::knob, id (pid::eqband::freq), "Freq", hue);
        slots[3]->bind (apvts, Kind::knob, id (pid::eqband::gain), "Gain", hue);
        if (b.isCut())
            slots[4]->bind (apvts, Kind::choice, id (pid::eqband::slope), "Slope", hue);
        else
            slots[4]->bind (apvts, Kind::knob, id (pid::eqband::q), "Q", hue);
        slots[3]->setUsable (eqedit::hasGain (b.type));
        slots[4]->setUsable (b.isCut() || eqedit::hasQ (b.type));
        for (auto* s : slots)
            s->setVisible (true);
        noBand.setVisible (false);
        boundBand = selectedBand;
        boundType = b.type;
        badgeText = "BAND " + juce::String (selectedBand + 1);
        badgeTag = eqedit::roleTag (b.role);
        layoutBandRow();
        repaint();
    }

    void EqPanel::refresh()
    {
        DetailPanel::refresh();
        connectGraph();
        if (live == nullptr)
            if (auto* g = getGraph())
                g->refreshCurves();   // without live data (tests) the graph is not updated by the frame

        if (selectedBand >= 0 && eqedit::parameterBand (proc, getInstance(), selectedBand).type != boundType)
            bindSlots();

        const bool legacy = apvts.getRawParameterValue (pidFor (pid::eqLegacyResponse))->load() > 0.5f;
        if (legacy != lastLegacyShown)
        {
            lastLegacyShown = legacy;
            legacyResponse->setVisible (legacy);
            if (isMoreOpen())
                resized();
        }
    }

    void EqPanel::resized()
    {
        DetailPanel::resized();
        layoutBandRow();
    }

    void EqPanel::layoutBandRow()
    {
        auto row = getMainRowArea().withTrimmedTop (6);
        if (row.isEmpty())
            return;
        badgeArea = row.removeFromLeft (std::min (96, row.getWidth() / 6));
        const int widths[5] { 96, 150, 96, 96, 132 };
        const int gap = 16;
        int total = gap * 4;
        for (int w : widths)
            total += w;
        const float shrink = total > row.getWidth() ? (float) row.getWidth() / (float) total : 1.0f;
        float x = (float) row.getX() + (float) std::max (0, (row.getWidth() - badgeArea.getWidth() - total) / 2);
        for (int i = 0; i < 5; ++i)
        {
            const int w = (int) ((float) widths[i] * shrink);
            slots[(size_t) i]->setBounds ((int) x, row.getY(), std::min (w, row.getRight() - (int) x), row.getHeight());
            x += (float) (widths[i] + gap) * shrink;
        }
        noBand.setBounds (row.withSizeKeepingCentre (std::min (row.getWidth(), 220), 20));
    }

    void EqPanel::paintOverChildren (juce::Graphics& g)
    {
        // Badge of the selected band: its colour, number and role
        if (selectedBand < 0 || badgeArea.isEmpty())
            return;
        auto r = badgeArea.toFloat().withSizeKeepingCentre ((float) badgeArea.getWidth(), 44.0f).translated (0.0f, -8.0f);
        const auto c = eqedit::bandColour (selectedBand);
        const auto dot = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ r.getX() + 10.0f, r.getY() + 12.0f });
        drawGlow (g, dot.getCentre(), 12.0f, 0.3f, c);
        g.setColour (c);
        g.fillEllipse (dot);
        g.setColour (colours::textStrong);
        g.setFont (semiBold (11.0f, 0.15f));
        g.drawText (badgeText, juce::Rectangle<float> (r.getX() + 22.0f, r.getY() + 4.0f, r.getWidth() - 22.0f, 16.0f),
                    juce::Justification::centredLeft, false);
        if (badgeTag.isNotEmpty())
        {
            g.setColour (c.withAlpha (0.85f));
            g.setFont (semiBold (9.0f, 0.12f));
            g.drawText (badgeTag, juce::Rectangle<float> (r.getX() + 22.0f, r.getY() + 22.0f, r.getWidth() - 22.0f, 14.0f),
                        juce::Justification::centredLeft, false);
        }
    }
}
