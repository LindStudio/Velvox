#include "DetailPanels.h"
#include "ChainView.h"
#include "../PluginProcessor.h"
#include "../Presets/ModulePresets.h"
#include "Icons.h"
#include "HelpContent.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        constexpr int headerHeight = 74;
        constexpr int padding = 22;
        constexpr int captionH = 34;
        constexpr int sectionGap = 12;

        void emphasise (std::initializer_list<Knob*> knobs, Knob::Prominence p)
        {
            for (auto* k : knobs)
                k->setProminence (p);
        }
    }

    //==========================================================================
    DetailPanel::DetailPanel (VelvoxAudioProcessor& p, juce::String t, juce::String s, const char* onId)
        : proc (p), apvts (p.getState()), title (std::move (t)), subtitle (std::move (s)), onParamId (onId)
    {
        if (onParamId != nullptr)
        {
            power = std::make_unique<PowerButton> (apvts, onParamId);
            addAndMakeVisible (*power);
        }

        helpButton.setTooltip ("Help for this module: what it does, how to use it, good starting points");
        helpButton.onClick = [this] { if (onHelp) onHelp (moduleId == -2 ? -1 : moduleId); };
        addAndMakeVisible (helpButton);
    }

    void DetailPanel::setVisual (std::unique_ptr<ModuleView> view)
    {
        visual = std::move (view);
        if (visual != nullptr)
            addAndMakeVisible (*visual);
        resized();
    }

    void DetailPanel::setModule (int id)
    {
        moduleId = id;
        if (id >= 0 && ! getModulePresets ((ModuleId) id).empty())
        {
            presetsButton.setTooltip ("Starting points for this module only - the rest of the chain stays as it is");
            presetsButton.onClick = [this] { showModulePresets(); };
            addAndMakeVisible (presetsButton);
        }
        resized();
        repaint();
    }

    void DetailPanel::showModulePresets()
    {
        const auto& presets = getModulePresets ((ModuleId) moduleId);
        juce::PopupMenu menu;
        menu.addSectionHeader (juce::String (dsp::moduleDisplayName ((ModuleId) moduleId)) + " presets");
        for (int i = 0; i < (int) presets.size(); ++i)
            menu.addItem (i + 1, presets[(size_t) i].name);

        juce::Component::SafePointer<DetailPanel> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton).withMinimumWidth (180),
                            [safe] (int result)
                            {
                                if (safe == nullptr || result <= 0)
                                    return;
                                const auto& list = getModulePresets ((ModuleId) safe->moduleId);
                                if (result - 1 < (int) list.size())
                                    applyModulePreset (list[(size_t) result - 1], safe->apvts);
                            });
    }

    Knob& DetailPanel::knob (const char* paramId, const juce::String& label)
    {
        auto k = std::make_unique<Knob> (apvts, paramId, label);
        if (proc.effectiveValueOf (paramId).has_value())
        {
            const juce::String id (paramId);
            auto* procPtr = &proc;
            k->effectiveValue = [procPtr, id] { return procPtr->effectiveValueOf (id).value_or (0.0f); };
        }
        auto& ref = *k;
        knobs.push_back (k.get());
        addAndMakeVisible (ref);
        owned.push_back (std::move (k));
        return ref;
    }

    Toggle& DetailPanel::toggle (const char* paramId, const juce::String& label)
    {
        auto t = std::make_unique<Toggle> (apvts, paramId, label);
        auto& ref = *t;
        addAndMakeVisible (ref);
        owned.push_back (std::move (t));
        return ref;
    }

    Choice& DetailPanel::choice (const char* paramId, const juce::String& caption)
    {
        auto c = std::make_unique<Choice> (apvts, paramId, caption);
        auto& ref = *c;
        addAndMakeVisible (ref);
        owned.push_back (std::move (c));
        return ref;
    }

    void DetailPanel::row (std::initializer_list<juce::Component*> items, juce::Rectangle<int> area)
    {
        const int n = (int) items.size();
        if (n == 0)
            return;
        const int w = area.getWidth() / n;
        int i = 0;
        for (auto* c : items)
        {
            if (c != nullptr)
                c->setBounds (area.getX() + i * w, area.getY(), w, area.getHeight());
            ++i;
        }
    }

    void DetailPanel::section (const juce::String& caption, juce::Rectangle<int> area)
    {
        sections.push_back ({ caption, area });
    }

    void DetailPanel::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();
        const bool on = onParamId == nullptr || apvts.getRawParameterValue (onParamId)->load() > 0.5f;

        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 16.0f);
        if (on)
        {
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clip;
            clip.addRoundedRectangle (r, 16.0f);
            g.reduceClipRegion (clip);
            drawGlow (g, { r.getX() + 80.0f, r.getY() }, 300.0f, 0.06f, colours::teal);
            drawGlow (g, { r.getRight() - 120.0f, r.getY() }, 280.0f, 0.045f, colours::blue);
        }
        drawGlass (g, r, 16.0f, 0.012f);

        // Header
        auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 0);
        header.removeFromLeft (power != nullptr ? 50 : 0);
        if (moduleId != -2)
        {
            const auto icon = header.removeFromLeft (26).toFloat().withSizeKeepingCentre (24.0f, 24.0f);
            drawModuleIcon (g, moduleId, icon, on ? colours::cyan.interpolatedWith (colours::text, 0.2f) : colours::textDim);
            header.removeFromLeft (12);
        }
        header.removeFromRight (helpButton.getWidth() + 10);
        if (presetsButton.isVisible())
            header.removeFromRight (presetsButton.getWidth() + 12);
        g.setColour (on ? colours::text : colours::textDim);
        g.setFont (font (typesize::title, true));
        const int titleW = juce::GlyphArrangement::getStringWidthInt (font (typesize::title, true), title) + 18;
        g.drawText (title, header.removeFromLeft (titleW), juce::Justification::centredLeft, false);
        if (! on)
        {
            // Plain-language state next to the name
            auto badge = header.removeFromLeft (52).toFloat().withSizeKeepingCentre (46.0f, 24.0f);
            g.setColour (colours::line.brighter (0.4f));
            g.drawRoundedRectangle (badge.reduced (0.5f), 12.0f, 1.0f);
            g.setColour (colours::textDim);
            g.setFont (medium (12.5f));
            g.drawText ("Off", badge, juce::Justification::centred, false);
            header.removeFromLeft (8);
        }
        g.setColour (colours::textDim);
        g.setFont (font (typesize::body));
        g.drawFittedText (subtitle, header, juce::Justification::centredLeft, 1, 0.9f);

        // Stage for the module view: recessed, with the aurora catching its top edge
        if (! visualArea.isEmpty())
        {
            const auto v = visualArea.toFloat();
            g.setColour (colours::background.withAlpha (0.7f));
            g.fillRoundedRectangle (v, 12.0f);
            g.setColour (colours::lineSoft);
            g.drawRoundedRectangle (v.reduced (0.5f), 12.0f, 1.0f);
            g.setColour (colours::cyan.withAlpha (on ? 0.35f : 0.1f));
            g.fillRect (v.reduced (24.0f, 0.0f).withHeight (1.0f));
        }

        // Sections: faint field with a caption and a hairline
        for (const auto& s : sections)
        {
            const auto a = s.area.toFloat();
            g.setColour (colours::accent.withAlpha (0.022f));
            g.fillRoundedRectangle (a, radius::card);
            g.setColour (colours::lineSoft);
            g.drawRoundedRectangle (a.reduced (0.5f), radius::card, 1.0f);
            auto cap = a.reduced (16.0f, 0.0f).withHeight ((float) captionH).withTrimmedTop (2.0f);
            const auto headingFont = font (typesize::heading, true);
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (headingFont, s.caption) + 12.0f;
            g.setColour (colours::text.withAlpha (0.86f));
            g.setFont (headingFont);
            g.drawText (s.caption, cap, juce::Justification::centredLeft, false);
            g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.10f), cap.getX() + w, 0.0f,
                                                     colours::accent.withAlpha (0.0f), cap.getRight(), 0.0f, false));
            g.fillRect (juce::Rectangle<float> (cap.getX() + w, cap.getCentreY(), cap.getWidth() - w, 1.0f));
        }
    }

    void DetailPanel::resized()
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (headerHeight).reduced (padding, 0);
        if (power != nullptr)
            power->setBounds (header.removeFromLeft (36).withSizeKeepingCentre (36, 36));
        helpButton.setBounds (header.removeFromRight (36).withSizeKeepingCentre (36, 34));
        header.removeFromRight (10);
        presetsButton.setBounds (header.removeFromRight (108).withSizeKeepingCentre (108, 34));

        auto content = r.reduced (padding, 0).withTrimmedBottom (padding);
        visualArea = {};
        if (visual != nullptr && visualHeight > 0)
        {
            visualArea = content.removeFromTop (visualHeight);
            content.removeFromTop (16);
            visual->setBounds (visualArea.reduced (1));
        }

        sections.clear();
        layoutContent (content);
    }

    void DetailPanel::refresh()
    {
        for (auto* k : knobs)
            k->refreshEffective();
        if (visual != nullptr && live != nullptr)
            visual->update (*live);

        const int on = onParamId == nullptr || apvts.getRawParameterValue (onParamId)->load() > 0.5f ? 1 : 0;
        if (on != lastOn)
        {
            lastOn = on;
            for (auto* c : getChildren())
                if (c != power.get() && c != visual.get() && c != &presetsButton && c != &helpButton)
                    c->setAlpha (on ? 1.0f : 0.55f);
            repaint();
        }
    }

    //==========================================================================
    namespace
    {
        // Inner area of a section (below its caption)
        juce::Rectangle<int> inner (juce::Rectangle<int> section) { return section.withTrimmedTop (captionH).reduced (10, 4); }

        juce::Label& noteLabel (juce::Label& l)
        {
            l.setFont (font (13.5f));
            l.setColour (juce::Label::textColourId, colours::textDim);
            l.setJustificationType (juce::Justification::centredLeft);
            return l;
        }

        // Two stacked sections filling the area; the top one takes `topShare`
        // (more room for the group holding the main controls)
        std::pair<juce::Rectangle<int>, juce::Rectangle<int>> split (juce::Rectangle<int> r, float topShare = 0.5f)
        {
            auto top = r.removeFromTop (juce::roundToInt ((float) (r.getHeight() - sectionGap) * topShare));
            r.removeFromTop (sectionGap);
            return { top, r };
        }

        float grFrom (const LiveData* live, const LiveData::History LiveData::* h) { return live != nullptr ? (live->*h).ago (0) : 0.0f; }

        class EqPanel final : public DetailPanel
        {
        public:
            explicit EqPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Equalizer", help::shortDescription ((int) ModuleId::eq), pid::eqOn)
            {
                visualHeight = 184;
                lcOn = &toggle (pid::hpfOn, "Low cut");
                lcSlope = &choice (pid::hpfSlope, {});
                lcFreq = &knob (pid::hpfFreq, "Frequency");
                lowType = &choice (pid::lowType, "Type");
                lowFreq = &knob (pid::lowFreq, "Freq");
                lowGain = &knob (pid::lowGain, "Gain");
                lowQ = &knob (pid::lowQ, "Q");
                midFreq = &knob (pid::midFreq, "Freq");
                midGain = &knob (pid::midGain, "Gain");
                midQ = &knob (pid::midQ, "Q");
                highType = &choice (pid::highType, "Type");
                highFreq = &knob (pid::highFreq, "Freq");
                highGain = &knob (pid::highGain, "Gain");
                highQ = &knob (pid::highQ, "Q");
                hcOn = &toggle (pid::lpfOn, "High cut");
                hcSlope = &choice (pid::lpfSlope, {});
                hcFreq = &knob (pid::lpfFreq, "Frequency");
                mix = &knob (pid::eqMix, "Mix");
                emphasise ({ lowGain, midGain, highGain }, Knob::Prominence::primary);
                emphasise ({ lowQ, midQ, highQ }, Knob::Prominence::detail);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto [top, bottom] = split (r);

                const int w = top.getWidth();
                auto lc = top.removeFromLeft (w * 2 / 9);
                top.removeFromLeft (sectionGap);
                auto low = top.removeFromLeft (w * 4 / 9);
                top.removeFromLeft (sectionGap);
                auto mid = top;
                section ("Low cut", lc);
                section ("Low band", low);
                section ("Mid", mid);
                auto lcIn = inner (lc);
                auto lcCtl = lcIn.removeFromLeft (lcIn.getWidth() / 2);
                lcOn->setBounds (lcCtl.removeFromTop (lcCtl.getHeight() / 2).withSizeKeepingCentre (lcCtl.getWidth() - 6, 32).translated (0, 6));
                lcSlope->setBounds (lcCtl.withSizeKeepingCentre (lcCtl.getWidth() - 6, 32).translated (0, -6));
                lcFreq->setBounds (lcIn);
                row ({ lowType, lowFreq, lowGain, lowQ }, inner (low));
                row ({ midFreq, midGain, midQ }, inner (mid));

                auto high = bottom.removeFromLeft (w * 4 / 9);
                bottom.removeFromLeft (sectionGap);
                auto hc = bottom.removeFromLeft (w * 2 / 9);
                bottom.removeFromLeft (sectionGap);
                auto out = bottom;
                section ("High band", high);
                section ("High cut", hc);
                section ("Output", out);
                row ({ highType, highFreq, highGain, highQ }, inner (high));
                auto hcIn = inner (hc);
                auto hcCtl = hcIn.removeFromLeft (hcIn.getWidth() / 2);
                hcOn->setBounds (hcCtl.removeFromTop (hcCtl.getHeight() / 2).withSizeKeepingCentre (hcCtl.getWidth() - 6, 32).translated (0, 6));
                hcSlope->setBounds (hcCtl.withSizeKeepingCentre (hcCtl.getWidth() - 6, 32).translated (0, -6));
                hcFreq->setBounds (hcIn);
                row ({ mix }, inner (out).withSizeKeepingCentre (130, inner (out).getHeight()));
            }

            Toggle* lcOn; Choice* lcSlope; Knob* lcFreq;
            Choice* lowType; Knob *lowFreq, *lowGain, *lowQ;
            Knob *midFreq, *midGain, *midQ;
            Choice* highType; Knob *highFreq, *highGain, *highQ;
            Toggle* hcOn; Choice* hcSlope; Knob* hcFreq; Knob* mix;
        };

        class CompPanel final : public DetailPanel
        {
        public:
            explicit CompPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Compressor", help::shortDescription ((int) ModuleId::comp), pid::compOn)
            {
                for (auto& [id, label, ptr] : std::initializer_list<std::tuple<const char*, const char*, Knob**>> {
                         { pid::compThreshold, "Threshold", &threshold }, { pid::compRatio, "Ratio", &ratio },
                         { pid::compAttack, "Attack", &attack }, { pid::compRelease, "Release", &release },
                         { pid::compKnee, "Knee", &knee }, { pid::compMakeup, "Makeup", &makeup },
                         { pid::compMix, "Mix", &mix }, { pid::compScHpf, "SC filter", &scHpf } })
                    *ptr = &knob (id, label);
                detector = &choice (pid::compDetector, "Detector");
                emphasise ({ threshold, ratio }, Knob::Prominence::primary);
                emphasise ({ knee, scHpf }, Knob::Prominence::detail);
                addAndMakeVisible (gr);
                gr.setTooltip ("Gain reduction in dB");
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (grFrom (live, &LiveData::compGr));
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                gr.setBounds (r.removeFromRight (60));
                r.removeFromRight (sectionGap);
                auto [dyn, out] = split (r);
                section ("Dynamics", dyn);
                section ("Output and detector", out);
                row ({ threshold, ratio, attack, release, knee }, inner (dyn));
                row ({ makeup, mix, scHpf, detector }, inner (out));
            }

            Knob *threshold, *ratio, *attack, *release, *knee, *makeup, *mix, *scHpf;
            Choice* detector;
            GainReductionMeter gr { colours::accent, "GR" };
        };

        class DeEssPanel final : public DetailPanel
        {
        public:
            explicit DeEssPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "De-esser", help::shortDescription ((int) ModuleId::deess), pid::deessOn)
            {
                freq = &knob (pid::deessFreq, "Frequency");
                threshold = &knob (pid::deessThreshold, "Threshold");
                range = &knob (pid::deessRange, "Range");
                mode = &choice (pid::deessMode, "Mode");
                listen = &toggle (pid::deessListen, "Listen");
                emphasise ({ freq, threshold }, Knob::Prominence::primary);
                addAndMakeVisible (gr);
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (grFrom (live, &LiveData::deessGr));
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                gr.setBounds (r.removeFromRight (60));
                r.removeFromRight (sectionGap);
                auto [a, b] = split (r, 0.62f);
                section ("Detection", a);
                section ("Behaviour", b);
                row ({ freq, threshold, range }, inner (a));
                auto in = inner (b);
                mode->setBounds (in.removeFromLeft (in.getWidth() / 2));
                listen->setBounds (in.withSizeKeepingCentre (130, 34));
            }

            Knob *freq, *threshold, *range;
            Choice* mode;
            Toggle* listen;
            GainReductionMeter gr { colours::accent, "GR" };
        };

        class SatPanel final : public DetailPanel
        {
        public:
            explicit SatPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Saturation", help::shortDescription ((int) ModuleId::sat), pid::satOn)
            {
                visualHeight = 268;
                character = &choice (pid::satCharacter, "Character");
                drive = &knob (pid::satDrive, "Drive");
                output = &knob (pid::satOutput, "Output");
                mix = &knob (pid::satMix, "Mix");
                drive->setProminence (Knob::Prominence::primary);
                drive->setMaxDialSize (124);
                for (auto* k : { output, mix })
                    k->setMaxDialSize (88);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                section ("Tone", r);
                row ({ character, drive, output, mix }, inner (r));
            }

            Choice* character;
            Knob *drive, *output, *mix;
        };

        class DoublerPanel final : public DetailPanel
        {
        public:
            explicit DoublerPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Doubler", help::shortDescription ((int) ModuleId::doubler), pid::doublerOn)
            {
                mix = &knob (pid::doublerMix, "Mix");
                width = &knob (pid::doublerWidth, "Width");
                delay = &knob (pid::doublerDelay, "Timing");
                detune = &knob (pid::doublerDetune, "Detune");
                monoSafe = &knob (pid::doublerMonoSafe, "Mono safe");
                tone = &knob (pid::doublerTone, "Tone");
                emphasise ({ mix, width }, Knob::Prominence::primary);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto [a, b] = split (r);
                section ("Voices", a);
                section ("Stereo and tone", b);
                row ({ mix, delay, detune }, inner (a));
                row ({ width, monoSafe, tone }, inner (b));
            }

            Knob *mix, *width, *delay, *detune, *monoSafe, *tone;
        };

        class PitchPanel final : public DetailPanel
        {
        public:
            explicit PitchPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Pitch", help::shortDescription ((int) ModuleId::pitch), pid::pitchOn)
            {
                semis = &knob (pid::pitchSemitones, "Pitch");
                fine = &knob (pid::pitchCents, "Fine");
                formant = &knob (pid::pitchFormant, "Formant");
                mix = &knob (pid::pitchMix, "Mix");
                keep = &toggle (pid::pitchPreserveFormants, "Keep formants");
                emphasise ({ semis }, Knob::Prominence::primary);
                addAndMakeVisible (noteLabel (info));
            }

            void refresh() override
            {
                DetailPanel::refresh();
                const int latency = proc.getChain().getLatencyFor (true) - proc.getChain().getLatencyFor (false);
                const double ms = 1000.0 * latency / std::max (1.0, proc.getSampleRate());
                const juce::String text = "While on, pitch adds " + juce::String (juce::roundToInt (ms)) + " ms latency. "
                                          "FL Studio compensates it automatically; avoid automating the on/off switch.";
                if (info.getText() != text)
                    info.setText (text, juce::dontSendNotification);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                info.setBounds (r.removeFromBottom (22).withTrimmedLeft (6));
                r.removeFromBottom (6);
                auto [a, b] = split (r);
                section ("Transpose", a);
                section ("Character", b);
                row ({ semis, fine, mix }, inner (a));
                auto in = inner (b);
                keep->setBounds (in.removeFromLeft (in.getWidth() / 3).withSizeKeepingCentre (170, 34));
                row ({ formant }, in.removeFromLeft (in.getWidth() / 2));
            }

            Knob *semis, *fine, *formant, *mix;
            Toggle* keep;
            juce::Label info;
        };

        class ChopPanel final : public DetailPanel
        {
        public:
            explicit ChopPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Vocal chop", help::shortDescription ((int) ModuleId::chop), pid::chopOn)
            {
                visualHeight = 268;
                pattern = &choice (pid::chopPattern, "Pattern");
                rate = &choice (pid::chopRate, "Step length");
                depth = &knob (pid::chopDepth, "Depth");
                smooth = &knob (pid::chopSmooth, "Smooth");
                depth->setProminence (Knob::Prominence::primary);
                smooth->setMaxDialSize (88);
                info.setText ("Steps follow the song position while FL Studio plays. R = repeat the previous slice (stutter).", juce::dontSendNotification);
                addAndMakeVisible (noteLabel (info));
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                info.setBounds (r.removeFromBottom (22).withTrimmedLeft (6));
                r.removeFromBottom (6);
                section ("Pattern", r);
                row ({ pattern, rate, depth, smooth }, inner (r));
            }

            Choice *pattern, *rate;
            Knob *depth, *smooth;
            juce::Label info;
        };

        class DelayPanel final : public DetailPanel
        {
        public:
            explicit DelayPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Delay", help::shortDescription ((int) ModuleId::delay), pid::delayOn)
            {
                visualHeight = 170;
                time = &knob (pid::delayTime, "Time");
                division = &choice (pid::delayDivision, "Note");
                sync = &toggle (pid::delaySync, "Sync");
                pingPong = &toggle (pid::delayPingPong, "Ping-pong");
                feedback = &knob (pid::delayFeedback, "Feedback");
                mix = &knob (pid::delayMix, "Mix");
                lowCut = &knob (pid::delayLowCut, "Low cut");
                highCut = &knob (pid::delayHighCut, "High cut");
                toReverb = &knob (pid::delayToReverb, "Into reverb");
                duckAmount = &knob (pid::delayDuckAmount, "Amount");
                duckThreshold = &knob (pid::delayDuckThreshold, "Threshold");
                duckAttack = &knob (pid::delayDuckAttack, "Attack");
                duckRelease = &knob (pid::delayDuckRelease, "Release");
                source = &choice (pid::duckSource, "Duck source");
                emphasise ({ time, feedback, mix }, Knob::Prominence::primary);
                emphasise ({ lowCut, highCut, toReverb, duckAttack, duckRelease }, Knob::Prominence::detail);
                addAndMakeVisible (gr);
                addAndMakeVisible (noteLabel (info));
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (grFrom (live, &LiveData::delayDuck));
                const bool synced = apvts.getRawParameterValue (pid::delaySync)->load() > 0.5f;
                time->setVisible (! synced);
                division->setVisible (synced);

                const auto& m = proc.getChain().meters;
                juce::String text = synced ? (m.tempoSynced.load() ? juce::String (juce::roundToInt (m.effectiveDelayMs.load())) + " ms at host tempo"
                                                                  : "No host tempo: using the Time value")
                                           : juce::String();
                if (apvts.getRawParameterValue (pid::duckSource)->load() > 0.5f && ! m.sidechainInUse.load())
                    text << (text.isEmpty() ? "" : "  |  ") << "No sidechain connected: ducking follows the vocal";
                if (info.getText() != text)
                    info.setText (text, juce::dontSendNotification);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                info.setBounds (r.removeFromBottom (20).withTrimmedLeft (6));
                r.removeFromBottom (4);
                auto [echo, duck] = split (r);
                section ("Echo", echo);
                auto e = inner (echo);
                auto toggles = e.removeFromLeft (136);
                sync->setBounds (toggles.removeFromTop (toggles.getHeight() / 2).withSizeKeepingCentre (124, 32).translated (0, 4));
                pingPong->setBounds (toggles.withSizeKeepingCentre (124, 32).translated (0, -4));
                row ({ time, feedback, mix, lowCut, highCut, toReverb }, e);
                division->setBounds (time->getBounds());

                gr.setBounds (duck.removeFromRight (60));
                duck.removeFromRight (sectionGap);
                section ("Ducking", duck);
                row ({ source, duckAmount, duckThreshold, duckAttack, duckRelease }, inner (duck));
            }

            Knob *time, *feedback, *mix, *lowCut, *highCut, *toReverb, *duckAmount, *duckThreshold, *duckAttack, *duckRelease;
            Choice *division, *source;
            Toggle *sync, *pingPong;
            GainReductionMeter gr { colours::accent, "Duck" };
            juce::Label info;
        };

        class ReverbPanel final : public DetailPanel
        {
        public:
            explicit ReverbPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Reverb", help::shortDescription ((int) ModuleId::reverb), pid::reverbOn)
            {
                type = &choice (pid::reverbType, "Type");
                mix = &knob (pid::reverbMix, "Mix");
                decay = &knob (pid::reverbDecay, "Decay");
                preDelay = &knob (pid::reverbPreDelay, "Pre-delay");
                size = &knob (pid::reverbSize, "Size");
                tone = &knob (pid::reverbDamping, "Tone");
                lowCut = &knob (pid::reverbLowCut, "Low cut");
                width = &knob (pid::reverbWidth, "Width");
                duckAmount = &knob (pid::reverbDuckAmount, "Amount");
                duckThreshold = &knob (pid::reverbDuckThreshold, "Threshold");
                duckAttack = &knob (pid::reverbDuckAttack, "Attack");
                duckRelease = &knob (pid::reverbDuckRelease, "Release");
                source = &choice (pid::duckSource, "Duck source");
                emphasise ({ mix, decay }, Knob::Prominence::primary);
                emphasise ({ tone, lowCut, width, duckAttack, duckRelease }, Knob::Prominence::detail);
                addAndMakeVisible (gr);
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (grFrom (live, &LiveData::reverbDuck));
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto [space, duck] = split (r);
                section ("Space", space);
                row ({ type, mix, decay, preDelay, size, tone, lowCut, width }, inner (space));
                gr.setBounds (duck.removeFromRight (60));
                duck.removeFromRight (sectionGap);
                section ("Ducking", duck);
                row ({ source, duckAmount, duckThreshold, duckAttack, duckRelease }, inner (duck));
            }

            Choice *type, *source;
            Knob *mix, *decay, *preDelay, *size, *tone, *lowCut, *width, *duckAmount, *duckThreshold, *duckAttack, *duckRelease;
            GainReductionMeter gr { colours::accent, "Duck" };
        };

        class OutputPanel final : public DetailPanel
        {
        public:
            explicit OutputPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Output", help::shortDescription (-1), nullptr)
            {
                width = &knob (pid::stereoWidth, "Width");
                monoBelow = &knob (pid::monoBelow, "Mono below");
                mode = &choice (pid::levelMatchMode, "Level match");
                window = &choice (pid::levelMatchWindow, "Measure over");
                range = &knob (pid::levelMatchRange, "Max correction");
                clip = &toggle (pid::safetyClip, "Clip guard");
                emphasise ({ width }, Knob::Prominence::primary);
                addAndMakeVisible (correlation);
                correlation.setTooltip ("Phase correlation: +1 = mono-safe, below 0 = parts cancel in mono");
                for (auto* l : { &inValue, &procValue, &matchValue })
                {
                    l->setFont (display (30.0f));
                    l->setColour (juce::Label::textColourId, colours::text);
                    l->setJustificationType (juce::Justification::centred);
                    addAndMakeVisible (*l);
                }
            }

            void refresh() override
            {
                DetailPanel::refresh();
                const auto& m = proc.getChain().meters;
                correlation.push (m.correlation.load());
                const auto fmt = [] (float lufs) { return lufs <= -99.0f ? juce::String ("-") : juce::String (lufs, 1); };
                const auto set = [] (juce::Label& l, const juce::String& t) { if (l.getText() != t) l.setText (t, juce::dontSendNotification); };
                set (inValue, fmt (m.inputLufs.load()));
                set (procValue, fmt (m.processedLufs.load()));
                const float match = m.levelMatchDb.load();
                set (matchValue, (match > 0.05f ? "+" : "") + juce::String (match, 1));
            }

            void paintOverChildren (juce::Graphics& g) override
            {
                const auto under = [&] (juce::Label& l, const juce::String& text)
                {
                    drawCaption (g, text, l.getBounds().toFloat().translated (0.0f, 30.0f).withHeight (16.0f),
                                 juce::Justification::centred, colours::textDim, 9.5f);
                };
                under (inValue, "Input LUFS");
                under (procValue, "Processed LUFS");
                under (matchValue, "Match dB");
                drawCaption (g, "Correlation", correlation.getBounds().toFloat().translated (0.0f, -22.0f).withHeight (16.0f),
                             juce::Justification::centredLeft, colours::textDim, 10.0f);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto [stereo, loud] = split (r);
                section ("Stereo", stereo);
                auto s = inner (stereo);
                auto corr = s.removeFromRight (s.getWidth() / 3);
                correlation.setBounds (corr.withSizeKeepingCentre (corr.getWidth() - 30, 32));
                row ({ width, monoBelow }, s);

                section ("Loudness", loud);
                auto l = inner (loud);
                auto readouts = l.removeFromRight (l.getWidth() * 2 / 5);
                const int w = readouts.getWidth() / 3;
                for (auto* lab : { &inValue, &procValue, &matchValue })
                    lab->setBounds (readouts.removeFromLeft (w).withSizeKeepingCentre (w, 36).translated (0, -10));
                row ({ mode, window, range, clip }, l);
                clip->setBounds (clip->getBounds().withSizeKeepingCentre (136, 34));
            }

            Knob *width, *monoBelow, *range;
            Choice *mode, *window;
            Toggle* clip;
            CorrelationView correlation;
            juce::Label inValue, procValue, matchValue;
        };

        //======================================================================
        class PulsePanel final : public DetailPanel
        {
        public:
            explicit PulsePanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Pulse", help::shortDescription ((int) ModuleId::pulse), pid::pulseOn)
            {
                visualHeight = 188;
                rate = &choice (pid::pulseRate, "Cycle");
                bpm = &knob (pid::pulseBpm, "Manual BPM");
                depth = &knob (pid::pulseDepth, "Depth");
                mix = &knob (pid::pulseMix, "Mix");
                interp = &choice (pid::pulseInterp, "Curve");
                tension = &knob (pid::pulseTension, "Shape");
                attack = &knob (pid::pulseAttack, "Attack");
                release = &knob (pid::pulseRelease, "Release");
                smooth = &knob (pid::pulseSmooth, "Smooth");
                emphasise ({ depth }, Knob::Prominence::primary);
                emphasise ({ bpm, attack, release, smooth }, Knob::Prominence::detail);
            }

            void refresh() override
            {
                DetailPanel::refresh();
                // The manual tempo only matters when the host sends none
                const bool host = proc.getChain().getPulse().displayHostTempo.load();
                bpm->setAlpha (host ? 0.4f : (apvts.getRawParameterValue (pid::pulseOn)->load() > 0.5f ? 1.0f : 0.55f));
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto [a, b] = split (r);
                section ("Rhythm", a);
                section ("Shape and smoothing", b);
                row ({ rate, depth, mix, bpm }, inner (a));
                row ({ interp, tension, attack, release, smooth }, inner (b));
            }

            Choice *rate, *interp;
            Knob *bpm, *depth, *mix, *tension, *attack, *release, *smooth;
        };

        class SmashPanel final : public DetailPanel
        {
        public:
            explicit SmashPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Vocal Smash", help::shortDescription ((int) ModuleId::smash), pid::smashOn)
            {
                amount = &knob (pid::smashAmount, "Amount");
                attack = &knob (pid::smashAttack, "Attack");
                release = &knob (pid::smashRelease, "Release");
                tone = &knob (pid::smashTone, "Tone");
                blend = &knob (pid::smashBlend, "Blend");
                output = &knob (pid::smashOutput, "Output");
                emphasise ({ amount, blend }, Knob::Prominence::primary);
                addAndMakeVisible (gr);
                gr.setTooltip ("Gain reduction of the squashed path");
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (grFrom (live, &LiveData::smashGr));
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                gr.setBounds (r.removeFromRight (60));
                r.removeFromRight (sectionGap);
                auto [a, b] = split (r);
                section ("Squashed path", a);
                section ("Blend and level", b);
                row ({ amount, attack, release, tone }, inner (a));
                row ({ blend, output }, inner (b).withSizeKeepingCentre (inner (b).getWidth() / 2, inner (b).getHeight()));
            }

            Knob *amount, *attack, *release, *tone, *blend, *output;
            GainReductionMeter gr { colours::accent, "GR" };
        };

        class ChoirPanel final : public DetailPanel
        {
        public:
            explicit ChoirPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Choir", help::shortDescription ((int) ModuleId::choir), pid::choirOn)
            {
                voices = &choice (pid::choirVoices, "Voices");
                detune = &knob (pid::choirDetune, "Detune");
                timing = &knob (pid::choirTiming, "Timing");
                humanize = &knob (pid::choirHumanize, "Humanize");
                width = &knob (pid::choirWidth, "Width");
                density = &knob (pid::choirDensity, "Density");
                mix = &knob (pid::choirMix, "Mix");
                emphasise ({ detune, mix }, Knob::Prominence::primary);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto [a, b] = split (r);
                section ("Voices", a);
                section ("Stereo and blend", b);
                row ({ voices, detune, timing, humanize }, inner (a));
                row ({ width, density, mix }, inner (b));
            }

            Choice* voices;
            Knob *detune, *timing, *humanize, *width, *density, *mix;
        };

        class SmoothPanel final : public DetailPanel
        {
        public:
            explicit SmoothPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Smooth", help::shortDescription ((int) ModuleId::smooth), pid::smoothOn)
            {
                amount = &knob (pid::smoothAmount, "Amount");
                sensitivity = &knob (pid::smoothSensitivity, "Sensitivity");
                low = &knob (pid::smoothLow, "From");
                high = &knob (pid::smoothHigh, "To");
                mode = &choice (pid::smoothMode, "Mode");
                attack = &knob (pid::smoothAttack, "Attack");
                release = &knob (pid::smoothRelease, "Release");
                output = &knob (pid::smoothOutput, "Output");
                delta = &toggle (pid::smoothDelta, "Delta");
                emphasise ({ amount }, Knob::Prominence::primary);
                emphasise ({ attack, release }, Knob::Prominence::detail);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto [a, b] = split (r);
                section ("Detection", a);
                section ("Behaviour", b);
                row ({ amount, sensitivity, low, high }, inner (a));
                auto in = inner (b);
                auto toggleArea = in.removeFromRight (in.getWidth() / 5);
                delta->setBounds (toggleArea.withSizeKeepingCentre (112, 34));
                row ({ mode, attack, release, output }, in);
            }

            Knob *amount, *sensitivity, *low, *high, *attack, *release, *output;
            Choice* mode;
            Toggle* delta;
        };

        class RidePanel final : public DetailPanel
        {
        public:
            explicit RidePanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Level Ride", help::shortDescription ((int) ModuleId::ride), pid::rideOn)
            {
                target = &knob (pid::rideTarget, "Target");
                amount = &knob (pid::rideAmount, "Amount");
                boost = &knob (pid::rideMaxBoost, "Max boost");
                cut = &knob (pid::rideMaxCut, "Max cut");
                response = &knob (pid::rideResponse, "Response");
                phrase = &knob (pid::ridePhrase, "Phrase");
                transient = &knob (pid::rideTransient, "Transients");
                silence = &knob (pid::rideSilence, "Ignore below");
                emphasise ({ target, amount }, Knob::Prominence::primary);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto [a, b] = split (r);
                section ("Target", a);
                section ("Behaviour", b);
                row ({ target, amount, boost, cut }, inner (a));
                row ({ response, phrase, transient, silence }, inner (b));
            }

            Knob *target, *amount, *boost, *cut, *response, *phrase, *transient, *silence;
        };

        //======================================================================
        // VOCAL CHECK findings: one row per finding, most important first.
        // Clicking a row with a recommended module opens it.
        class FindingsList final : public juce::Component
        {
        public:
            explicit FindingsList (VelvoxAudioProcessor& p) : proc (p) {}

            std::function<void (int moduleId)> onOpen;
            static constexpr int rowHeight = 72;

            void refresh (bool moduleOn)
            {
                const auto& r = proc.getLatestAnalysis();
                juce::String key = juce::String ((int) moduleOn) + juce::String ((int) r.enoughMaterial) + juce::String (juce::roundToInt (r.voicedSeconds * 2.0f));
                for (auto& f : r.findings)
                    key << f.title << (int) f.certainty;
                if (key != lastKey)
                {
                    lastKey = key;
                    on = moduleOn;
                    repaint();
                }
            }

            void paint (juce::Graphics& g) override
            {
                const auto& r = proc.getLatestAnalysis();
                auto area = getLocalBounds().toFloat();

                juce::String status;
                if (! on)
                    status = "VOCAL CHECK is off. Switch it on with the power button - it analyses the signal at its place in the chain and never changes the sound.";
                else if (! r.enoughMaterial)
                    status = "Collecting material: " + juce::String (r.voicedSeconds, 1) + " of " + juce::String (juce::roundToInt (dsp::VocalAnalyzer::minVoicedSeconds))
                             + " s of voice. Play the vocal - pauses are ignored.";
                else if (r.findings.empty())
                    status = "No problems detected in " + juce::String (r.voicedSeconds, 1) + " s of voice.";

                // Level findings (clipping, headroom) can exist before there is enough material
                int shown = 0;
                const int maxRows = std::max (1, (int) (area.getHeight() - (status.isNotEmpty() ? 26.0f : 0.0f)) / rowHeight);
                rows.clear();
                for (auto& f : r.findings)
                {
                    if (shown == maxRows)
                        break;
                    auto row = area.removeFromTop ((float) rowHeight).reduced (0.0f, 3.0f);
                    paintFinding (g, f, row);
                    rows.push_back ({ row, f.module.has_value() ? (int) *f.module : -1 });
                    ++shown;
                }
                if ((int) r.findings.size() > shown)
                    drawCaption (g, "+ " + juce::String ((int) r.findings.size() - shown) + " more (less important)", area.removeFromTop (18.0f),
                                 juce::Justification::centredLeft, colours::textDim, 9.5f);

                if (status.isNotEmpty())
                {
                    g.setColour (colours::textDim);
                    g.setFont (font (14.0f));
                    g.drawFittedText (status, area.removeFromTop (40.0f).toNearestInt(), juce::Justification::centredLeft, 2);
                }
            }

            void mouseUp (const juce::MouseEvent& e) override
            {
                for (auto& [rect, id] : rows)
                    if (rect.contains (e.position) && id >= 0 && onOpen)
                        onOpen (id);
            }

            void mouseMove (const juce::MouseEvent& e) override
            {
                bool overLink = false;
                for (auto& [rect, id] : rows)
                    overLink = overLink || (rect.contains (e.position) && id >= 0);
                setMouseCursor (overLink ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            }

        private:
            void paintFinding (juce::Graphics& g, const dsp::Finding& f, juce::Rectangle<float> r)
            {
                const bool sure = f.certainty == dsp::Finding::Certainty::sure;
                g.setColour (colours::accent.withAlpha (0.025f));
                g.fillRoundedRectangle (r, 10.0f);

                auto inner = r.reduced (14.0f, 8.0f);
                auto badge = inner.removeFromLeft (88.0f).removeFromTop (22.0f);
                if (sure)
                {
                    g.setGradientFill (auroraGradient (badge.getTopLeft(), badge.getTopRight(), 0.85f));
                    g.fillRoundedRectangle (badge, 11.0f);
                    drawCaption (g, "Detected", badge, juce::Justification::centred, colours::background, 9.0f);
                }
                else
                {
                    g.setColour (colours::line.brighter (0.5f));
                    g.drawRoundedRectangle (badge.reduced (0.5f), 11.0f, 1.0f);
                    drawCaption (g, "Possible", badge, juce::Justification::centred, colours::textDim, 9.0f);
                }
                inner.removeFromLeft (12.0f);

                if (f.module.has_value())
                {
                    auto link = inner.removeFromRight (180.0f).removeFromTop (20.0f);
                    drawCaption (g, juce::String ("Open ") + dsp::moduleDisplayName (*f.module) + "  >", link, juce::Justification::centredRight, colours::cyan, 10.0f);
                }

                g.setColour (colours::text);
                g.setFont (font (14.5f, true));
                g.drawText (f.title, inner.removeFromTop (20.0f), juce::Justification::centredLeft, true);
                g.setColour (colours::textDim);
                g.setFont (font (13.0f));
                g.drawText (f.detail, inner.removeFromTop (18.0f), juce::Justification::centredLeft, true);
                g.setColour (colours::cyan.interpolatedWith (colours::text, 0.4f));
                g.drawText (f.advice, inner.removeFromTop (18.0f), juce::Justification::centredLeft, true);
            }

            VelvoxAudioProcessor& proc;
            bool on = false;
            juce::String lastKey;
            std::vector<std::pair<juce::Rectangle<float>, int>> rows;
        };

        class CheckPanel final : public DetailPanel
        {
        public:
            explicit CheckPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Vocal Check", help::shortDescription ((int) ModuleId::check), pid::checkOn),
                  findings (p)
            {
                visualHeight = 196;
                mode = &choice (pid::checkMode, "Mode");
                addAndMakeVisible (findings);
                addAndMakeVisible (startStop);
                addAndMakeVisible (resetButton);
                addAndMakeVisible (statusLabel);
                statusLabel.setFont (font (13.0f));
                statusLabel.setColour (juce::Label::textColourId, colours::textDim);
                statusLabel.setJustificationType (juce::Justification::topLeft);

                startStop.setTooltip ("Capture mode: analyse everything between Start and Stop");
                startStop.onClick = [this]
                {
                    auto& a = proc.getAnalyzer();
                    if (a.isCapturing()) a.stopCapture(); else a.startCapture();
                    proc.refreshAnalysis();
                };
                resetButton.setTooltip ("Forget everything analysed so far (also the peak / clip memory)");
                resetButton.onClick = [this] { proc.getAnalyzer().clear(); proc.refreshAnalysis(); };
                findings.onOpen = [this] (int id) { if (onOpenModule) onOpenModule (id); };
            }

            void refresh() override
            {
                DetailPanel::refresh();
                auto& a = proc.getAnalyzer();
                const bool on = apvts.getRawParameterValue (pid::checkOn)->load() > 0.5f;
                const bool capture = a.getMode() == dsp::VocalAnalyzer::Mode::capture;
                startStop.setEnabled (on && capture);
                startStop.setButtonText (a.isCapturing() ? "Stop" : "Start");
                startStop.setToggleState (a.isCapturing(), juce::dontSendNotification);
                resetButton.setEnabled (on);

                const auto& r = proc.getLatestAnalysis();
                const juce::String text = ! on ? juce::String ("Off - the audio is not analysed.")
                                        : capture ? (a.isCapturing() ? "Capturing ... " + juce::String (r.analysedSeconds, 1) + " s"
                                                                     : "Capture stopped. " + juce::String (r.analysedSeconds, 1) + " s analysed.")
                                                  : "Continuous: last " + juce::String (juce::roundToInt (std::min (30.0f, r.analysedSeconds))) + " s.";
                if (statusLabel.getText() != text)
                    statusLabel.setText (text, juce::dontSendNotification);
                findings.refresh (on);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto controls = r.removeFromLeft (236);
                r.removeFromLeft (16);
                section ("Analysis", controls);
                auto c = inner (controls).reduced (6, 0);
                mode->setBounds (c.removeFromTop (66));
                c.removeFromTop (10);
                auto buttons = c.removeFromTop (34);
                startStop.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 4));
                buttons.removeFromLeft (8);
                resetButton.setBounds (buttons);
                c.removeFromTop (12);
                statusLabel.setBounds (c.removeFromTop (44));
                findings.setBounds (r);
            }

            Choice* mode;
            FindingsList findings;
            juce::TextButton startStop { "Start" }, resetButton { "Reset" };
            juce::Label statusLabel;
        };
    }

    std::unique_ptr<DetailPanel> createDetailPanel (VelvoxAudioProcessor& p, int id, const SpectrumSource* spectrum, const LiveData* live)
    {
        std::unique_ptr<DetailPanel> panel;
        if (id < 0)
        {
            panel = std::make_unique<OutputPanel> (p);
        }
        else
        {
            switch ((ModuleId) id)
            {
                case ModuleId::eq:      panel = std::make_unique<EqPanel> (p); break;
                case ModuleId::comp:    panel = std::make_unique<CompPanel> (p); break;
                case ModuleId::deess:   panel = std::make_unique<DeEssPanel> (p); break;
                case ModuleId::sat:     panel = std::make_unique<SatPanel> (p); break;
                case ModuleId::doubler: panel = std::make_unique<DoublerPanel> (p); break;
                case ModuleId::pitch:   panel = std::make_unique<PitchPanel> (p); break;
                case ModuleId::chop:    panel = std::make_unique<ChopPanel> (p); break;
                case ModuleId::delay:   panel = std::make_unique<DelayPanel> (p); break;
                case ModuleId::reverb:  panel = std::make_unique<ReverbPanel> (p); break;
                case ModuleId::pulse:   panel = std::make_unique<PulsePanel> (p); break;
                case ModuleId::smash:   panel = std::make_unique<SmashPanel> (p); break;
                case ModuleId::choir:   panel = std::make_unique<ChoirPanel> (p); break;
                case ModuleId::smooth:  panel = std::make_unique<SmoothPanel> (p); break;
                case ModuleId::check:   panel = std::make_unique<CheckPanel> (p); break;
                case ModuleId::ride:    panel = std::make_unique<RidePanel> (p); break;
            }
            if (panel == nullptr)
                panel = std::make_unique<OutputPanel> (p);
        }
        panel->setLiveData (live);
        panel->setVisual (createModuleView (p, id, spectrum));
        panel->setModule (id);
        return panel;
    }
}
