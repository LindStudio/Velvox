#include "DetailPanels.h"
#include "ChainView.h"
#include "../PluginProcessor.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        constexpr int headerHeight = 64;
        constexpr int padding = 20;
        constexpr int captionH = 26;
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
    }

    void DetailPanel::setVisual (std::unique_ptr<ModuleView> view)
    {
        visual = std::move (view);
        if (visual != nullptr)
            addAndMakeVisible (*visual);
        resized();
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
        header.removeFromLeft (power != nullptr ? 42 : 0);
        g.setColour (on ? colours::text : colours::textDim);
        g.setFont (font (19.0f, true));
        const int titleW = juce::GlyphArrangement::getStringWidthInt (font (19.0f, true), title) + 16;
        g.drawText (title, header.removeFromLeft (titleW), juce::Justification::centredLeft, false);
        g.setColour (colours::textDim);
        g.setFont (font (12.5f));
        g.drawText (subtitle, header, juce::Justification::centredLeft, true);

        // Stage for the module view: recessed, with the aurora catching its top edge
        if (! visualArea.isEmpty())
        {
            const auto v = visualArea.toFloat();
            g.setColour (colours::background.withAlpha (0.7f));
            g.fillRoundedRectangle (v, 12.0f);
            g.setColour (colours::lineSoft);
            g.drawRoundedRectangle (v.reduced (0.5f), 12.0f, 1.0f);
            g.setGradientFill (auroraGradient (v.getTopLeft(), v.getTopRight(), on ? 0.35f : 0.1f));
            g.fillRect (v.reduced (24.0f, 0.0f).withHeight (1.0f));
        }

        // Sections: faint field with a caption and a hairline
        for (const auto& s : sections)
        {
            const auto a = s.area.toFloat();
            g.setColour (colours::accent.withAlpha (0.014f));
            g.fillRoundedRectangle (a, 12.0f);
            auto cap = a.reduced (14.0f, 0.0f).withHeight ((float) captionH);
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (caps (9.0f), s.caption.toUpperCase()) + 10.0f;
            drawCaption (g, s.caption, cap, juce::Justification::centredLeft, colours::textDim, 9.0f);
            g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.08f), cap.getX() + w, 0.0f,
                                                     colours::accent.withAlpha (0.0f), cap.getRight(), 0.0f, false));
            g.fillRect (juce::Rectangle<float> (cap.getX() + w, cap.getCentreY(), cap.getWidth() - w, 1.0f));
        }
    }

    void DetailPanel::resized()
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (headerHeight).reduced (padding, 0);
        if (power != nullptr)
            power->setBounds (header.removeFromLeft (30).withSizeKeepingCentre (30, 30));

        auto content = r.reduced (padding, 0).withTrimmedBottom (padding);
        visualArea = {};
        if (visual != nullptr && visualHeight > 0)
        {
            visualArea = content.removeFromTop (visualHeight);
            content.removeFromTop (14);
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
                if (c != power.get() && c != visual.get())
                    c->setAlpha (on ? 1.0f : 0.4f);
            repaint();
        }
    }

    //==========================================================================
    namespace
    {
        // Inner area of a section (below its caption)
        juce::Rectangle<int> inner (juce::Rectangle<int> section) { return section.withTrimmedTop (captionH).reduced (8, 2); }

        juce::Label& noteLabel (juce::Label& l)
        {
            l.setFont (font (12.0f));
            l.setColour (juce::Label::textColourId, colours::textDim);
            l.setJustificationType (juce::Justification::centredLeft);
            return l;
        }

        // Two stacked sections filling the area
        std::pair<juce::Rectangle<int>, juce::Rectangle<int>> split (juce::Rectangle<int> r)
        {
            auto top = r.removeFromTop (r.getHeight() / 2 - 5);
            r.removeFromTop (10);
            return { top, r };
        }

        float grFrom (const LiveData* live, const LiveData::History LiveData::* h) { return live != nullptr ? (live->*h).ago (0) : 0.0f; }

        class EqPanel final : public DetailPanel
        {
        public:
            explicit EqPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Equalizer", "Remove rumble, shape body, presence and air", pid::eqOn)
            {
                visualHeight = 216;
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
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto [top, bottom] = split (r);

                const int w = top.getWidth();
                auto lc = top.removeFromLeft (w * 2 / 9);
                top.removeFromLeft (10);
                auto low = top.removeFromLeft (w * 4 / 9);
                top.removeFromLeft (10);
                auto mid = top;
                section ("Low cut", lc);
                section ("Low band", low);
                section ("Mid", mid);
                auto lcIn = inner (lc);
                auto lcCtl = lcIn.removeFromLeft (lcIn.getWidth() / 2);
                lcOn->setBounds (lcCtl.removeFromTop (lcCtl.getHeight() / 2).withSizeKeepingCentre (lcCtl.getWidth() - 6, 26));
                lcSlope->setBounds (lcCtl.withSizeKeepingCentre (lcCtl.getWidth() - 6, 26));
                lcFreq->setBounds (lcIn);
                row ({ lowType, lowFreq, lowGain, lowQ }, inner (low));
                row ({ midFreq, midGain, midQ }, inner (mid));

                auto high = bottom.removeFromLeft (w * 4 / 9);
                bottom.removeFromLeft (10);
                auto hc = bottom.removeFromLeft (w * 2 / 9);
                bottom.removeFromLeft (10);
                auto out = bottom;
                section ("High band", high);
                section ("High cut", hc);
                section ("Output", out);
                row ({ highType, highFreq, highGain, highQ }, inner (high));
                auto hcIn = inner (hc);
                auto hcCtl = hcIn.removeFromLeft (hcIn.getWidth() / 2);
                hcOn->setBounds (hcCtl.removeFromTop (hcCtl.getHeight() / 2).withSizeKeepingCentre (hcCtl.getWidth() - 6, 26));
                hcSlope->setBounds (hcCtl.withSizeKeepingCentre (hcCtl.getWidth() - 6, 26));
                hcFreq->setBounds (hcIn);
                row ({ mix }, inner (out).withSizeKeepingCentre (110, inner (out).getHeight()));
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
                : DetailPanel (p, "Compressor", "Evens out the level so every word sits in the mix", pid::compOn)
            {
                for (auto& [id, label, ptr] : std::initializer_list<std::tuple<const char*, const char*, Knob**>> {
                         { pid::compThreshold, "Threshold", &threshold }, { pid::compRatio, "Ratio", &ratio },
                         { pid::compAttack, "Attack", &attack }, { pid::compRelease, "Release", &release },
                         { pid::compKnee, "Knee", &knee }, { pid::compMakeup, "Makeup", &makeup },
                         { pid::compMix, "Mix", &mix }, { pid::compScHpf, "SC filter", &scHpf } })
                    *ptr = &knob (id, label);
                detector = &choice (pid::compDetector, "Detector");
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
                gr.setBounds (r.removeFromRight (44));
                r.removeFromRight (12);
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
                : DetailPanel (p, "De-esser", "Tames sharp S and T sounds without dulling the voice", pid::deessOn)
            {
                freq = &knob (pid::deessFreq, "Frequency");
                threshold = &knob (pid::deessThreshold, "Threshold");
                range = &knob (pid::deessRange, "Range");
                mode = &choice (pid::deessMode, "Mode");
                listen = &toggle (pid::deessListen, "Listen");
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
                gr.setBounds (r.removeFromRight (44));
                r.removeFromRight (12);
                auto [a, b] = split (r);
                section ("Detection", a);
                section ("Behaviour", b);
                row ({ freq, threshold, range }, inner (a));
                auto in = inner (b);
                mode->setBounds (in.removeFromLeft (in.getWidth() / 2));
                listen->setBounds (in.withSizeKeepingCentre (110, 28));
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
                : DetailPanel (p, "Saturation", "Adds harmonics for warmth, grit and density", pid::satOn)
            {
                visualHeight = 230;
                character = &choice (pid::satCharacter, "Character");
                drive = &knob (pid::satDrive, "Drive");
                output = &knob (pid::satOutput, "Output");
                mix = &knob (pid::satMix, "Mix");
                drive->setMaxDialSize (110);
                drive->setHero (true);
                for (auto* k : { output, mix })
                    k->setMaxDialSize (84);
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
                : DetailPanel (p, "Doubler", "Two extra takes left and right for a wide, thick vocal", pid::doublerOn)
            {
                mix = &knob (pid::doublerMix, "Mix");
                width = &knob (pid::doublerWidth, "Width");
                delay = &knob (pid::doublerDelay, "Timing");
                detune = &knob (pid::doublerDetune, "Detune");
                monoSafe = &knob (pid::doublerMonoSafe, "Mono safe");
                tone = &knob (pid::doublerTone, "Tone");
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
                : DetailPanel (p, "Pitch", "Creative transposition (not pitch correction)", pid::pitchOn)
            {
                semis = &knob (pid::pitchSemitones, "Pitch");
                fine = &knob (pid::pitchCents, "Fine");
                formant = &knob (pid::pitchFormant, "Formant");
                mix = &knob (pid::pitchMix, "Mix");
                keep = &toggle (pid::pitchPreserveFormants, "Keep formants");
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
                info.setBounds (r.removeFromBottom (20).withTrimmedLeft (6));
                r.removeFromBottom (4);
                auto [a, b] = split (r);
                section ("Transpose", a);
                section ("Character", b);
                row ({ semis, fine, mix }, inner (a));
                auto in = inner (b);
                keep->setBounds (in.removeFromLeft (in.getWidth() / 3).withSizeKeepingCentre (150, 28));
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
                : DetailPanel (p, "Vocal chop", "Tempo-synced gate and stutter patterns", pid::chopOn)
            {
                visualHeight = 230;
                pattern = &choice (pid::chopPattern, "Pattern");
                rate = &choice (pid::chopRate, "Step length");
                depth = &knob (pid::chopDepth, "Depth");
                smooth = &knob (pid::chopSmooth, "Smooth");
                depth->setMaxDialSize (84);
                smooth->setMaxDialSize (84);
                info.setText ("Steps follow the song position while FL Studio plays. R = repeat the previous slice (stutter).", juce::dontSendNotification);
                addAndMakeVisible (noteLabel (info));
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                info.setBounds (r.removeFromBottom (20).withTrimmedLeft (6));
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
                : DetailPanel (p, "Delay", "Echoes beside the voice; ducking keeps the words clear", pid::delayOn)
            {
                visualHeight = 186;
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
                info.setBounds (r.removeFromBottom (18).withTrimmedLeft (6));
                r.removeFromBottom (4);
                auto [echo, duck] = split (r);
                section ("Echo", echo);
                auto e = inner (echo);
                auto toggles = e.removeFromLeft (116);
                sync->setBounds (toggles.removeFromTop (toggles.getHeight() / 2).withSizeKeepingCentre (104, 26));
                pingPong->setBounds (toggles.withSizeKeepingCentre (104, 26));
                row ({ time, feedback, mix, lowCut, highCut, toReverb }, e);
                division->setBounds (time->getBounds());

                gr.setBounds (duck.removeFromRight (44));
                duck.removeFromRight (12);
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
                : DetailPanel (p, "Reverb", "Room and depth; ducking clears space while you sing", pid::reverbOn)
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
                gr.setBounds (duck.removeFromRight (44));
                duck.removeFromRight (12);
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
                : DetailPanel (p, "Output", "Stereo image, mono safety and fair loudness comparison", nullptr)
            {
                width = &knob (pid::stereoWidth, "Width");
                monoBelow = &knob (pid::monoBelow, "Mono below");
                mode = &choice (pid::levelMatchMode, "Level match");
                window = &choice (pid::levelMatchWindow, "Measure over");
                range = &knob (pid::levelMatchRange, "Max correction");
                clip = &toggle (pid::safetyClip, "Clip guard");
                addAndMakeVisible (correlation);
                correlation.setTooltip ("Phase correlation: +1 = mono-safe, below 0 = parts cancel in mono");
                for (auto* l : { &inValue, &procValue, &matchValue })
                {
                    l->setFont (display (24.0f));
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
                    drawCaption (g, text, l.getBounds().toFloat().translated (0.0f, 24.0f).withHeight (14.0f),
                                 juce::Justification::centred, colours::textFaint, 8.5f);
                };
                under (inValue, "Input LUFS");
                under (procValue, "Processed LUFS");
                under (matchValue, "Match dB");
                drawCaption (g, "Correlation", correlation.getBounds().toFloat().translated (0.0f, -18.0f).withHeight (14.0f),
                             juce::Justification::centredLeft, colours::textDim, 9.0f);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto [stereo, loud] = split (r);
                section ("Stereo", stereo);
                auto s = inner (stereo);
                auto corr = s.removeFromRight (s.getWidth() / 3);
                correlation.setBounds (corr.withSizeKeepingCentre (corr.getWidth() - 30, 24));
                row ({ width, monoBelow }, s);

                section ("Loudness", loud);
                auto l = inner (loud);
                auto readouts = l.removeFromRight (l.getWidth() * 2 / 5);
                const int w = readouts.getWidth() / 3;
                for (auto* lab : { &inValue, &procValue, &matchValue })
                    lab->setBounds (readouts.removeFromLeft (w).withSizeKeepingCentre (w, 30).translated (0, -8));
                row ({ mode, window, range, clip }, l);
                clip->setBounds (clip->getBounds().withSizeKeepingCentre (116, 28));
            }

            Knob *width, *monoBelow, *range;
            Choice *mode, *window;
            Toggle* clip;
            CorrelationView correlation;
            juce::Label inValue, procValue, matchValue;
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
            }
            if (panel == nullptr)
                panel = std::make_unique<OutputPanel> (p);
        }
        panel->setLiveData (live);
        panel->setVisual (createModuleView (p, id, spectrum));
        return panel;
    }
}
