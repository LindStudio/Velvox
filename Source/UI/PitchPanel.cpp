#include "PitchPanel.h"
#include "../PluginProcessor.h"
#include "HelpContent.h"
#include "../Text/I18n.h"
#include "../Text/ParamText.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        juce::String dash() { return juce::String::fromUTF8 ("–"); }

        // The semitone dial's rotary range (Knob: 1.25 pi .. 2.75 pi, clockwise from 12 o'clock)
        constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
        constexpr float endAngle = juce::MathConstants<float>::pi * 2.75f;
        constexpr int maxSemis = 24;
    }

    namespace pitchnotes
    {
        juce::String noteName (float hz)
        {
            if (! (hz > 0.0f))
                return dash();
            const int note = juce::roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f));
            return juce::MidiMessage::getMidiNoteName (juce::jlimit (0, 127, note), true, true, 4);
        }

        float centsOff (float hz)
        {
            if (! (hz > 0.0f))
                return 0.0f;
            const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
            return 100.0f * (midi - std::round (midi));
        }
    }

    //==========================================================================
    PitchStage::PitchStage (VelvoxAudioProcessor& p) : proc (p)
    {
        setInterceptsMouseClicks (false, false);   // the knobs on top take the gestures
        inText = outText = dash();
    }

    std::unique_ptr<ModuleView> createPitchStage (VelvoxAudioProcessor& proc)
    {
        return std::make_unique<PitchStage> (proc);
    }

    void PitchStage::setDialGeometry (juce::Point<float> centre, float radius)
    {
        dialCentre = centre;
        dialRadius = radius;
        repaint();
    }

    void PitchStage::update (const LiveData& d)
    {
        const auto& ps = d.settings.pitch;
        const bool changedSettings = ps.enabled != on || ps.semitones != semitones || ps.cents != cents || ps.mix != mix;
        on = ps.enabled;
        semitones = ps.semitones;
        cents = ps.cents;
        mix = ps.mix;

        const float hz = on ? proc.getChain().getPitch().getDetectedHz() : 0.0f;
        if (hz > 0.0f && std::isfinite (hz))
        {
            lastVoiced = d.time;
            // Glide within a note (vibrato), jump to a new one
            inHz = inHz > 0.0f && std::abs (std::log2 (hz / inHz)) < 0.04f ? inHz + (hz - inHz) * 0.35f : hz;
        }
        if (d.time < lastVoiced)
            lastVoiced = d.time;   // the clock restarted
        voiced = on && inHz > 0.0f && d.time - lastVoiced <= holdSeconds;
        if (! voiced)
            inHz = 0.0f;
        outHz = voiced ? inHz * std::exp2 ((semitones + cents * 0.01f) / 12.0f) : 0.0f;

        const auto newIn = voiced ? pitchnotes::noteName (inHz) : dash();
        const auto newOut = voiced ? pitchnotes::noteName (outHz) : dash();
        const auto signedCents = [] (float c) { const int r = juce::roundToInt (c); return (r > 0 ? "+" : "") + juce::String (r) + " ct"; };
        const auto newInDetail = voiced ? juce::String (juce::roundToInt (inHz)) + " Hz  " + signedCents (pitchnotes::centsOff (inHz)) : juce::String();
        const auto newOutDetail = voiced ? juce::String (juce::roundToInt (outHz)) + " Hz  " + signedCents (pitchnotes::centsOff (outHz)) : juce::String();
        if (changedSettings || newIn != inText || newOut != outText || newInDetail != inDetail || newOutDetail != outDetail)
        {
            inText = newIn;
            outText = newOut;
            inDetail = newInDetail;
            outDetail = newOutDetail;
            repaint();
        }
    }

    void PitchStage::paint (juce::Graphics& g)
    {
        const auto hue = moduleColour (ModuleId::pitch);
        const float a = on ? 1.0f : 0.45f;

        // Semitone ticks around the big dial: every semitone, octaves longer and labelled, the set value lit
        if (dialRadius > 10.0f)
        {
            const int current = juce::roundToInt (juce::jlimit (-24.0f, 24.0f, semitones));
            for (int k = -maxSemis; k <= maxSemis; ++k)
            {
                const float angle = startAngle + (endAngle - startAngle) * (float) (k + maxSemis) / (float) (2 * maxSemis);
                const bool octave = k % 12 == 0;
                const float r0 = dialRadius + 7.0f, r1 = r0 + (octave ? 8.0f : 4.0f);
                const auto p0 = dialCentre.getPointOnCircumference (r0, angle), p1 = dialCentre.getPointOnCircumference (r1, angle);
                if (k == current)
                {
                    drawGlow (g, p1, 9.0f, 0.5f * a, hue);
                    g.setColour (hue.withAlpha (a));
                    g.drawLine ({ p0, dialCentre.getPointOnCircumference (r1 + 3.0f, angle) }, 2.2f);
                }
                else
                {
                    g.setColour ((octave ? colours::textMuted : colours::textFaint).withAlpha ((octave ? 0.9f : 0.55f) * a));
                    g.drawLine ({ p0, p1 }, octave ? 1.4f : 1.0f);
                }
                if (octave)
                {
                    static const juce::String labels[] { "-24", "-12", "0", "+12", "+24" };
                    g.setFont (medium (9.5f));
                    g.setColour (colours::textFaint.withAlpha (a));
                    g.drawText (labels[(k + maxSemis) / 12], juce::Rectangle<float> (30.0f, 12.0f).withCentre (dialCentre.getPointOnCircumference (r1 + 13.0f, angle)),
                                juce::Justification::centred, false);
                }
            }
        }

        // Read-out: sung note -> note that comes out
        if (readoutArea.getWidth() > 60.0f)
        {
            auto r = readoutArea;
            const float blockH = 74.0f;
            r = r.withSizeKeepingCentre (r.getWidth(), std::min (r.getHeight(), blockH));
            const float arrowW = 34.0f;
            auto left = r.removeFromLeft ((r.getWidth() - arrowW) * 0.5f);
            auto arrow = r.removeFromLeft (arrowW);
            auto right = r;

            const auto block = [&] (juce::Rectangle<float> b, const juce::String& caption, const juce::String& note, const juce::String& detail, juce::Colour c)
            {
                g.setFont (semiBold (9.0f, 0.14f));
                g.setColour (colours::textFaint);
                g.drawText (caption, b.removeFromTop (14.0f), juce::Justification::centred, false);
                g.setFont (display (34.0f));
                g.setColour (c);
                g.drawText (note, b.removeFromTop (40.0f), juce::Justification::centred, false);
                g.setFont (monoFont (10.5f));
                g.setColour (colours::textMuted.withAlpha (a));
                g.drawText (detail, b.removeFromTop (16.0f), juce::Justification::centred, false);
            };
            block (left, "IN", inText, inDetail, (voiced ? colours::text : colours::textFaint).withAlpha (a));
            block (right, "OUT", outText, outDetail, (voiced ? hue.interpolatedWith (juce::Colours::white, 0.2f) : colours::textFaint).withAlpha (a * (0.4f + 0.6f * mix)));

            // Arrow
            const auto c = arrow.getCentre().translated (0.0f, 3.0f);
            juce::Path p;
            p.startNewSubPath (c.x - 10.0f, c.y);
            p.lineTo (c.x + 9.0f, c.y);
            p.startNewSubPath (c.x + 4.0f, c.y - 5.0f);
            p.lineTo (c.x + 9.0f, c.y);
            p.lineTo (c.x + 4.0f, c.y + 5.0f);
            g.setColour ((voiced ? hue : colours::textFaint).withAlpha (0.8f * a));
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    //==========================================================================
    namespace
    {
        // PITCH (spec 5.10): the two big controls in the visual; Fine, Mix, Formanten erhalten in the main row
        class PitchPanel final : public DetailPanel
        {
        public:
            explicit PitchPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Pitch", help::shortDescription ((int) ModuleId::pitch), pid::pitchOn)
            {
                // German names with umlauts in capitals: the knob upper-cases its name, juce::String::toUpperCase leaves ä / ö / ü
                semis = &knob (pid::pitchSemitones, i18n::tr ("HALBTÖNE", "Semitones"));
                formant = &knob (pid::pitchFormant, "Formant");
                auto& fine = knob (pid::pitchCents, i18n::tr ("Fein", "Fine"));
                auto& mix = knob (pid::pitchMix, "Mix");
                auto& keep = toggle (pid::pitchPreserveFormants, i18n::tr ("Formanten erhalten", "Keep formants"));
                setMainControls ({ &fine, &mix, &keep });
                placeInVisual ({ semis, formant });
                for (auto* k : { semis, formant })
                    k->setProminence (Knob::Prominence::primary);
            }

            void refresh() override
            {
                DetailPanel::refresh();
                // The latency note lives in the hover card, not on the panel
                if (++frame % 30 != 1)
                    return;
                auto withPitch = proc.getRouting();
                if (! withPitch.contains (dsp::ModuleId::pitch))
                    withPitch.insertDefault (dsp::ModuleId::pitch, 0);
                const int latency = proc.getChain().getLatencyFor (withPitch, true) - proc.getChain().getLatencyFor (withPitch, false);
                const double ms = 1000.0 * latency / std::max (1.0, proc.getSampleRate());
                const auto* spec = findParameterSpec (pid::pitchSemitones);
                semis->setDescription ((spec != nullptr ? i18n::paramDescription (pid::pitchSemitones, spec->description) : juce::String()) + "\n\n"
                                       + i18n::format (i18n::tr ("Solange Pitch an ist: {0} ms Latenz.", "While Pitch is on: {0} ms latency."),
                                                       { juce::String (juce::roundToInt (ms)) }));
            }

        private:
            void layoutVisual (juce::Rectangle<int> area) override
            {
                const auto a = area.reduced (12, 8);
                constexpr int captionH = 44;   // value + name under the dial
                const int big = juce::jlimit (110, 168, a.getHeight() - captionH - 34);
                const int small = juce::jlimit (84, 118, (big * 7) / 10);
                const int column = std::max (big + 70, a.getWidth() / 3);

                auto left = a.withWidth (column);
                auto right = a.withTrimmedLeft (a.getWidth() - column);
                semis->setDialSize (big);
                formant->setDialSize (small);
                semis->setBounds (left.withSizeKeepingCentre (big + 24, big + captionH).translated (0, 6));
                formant->setBounds (right.withSizeKeepingCentre (small + 24, small + captionH).translated (0, 6));

                if (auto* stage = dynamic_cast<PitchStage*> (getVisual()))
                {
                    const auto origin = stage->getPosition();
                    const auto dial = semis->getDialArea().translated (semis->getX() - origin.x, semis->getY() - origin.y).toFloat();
                    stage->setDialGeometry (dial.getCentre(), dial.getWidth() * 0.5f);
                    const auto mid = juce::Rectangle<int>::leftTopRightBottom (left.getRight(), a.getY(), right.getX(), a.getBottom())
                                         .translated (-origin.x, -origin.y).toFloat();
                    stage->setReadoutArea (mid);
                }
            }

            Knob* semis = nullptr;
            Knob* formant = nullptr;
            int frame = 0;
        };
    }

    std::unique_ptr<DetailPanel> createPitchPanel (VelvoxAudioProcessor& proc)
    {
        return std::make_unique<PitchPanel> (proc);
    }
}
