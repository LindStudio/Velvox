#include "ParameterLayout.h"
#include "../DSP/ChopModule.h"
#include "../DSP/PulseModule.h"
#include "../DSP/Instances.h"
#include "../DSP/EqDesign.h"
#include <deque>
#include <unordered_map>

namespace velvox
{
    using T = ParameterSpec::Type;

    std::optional<dsp::ModuleId> moduleOfParameter (const ParameterSpec& s)
    {
        using M = dsp::ModuleId;
        const juce::String module (s.module), id (s.id);
        if (module == "EQ")         return M::eq;
        if (module == "Compressor") return M::comp;
        if (module == "De-Esser")   return M::deess;
        if (module == "Saturation") return M::sat;
        if (module == "Delay")      return M::delay;
        if (module == "Reverb")     return M::reverb;
        if (module == "Doubler")    return M::doubler;
        if (module == "Smash")      return M::smash;
        if (module == "Choir")      return M::choir;
        if (module == "Smooth")     return M::smooth;
        if (module == "Ride")       return M::ride;
        if (module == "Pitch")      return M::pitch;
        if (module == "Chop")       return M::chop;
        if (module == "Pulse")      return M::pulse;
        if (module == "Check")      return M::check;
        if (module == "Ducking")
        {
            if (id.startsWith ("delayDuck"))  return M::delay;
            if (id.startsWith ("reverbDuck")) return M::reverb;
        }
        return std::nullopt;
    }

    namespace
    {
        // 0.6 EQ bands (instance 1; the banks of instances 2..4 follow from appendInstanceBanks) and the
        // 0.5 fixed-band IDs, which stay registered for old projects but are hidden and not automatable
        void appendEqBandSpecs (std::vector<ParameterSpec>& specs)
        {
            static std::deque<std::string> strings;
            const auto keep = [] (const juce::String& t) { strings.push_back (t.toStdString()); return strings.back().c_str(); };

            static const juce::StringArray types { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt" };
            static const juce::StringArray slopes { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
            static const juce::StringArray roles { "None", "Presence", "Warmth", "Air", "Low Cut", "Mud", "Auto", "High Cut" };

            for (int n = 1; n <= dsp::maxEqBands; ++n)
            {
                const juce::String band = "EQ Band " + juce::String (n) + " ";
                const auto id = [&] (const char* field) { return keep (pid::eqBandId (n, field)); };
                specs.push_back ({ id (pid::eqband::on), keep (band + "On"), "EQ", T::Bool, 0, 1, 0, 0, "", {}, "Switches the band on." });
                specs.push_back ({ id (pid::eqband::type), keep (band + "Type"), "EQ", T::Choice, 0, (float) types.size() - 1, 0, 0, "", types,
                                   "Bell, shelves, cuts, notch, band pass or tilt." });
                specs.push_back ({ id (pid::eqband::freq), keep (band + "Freq"), "EQ", T::Float, dsp::eqdesign::minFreq, dsp::eqdesign::maxFreq, 1000.0f, 1000.0f, "Hz", {},
                                   "Centre / corner frequency of the band." });
                specs.push_back ({ id (pid::eqband::gain), keep (band + "Gain"), "EQ", T::Float, -dsp::eqdesign::maxGainDb, dsp::eqdesign::maxGainDb, 0.0f, 0.0f, "dB", {},
                                   "Boost or cut of the band (tilt: from the lows to the highs)." });
                specs.push_back ({ id (pid::eqband::q), keep (band + "Q"), "EQ", T::Float, dsp::eqdesign::minQ, dsp::eqdesign::maxQ, 1.0f, 1.0f, "", {},
                                   "Bandwidth; higher = narrower." });
                specs.push_back ({ id (pid::eqband::slope), keep (band + "Slope"), "EQ", T::Choice, 0, (float) slopes.size() - 1, 3, 0, "", slopes,
                                   "Steepness of a low or high cut." });
                ParameterSpec role { id (pid::eqband::role), keep (band + "Role"), "EQ", T::Choice, 0, (float) roles.size() - 1, 0, 0, "", roles,
                                     "Internal: which feature owns the band (macros, automatic mix, Vocal Check); user bands have none." };
                role.automatable = false;
                role.hidden = true;
                specs.push_back (role);
            }

            ParameterSpec legacyFlag { pid::eqLegacyResponse, "EQ Legacy Response", "EQ", T::Bool, 0, 1, 0, 0, "", {},
                                       "Internal: the bands use the response of the 0.5 EQ (set when an older project is loaded)." };
            legacyFlag.automatable = false;
            legacyFlag.hidden = true;
            specs.push_back (legacyFlag);

            for (auto& s : specs)
            {
                const juce::String sid (s.id);
                for (auto* legacyId : { pid::hpfOn, pid::hpfFreq, pid::hpfSlope, pid::lowFreq, pid::lowGain, pid::midFreq, pid::midGain, pid::midQ,
                                        pid::highFreq, pid::highGain, pid::lowType, pid::lowQ, pid::highType, pid::highQ, pid::lpfOn, pid::lpfFreq, pid::lpfSlope })
                    if (sid == legacyId)
                    {
                        s.automatable = false;
                        s.hidden = true;
                    }
            }
        }

        // 0.6 Saturation multiband (spec 5.6): off by default = the single-band module of 0.2-0.5
        void appendSatMultibandSpecs (std::vector<ParameterSpec>& specs)
        {
            const juce::StringArray characters { "Warm", "Tape", "Tube", "Hard" };
            specs.push_back ({ pid::satMultiband, "Sat Multiband", "Saturation", T::Bool, 0, 1, 0, 0, "", {},
                               "Splits the voice into Low / Mid / High and saturates each band on its own." });
            specs.push_back ({ pid::satXoverLow, "Sat Crossover Low", "Saturation", T::Float, 60.0f, 1000.0f, 250.0f, 250.0f, "Hz", {},
                               "Border between the Low and the Mid band." });
            specs.push_back ({ pid::satXoverHigh, "Sat Crossover High", "Saturation", T::Float, 1000.0f, 12000.0f, 4000.0f, 4000.0f, "Hz", {},
                               "Border between the Mid and the High band (kept at least 1.5 x the low crossover)." });
            const struct { const char *on, *drive, *character, *name; int defaultCharacter; } bands[] {
                { pid::satLowOn, pid::satLowDrive, pid::satLowChar, "Low", 0 },
                { pid::satMidOn, pid::satMidDrive, pid::satMidChar, "Mid", 1 },
                { pid::satHighOn, pid::satHighDrive, pid::satHighChar, "High", 1 },
            };
            static std::deque<std::string> strings;
            const auto keep = [] (const juce::String& t) { strings.push_back (t.toStdString()); return strings.back().c_str(); };
            for (const auto& b : bands)
            {
                const juce::String n (b.name);
                specs.push_back ({ b.on, keep ("Sat " + n + " On"), "Saturation", T::Bool, 0, 1, 1, 0, "", {},
                                   keep ("Saturates the " + n.toLowerCase() + " band (off: the band passes clean).") });
                specs.push_back ({ b.drive, keep ("Sat " + n + " Drive"), "Saturation", T::Float, 0.0f, 24.0f, 6.0f, 0.0f, "dB", {},
                                   keep ("Saturation amount of the " + n.toLowerCase() + " band (level compensated).") });
                specs.push_back ({ b.character, keep ("Sat " + n + " Character"), "Saturation", T::Choice, 0, 3, (float) b.defaultCharacter, 0, "", characters,
                                   keep ("Character of the " + n.toLowerCase() + " band.") });
            }
            specs.push_back ({ pid::satLink, "Sat Link", "Saturation", T::Bool, 0, 1, 1, 0, "", {},
                               "Moving one band's drive moves all three together (their differences stay)." });
        }

        // Parameters of instances 2..n, generated from the instance-1 table
        void appendInstanceBanks (std::vector<ParameterSpec>& specs)
        {
            static std::deque<std::string> strings;   // stable storage for generated IDs / names
            const auto keep = [] (const juce::String& t) { strings.push_back (t.toStdString()); return strings.back().c_str(); };

            const auto base = specs;   // instance-1 table
            for (int instance = 1; instance <= dsp::maxExtraInstances; ++instance)
                for (const auto& spec : base)
                {
                    const auto module = moduleOfParameter (spec);
                    if (! module.has_value() || instance >= dsp::maxInstances (*module))
                        continue;

                    // "Comp Threshold" -> "Compressor 2: Threshold"
                    juce::String name (spec.name);
                    for (auto* prefix : { "EQ ", "Comp ", "De-Ess ", "Sat ", "Delay ", "Reverb ", "Doubler ", "Smash ", "Choir ", "Smooth ", "Ride " })
                        if (name.startsWith (prefix))
                        {
                            name = name.substring ((int) std::strlen (prefix));
                            break;
                        }

                    ParameterSpec copy = spec;
                    copy.id = keep (dsp::instanceParamId (spec.id, instance));
                    copy.name = keep (dsp::instanceDisplayName (*module, instance) + ": " + name);
                    // A new EQ instance starts neutral: no low cut
                    if (juce::String (spec.id) == pid::hpfOn)
                        copy.defaultValue = 0.0f;
                    specs.push_back (copy);
                }
        }
    }

    const std::vector<ParameterSpec>& getParameterSpecs()
    {
        static const std::vector<ParameterSpec> specs = []
        {
            std::vector<ParameterSpec> table
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
            { pid::reverbType,     "Reverb Type",      "Reverb", T::Choice, 0, 7, 1, 0, "",
              { "Room", "Plate", "Hall", "Large Hall", "Ambience", "Atmosphere", "Cinematic", "Ethereal" },
              "Character of the space: Room = natural and close, Plate = dense and bright, Hall / Large Hall = concert spaces, "
              "Ambience = short studio air, Atmosphere = huge lush EDM space, Cinematic = dark and deep, Ethereal = bright, wide and floating." },
            { pid::reverbMix,      "Reverb Mix",       "Reverb", T::Float, 0.0f, 100.0f, 18.0f, 0.0f, "%", {}, "Level of the reverb return added to the dry signal." },
            { pid::reverbDecay,    "Reverb Decay",     "Reverb", T::Float, 0.2f, 20.0f, 1.8f, 2.5f, "s", {}, "Reverb time (RT60) in the mid range." },
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

            // ---------------- Ducking (0.2) ----------------
            { pid::duckSource, "Duck Source", "Ducking", T::Choice, 0, 1, 0, 0, "", { "Vocal (internal)", "Sidechain input" },
              "What pushes reverb and delay down: the vocal itself, or the sidechain input of the plugin (e.g. a kick)." },
            { pid::delayDuckAmount,    "Delay Duck Amount",    "Ducking", T::Float, 0.0f, 24.0f, 0.0f, 0.0f, "dB", {}, "How much the delay drops while the vocal is active (0 = off)." },
            { pid::delayDuckThreshold, "Delay Duck Threshold", "Ducking", T::Float, -60.0f, 0.0f, -30.0f, 0.0f, "dB", {}, "Vocal level where delay ducking starts." },
            { pid::delayDuckAttack,    "Delay Duck Attack",    "Ducking", T::Float, 1.0f, 200.0f, 10.0f, 20.0f, "ms", {}, "How fast the delay is pushed down." },
            { pid::delayDuckRelease,   "Delay Duck Release",   "Ducking", T::Float, 20.0f, 2000.0f, 300.0f, 300.0f, "ms", {}, "How fast the delay comes back in the gaps." },
            { pid::reverbDuckAmount,    "Reverb Duck Amount",    "Ducking", T::Float, 0.0f, 24.0f, 0.0f, 0.0f, "dB", {}, "How much the reverb drops while the vocal is active (0 = off)." },
            { pid::reverbDuckThreshold, "Reverb Duck Threshold", "Ducking", T::Float, -60.0f, 0.0f, -30.0f, 0.0f, "dB", {}, "Vocal level where reverb ducking starts." },
            { pid::reverbDuckAttack,    "Reverb Duck Attack",    "Ducking", T::Float, 1.0f, 200.0f, 20.0f, 20.0f, "ms", {}, "How fast the reverb is pushed down." },
            { pid::reverbDuckRelease,   "Reverb Duck Release",   "Ducking", T::Float, 20.0f, 2000.0f, 500.0f, 300.0f, "ms", {},               "How fast the reverb comes back in the gaps." },

            // ---------------- Pitch (0.2) ----------------
            { pid::pitchOn,        "Pitch On",       "Pitch", T::Bool, 0, 1, 0, 0, "", {},
              "Enables the creative pitch shifter. Adds about 120 ms latency while on (compensated by the DAW)." },
            { pid::pitchSemitones, "Pitch",          "Pitch", T::Float, -24.0f, 24.0f, 0.0f, 0.0f, "st", {}, "Transposition in semitones.", 1.0f },
            { pid::pitchCents,     "Pitch Fine",     "Pitch", T::Float, -100.0f, 100.0f, 0.0f, 0.0f, "ct", {}, "Fine tuning in cents." },
            { pid::pitchPreserveFormants, "Pitch Keep Formants", "Pitch", T::Bool, 0, 1, 1, 0, "", {},
              "On: the voice keeps its natural character when shifted. Off: formants move with the pitch (chipmunk / monster)." },
            { pid::pitchFormant,   "Pitch Formant",  "Pitch", T::Float, -12.0f, 12.0f, 0.0f, 0.0f, "st", {}, "Moves the vocal character (formants) up or down independently of the pitch.", 0.1f },
            { pid::pitchMix,       "Pitch Mix",      "Pitch", T::Float, 0.0f, 100.0f, 100.0f, 0.0f, "%", {}, "Blend of shifted and original voice." },

            // ---------------- Vocal chop (0.2) ----------------
            { pid::chopOn,      "Chop On",      "Chop", T::Bool, 0, 1, 0, 0, "", {}, "Enables the tempo-synced vocal chopper." },
            { pid::chopPattern, "Chop Pattern", "Chop", T::Choice, 0, (float) (dsp::ChopModule::patterns().size() - 1), 2, 0, "",
              dsp::ChopModule::patternNames(), "16-step rhythm: plays, mutes or repeats (stutters) the vocal." },
            { pid::chopRate,    "Chop Rate",    "Chop", T::Choice, 0, 3, 1, 0, "", dsp::ChopModule::rateNames(), "Length of one step, synced to the host tempo." },
            { pid::chopDepth,   "Chop Depth",   "Chop", T::Float, 0.0f, 100.0f, 100.0f, 0.0f, "%", {}, "How strongly the pattern is applied." },
            { pid::chopSmooth,  "Chop Smooth",  "Chop", T::Float, 0.5f, 30.0f, 3.0f, 5.0f, "ms", {}, "Softens the edges of each step (higher = smoother, less clicky)." },

            // ---------------- Pro extensions (0.2) ----------------
            { pid::lowType,  "Low Band Type",  "EQ", T::Choice, 0, 1, 0, 0, "", { "Shelf", "Bell" }, "Low band as shelf (whole low end) or bell (around the frequency)." },
            { pid::lowQ,     "Low Q",          "EQ", T::Float, 0.3f, 6.0f, 0.7071f, 1.2f, "", {}, "Bandwidth of the low band (shelf: slope shape)." },
            { pid::highType, "High Band Type", "EQ", T::Choice, 0, 1, 0, 0, "", { "Shelf", "Bell" }, "High band as shelf (air) or bell (presence)." },
            { pid::highQ,    "High Q",         "EQ", T::Float, 0.3f, 6.0f, 0.7071f, 1.2f, "", {}, "Bandwidth of the high band (shelf: slope shape)." },
            { pid::lpfOn,    "High Cut On",    "EQ", T::Bool, 0, 1, 0, 0, "", {}, "Enables the low-pass (high cut) filter." },
            { pid::lpfFreq,  "High Cut",       "EQ", T::Float, 1000.0f, 20000.0f, 18000.0f, 6000.0f, "Hz", {}, "Low-pass cutoff frequency (e.g. telephone or lo-fi effects)." },
            { pid::lpfSlope, "High Cut Slope", "EQ", T::Choice, 0, 1, 1, 0, "", { "12 dB/oct", "24 dB/oct" }, "High-cut steepness." },
            { pid::satCharacter, "Sat Character", "Saturation", T::Choice, 0, 3, 0, 0, "", { "Warm", "Tape", "Tube", "Hard" },
              "Warm: smooth, slightly asymmetric. Tape: gentle, symmetric. Tube: rich even harmonics. Hard: firm clipping." },
            { pid::deessMode,    "De-Ess Mode",   "De-Esser", T::Choice, 0, 1, 0, 0, "", { "Split band", "Wideband" },
              "Split band reduces only the S frequencies; wideband dips the whole voice on each S." },
            { pid::compDetector, "Comp Detector", "Compressor", T::Choice, 0, 1, 0, 0, "", { "Peak", "RMS" },
              "Peak reacts to every transient; RMS follows the average level (smoother)." },
            { pid::compScHpf,    "Comp SC Filter", "Compressor", T::Float, 20.0f, 500.0f, 20.0f, 100.0f, "Hz", {},
              "Ignores low frequencies when detecting, so plosives and bass do not trigger compression (20 Hz = off)." },

            // ---------------- Macros (0.2) ----------------
            { pid::macroPresence,   "Presence",   "Macros", T::Float, 0.0f, 100.0f, 0.0f, 0.0f, "%", {},
              "Brings the vocal forward: brighter top end, sibilance kept in check." },
            { pid::macroWarmth,     "Warmth",     "Macros", T::Float, 0.0f, 100.0f, 0.0f, 0.0f, "%", {},
              "Fuller, rounder sound: more body, softer highs, gentle saturation." },
            { pid::macroAggression, "Aggression", "Macros", T::Float, 0.0f, 100.0f, 0.0f, 0.0f, "%", {},
              "Harder, more intense: faster heavier compression and more drive." },
            { pid::macroWidth,      "Width",      "Macros", T::Float, 0.0f, 100.0f, 0.0f, 0.0f, "%", {},
              "Wider vocal: doubled voices left and right, wider stereo image. The centre stays intact." },
            { pid::macroSpace,      "Space",      "Macros", T::Float, 0.0f, 100.0f, 0.0f, 0.0f, "%", {},
              "More room: reverb and delay that step back while you sing." },
            { pid::macroDensity,    "Density",    "Macros", T::Float, 0.0f, 100.0f, 0.0f, 0.0f, "%", {},
              "Thicker and steadier: more compression and a touch of saturation." },

            // ---------------- PULSE (0.3) ----------------
            { pid::pulseOn,      "Pulse On",      "Pulse", T::Bool, 0, 1, 0, 0, "", {}, "Enables PULSE, the rhythmic volume shaper." },
            { pid::pulseRate,    "Pulse Rate",    "Pulse", T::Choice, 0, 9, 2, 0, "", dsp::PulseModule::rateNames(),
              "Length of one curve cycle, synced to the host tempo (or the manual BPM when the host sends none)." },
            { pid::pulseDepth,   "Pulse Depth",   "Pulse", T::Float, 0.0f, 100.0f, 60.0f, 0.0f, "%", {}, "How far the curve pulls the volume down at its lowest points." },
            { pid::pulseAttack,  "Pulse Attack",  "Pulse", T::Float, 0.1f, 50.0f, 1.0f, 5.0f, "ms", {}, "How fast the volume may drop - longer softens hard dips." },
            { pid::pulseRelease, "Pulse Release", "Pulse", T::Float, 1.0f, 300.0f, 10.0f, 40.0f, "ms", {}, "How fast the volume may come back up." },
            { pid::pulseSmooth,  "Pulse Smooth",  "Pulse", T::Float, 0.0f, 30.0f, 2.0f, 5.0f, "ms", {}, "Final rounding of the gain movement against clicks." },
            { pid::pulseInterp,  "Pulse Interpolation", "Pulse", T::Choice, 0, 2, 1, 0, "", { "Linear", "Smooth", "Step" },
              "How the curve moves between its points: straight lines, S-shaped curves or hard steps." },
            { pid::pulseTension, "Pulse Shape",   "Pulse", T::Float, -100.0f, 100.0f, 0.0f, 0.0f, "%", {},
              "Bends every curve segment: negative = fast start, positive = slow start (punchier pumps)." },
            { pid::pulseMix,     "Pulse Mix",     "Pulse", T::Float, 0.0f, 100.0f, 100.0f, 0.0f, "%", {}, "Blend between the original and the shaped vocal." },
            { pid::pulseBpm,     "Pulse BPM",     "Pulse", T::Float, 40.0f, 250.0f, 150.0f, 0.0f, "BPM", {}, "Tempo used when the host does not report one.", 0.1f },
            { pid::pulseX[0], "Pulse Point 1 Time", "Pulse", T::Float, 0.0f, 1.0f, 0.00f, 0.0f, "", {}, "Curve point position in the cycle." },
            { pid::pulseX[1], "Pulse Point 2 Time", "Pulse", T::Float, 0.0f, 1.0f, 0.06f, 0.0f, "", {}, "Curve point position in the cycle." },
            { pid::pulseX[2], "Pulse Point 3 Time", "Pulse", T::Float, 0.0f, 1.0f, 0.28f, 0.0f, "", {}, "Curve point position in the cycle." },
            { pid::pulseX[3], "Pulse Point 4 Time", "Pulse", T::Float, 0.0f, 1.0f, 0.50f, 0.0f, "", {}, "Curve point position in the cycle." },
            { pid::pulseX[4], "Pulse Point 5 Time", "Pulse", T::Float, 0.0f, 1.0f, 0.75f, 0.0f, "", {}, "Curve point position in the cycle." },
            { pid::pulseX[5], "Pulse Point 6 Time", "Pulse", T::Float, 0.0f, 1.0f, 0.95f, 0.0f, "", {}, "Curve point position in the cycle." },
            { pid::pulseY[0], "Pulse Point 1 Level", "Pulse", T::Float, 0.0f, 1.0f, 0.00f, 0.0f, "", {}, "Curve point volume (1 = untouched)." },
            { pid::pulseY[1], "Pulse Point 2 Level", "Pulse", T::Float, 0.0f, 1.0f, 0.10f, 0.0f, "", {}, "Curve point volume (1 = untouched)." },
            { pid::pulseY[2], "Pulse Point 3 Level", "Pulse", T::Float, 0.0f, 1.0f, 0.70f, 0.0f, "", {}, "Curve point volume (1 = untouched)." },
            { pid::pulseY[3], "Pulse Point 4 Level", "Pulse", T::Float, 0.0f, 1.0f, 0.95f, 0.0f, "", {}, "Curve point volume (1 = untouched)." },
            { pid::pulseY[4], "Pulse Point 5 Level", "Pulse", T::Float, 0.0f, 1.0f, 1.00f, 0.0f, "", {}, "Curve point volume (1 = untouched)." },
            { pid::pulseY[5], "Pulse Point 6 Level", "Pulse", T::Float, 0.0f, 1.0f, 1.00f, 0.0f, "", {}, "Curve point volume (1 = untouched)." },

            // ---------------- VOCAL SMASH (0.3) ----------------
            { pid::smashOn,      "Smash On",      "Smash", T::Bool, 0, 1, 0, 0, "", {}, "Enables VOCAL SMASH, a dedicated parallel compressor." },
            { pid::smashAmount,  "Smash Amount",  "Smash", T::Float, 0.0f, 100.0f, 50.0f, 0.0f, "%", {},
              "How hard the parallel path is squashed (lower threshold, higher ratio). The original keeps its dynamics." },
            { pid::smashBlend,   "Smash Blend",   "Smash", T::Float, 0.0f, 100.0f, 35.0f, 0.0f, "%", {}, "How much of the squashed path is added under the original." },
            { pid::smashAttack,  "Smash Attack",  "Smash", T::Float, 0.1f, 30.0f, 2.0f, 3.0f, "ms", {}, "Attack of the squashed path - fast catches every consonant." },
            { pid::smashRelease, "Smash Release", "Smash", T::Float, 20.0f, 500.0f, 80.0f, 100.0f, "ms", {}, "Release of the squashed path - short = denser, long = smoother." },
            { pid::smashTone,    "Smash Tone",    "Smash", T::Float, -100.0f, 100.0f, 20.0f, 0.0f, "%", {},
              "Tilts the colour of the squashed path: negative = warmer, positive = brighter and more present." },
            { pid::smashOutput,  "Smash Output",  "Smash", T::Float, -12.0f, 12.0f, 0.0f, 0.0f, "dB", {}, "Output trim of the module." },

            // ---------------- CHOIR (0.3) ----------------
            { pid::choirOn,       "Choir On",       "Choir", T::Bool, 0, 1, 0, 0, "", {}, "Enables CHOIR, multi-voice doubling." },
            { pid::choirVoices,   "Choir Voices",   "Choir", T::Choice, 0, 3, 1, 0, "", { "2", "4", "6", "8" }, "Number of virtual voices around the original." },
            { pid::choirDetune,   "Choir Detune",   "Choir", T::Float, 0.0f, 40.0f, 10.0f, 12.0f, "ct", {}, "Pitch difference between the voices (each voice gets its own offset)." },
            { pid::choirTiming,   "Choir Timing",   "Choir", T::Float, 0.0f, 60.0f, 18.0f, 20.0f, "ms", {}, "How late the voices come in compared with each other - like singers who are not perfectly together." },
            { pid::choirHumanize, "Choir Humanize", "Choir", T::Float, 0.0f, 100.0f, 50.0f, 0.0f, "%", {}, "Slow, independent drift of pitch and timing per voice so the choir never sounds static." },
            { pid::choirWidth,    "Choir Width",    "Choir", T::Float, 0.0f, 100.0f, 80.0f, 0.0f, "%", {}, "How far the voices spread across the stereo field." },
            { pid::choirDensity,  "Choir Density",  "Choir", T::Float, 0.0f, 100.0f, 50.0f, 0.0f, "%", {},
              "Thickness of the choir: low = voices sit softly behind, high = every voice at full level." },
            { pid::choirMix,      "Choir Mix",      "Choir", T::Float, 0.0f, 100.0f, 35.0f, 0.0f, "%", {}, "Balance between the original vocal and the choir." },

            // ---------------- SMOOTH (0.3) ----------------
            { pid::smoothOn,          "Smooth On",          "Smooth", T::Bool, 0, 1, 0, 0, "", {}, "Enables SMOOTH, adaptive resonance control." },
            { pid::smoothAmount,      "Smooth Amount",      "Smooth", T::Float, 0.0f, 100.0f, 50.0f, 0.0f, "%", {}, "How strongly detected resonances are reduced." },
            { pid::smoothSensitivity, "Smooth Sensitivity", "Smooth", T::Float, 0.0f, 100.0f, 50.0f, 0.0f, "%", {},
              "How far a frequency must stick out of its neighbours before it counts as a resonance." },
            { pid::smoothLow,         "Smooth Low",         "Smooth", T::Float, 100.0f, 8000.0f, 800.0f, 1000.0f, "Hz", {}, "Lowest frequency that is treated." },
            { pid::smoothHigh,        "Smooth High",        "Smooth", T::Float, 1000.0f, 18000.0f, 12000.0f, 6000.0f, "Hz", {}, "Highest frequency that is treated." },
            { pid::smoothAttack,      "Smooth Attack",      "Smooth", T::Float, 0.5f, 50.0f, 5.0f, 8.0f, "ms", {}, "How fast a reduction reacts." },
            { pid::smoothRelease,     "Smooth Release",     "Smooth", T::Float, 10.0f, 500.0f, 80.0f, 100.0f, "ms", {}, "How fast a reduction lets go." },
            { pid::smoothMode,        "Smooth Mode",        "Smooth", T::Choice, 0, 1, 0, 0, "", { "Soft", "Hard" },
              "Soft: wide, gentle reductions. Hard: narrow, stronger and faster - for aggressive resonances." },
            { pid::smoothOutput,      "Smooth Output",      "Smooth", T::Float, -12.0f, 12.0f, 0.0f, 0.0f, "dB", {}, "Output trim of the module." },
            { pid::smoothDelta,       "Smooth Delta",       "Smooth", T::Bool, 0, 1, 0, 0, "", {}, "Listen only to what is being removed (for tuning)." },

            // ---------------- VOCAL CHECK (0.3) ----------------
            { pid::checkOn,   "Check On",   "Check", T::Bool, 0, 1, 0, 0, "", {},
              "Enables VOCAL CHECK. It only listens: the audio passes through unchanged." },
            { pid::checkMode, "Check Mode", "Check", T::Choice, 0, 1, 0, 0, "", { "Continuous", "Capture" },
              "Continuous: rolling analysis of the last ~30 s. Capture: analyses everything from Start until Stop." },

            // ---------------- LEVEL RIDE (0.3) ----------------
            { pid::rideOn,        "Ride On",        "Ride", T::Bool, 0, 1, 0, 0, "", {}, "Enables LEVEL RIDE, the vocal loudness rider." },
            { pid::rideTarget,    "Ride Target",    "Ride", T::Float, -40.0f, -6.0f, -18.0f, 0.0f, "dB", {}, "Level the rider aims for (RMS of the voice)." },
            { pid::rideAmount,    "Ride Amount",    "Ride", T::Float, 0.0f, 100.0f, 70.0f, 0.0f, "%", {}, "How much of the distance to the target is corrected." },
            { pid::rideMaxBoost,  "Ride Max Boost", "Ride", T::Float, 0.0f, 18.0f, 8.0f, 0.0f, "dB", {}, "Largest lift for quiet words." },
            { pid::rideMaxCut,    "Ride Max Cut",   "Ride", T::Float, 0.0f, 18.0f, 8.0f, 0.0f, "dB", {}, "Largest reduction for loud words." },
            { pid::rideResponse,  "Ride Response",  "Ride", T::Float, 0.0f, 100.0f, 50.0f, 0.0f, "%", {}, "Speed of the gain moves: low = slow and smooth, high = word by word." },
            { pid::ridePhrase,    "Ride Phrase Smooth", "Ride", T::Float, 0.0f, 100.0f, 40.0f, 0.0f, "%", {},
              "Averages the level over longer phrases instead of single syllables - keeps the natural flow." },
            { pid::rideTransient, "Ride Preserve Transients", "Ride", T::Float, 0.0f, 100.0f, 60.0f, 0.0f, "%", {},
              "Ignores short peaks (consonants, plosives) so they are neither ducked nor pumped." },
            { pid::rideSilence,   "Ride Ignore Below", "Ride", T::Float, -80.0f, -30.0f, -50.0f, 0.0f, "dB", {},
              "Below this level the rider treats the signal as a pause and never boosts breaths or room noise." },

            // ---------------- Reverb engine (0.4) ----------------
            { pid::reverbEarly,     "Reverb Early",     "Reverb", T::Float, 0.0f, 100.0f, 50.0f, 0.0f, "%", {},
              "Level of the early reflections - the first, distinct echoes that tell the ear how big and close the space is." },
            { pid::reverbDiffusion, "Reverb Diffusion", "Reverb", T::Float, 0.0f, 100.0f, 70.0f, 0.0f, "%", {},
              "How quickly the echoes melt into a smooth tail. Low = grainy, distinct; high = dense and silky." },
            { pid::reverbMod,       "Reverb Movement",  "Reverb", T::Float, 0.0f, 100.0f, 30.0f, 0.0f, "%", {},
              "Slow movement inside the tail. Makes long spaces lush and avoids metallic ringing; 0 = static." },
            { pid::reverbBass,      "Reverb Bass Decay","Reverb", T::Float, 0.5f, 2.0f, 1.0f, 1.0f, "x", {},
              "Decay of the low frequencies relative to the mids. Below 1 keeps long tails from getting muddy." },
            { pid::reverbHighCut,   "Reverb High Cut",  "Reverb", T::Float, 1000.0f, 20000.0f, 12000.0f, 6000.0f, "Hz", {},
              "Low-pass on the reverb return: lower = darker, further back." },

            // ---------------- Delay (0.4) ----------------
            { pid::delaySpread,    "Delay Spread",    "Delay", T::Float, 0.0f, 50.0f, 0.0f, 12.0f, "ms", {},
              "The right side repeats this much later than the left - wide, independent stereo echoes." },
            { pid::delayDrive,     "Delay Drive",     "Delay", T::Float, 0.0f, 100.0f, 0.0f, 0.0f, "%", {},
              "Tape-like saturation of the repeats: warmer, each echo a little rounder." },
            { pid::delayDiffusion, "Delay Diffusion", "Delay", T::Float, 0.0f, 100.0f, 0.0f, 0.0f, "%", {},
              "Smears the repeats into a soft wash - from clear echoes to ambient clouds." },
            { pid::delayMod,       "Delay Modulation","Delay", T::Float, 0.0f, 100.0f, 0.0f, 0.0f, "%", {},
              "Slow wow and flutter on the repeats, like a tape echo." },

            // ---------------- Pulse (0.6, "like Kickstart") ----------------
            { pid::pulseShape,  "Pulse Curve",  "Pulse", T::Choice, 0, (float) (dsp::PulseModule::shapeNames().size() - 1), 0, 0, "",
              dsp::PulseModule::shapeNames(), "Pump shape: Custom uses the drawn curve; Classic, Punch, Swell and Gate are ready-made pumps." },
            { pid::pulseAmount, "Pulse Amount", "Pulse", T::Float, 0.0f, 100.0f, 50.0f, 0.0f, "%", {},
              "How hard the vocal pumps: 0 = untouched, 50 = the Depth setting, 100 = ducked fully at the beat." },
        };
            appendEqBandSpecs (table);
            appendSatMultibandSpecs (table);
            appendInstanceBanks (table);
            return table;
        }();

        return specs;
    }

    const ParameterSpec* findParameterSpec (const juce::String& id)
    {
        static const auto index = []
        {
            std::unordered_map<std::string, const ParameterSpec*> m;
            for (auto& s : getParameterSpecs())
                m[s.id] = &s;
            return m;
        }();
        const auto it = index.find (id.toStdString());
        return it != index.end() ? it->second : nullptr;
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

        const auto baseId = dsp::splitInstanceParamId (spec.id).first;
        if ((baseId == pid::monoBelow || baseId == pid::compScHpf) && value <= 20.5f)
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
        if (unit == "st")
            return (value > 0.05f ? "+" : "") + juce::String (value, std::abs (value - std::round (value)) < 0.05f ? 0 : 1) + " st";
        if (unit == "BPM")
            return juce::String (value, 1) + " BPM";
        if (unit == "x")
            return juce::String (value, 2) + "x";
        if (unit == ":1")
            return juce::String (value, value < 10.0f ? 1 : 0) + ":1";

        return juce::String (value, 2);
    }

    static juce::NormalisableRange<float> makeRange (const ParameterSpec& s)
    {
        juce::NormalisableRange<float> r (s.minValue, s.maxValue);
        if (s.interval > 0.0f)
            r.interval = s.interval;
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
                                          .withAutomatable (s.automatable)
                                          .withLabel (s.unit)
                                          .withStringFromValueFunction ([specPtr] (float v, int) { return formatValue (*specPtr, v); })
                                          .withValueFromStringFunction ([] (const juce::String& t) { return t.retainCharacters ("-0123456789.").getFloatValue()
                                                                                                          * (t.containsIgnoreCase ("k") ? 1000.0f : 1.0f); });
                    layout.add (std::make_unique<juce::AudioParameterFloat> (id, s.name, makeRange (s), s.defaultValue, attributes));
                    break;
                }

                case T::Bool:
                    layout.add (std::make_unique<juce::AudioParameterBool> (id, s.name, s.defaultValue > 0.5f,
                                                                            juce::AudioParameterBoolAttributes().withAutomatable (s.automatable)));
                    break;

                case T::Choice:
                {
                    const auto choices = dsp::splitInstanceParamId (s.id).first == pid::delayDivision ? delaydiv::names() : s.choices;
                    layout.add (std::make_unique<juce::AudioParameterChoice> (id, s.name, choices, (int) s.defaultValue,
                                                                              juce::AudioParameterChoiceAttributes().withAutomatable (s.automatable)));
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
                const auto choices = dsp::splitInstanceParamId (s.id).first == pid::delayDivision ? delaydiv::names() : s.choices;
                juce::Array<juce::var> c;
                for (auto& ch : choices)
                    c.add (ch);
                obj->setProperty ("choices", c);
            }

            obj->setProperty ("default", s.defaultValue);
            if (s.hidden)
                obj->setProperty ("hidden", true);
            if (! s.automatable)
                obj->setProperty ("automatable", false);
            list.add (juce::var (obj));
        }

        return juce::var (list);
    }
}
