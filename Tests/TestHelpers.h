#pragma once

// Shared helpers for the offline tests: a real processor driven with test signals.

#include "PluginProcessor.h"
#include "Presets/FactoryPresets.h"

namespace velvox::test
{
    constexpr double pi = 3.14159265358979323846;

    struct Harness
    {
        // The processor is large (~220 kB, the DSP chain): on the heap, like in
        // a host, so tests with several harnesses do not exhaust the stack
        std::unique_ptr<VelvoxAudioProcessor> procHolder = std::make_unique<VelvoxAudioProcessor>();
        VelvoxAudioProcessor& proc = *procHolder;
        double fs;
        int blockSize;
        int channels;

        Harness (double sampleRate = 48000.0, int bs = 512, int numChannels = 2)
            : fs (sampleRate), blockSize (bs), channels (numChannels)
        {
            if (numChannels == 1)
            {
                juce::AudioProcessor::BusesLayout mono;
                mono.inputBuses.add (juce::AudioChannelSet::mono());
                mono.inputBuses.add (juce::AudioChannelSet::disabled()); // sidechain
                mono.outputBuses.add (juce::AudioChannelSet::mono());
                proc.setBusesLayout (mono);
            }
            proc.setRateAndBufferSizeDetails (fs, bs);
            proc.prepareToPlay (fs, bs);
        }

        void set (const char* id, float value)
        {
            auto* p = proc.getState().getParameter (id);
            jassert (p != nullptr);
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        }

        float get (const char* id)
        {
            return proc.getState().getRawParameterValue (id)->load();
        }

        // Every module off, unity gains, no safety clip: a clean pass-through
        void neutral()
        {
            for (auto* id : { pid::eqOn, pid::compOn, pid::deessOn, pid::satOn, pid::delayOn, pid::reverbOn,
                              pid::hpfOn, pid::safetyClip, pid::bypass, pid::deessListen, pid::delaySync, pid::delayPingPong })
                set (id, 0.0f);
            set (pid::inputGain, 0.0f);
            set (pid::outputGain, 0.0f);
        }

        // Processes a whole signal in host-sized blocks (last block may be shorter)
        juce::AudioBuffer<float> run (const juce::AudioBuffer<float>& input, int bs = -1)
        {
            if (bs <= 0)
                bs = blockSize;

            juce::AudioBuffer<float> out (input);
            juce::MidiBuffer midi;

            for (int start = 0; start < out.getNumSamples(); start += bs)
            {
                const int n = std::min (bs, out.getNumSamples() - start);
                juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), start, n);
                proc.processBlock (view, midi);
            }
            return out;
        }

        juce::AudioBuffer<float> sine (double freq, float amp, double seconds) const
        {
            const int n = (int) (seconds * fs);
            juce::AudioBuffer<float> b (channels, n);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < n; ++i)
                    b.setSample (ch, i, amp * (float) std::sin (2.0 * pi * freq * i / fs));
            return b;
        }

        juce::AudioBuffer<float> noise (float amp, double seconds, int seed = 1) const
        {
            juce::Random r (seed);
            const int n = (int) (seconds * fs);
            juce::AudioBuffer<float> b (channels, n);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < n; ++i)
                    b.setSample (ch, i, amp * (r.nextFloat() * 2.0f - 1.0f));
            return b;
        }

        juce::AudioBuffer<float> silence (double seconds) const
        {
            juce::AudioBuffer<float> b (channels, (int) (seconds * fs));
            b.clear();
            return b;
        }
    };

    inline float rmsDb (const juce::AudioBuffer<float>& b, int start, int len, int ch = 0)
    {
        return juce::Decibels::gainToDecibels (b.getRMSLevel (ch, start, len), -200.0f);
    }

    inline float peakOf (const juce::AudioBuffer<float>& b, int start, int len)
    {
        float m = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            m = std::max (m, b.getMagnitude (ch, start, len));
        return m;
    }

    inline bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)))
                    return false;
        return true;
    }

    // Goertzel magnitude of one frequency
    inline double toneLevel (const juce::AudioBuffer<float>& b, int start, int len, double freq, double fs)
    {
        const double w = 2.0 * pi * freq / fs;
        const double coeff = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double s = b.getSample (0, start + i) + coeff * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        return std::sqrt (s1 * s1 + s2 * s2 - coeff * s1 * s2) / (len * 0.5);
    }
}
