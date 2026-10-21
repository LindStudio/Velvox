#pragma once

#include "AutoMixPlanner.h"
#include "AutoMixObjective.h"

namespace velvox::automix
{
    // Measurements of one offline render of the captured audio
    struct RenderMetrics
    {
        bool valid = false;
        float lufs = -100.0f;              // integrated, gated
        float peakDb = -100.0f;            // sample peak before the clip guard
        float compGrMedianDb = 0.0f;       // compressor reduction on voiced blocks (median)
        float compGrMaxDb = 0.0f;
        float deessGrMaxDb = 0.0f;
        float topDb = -60.0f, presenceDb = -60.0f, bodyDb = -60.0f;   // relative to the core, like VocalProfile
        float sibilanceVsVowelDb = -100.0f;
        bool sibilanceValid = false;
        float correlation = 1.0f;          // left / right of the output
        float monoLossDb = 0.0f;           // loudness of the mono sum minus stereo loudness

        // 0.4: what the candidate search compares
        float lowMidDb = -60.0f, airDb = -60.0f;
        float dynamicsDb = 0.0f;           // phrase-level variation (VocalProfiler)
        bool dynamicsValid = false;
        float crestDb = 0.0f;
        float gapDb = -100.0f;             // level in the gaps right after words, relative to the words
        bool gapValid = false;
        int clipRuns = 0;                  // runs at full scale before the clip guard
    };

    // One chain the automatic mix rendered and scored
    struct CandidateResult
    {
        juce::String name;                 // what differs from the base plan (German, for the report)
        ObjectiveBreakdown score;
        RenderMetrics metrics;
        dsp::Routing routing;
    };

    struct AutoMixResult
    {
        bool ok = false;
        juce::String error;
        VocalProfile profile;
        MixPlan plan;
        RenderMetrics original;            // the input as captured (incl. input gain)
        RenderMetrics processed;           // the final plan
        int iterations = 0;
        std::vector<juce::String> adjustments;   // what the verification changed
        std::vector<CandidateResult> candidates; // every candidate of the search (index 0 = base plan)
        int chosenCandidate = 0;
        double seconds = 0.0;                    // analysis + all renders
    };

    // analyse -> plan -> render -> measure -> adjust (max. 4 rounds) -> set the
    // output level. Runs entirely off the audio thread on a copy of the audio,
    // through a private VocalChain with exactly the plugin's parameter mapping.
    class AutoMixEngine
    {
    public:
        using Progress = std::function<void (float fraction, const juce::String& stage)>;

        static AutoMixResult run (const CapturedAudio& audio, const std::map<juce::String, float>& current,
                                  const std::function<bool()>& shouldStop = {}, const Progress& progress = {});

        // Renders `audio` with `current` overridden by `values` and measures the result.
        // `output` (optional) receives the processed audio (clip guard off).
        static RenderMetrics render (const CapturedAudio& audio, const std::map<juce::String, float>& current,
                                     const std::map<juce::String, float>& values, const dsp::Routing& routing,
                                     CapturedAudio* output = nullptr);

        static RenderMetrics measure (const CapturedAudio& audio);

        // render() plus the S / vowel balance measured on the reference frames of `profile`
        static RenderMetrics renderChecked (const CapturedAudio& audio, const std::map<juce::String, float>& current,
                                            const MixPlan& plan, const VocalProfile& profile);

        // Output loudness the mix aims for: the input loudness, kept within a
        // sensible working range. Loudness is matched, not maximised - the
        // comparison with the original stays fair.
        static float targetLufs (float inputLufs) noexcept { return juce::jlimit (-24.0f, -14.0f, inputLufs); }

        // Candidate chains derived from a plan: each changes one interacting decision
        // (dynamics lighter / denser, saturation, tonal EQ after the dynamics as a second EQ
        // instance, effects drier / wetter, de-esser stronger). Bounded, deterministic.
        static std::vector<std::pair<juce::String, MixPlan>> candidatesFor (const MixPlan& base, const VocalProfile& profile);
    };
}
