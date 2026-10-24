// Reference host core (tools/refhost): offline rendering through any AudioProcessor with
// latency compensation, parameter lookup by index / ID / name, parameter setting by text
// (with a fallback for plugins whose text parser is broken), and the single-module
// Headliner processor used for module-vs-reference measurements.

#include "TestHelpers.h"
#include "RefHostCore.h"
#include "HeadlinerModule.h"

using namespace velvox;
using namespace velvox::test;

namespace
{
    // A processor that delays by its reported latency and applies a gain; its float parameter
    // has a deliberately broken text parser (always 0), like some third-party plugins.
    class FakeDelayFx final : public juce::AudioProcessor
    {
    public:
        static constexpr int latency = 333;

        FakeDelayFx()
            : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                               .withOutput ("Out", juce::AudioChannelSet::stereo()))
        {
            addParameter (gain = new juce::AudioParameterFloat (
                juce::ParameterID { "gain", 1 }, "Gain dB", juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f,
                juce::AudioParameterFloatAttributes()
                    .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " dB"; })
                    .withValueFromStringFunction ([] (const juce::String&) { return 0.0f; })));
            addParameter (mode = new juce::AudioParameterChoice (juce::ParameterID { "mode", 1 }, "Mode",
                                                                 juce::StringArray { "A", "B", "C" }, 0));
            addParameter (freq = new juce::AudioParameterFloat (
                juce::ParameterID { "freq", 1 }, "Frequency", juce::NormalisableRange<float> (20.0f, 20000.0f, 0.0f, 0.25f), 1000.0f,
                juce::AudioParameterFloatAttributes()
                    .withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                                                                                           : juce::String (v, 1) + " Hz"; })));
            // German-locale decimals ("2,50") and a continuous parameter shown as yes / no (as some plugins do)
            addParameter (amount = new juce::AudioParameterFloat (
                juce::ParameterID { "amount", 1 }, "Amount", juce::NormalisableRange<float> (0.0f, 10.0f), 0.0f,
                juce::AudioParameterFloatAttributes()
                    .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2).replaceCharacter ('.', ',') + " dB"; })));
            addParameter (flag = new juce::AudioParameterFloat (
                juce::ParameterID { "flag", 1 }, "Flag", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                juce::AudioParameterFloatAttributes()
                    .withStringFromValueFunction ([] (float v, int) { return v > 0.5f ? juce::String ("yes") : juce::String ("no"); })));
            // A plugin whose value-to-text conversion ignores the value and always shows the current one
            addParameter (depth = new juce::AudioParameterFloat (
                juce::ParameterID { "depth", 1 }, "Depth", juce::NormalisableRange<float> (0.0f, 100.0f), 38.0f,
                juce::AudioParameterFloatAttributes()
                    .withStringFromValueFunction ([this] (float, int) { return juce::String (depth != nullptr ? depth->get() : 38.0f, 1) + " %"; })
                    .withValueFromStringFunction ([] (const juce::String&) { return 38.0f; })));
        }

        void prepareToPlay (double, int) override
        {
            setLatencySamples (latency);
            line.assign ((size_t) getTotalNumInputChannels(), std::vector<float> ((size_t) latency, 0.0f));
            pos = 0;
        }
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            const float g = juce::Decibels::decibelsToGain (gain->get());
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float x = b.getSample (ch, i);
                    b.setSample (ch, i, line[(size_t) ch][(size_t) pos] * g);
                    line[(size_t) ch][(size_t) pos] = x;
                }
                pos = (pos + 1) % latency;
            }
        }

        const juce::String getName() const override { return "FakeDelayFx"; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}

        juce::AudioParameterFloat* gain = nullptr;
        juce::AudioParameterChoice* mode = nullptr;
        juce::AudioParameterFloat* freq = nullptr;
        juce::AudioParameterFloat* amount = nullptr;
        juce::AudioParameterFloat* flag = nullptr;
        juce::AudioParameterFloat* depth = nullptr;

    private:
        std::vector<std::vector<float>> line;
        int pos = 0;
    };
}

class RefHostTests final : public juce::UnitTest
{
public:
    RefHostTests() : juce::UnitTest ("RefHost core", "RefHost") {}

    void runTest() override
    {
        constexpr double fs = 48000.0;

        beginTest ("latency compensation: output aligned to the input, length = input + tail");
        {
            FakeDelayFx fx;
            juce::AudioBuffer<float> in (2, 5000);
            juce::Random r (7);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < in.getNumSamples(); ++i)
                    in.setSample (ch, i, r.nextFloat() - 0.5f);
            refhost::RenderStats st;
            const auto out = refhost::renderOffline (fx, in, fs, 512, 100, st);
            expectEquals (st.latency, FakeDelayFx::latency);
            expectEquals (out.getNumSamples(), 5100);
            expectEquals (out.getNumChannels(), 2);
            float maxErr = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 5000; ++i)
                    maxErr = std::max (maxErr, std::abs (out.getSample (ch, i) - in.getSample (ch, i)));
            expectLessThan (maxErr, 1.0e-7f, "sample-exact after compensation");
            expect (st.blocks >= 10 && st.meanBlockMicros >= 0.0 && st.maxBlockMicros >= st.meanBlockMicros);
        }

        beginTest ("mono input feeds both channels of a stereo processor");
        {
            FakeDelayFx fx;
            auto in = juce::AudioBuffer<float> (1, 2000);
            for (int i = 0; i < 2000; ++i)
                in.setSample (0, i, (float) std::sin (0.01 * i));
            refhost::RenderStats st;
            const auto out = refhost::renderOffline (fx, in, fs, 256, 0, st);
            float maxDiff = 0.0f;
            for (int i = 0; i < 2000; ++i)
                maxDiff = std::max (maxDiff, std::abs (out.getSample (0, i) - out.getSample (1, i)));
            expectLessThan (maxDiff, 1.0e-9f);
            expectWithinAbsoluteError (out.getSample (0, 1500), in.getSample (0, 1500), 1.0e-7f);
        }

        beginTest ("parameter lookup by index, ID, name and unique prefix");
        {
            FakeDelayFx fx;
            expect (refhost::findParameter (fx, "0") == fx.gain);
            expect (refhost::findParameter (fx, "mode") == fx.mode);
            expect (refhost::findParameter (fx, "gain db") == fx.gain);
            expect (refhost::findParameter (fx, "Freq") == fx.freq);
            expect (refhost::findParameter (fx, "Gai") == fx.gain);
            expect (refhost::findParameter (fx, "nothing") == nullptr);
            expect (refhost::describeParameters (fx).contains ("Gain dB"));
        }

        beginTest ("parameter set by text: broken parser falls back to search, units and choices");
        {
            FakeDelayFx fx;
            juce::String err;
            expect (refhost::setParameterText (*fx.gain, "6", err), err);
            expectWithinAbsoluteError (fx.gain->get(), 6.0f, 0.05f);
            expect (refhost::setParameterText (*fx.gain, "-12.5 dB", err), err);
            expectWithinAbsoluteError (fx.gain->get(), -12.5f, 0.05f);
            expect (refhost::setParameterText (*fx.mode, "B", err), err);
            expectEquals (fx.mode->getIndex(), 1);
            expect (refhost::setParameterText (*fx.freq, "2.5k", err), err);
            expectWithinAbsoluteError (fx.freq->get(), 2500.0f, 25.0f);
            expect (refhost::setParameterText (*fx.freq, "300 Hz", err), err);
            expectWithinAbsoluteError (fx.freq->get(), 300.0f, 3.0f);
            expect (! refhost::setParameterText (*fx.mode, "Z", err), "unknown choice is an error");
            expect (refhost::setParameterText (*fx.amount, "2.5", err), err);
            expectWithinAbsoluteError (fx.amount->get(), 2.5f, 0.02f);
            expect (refhost::setParameterText (*fx.freq, "2k5", err), err);
            expectWithinAbsoluteError (fx.freq->get(), 2500.0f, 25.0f);
            expect (refhost::setParameterText (*fx.flag, "yes", err), err);
            expectGreaterThan (fx.flag->get(), 0.5f);
            expect (refhost::setParameterText (*fx.flag, "no", err), err);
            expectLessThan (fx.flag->get(), 0.5f);
            expect (refhost::describeParameters (fx).contains ("[A|B|C]"), "choices are listed");
            expect (refhost::setParameterText (*fx.depth, "30", err), err);
            expectWithinAbsoluteError (fx.depth->get(), 30.0f, 0.2f);
        }

        beginTest ("single-module Headliner processor: one insert, module on, transparent at neutral settings");
        {
            auto proc = refhost::makeSingleModule (dsp::ModuleId::eq, 0);
            expectEquals (proc->getRouting().size(), 1);
            expect (proc->getRouting().isInsert (dsp::ModuleId::eq, 0));
            expectEquals (proc->getState().getRawParameterValue (pid::eqOn)->load(), 1.0f);
            expectEquals (proc->getState().getRawParameterValue (pid::safetyClip)->load(), 0.0f);
            for (auto* id : { pid::lowGain, pid::midGain, pid::highGain })
                expectEquals (proc->getState().getRawParameterValue (id)->load(), 0.0f);
            for (auto* id : { pid::hpfOn, pid::lpfOn })
            {
                juce::String err;
                expect (refhost::setParameterText (*refhost::findParameter (*proc, id), "0", err), err);
            }

            juce::AudioBuffer<float> in (2, 24000);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < in.getNumSamples(); ++i)
                    in.setSample (ch, i, 0.3f * (float) std::sin (2.0 * pi * 997.0 * i / fs));
            refhost::RenderStats st;
            const auto out = refhost::renderOffline (*proc, in, fs, 512, 0, st);
            double err2 = 0.0, sig2 = 0.0;
            for (int i = 4800; i < 24000; ++i)
            {
                const double d = out.getSample (0, i) - in.getSample (0, i);
                err2 += d * d;
                sig2 += (double) in.getSample (0, i) * in.getSample (0, i);
            }
            expectLessThan (10.0 * std::log10 (err2 / sig2 + 1.0e-30), -90.0, "EQ at 0 dB nulls against the input");
        }

        beginTest ("single-module processor uses the fresh (true dry/wet) mix law: delay at 100 % has no dry");
        {
            auto proc = refhost::makeSingleModule (dsp::ModuleId::delay, 0);
            expect (proc->getMixLaws().lawOf (dsp::ModuleId::delay, 0) == dsp::MixLaw::trueDryWet);
            for (auto [id, v] : { std::pair<const char*, const char*> { pid::delayMix, "100" }, { pid::delayFeedback, "0" },
                                  { pid::delayTime, "200" }, { pid::delaySync, "0" } })
            {
                juce::String err;
                expect (refhost::setParameterText (*refhost::findParameter (*proc, id), v, err), err);
            }
            juce::AudioBuffer<float> in (2, 48000);
            in.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 12000; i < 12480; ++i)
                    in.setSample (ch, i, 0.5f * (float) std::sin (0.2 * i));   // burst after the start-up
            refhost::RenderStats st;
            const auto out = refhost::renderOffline (*proc, in, fs, 512, 0, st);
            expectLessThan (out.getMagnitude (0, 12000, 480), 1.0e-3f, "no dry during the burst");
        }
    }
};

static RefHostTests refHostTests;
