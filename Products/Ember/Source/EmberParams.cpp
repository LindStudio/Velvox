#include "EmberParams.h"
#include "Text/I18n.h"

namespace ember
{
    namespace
    {
        using Float = juce::AudioParameterFloat;
        using Attr = juce::AudioParameterFloatAttributes;

        juce::NormalisableRange<float> logRange (float lo, float hi, float centre)
        {
            juce::NormalisableRange<float> r (lo, hi);
            r.setSkewForCentre (centre);
            return r;
        }

        Attr unit (const juce::String& label, int decimals)
        {
            return Attr().withLabel (label).withStringFromValueFunction ([decimals, label] (float v, int)
            {
                return vocalypse::i18n::number (v, decimals) + " " + label;
            });
        }

        Attr hz()
        {
            return Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int)
            {
                return v >= 1000.0f ? vocalypse::i18n::number (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
            });
        }

        Attr percent()
        {
            return Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int)
            {
                return juce::String (juce::roundToInt (v)) + " %";
            });
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        const auto id = [] (const char* s) { return juce::ParameterID { s, 1 }; };

        layout.add (std::make_unique<Float> (id (pid::freq), "Frequency", logRange (2000.0f, 16000.0f, 6500.0f), 6500.0f, hz()));
        layout.add (std::make_unique<Float> (id (pid::threshold), "Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -30.0f, unit ("dB", 1)));
        layout.add (std::make_unique<Float> (id (pid::range), "Range", juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 8.0f, unit ("dB", 1)));
        layout.add (std::make_unique<Float> (id (pid::amount), "Tame", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, percent()));
        layout.add (std::make_unique<Float> (id (pid::attack), "Attack", logRange (0.1f, 20.0f, 1.0f), 0.5f, unit ("ms", 2)));
        layout.add (std::make_unique<Float> (id (pid::release), "Release", logRange (10.0f, 400.0f, 60.0f), 60.0f, unit ("ms", 0)));
        layout.add (std::make_unique<Float> (id (pid::lookahead), "Lookahead", juce::NormalisableRange<float> (0.0f, maxLookaheadMs, 0.01f), 2.0f, unit ("ms", 2)));
        layout.add (std::make_unique<Float> (id (pid::mix), "Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, percent()));
        layout.add (std::make_unique<Float> (id (pid::output), "Output", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, unit ("dB", 1)));
        layout.add (std::make_unique<juce::AudioParameterChoice> (id (pid::mode), "Mode", modeChoices, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (id (pid::detection), "Detection", detectionChoices, 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (id (pid::listen), "Listen", listenChoices, 0));
        layout.add (std::make_unique<juce::AudioParameterBool> (id (pid::bypass), "Bypass", false));
        return layout;
    }

    ParamCache::ParamCache (const APVTS& state)
    {
        const auto p = [&] (const char* id) { return state.getRawParameterValue (id); };
        freq = p (pid::freq); threshold = p (pid::threshold); range = p (pid::range); amount = p (pid::amount);
        attack = p (pid::attack); release = p (pid::release); lookahead = p (pid::lookahead); mix = p (pid::mix);
        output = p (pid::output); mode = p (pid::mode); detection = p (pid::detection); listen = p (pid::listen);
        bypass = p (pid::bypass);
    }

    Settings ParamCache::read() const noexcept
    {
        Settings s;
        s.freq = freq->load();
        s.thresholdDb = threshold->load();
        s.rangeDb = range->load();
        s.amount = amount->load() * 0.01f;
        s.attackMs = attack->load();
        s.releaseMs = release->load();
        s.lookaheadMs = lookahead->load();
        s.mix = mix->load() * 0.01f;
        s.outputDb = output->load();
        s.mode = (int) std::lround (mode->load());
        s.relative = std::lround (detection->load()) == 0;
        s.listen = (int) std::lround (listen->load());
        s.bypass = bypass->load() > 0.5f;
        return s;
    }

    Settings readSettings (const APVTS& state)
    {
        return ParamCache (state).read();
    }
}
