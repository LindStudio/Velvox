#include "ParameterLayout.h"

namespace velvox
{
    using T = ParameterSpec::Type;

    const std::vector<ParameterSpec>& getParameterSpecs()
    {
        static const std::vector<ParameterSpec> specs
        {
            // ---------------- Global ----------------
            { pid::inputGain,  "Input Gain",  "Global", T::Float, -24.0f, 24.0f, 0.0f, 0.0f, "dB", {}, "Gain applied before the processing chain." },
            { pid::outputGain, "Output Gain", "Global", T::Float, -24.0f, 24.0f, 0.0f, 0.0f, "dB", {}, "Gain applied after the processing chain." },
            { pid::bypass,     "Bypass",      "Global", T::Bool, 0, 1, 0, 0, "", {}, "Bypasses the whole chain (latency compensated)." },
            { pid::safetyClip, "Safety Clip", "Global", T::Bool, 0, 1, 1, 0, "", {}, "Soft clipper above -1 dBFS that keeps the output below 0 dBFS." },

            // ---------------- EQ ----------------
            { pid::eqOn,     "EQ On",       "EQ", T::Bool, 0, 1, 1, 0, "", {}, "Enables the equalizer." },
            { pid::eqMix,    "EQ Mix",      "EQ", T::Float, 0.0f, 100.0f, 100.0f, 0.0f, "%", {}, "Dry/wet of the equalizer." },
            { pid::hpfOn,    "Low Cut On",  "EQ", T::Bool, 0, 1, 1, 0, "", {}, "Enables the high-pass (low cut) filter." },
            { pid::hpfFreq,  "Low Cut",     "EQ", T::Float, 20.0f, 600.0f, 80.0f, 120.0f, "Hz", {}, "High-pass cutoff frequency." },
            { pid::hpfSlope, "Low Cut Slope","EQ", T::Choice, 0, 1, 1, 0, "", { "12 dB/oct", "24 dB/oct" }, "High-pass steepness." },
            { pid::lowFreq,  "Low Freq",    "EQ", T::Float, 40.0f, 600.0f, 150.0f, 160.0f, "Hz", {}, "Low shelf corner frequency." },
            { pid::lowGain,  "Low Gain",    "EQ", T::Float, -18.0f, 18.0f, 0.0f, 0.0f, "dB", {}, "Low shelf gain (body / warmth)." },
            { pid::midFreq,  "Mid Freq",    "EQ", T::Float, 200.0f, 8000.0f, 1500.0f, 1200.0f, "Hz", {}, "Mid bell centre frequency." },
            { pid::midGain,  "Mid Gain",    "EQ", T::Float, -18.0f, 18.0f, 0.0f, 0.0f, "dB", {}, "Mid bell gain (boxiness / presence)." },
            { pid::midQ,     "Mid Q",       "EQ", T::Float, 0.3f, 8.0f, 1.0f, 1.2f, "", {}, "Mid bell bandwidth; higher = narrower." },
            { pid::highFreq, "High Freq",   "EQ", T::Float, 2000.0f, 18000.0f, 10000.0f, 7000.0f, "Hz", {}, "High shelf corner frequency." },
            { pid::highGain, "High Gain",   "EQ", T::Float, -18.0f, 18.0f, 0.0f, 0.0f, "dB", {}, "High shelf gain (air / brightness)." },

            // ---------------- Compressor ----------------
            { pid::compOn,        "Comp On",        "Compressor", T::Bool, 0, 1, 1, 0, "", {}, "Enables the compressor." },
            { pid::compThreshold, "Comp Threshold", "Compressor", T::Float, -60.0f, 0.0f, -18.0f, 0.0f, "dB", {}, "Level above which gain reduction starts." },
            { pid::compRatio,     "Comp Ratio",     "Compressor", T::Float, 1.0f, 20.0f, 3.0f, 4.0f, ":1", {}, "Amount of compression above the threshold." },
            { pid::compAttack,    "Comp Attack",    "Compressor", T::Float, 0.1f, 100.0f, 10.0f, 10.0f, "ms", {}, "How fast gain reduction engages." },
            { pid::compRelease,   "Comp Release",   "Compressor", T::Float, 10.0f, 1000.0f, 120.0f, 150.0f, "ms", {}, "How fast gain reduction recovers." },
            { pid::compKnee,      "Comp Knee",      "Compressor", T::Float, 0.0f, 18.0f, 6.0f, 0.0f, "dB", {}, "Width of the soft knee around the threshold." },
            { pid::compMakeup,    "Comp Makeup",    "Compressor", T::Float, -12.0f, 24.0f, 0.0f, 0.0f, "dB", {}, "Gain added after compression." },
            { pid::compMix,       "Comp Mix",       "Compressor", T::Float, 0.0f, 100.0f, 100.0f, 0.0f, "%", {}, "Dry/wet for parallel compression." },

            // ---------------- De-esser ----------------
            { pid::deessOn,        "De-Ess On",        "De-Esser", T::Bool, 0, 1, 1, 0, "", {}, "Enables the de-esser." },
            { pid::deessFreq,      "De-Ess Freq",      "De-Esser", T::Float, 2000.0f, 12000.0f, 6500.0f, 5500.0f, "Hz", {}, "Lower edge of the sibilance band that gets reduced." },
            { pid::deessThreshold, "De-Ess Threshold", "De-Esser", T::Float, -60.0f, 0.0f, -30.0f, 0.0f, "dB", {}, "Sibilance level at which reduction starts." },
            { pid::deessRange,     "De-Ess Range",     "De-Esser", T::Float, 0.0f, 24.0f, 6.0f, 0.0f, "dB", {}, "Maximum reduction of the sibilance band (intensity)." },
            { pid::deessListen,    "De-Ess Listen",    "De-Esser", T::Bool, 0, 1, 0, 0, "", {}, "Solo the detected sibilance band for tuning." },

            // ---------------- Saturation ----------------
            { pid::satOn,     "Sat On",     "Saturation", T::Bool, 0, 1, 0, 0, "", {}, "Enables the saturation stage." },
            { pid::satDrive,  "Sat Drive",  "Saturation", T::Float, 0.0f, 24.0f, 6.0f, 0.0f, "dB", {}, "Amount of harmonic saturation (level compensated)." },
            { pid::satOutput, "Sat Output", "Saturation", T::Float, -24.0f, 6.0f, 0.0f, 0.0f, "dB", {}, "Output trim of the saturation stage." },
            { pid::satMix,    "Sat Mix",    "Saturation", T::Float, 0.0f, 100.0f, 100.0f, 0.0f, "%", {}, "Dry/wet of the saturation stage." },

            // ---------------- Delay ----------------
            { pid::delayOn,       "Delay On",       "Delay", T::Bool, 0, 1, 0, 0, "", {}, "Enables the delay (parallel send)." },
            { pid::delayTime,     "Delay Time",     "Delay", T::Float, 1.0f, 2000.0f, 375.0f, 300.0f, "ms", {}, "Delay time when not tempo synced." },
            { pid::delaySync,     "Delay Sync",     "Delay", T::Bool, 0, 1, 0, 0, "", {}, "Sync delay time to the host tempo." },
            { pid::delayDivision, "Delay Division", "Delay", T::Choice, 0, 13, 9, 0, "", {}, "Note value used when synced." },
            { pid::delayFeedback, "Delay Feedback", "Delay", T::Float, 0.0f, 90.0f, 30.0f, 0.0f, "%", {}, "Amount of repeats (limited for stability)." },
            { pid::delayMix,      "Delay Mix",      "Delay", T::Float, 0.0f, 100.0f, 20.0f, 0.0f, "%", {}, "Level of the delay return added to the dry signal." },
            { pid::delayLowCut,   "Delay Low Cut",  "Delay", T::Float, 20.0f, 2000.0f, 300.0f, 300.0f, "Hz", {}, "High-pass on the repeats." },
            { pid::delayHighCut,  "Delay High Cut", "Delay", T::Float, 1000.0f, 20000.0f, 6000.0f, 5000.0f, "Hz", {}, "Low-pass on the repeats." },
            { pid::delayPingPong, "Delay Ping-Pong","Delay", T::Bool, 0, 1, 0, 0, "", {}, "Alternate repeats between left and right." },

            // ---------------- Reverb ----------------
            { pid::reverbOn,       "Reverb On",        "Reverb", T::Bool, 0, 1, 0, 0, "", {}, "Enables the reverb (parallel send)." },
            { pid::reverbType,     "Reverb Type",      "Reverb", T::Choice, 0, 1, 1, 0, "", { "Room", "Plate" }, "Room = natural, sparser; Plate = dense, bright." },
            { pid::reverbMix,      "Reverb Mix",       "Reverb", T::Float, 0.0f, 100.0f, 18.0f, 0.0f, "%", {}, "Level of the reverb return added to the dry signal." },
            { pid::reverbDecay,    "Reverb Decay",     "Reverb", T::Float, 0.2f, 10.0f, 1.8f, 2.0f, "s", {}, "Reverb time (RT60)." },
            { pid::reverbPreDelay, "Reverb Pre-Delay", "Reverb", T::Float, 0.0f, 250.0f, 20.0f, 60.0f, "ms", {}, "Gap before the reverb starts." },
            { pid::reverbSize,     "Reverb Size",      "Reverb", T::Float, 0.0f, 100.0f, 60.0f, 0.0f, "%", {}, "Virtual room / plate size." },
            { pid::reverbDamping,  "Reverb Tone",      "Reverb", T::Float, 1000.0f, 20000.0f, 7000.0f, 6000.0f, "Hz", {}, "High-frequency damping inside the tail." },
            { pid::reverbLowCut,   "Reverb Low Cut",   "Reverb", T::Float, 20.0f, 1000.0f, 200.0f, 200.0f, "Hz", {}, "High-pass on the reverb input." },
            { pid::reverbWidth,    "Reverb Width",     "Reverb", T::Float, 0.0f, 100.0f, 100.0f, 0.0f, "%", {}, "Stereo width of the reverb return." },

            // ---------------- Routing (0.2) ----------------
            { pid::delayToReverb,  "Delay to Reverb",  "Routing", T::Float, 0.0f, 100.0f, 0.0f, 0.0f, "%", {}, "Sends the delay echoes into the reverb (when both run as sends)." },

            // ---------------- Stereo (0.2) ----------------
            { pid::stereoWidth, "Stereo Width", "Stereo", T::Float, 0.0f, 200.0f, 100.0f, 100.0f, "%", {},
              "Width of the whole output. Only the side signal changes, so the centre vocal and the mono sum stay intact." },
            { pid::monoBelow,   "Mono Below",   "Stereo", T::Float, 20.0f, 500.0f, 20.0f, 120.0f, "Hz", {},
              "Keeps everything below this frequency in mono (20 Hz = off)." },

            // ---------------- Level matching (0.2) ----------------
            { pid::levelMatchMode,   "Level Match",        "Output", T::Choice, 0, 2, 0, 0, "", { "Off", "Bypass match", "Auto gain" },
              "Bypass match: the bypass plays as loud as the processed sound (fair A/B). Auto gain: the processed sound is kept as loud as the input." },
            { pid::levelMatchWindow, "Level Match Window", "Output", T::Choice, 0, 2, 1, 0, "", { "Fast (0.4 s)", "Medium (3 s)", "Slow (10 s)" },
              "How long the loudness is averaged before matching." },
            { pid::levelMatchRange,  "Level Match Range",  "Output", T::Float, 0.0f, 24.0f, 12.0f, 0.0f, "dB", {},
              "Maximum correction the level matcher may apply." },

            // ---------------- Doubler (0.2) ----------------
            { pid::doublerOn,       "Doubler On",        "Doubler", T::Bool, 0, 1, 0, 0, "", {}, "Enables the vocal doubler." },
            { pid::doublerMix,      "Doubler Mix",       "Doubler", T::Float, 0.0f, 100.0f, 50.0f, 0.0f, "%", {}, "Level of the doubled voices (the original stays at full level)." },
            { pid::doublerWidth,    "Doubler Width",     "Doubler", T::Float, 0.0f, 100.0f, 80.0f, 0.0f, "%", {}, "How far left and right the doubled voices are placed." },
            { pid::doublerDelay,    "Doubler Delay",     "Doubler", T::Float, 5.0f, 40.0f, 14.0f, 15.0f, "ms", {}, "Timing offset of the doubled voices - longer feels looser." },
            { pid::doublerDetune,   "Doubler Detune",    "Doubler", T::Float, 0.0f, 25.0f, 8.0f, 0.0f, "ct", {}, "Pitch variation of the doubled voices in cents." },
            { pid::doublerMonoSafe, "Doubler Mono Safe", "Doubler", T::Float, 0.0f, 100.0f, 50.0f, 0.0f, "%", {},
              "Keeps the doubled voices out of the mono sum. 100 % = mono playback sounds exactly like the original." },
            { pid::doublerTone,     "Doubler Tone",      "Doubler", T::Float, 1000.0f, 20000.0f, 9000.0f, 6000.0f, "Hz", {}, "High cut on the doubled voices (darker = further back)." },
        };

        return specs;
    }

    const ParameterSpec* findParameterSpec (const juce::String& id)
    {
        for (auto& s : getParameterSpecs())
            if (id == s.id)
                return &s;
        return nullptr;
    }

    namespace delaydiv
    {
        const juce::StringArray& names()
        {
            static const juce::StringArray n { "1/64", "1/32", "1/16T", "1/16", "1/16D",
                                               "1/8T", "1/8", "1/8D", "1/4T", "1/4",
                                               "1/4D", "1/2", "1/2D", "1/1" };
            return n;
        }

        double beatsForIndex (int index)
        {
            static const double beats[] { 1.0 / 16.0, 1.0 / 8.0, 1.0 / 6.0, 1.0 / 4.0, 3.0 / 8.0,
                                          1.0 / 3.0, 1.0 / 2.0, 3.0 / 4.0, 2.0 / 3.0, 1.0,
                                          1.5, 2.0, 3.0, 4.0 };
            return beats[juce::jlimit (0, (int) std::size (beats) - 1, index)];
        }
    }

    juce::String formatValue (const ParameterSpec& spec, float value)
    {
        const juce::String unit (spec.unit);

        if (juce::String (spec.id) == pid::monoBelow && value <= 20.5f)
            return "Off";

        if (unit == "Hz")
            return value >= 1000.0f ? juce::String (value / 1000.0f, value >= 10000.0f ? 1 : 2) + " kHz"
                                    : juce::String (juce::roundToInt (value)) + " Hz";
        if (unit == "dB")
        {
            const auto rounded = std::abs (value) < 0.05f ? 0.0f : value;
            return (rounded > 0.0f ? "+" : "") + juce::String (rounded, 1) + " dB";
        }
        if (unit == "ms")
            return value < 10.0f ? juce::String (value, 1) + " ms" : juce::String (juce::roundToInt (value)) + " ms";
        if (unit == "%")
            return juce::String (juce::roundToInt (value)) + " %";
        if (unit == "s")
            return juce::String (value, 2) + " s";
        if (unit == "ct")
            return juce::String (value, 1) + " ct";
        if (unit == ":1")
            return juce::String (value, value < 10.0f ? 1 : 0) + ":1";

        return juce::String (value, 2);
    }

    static juce::NormalisableRange<float> makeRange (const ParameterSpec& s)
    {
        juce::NormalisableRange<float> r (s.minValue, s.maxValue);
        if (s.skewCentre > s.minValue && s.skewCentre < s.maxValue)
            r.setSkewForCentre (s.skewCentre);
        return r;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        for (const auto& s : getParameterSpecs())
        {
            const juce::ParameterID id { s.id, parameterVersion };

            switch (s.type)
            {
                case T::Float:
                {
                    const auto* specPtr = &s;
                    auto attributes = juce::AudioParameterFloatAttributes()
                                          .withLabel (s.unit)
                                          .withStringFromValueFunction ([specPtr] (float v, int) { return formatValue (*specPtr, v); })
                                          .withValueFromStringFunction ([] (const juce::String& t) { return t.retainCharacters ("-0123456789.").getFloatValue()
                                                                                                          * (t.containsIgnoreCase ("k") ? 1000.0f : 1.0f); });
                    layout.add (std::make_unique<juce::AudioParameterFloat> (id, s.name, makeRange (s), s.defaultValue, attributes));
                    break;
                }

                case T::Bool:
                    layout.add (std::make_unique<juce::AudioParameterBool> (id, s.name, s.defaultValue > 0.5f));
                    break;

                case T::Choice:
                {
                    const auto choices = juce::String (s.id) == pid::delayDivision ? delaydiv::names() : s.choices;
                    layout.add (std::make_unique<juce::AudioParameterChoice> (id, s.name, choices, (int) s.defaultValue));
                    break;
                }
            }
        }

        return layout;
    }

    juce::var createParameterSchema()
    {
        juce::Array<juce::var> list;

        for (const auto& s : getParameterSpecs())
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("id", s.id);
            obj->setProperty ("name", s.name);
            obj->setProperty ("module", s.module);
            obj->setProperty ("type", s.type == T::Float ? "float" : s.type == T::Bool ? "bool" : "choice");
            obj->setProperty ("description", s.description);

            if (s.type == T::Float)
            {
                obj->setProperty ("min", s.minValue);
                obj->setProperty ("max", s.maxValue);
                obj->setProperty ("unit", s.unit);
            }

            if (s.type == T::Choice)
            {
                const auto choices = juce::String (s.id) == pid::delayDivision ? delaydiv::names() : s.choices;
                juce::Array<juce::var> c;
                for (auto& ch : choices)
                    c.add (ch);
                obj->setProperty ("choices", c);
            }

            obj->setProperty ("default", s.defaultValue);
            list.add (juce::var (obj));
        }

        return juce::var (list);
    }
}
