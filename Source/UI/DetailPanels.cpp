#include "DetailPanels.h"
#include "ChainView.h"
#include "../PluginProcessor.h"
#include "../DSP/ChopModule.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        constexpr int headerHeight = 54;
        constexpr int padding = 18;
        constexpr int knobH = 110;
    }

    //==========================================================================
    DetailPanel::DetailPanel (VelvoxAudioProcessor& p, juce::String t, juce::String s, juce::Colour c, const char* onId)
        : proc (p), apvts (p.getState()), accent (c), title (std::move (t)), subtitle (std::move (s)), onParamId (onId)
    {
        if (onParamId != nullptr)
        {
            power = std::make_unique<PowerButton> (apvts, onParamId, accent);
            addAndMakeVisible (*power);
        }
    }

    Knob& DetailPanel::knob (const char* paramId, const juce::String& label)
    {
        auto k = std::make_unique<Knob> (apvts, paramId, label, accent);
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
        auto t = std::make_unique<Toggle> (apvts, paramId, label, accent);
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
        g.fillRoundedRectangle (r, 12.0f);
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (on ? 0.10f : 0.03f), r.getX(), r.getY(),
                                                 juce::Colours::transparentBlack, r.getX(), r.getY() + 180.0f, false));
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (colours::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
        g.setColour (on ? accent : accent.withAlpha (0.3f));
        g.fillRoundedRectangle (r.getX() + 18.0f, r.getY(), r.getWidth() - 36.0f, 2.0f, 1.0f);

        auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 0);
        header.removeFromLeft (power != nullptr ? 36 : 0);
        g.setColour (on ? colours::text : colours::textDim);
        g.setFont (display (22.0f));
        const int titleW = juce::GlyphArrangement::getStringWidthInt (display (22.0f), title) + 14;
        g.drawText (title, header.removeFromLeft (titleW), juce::Justification::centredLeft, false);
        g.setColour (colours::textDim);
        g.setFont (font (13.0f));
        g.drawText (subtitle, header, juce::Justification::centredLeft, true);

        for (const auto& s : sections)
        {
            const auto a = s.area.toFloat();
            g.setColour (colours::raised.withAlpha (0.55f));
            g.fillRoundedRectangle (a, 8.0f);
            g.setColour (colours::textDim);
            g.setFont (font (12.0f, true));
            g.drawText (s.caption, a.reduced (12.0f, 0.0f).withHeight (24.0f), juce::Justification::centredLeft, false);
        }
    }

    void DetailPanel::resized()
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop (headerHeight).reduced (padding, 0);
        if (power != nullptr)
            power->setBounds (header.removeFromLeft (26).withSizeKeepingCentre (26, 26));

        sections.clear();
        layoutContent (r.reduced (padding, 0).withTrimmedBottom (padding));
    }

    void DetailPanel::refresh()
    {
        for (auto* k : knobs)
            k->refreshEffective();

        const int on = onParamId == nullptr || apvts.getRawParameterValue (onParamId)->load() > 0.5f ? 1 : 0;
        if (on != lastOn)
        {
            lastOn = on;
            for (auto* c : getChildren())
                if (c != power.get())
                    c->setAlpha (on ? 1.0f : 0.5f);
            repaint();
        }
    }

    //==========================================================================
    namespace
    {
        // Inner area of a section (below its caption)
        juce::Rectangle<int> inner (juce::Rectangle<int> section) { return section.withTrimmedTop (24).reduced (8, 6); }

        class EqPanel final : public DetailPanel
        {
        public:
            explicit EqPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Equalizer", "Remove rumble, shape body, presence and air", colours::eq, pid::eqOn)
            {
                addAndMakeVisible (curve);
                auto* procPtr = &p;
                curve.provider = [procPtr] (dsp::EqSettings& s, bool& on, float& mix)
                {
                    const auto e = procPtr->readEffectiveSettings();
                    s = e.eq;
                    on = e.eqOn;
                    mix = e.eqMix;
                };
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

            void refresh() override
            {
                DetailPanel::refresh();
                curve.refresh();
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                curve.setBounds (r.removeFromTop ((int) (r.getHeight() * 0.38f)));
                r.removeFromTop (10);
                auto top = r.removeFromTop (r.getHeight() / 2 - 4);
                r.removeFromTop (8);
                auto bottom = r;

                const int w = top.getWidth();
                auto lc = top.removeFromLeft (w * 2 / 9);
                top.removeFromLeft (8);
                auto low = top.removeFromLeft (w * 4 / 9);
                top.removeFromLeft (8);
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
                bottom.removeFromLeft (8);
                auto hc = bottom.removeFromLeft (w * 2 / 9);
                bottom.removeFromLeft (8);
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

            EqCurve curve;
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
                : DetailPanel (p, "Compressor", "Evens out the level so every word sits in the mix", colours::comp, pid::compOn)
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
                gr.push (proc.getChain().getCompressor().gainReductionDb.pull());
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                gr.setBounds (r.removeFromRight (44).reduced (0, 6));
                r.removeFromRight (14);
                auto dyn = r.removeFromTop (r.getHeight() / 2 - 4);
                r.removeFromTop (8);
                section ("Dynamics", dyn);
                section ("Output and detector", r);
                row ({ threshold, ratio, attack, release, knee }, inner (dyn));
                row ({ makeup, mix, scHpf, detector }, inner (r));
            }

            Knob *threshold, *ratio, *attack, *release, *knee, *makeup, *mix, *scHpf;
            Choice* detector;
            GainReductionMeter gr { colours::comp, "GR" };
        };

        class DeEssPanel final : public DetailPanel
        {
        public:
            explicit DeEssPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "De-esser", "Tames sharp S and T sounds without dulling the voice", colours::deess, pid::deessOn)
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
                gr.push (proc.getChain().getDeEsser().gainReductionDb.pull());
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                gr.setBounds (r.removeFromRight (44).reduced (0, 6));
                r.removeFromRight (14);
                auto a = r.removeFromTop (r.getHeight() / 2 - 4);
                r.removeFromTop (8);
                section ("Detection", a);
                section ("Behaviour", r);
                row ({ freq, threshold, range }, inner (a));
                auto b = inner (r);
                mode->setBounds (b.removeFromLeft (b.getWidth() / 2));
                listen->setBounds (b.withSizeKeepingCentre (110, 28));
            }

            Knob *freq, *threshold, *range;
            Choice* mode;
            Toggle* listen;
            GainReductionMeter gr { colours::deess, "GR" };
        };

        class SatPanel final : public DetailPanel
        {
        public:
            explicit SatPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Saturation", "Adds harmonics for warmth, grit and density", colours::sat, pid::satOn)
            {
                addAndMakeVisible (curve);
                auto* procPtr = &p;
                curve.provider = [procPtr] (float& d, float& m, int& c, bool& a)
                {
                    const auto e = procPtr->readEffectiveSettings();
                    d = e.sat.driveDb; m = e.sat.mix; c = e.sat.character; a = e.sat.enabled;
                };
                curve.setTooltip ("Transfer curve: input level (horizontal) to output level (vertical)");
                character = &choice (pid::satCharacter, "Character");
                drive = &knob (pid::satDrive, "Drive");
                output = &knob (pid::satOutput, "Output");
                mix = &knob (pid::satMix, "Mix");
            }

            void refresh() override
            {
                DetailPanel::refresh();
                curve.refresh();
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                curve.setBounds (r.removeFromLeft (std::min (r.getHeight(), r.getWidth() / 3)).reduced (0, 10));
                r.removeFromLeft (16);
                section ("Tone", r);
                auto in = inner (r).withSizeKeepingCentre (inner (r).getWidth(), std::min (inner (r).getHeight(), 160));
                row ({ character, drive, output, mix }, in);
            }

            SaturationCurve curve;
            Choice* character;
            Knob *drive, *output, *mix;
        };

        class DoublerPanel final : public DetailPanel
        {
        public:
            explicit DoublerPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Doubler", "Two extra takes left and right for a wide, thick vocal", colours::doubler, pid::doublerOn)
            {
                mix = &knob (pid::doublerMix, "Mix");
                width = &knob (pid::doublerWidth, "Width");
                delay = &knob (pid::doublerDelay, "Timing");
                detune = &knob (pid::doublerDetune, "Detune");
                monoSafe = &knob (pid::doublerMonoSafe, "Mono safe");
                tone = &knob (pid::doublerTone, "Tone");
                note.setText ("Mono safe keeps the doubles out of the mono sum: at 100 % a mono speaker plays exactly the original vocal.",
                              juce::dontSendNotification);
                note.setFont (font (12.5f));
                note.setColour (juce::Label::textColourId, colours::textDim);
                note.setJustificationType (juce::Justification::centredLeft);
                addAndMakeVisible (note);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                note.setBounds (r.removeFromBottom (36));
                auto a = r.removeFromTop (r.getHeight() / 2 - 4);
                r.removeFromTop (8);
                section ("Voices", a);
                section ("Stereo and tone", r);
                row ({ mix, delay, detune }, inner (a));
                row ({ width, monoSafe, tone }, inner (r));
            }

            Knob *mix, *width, *delay, *detune, *monoSafe, *tone;
            juce::Label note;
        };

        class PitchPanel final : public DetailPanel
        {
        public:
            explicit PitchPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Pitch", "Creative transposition (not pitch correction)", colours::pitch, pid::pitchOn)
            {
                semis = &knob (pid::pitchSemitones, "Pitch");
                fine = &knob (pid::pitchCents, "Fine");
                formant = &knob (pid::pitchFormant, "Formant");
                mix = &knob (pid::pitchMix, "Mix");
                keep = &toggle (pid::pitchPreserveFormants, "Keep formants");
                info.setFont (font (12.5f));
                info.setColour (juce::Label::textColourId, colours::textDim);
                addAndMakeVisible (info);
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
                info.setBounds (r.removeFromBottom (36));
                auto a = r.removeFromTop (r.getHeight() / 2 - 4);
                r.removeFromTop (8);
                section ("Transpose", a);
                section ("Character", r);
                row ({ semis, fine, mix }, inner (a));
                auto b = inner (r);
                keep->setBounds (b.removeFromLeft (b.getWidth() / 3).withSizeKeepingCentre (150, 28));
                row ({ formant }, b.removeFromLeft (b.getWidth() / 2));
            }

            Knob *semis, *fine, *formant, *mix;
            Toggle* keep;
            juce::Label info;
        };

        class ChopPanel final : public DetailPanel
        {
        public:
            explicit ChopPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Vocal chop", "Tempo-synced gate and stutter patterns", colours::chop, pid::chopOn)
            {
                addAndMakeVisible (steps);
                pattern = &choice (pid::chopPattern, "Pattern");
                rate = &choice (pid::chopRate, "Step length");
                depth = &knob (pid::chopDepth, "Depth");
                smooth = &knob (pid::chopSmooth, "Smooth");
                info.setFont (font (12.5f));
                info.setColour (juce::Label::textColourId, colours::textDim);
                info.setText ("Steps follow the song position while FL Studio plays. R = repeat the previous slice (stutter).", juce::dontSendNotification);
                addAndMakeVisible (info);
            }

            void refresh() override
            {
                DetailPanel::refresh();
                const auto e = proc.readEffectiveSettings();
                const auto& pats = dsp::ChopModule::patterns();
                steps.set (pats[(size_t) juce::jlimit (0, (int) pats.size() - 1, e.chop.pattern)].steps,
                           proc.getChain().getChopStep(), e.chop.enabled, accent);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                info.setBounds (r.removeFromBottom (30));
                steps.setBounds (r.removeFromTop (54).reduced (0, 6));
                r.removeFromTop (12);
                section ("Pattern", r);
                row ({ pattern, rate, depth, smooth }, inner (r).withSizeKeepingCentre (inner (r).getWidth(), std::min (inner (r).getHeight(), 150)));
            }

            StepPatternView steps;
            Choice *pattern, *rate;
            Knob *depth, *smooth;
            juce::Label info;
        };

        class DelayPanel final : public DetailPanel
        {
        public:
            explicit DelayPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Delay", "Echoes beside the voice; ducking keeps the words clear", colours::delay, pid::delayOn)
            {
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
                info.setFont (font (12.0f));
                info.setColour (juce::Label::textColourId, colours::textDim);
                addAndMakeVisible (info);
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (proc.getChain().getDelayDucker().gainReductionDb.pull());
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
                info.setBounds (r.removeFromBottom (24));
                auto echo = r.removeFromTop (r.getHeight() / 2 - 4);
                r.removeFromTop (8);
                section ("Echo", echo);
                auto e = inner (echo);
                auto toggles = e.removeFromLeft (110);
                sync->setBounds (toggles.removeFromTop (toggles.getHeight() / 2).withSizeKeepingCentre (100, 28));
                pingPong->setBounds (toggles.withSizeKeepingCentre (100, 28));
                row ({ time, feedback, mix, lowCut, highCut, toReverb }, e);
                division->setBounds (time->getBounds());

                gr.setBounds (r.removeFromRight (44).reduced (0, 6));
                r.removeFromRight (10);
                section ("Ducking", r);
                row ({ source, duckAmount, duckThreshold, duckAttack, duckRelease }, inner (r));
            }

            Knob *time, *feedback, *mix, *lowCut, *highCut, *toReverb, *duckAmount, *duckThreshold, *duckAttack, *duckRelease;
            Choice *division, *source;
            Toggle *sync, *pingPong;
            GainReductionMeter gr { colours::delay, "Duck" };
            juce::Label info;
        };

        class ReverbPanel final : public DetailPanel
        {
        public:
            explicit ReverbPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Reverb", "Room and depth; ducking clears space while you sing", colours::reverb, pid::reverbOn)
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
                gr.push (proc.getChain().getReverbDucker().gainReductionDb.pull());
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto space = r.removeFromTop (r.getHeight() / 2 - 4);
                r.removeFromTop (8);
                section ("Space", space);
                row ({ type, mix, decay, preDelay, size, tone, lowCut, width }, inner (space));
                gr.setBounds (r.removeFromRight (44).reduced (0, 6));
                r.removeFromRight (10);
                section ("Ducking", r);
                row ({ source, duckAmount, duckThreshold, duckAttack, duckRelease }, inner (r));
            }

            Choice *type, *source;
            Knob *mix, *decay, *preDelay, *size, *tone, *lowCut, *width, *duckAmount, *duckThreshold, *duckAttack, *duckRelease;
            GainReductionMeter gr { colours::reverb, "Duck" };
        };

        class OutputPanel final : public DetailPanel
        {
        public:
            explicit OutputPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Output", "Stereo image, mono safety and fair loudness comparison", colours::io, nullptr)
            {
                width = &knob (pid::stereoWidth, "Width");
                monoBelow = &knob (pid::monoBelow, "Mono below");
                mode = &choice (pid::levelMatchMode, "Level match");
                window = &choice (pid::levelMatchWindow, "Measure over");
                range = &knob (pid::levelMatchRange, "Max correction");
                clip = &toggle (pid::safetyClip, "Clip guard");
                addAndMakeVisible (correlation);
                correlation.setTooltip ("Phase correlation: +1 = mono-safe, below 0 = parts cancel in mono");
                readout.setFont (display (15.0f));
                readout.setColour (juce::Label::textColourId, colours::text);
                readout.setJustificationType (juce::Justification::centredLeft);
                addAndMakeVisible (readout);
            }

            void refresh() override
            {
                DetailPanel::refresh();
                const auto& m = proc.getChain().meters;
                correlation.push (m.correlation.load());
                const auto fmt = [] (float lufs) { return lufs <= -99.0f ? juce::String ("--") : juce::String (lufs, 1); };
                const juce::String text = "Input " + fmt (m.inputLufs.load()) + " LUFS    Processed " + fmt (m.processedLufs.load())
                                          + " LUFS    Match " + juce::String (m.levelMatchDb.load(), 1) + " dB";
                if (readout.getText() != text)
                    readout.setText (text, juce::dontSendNotification);
            }

        private:
            void layoutContent (juce::Rectangle<int> r) override
            {
                auto stereo = r.removeFromTop (r.getHeight() / 2 - 4);
                r.removeFromTop (8);
                section ("Stereo", stereo);
                auto s = inner (stereo);
                auto corr = s.removeFromRight (s.getWidth() / 3);
                correlation.setBounds (corr.withSizeKeepingCentre (corr.getWidth() - 20, 34));
                row ({ width, monoBelow }, s);

                section ("Loudness", r);
                auto l = inner (r);
                readout.setBounds (l.removeFromBottom (26));
                row ({ mode, window, range, clip }, l);
                clip->setBounds (clip->getBounds().withSizeKeepingCentre (120, 28));
            }

            Knob *width, *monoBelow, *range;
            Choice *mode, *window;
            Toggle* clip;
            CorrelationView correlation;
            juce::Label readout;
        };
    }

    std::unique_ptr<DetailPanel> createDetailPanel (VelvoxAudioProcessor& p, int id)
    {
        if (id < 0)
            return std::make_unique<OutputPanel> (p);

        switch ((ModuleId) id)
        {
            case ModuleId::eq:      return std::make_unique<EqPanel> (p);
            case ModuleId::comp:    return std::make_unique<CompPanel> (p);
            case ModuleId::deess:   return std::make_unique<DeEssPanel> (p);
            case ModuleId::sat:     return std::make_unique<SatPanel> (p);
            case ModuleId::doubler: return std::make_unique<DoublerPanel> (p);
            case ModuleId::pitch:   return std::make_unique<PitchPanel> (p);
            case ModuleId::chop:    return std::make_unique<ChopPanel> (p);
            case ModuleId::delay:   return std::make_unique<DelayPanel> (p);
            case ModuleId::reverb:  return std::make_unique<ReverbPanel> (p);
        }
        return std::make_unique<OutputPanel> (p);
    }
}
