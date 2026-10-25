#include "ReverbPanel.h"
#include "../PluginProcessor.h"
#include "HelpContent.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        juce::RangedAudioParameter* param (VelvoxAudioProcessor& proc, const char* base, int instance)
        {
            auto& state = proc.getState();
            if (instance > 0)
                if (auto* p = state.getParameter (dsp::instanceParamId (base, instance)))
                    return p;
            return state.getParameter (base);
        }

        float realValue (VelvoxAudioProcessor& proc, const char* base, int instance)
        {
            auto* p = param (proc, base, instance);
            return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
        }

        // The three parameters the depth moves, in gesture order
        constexpr const char* depthParams[3] { pid::reverbMix, pid::reverbPreDelay, pid::reverbSize };
    }

    //==========================================================================
    namespace reverbedit
    {
        float depth (VelvoxAudioProcessor& proc, int instance)
        {
            return dsp::reverbDepthFromSettings (juce::roundToInt (realValue (proc, pid::reverbType, instance)),
                                                 realValue (proc, pid::reverbMix, instance), realValue (proc, pid::reverbPreDelay, instance),
                                                 realValue (proc, pid::reverbSize, instance));
        }

        dsp::ReverbDepthPoint pointFor (VelvoxAudioProcessor& proc, int instance, float d)
        {
            return dsp::reverbDepthPath (juce::roundToInt (realValue (proc, pid::reverbType, instance)), d);
        }

        void beginDepthGesture (VelvoxAudioProcessor& proc, int instance)
        {
            for (auto* id : depthParams)
                if (auto* p = param (proc, id, instance))
                    p->beginChangeGesture();
        }

        void writeDepth (VelvoxAudioProcessor& proc, int instance, float d)
        {
            const auto point = pointFor (proc, instance, d);
            const float values[3] { point.mixPercent, point.preDelayMs, point.sizePercent };
            for (int i = 0; i < 3; ++i)
                if (auto* p = param (proc, depthParams[i], instance))
                {
                    const float v = p->convertTo0to1 (values[i]);
                    if (std::abs (v - p->getValue()) > 1.0e-7f)
                        p->setValueNotifyingHost (v);
                }
        }

        void endDepthGesture (VelvoxAudioProcessor& proc, int instance)
        {
            for (auto* id : depthParams)
                if (auto* p = param (proc, id, instance))
                    p->endChangeGesture();
        }

        void setDepth (VelvoxAudioProcessor& proc, int instance, float d)
        {
            beginDepthGesture (proc, instance);
            writeDepth (proc, instance, d);
            endDepthGesture (proc, instance);
        }

        void resetDepth (VelvoxAudioProcessor& proc, int instance)
        {
            beginDepthGesture (proc, instance);
            for (auto* id : depthParams)
                if (auto* p = param (proc, id, instance))
                    p->setValueNotifyingHost (p->getDefaultValue());
            endDepthGesture (proc, instance);
        }
    }

    //==========================================================================
    namespace
    {
        // REVERB (spec 5.12): the room with the depth dot; Room, Size, Decay, Mix; everything else in Mehr
        class ReverbPanel final : public DetailPanel
        {
        public:
            explicit ReverbPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Reverb", help::shortDescription ((int) ModuleId::reverb), pid::reverbOn)
            {
                auto& type = choice (pid::reverbType, {});
                setCaption (type, "Room");
                type.box.setScrollWheelEnabled (true);
                segments (pid::duckSource, { "Vocal", "Sidechain" });
                auto& size = knob (pid::reverbSize, "Size");
                auto& decay = knob (pid::reverbDecay, "Decay");
                auto& mix = knob (pid::reverbMix, "Mix");
                knob (pid::reverbPreDelay, "Pre-Delay");
                knob (pid::reverbLowCut, "Low Cut");
                knob (pid::reverbHighCut, "High Cut");
                knob (pid::reverbDamping, "Damping");
                knob (pid::reverbBass, "Bass Decay");
                knob (pid::reverbWidth, "Width");
                knob (pid::reverbEarly, "Early");
                knob (pid::reverbDiffusion, "Diffusion");
                knob (pid::reverbMod, "Movement");
                knob (pid::reverbDuckAmount, "Ducking");
                knob (pid::reverbDuckThreshold, "Duck Thresh");
                knob (pid::reverbDuckAttack, "Duck Attack");
                knob (pid::reverbDuckRelease, "Duck Release");
                setMainControls ({ &type, &size, &decay, &mix });
                setMixKnob (&mix);
                gr.setTooltip ("Ducking (dB)");
                placeInVisual ({ &gr });
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (live != nullptr ? live->reverbDuck.ago (0) : 0.0f);
            }

        private:
            void layoutVisual (juce::Rectangle<int> area) override { placeMeter (gr, area); }
            GainReductionMeter gr { colours::accent, "Duck" };
        };
    }

    std::unique_ptr<DetailPanel> createReverbPanel (VelvoxAudioProcessor& proc)
    {
        return std::make_unique<ReverbPanel> (proc);
    }
}
