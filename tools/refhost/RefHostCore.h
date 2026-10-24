#pragma once

// Reference host core: offline rendering through any juce::AudioProcessor (a hosted VST3 or the
// in-process Headliner processor) and parameter access by index / ID / name / text.
// Nothing here is used by the plugin. Tested in Tests/RefHostTests.cpp.

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <limits>
#include <optional>

namespace velvox::refhost
{
    struct RenderStats
    {
        int latency = 0;
        int blocks = 0;
        double meanBlockMicros = 0.0;
        double maxBlockMicros = 0.0;
    };

    // A steady transport (some plugins need a play head for tempo-synced features)
    class SteadyPlayHead final : public juce::AudioPlayHead
    {
    public:
        double bpm = 120.0;
        double fs = 48000.0;
        juce::int64 samplePos = 0;

        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm (bpm);
            p.setTimeSignature (TimeSignature { 4, 4 });
            p.setTimeInSamples (samplePos);
            p.setTimeInSeconds ((double) samplePos / fs);
            const double ppq = (double) samplePos / fs * bpm / 60.0;
            p.setPpqPosition (ppq);
            p.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
            p.setIsPlaying (true);
            return p;
        }
    };

    inline juce::String parameterId (juce::AudioProcessorParameter& p)
    {
        if (auto* h = dynamic_cast<juce::HostedAudioProcessorParameter*> (&p))
            return h->getParameterID();
        if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (&p))
            return w->paramID;
        return {};
    }

    // key = parameter index, parameter ID, name (case-insensitive), or a unique name prefix
    inline juce::AudioProcessorParameter* findParameter (juce::AudioProcessor& proc, const juce::String& key)
    {
        const auto& params = proc.getParameters();
        const auto k = key.trim();
        if (k.isEmpty())
            return nullptr;

        if (k.containsOnly ("0123456789"))
        {
            const int idx = k.getIntValue();
            return juce::isPositiveAndBelow (idx, params.size()) ? params[idx] : nullptr;
        }
        for (auto* p : params)
            if (parameterId (*p) == k)
                return p;
        for (auto* p : params)
            if (p->getName (256).trim().equalsIgnoreCase (k))
                return p;
        for (auto* p : params)
            if (parameterId (*p).equalsIgnoreCase (k))
                return p;

        juce::AudioProcessorParameter* found = nullptr;
        for (auto* p : params)
            if (p->getName (256).trim().startsWithIgnoreCase (k))
            {
                if (found != nullptr)
                    return nullptr;   // ambiguous
                found = p;
            }
        return found;
    }

    inline juce::String describeParameters (juce::AudioProcessor& proc)
    {
        juce::String s;
        const auto& params = proc.getParameters();
        for (int i = 0; i < params.size(); ++i)
        {
            auto* p = params[i];
            const float v = p->getValue();
            auto text = p->getText (v, 64).trim();
            if (p->getLabel().isNotEmpty() && ! text.endsWithIgnoreCase (p->getLabel().trim()))
                text << " " << p->getLabel().trim();
            s << juce::String (i).paddedLeft (' ', 4) << "  " << parameterId (*p).paddedRight (' ', 22) << "  "
              << p->getName (64).paddedRight (' ', 30) << "  " << text.paddedRight (' ', 18)
              << "  n=" << juce::String (v, 4) << "  steps=" << p->getNumSteps() << (p->isDiscrete() ? " discrete" : "");
            const int steps = p->getNumSteps();
            if ((p->isDiscrete() || p->isBoolean()) && steps >= 2 && steps <= 64)
            {
                juce::StringArray names;
                for (int k = 0; k < steps; ++k)
                    names.add (p->getText ((float) k / (float) (steps - 1), 64).trim());
                s << " [" << names.joinIntoString ("|") << "]";
            }
            s << "\n";
        }
        return s;
    }

    namespace detail
    {
        struct Number
        {
            double raw = 0.0;      // the number as written
            double scaled = 0.0;   // with unit multipliers (k, ms, s) applied
        };

        // "2.5k" -> 2500, "1.20 kHz" -> 1200, "50 ms" -> 0.05 (scaled) / 50 (raw), "-inf" -> -inf
        inline std::optional<Number> parseNumber (const juce::String& text)
        {
            auto t = text.trim().toLowerCase();
            if (t.containsChar (',') && ! t.containsChar ('.'))
                t = t.replaceCharacter (',', '.');   // German-locale decimals
            // "2k5" (soothe style) -> "2.5k"
            for (int i = 1; i + 1 < t.length(); ++i)
                if (t[i] == 'k' && juce::CharacterFunctions::isDigit (t[i - 1]) && juce::CharacterFunctions::isDigit (t[i + 1]))
                {
                    int e = i + 1;
                    while (e < t.length() && juce::CharacterFunctions::isDigit (t[e]))
                        ++e;
                    t = t.substring (0, i) + "." + t.substring (i + 1, e) + "k" + t.substring (e);
                    break;
                }
            if (t.startsWith ("-inf") || t.startsWith ("-∞"))
                return Number { -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() };
            int i = 0;
            const int n = t.length();
            while (i < n && ! (juce::CharacterFunctions::isDigit (t[i]) || t[i] == '-' || t[i] == '+' || t[i] == '.'))
                ++i;
            int j = i;
            if (j < n && (t[j] == '-' || t[j] == '+'))
                ++j;
            bool digits = false;
            while (j < n && (juce::CharacterFunctions::isDigit (t[j]) || t[j] == '.'))
            {
                digits = digits || juce::CharacterFunctions::isDigit (t[j]);
                ++j;
            }
            if (! digits)
                return std::nullopt;
            Number num;
            num.raw = t.substring (i, j).getDoubleValue();
            auto unit = t.substring (j).trim();
            double mul = 1.0;
            if (unit.startsWith ("ms"))
                mul = 1.0e-3;
            else if (unit.startsWith ("khz") || unit.startsWith ("k"))
                mul = 1.0e3;
            else if (unit == "s" || unit.startsWith ("sec") || unit.startsWith ("s "))
                mul = 1.0;
            num.scaled = num.raw * mul;
            if (mul == 1.0e3)
                num.raw = num.scaled;   // "2.5k" means 2500 also when written without unit
            return num;
        }

        inline double mismatch (const Number& target, const Number& got)
        {
            auto rel = [] (double a, double b)
            {
                if (std::isinf (a) || std::isinf (b))
                    return (a == b) ? 0.0 : 1.0e9;
                return std::abs (a - b) / std::max (std::abs (a), 1.0e-2);
            };
            return std::min (rel (target.scaled, got.scaled), rel (target.raw, got.raw));
        }

        // probe = true for plugins whose value-to-text ignores the value (shows the current one): set, then read
        inline double mismatchAt (juce::AudioProcessorParameter& p, const Number& target, float v, bool probe = false)
        {
            if (probe)
                p.setValue (v);
            if (auto got = parseNumber (p.getText (v, 64)))
                return mismatch (target, *got);
            return 1.0e12;
        }
    }

    // Sets a parameter from text in the plugin's display units. Choices by name; numbers via the
    // plugin's own parser, verified by reading the text back, else by searching the normalized range.
    inline bool setParameterText (juce::AudioProcessorParameter& p, const juce::String& text, juce::String& error)
    {
        using namespace detail;
        const int steps = p.getNumSteps();
        const bool discrete = p.isDiscrete() || p.isBoolean() || (steps > 1 && steps <= 64);

        auto apply = [&p] (float v)
        {
            p.beginChangeGesture();
            p.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
            p.endChangeGesture();
        };

        // Choice by name
        if (discrete && steps > 1 && steps <= 4096)
        {
            for (int i = 0; i < steps; ++i)
            {
                const float v = (float) i / (float) (steps - 1);
                if (p.getText (v, 128).trim().equalsIgnoreCase (text.trim()))
                {
                    apply (v);
                    return true;
                }
            }
        }

        const auto target = parseNumber (text);
        if (! target)
        {
            // a continuous parameter that displays words ("yes" / "no", "Freq-shift"): scan its range
            const bool probeWords = ! discrete && p.getText (0.0f, 128) == p.getText (1.0f, 128) && p.getText (0.0f, 128) == p.getText (0.5f, 128);
            auto textAt = [&p, probeWords] (float v)
            {
                if (probeWords)
                    p.setValue (v);
                return p.getText (v, 128).trim();
            };
            for (int i = 0; i <= 400; ++i)
            {
                const float v = (float) i / 400.0f;
                if (textAt (v).equalsIgnoreCase (text.trim()))
                {
                    // centre of the run of values that show this text
                    int j = i;
                    while (j < 400 && textAt ((float) (j + 1) / 400.0f).equalsIgnoreCase (text.trim()))
                        ++j;
                    apply (0.5f * (v + (float) j / 400.0f));
                    return true;
                }
            }
            error = "'" + text + "' is no choice of " + p.getName (64) + " and no number";
            return false;
        }

        constexpr double tolerance = 0.005;
        const bool probe = ! discrete && p.getText (0.0f, 64) == p.getText (1.0f, 64) && p.getText (0.0f, 64) == p.getText (0.5f, 64);

        // 1. the plugin's own parser
        {
            const float v = p.getValueForText (text);
            if (std::isfinite (v) && mismatchAt (p, *target, v, probe) <= tolerance)
            {
                apply (v);
                return true;
            }
        }

        // 2. grid search over the normalized range, then local refinement
        constexpr int grid = 400;
        int best = -1;
        double bestCost = 1.0e300;
        for (int i = 0; i <= grid; ++i)
        {
            const double c = mismatchAt (p, *target, (float) i / grid, probe);
            if (c < bestCost)
            {
                bestCost = c;
                best = i;
            }
        }
        if ((best < 0 || bestCost >= 1.0e11) && discrete && steps > 1)
        {
            // a choice / switch whose texts are not numbers: the number is the step index
            const int idx = juce::jlimit (0, steps - 1, (int) std::lround (target->raw));
            apply ((float) idx / (float) (steps - 1));
            return true;
        }
        if (best < 0 || bestCost >= 1.0e11)
        {
            error = "cannot read numbers from " + p.getName (64) + " (text '" + p.getText (0.5f, 64) + "')";
            return false;
        }
        float lo = (float) std::max (0, best - 1) / grid, hi = (float) std::min (grid, best + 1) / grid;
        float bestV = (float) best / grid;
        if (! discrete)
        {
            for (int it = 0; it < 60; ++it)
            {
                const float m1 = lo + (hi - lo) / 3.0f, m2 = hi - (hi - lo) / 3.0f;
                if (mismatchAt (p, *target, m1, probe) <= mismatchAt (p, *target, m2, probe))
                    hi = m2;
                else
                    lo = m1;
            }
            const float v = 0.5f * (lo + hi);
            if (mismatchAt (p, *target, v, probe) < bestCost)
                bestV = v;
        }
        const double cost = mismatchAt (p, *target, bestV, probe);
        if (cost > 0.02)
        {
            error = p.getName (64) + ": closest reachable value to '" + text + "' is '" + p.getText (bestV, 64) + "'";
            return false;
        }
        apply (bestV);
        return true;
    }

    // Main bus stereo in/out where the processor allows it, other buses (sidechain) off
    inline void preferStereo (juce::AudioProcessor& proc)
    {
        auto layout = proc.getBusesLayout();
        for (int i = 0; i < layout.inputBuses.size(); ++i)
            layout.inputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
        for (int i = 0; i < layout.outputBuses.size(); ++i)
            layout.outputBuses.getReference (i) = i == 0 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
        if (! proc.setBusesLayout (layout))
        {
            // keep sidechains as they are, only ask for stereo main buses
            auto l2 = proc.getBusesLayout();
            if (! l2.inputBuses.isEmpty())
                l2.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
            if (! l2.outputBuses.isEmpty())
                l2.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
            proc.setBusesLayout (l2);
        }
    }

    // Renders `in` (mono is fed to both main inputs) through the processor in blocks of `block`.
    // The reported latency is compensated: the result starts at the output sample that belongs to
    // input sample 0 and is in.getNumSamples() + tailSamples long, with the main output's channels.
    inline juce::AudioBuffer<float> renderOffline (juce::AudioProcessor& proc, const juce::AudioBuffer<float>& in, double fs,
                                                   int block, int tailSamples, RenderStats& stats, double bpm = 120.0)
    {
        preferStereo (proc);
        SteadyPlayHead head;
        head.bpm = bpm;
        head.fs = fs;
        proc.setPlayHead (&head);
        proc.setNonRealtime (true);
        proc.setRateAndBufferSizeDetails (fs, block);
        proc.prepareToPlay (fs, block);

        stats = {};
        stats.latency = std::max (0, proc.getLatencySamples());

        const int mainIn = proc.getBusCount (true) > 0 ? proc.getChannelCountOfBus (true, 0) : 0;
        const int mainOut = proc.getBusCount (false) > 0 ? proc.getChannelCountOfBus (false, 0) : 2;
        const int bufferChannels = std::max ({ proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels(), 1 });
        const int n = in.getNumSamples();
        const int total = n + stats.latency + tailSamples;

        juce::AudioBuffer<float> out (std::max (1, mainOut), n + tailSamples);
        out.clear();
        juce::AudioBuffer<float> buf (bufferChannels, block);
        juce::MidiBuffer midi;

        double sumMicros = 0.0;
        for (int start = 0; start < total; start += block)
        {
            const int len = std::min (block, total - start);
            buf.setSize (bufferChannels, len, false, false, true);
            buf.clear();
            for (int ch = 0; ch < std::min (mainIn, bufferChannels); ++ch)
            {
                const int src = std::min (ch, in.getNumChannels() - 1);
                const int count = std::max (0, std::min (len, n - start));
                if (count > 0)
                    buf.copyFrom (ch, 0, in, src, start, count);
            }
            midi.clear();
            head.samplePos = start;

            const auto t0 = juce::Time::getHighResolutionTicks();
            proc.processBlock (buf, midi);
            const double us = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1.0e6;
            sumMicros += us;
            stats.maxBlockMicros = std::max (stats.maxBlockMicros, us);
            ++stats.blocks;

            // keep output samples [latency, latency + n + tail)
            const int from = std::max (start, stats.latency);
            const int to = start + len;
            if (to > from)
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    out.copyFrom (ch, from - stats.latency, buf, std::min (ch, bufferChannels - 1), from - start, to - from);
        }
        stats.meanBlockMicros = stats.blocks > 0 ? sumMicros / stats.blocks : 0.0;
        proc.releaseResources();
        proc.setPlayHead (nullptr);
        return out;
    }
}
