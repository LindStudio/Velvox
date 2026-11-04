#pragma once

#include "AutoMixPlanner.h"
#include "AutoMixObjective.h"

namespace vocalypse::automix
{
    // The level the auto mix analyses and plans at (dBFS, median voiced 50 ms RMS). The capture is measured, a copy
    // is scaled to this level, and every level-derived decision is made on that copy - so +-2 dB of recording level
    // cannot flip a decision (plan 2026-11-04-automix-konsistenz, U3). The plan's inputGain is the scaling.
    inline constexpr float workingLevelDb = -18.0f;

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
        std::vector<float> thirdsDb;       // 0.8: third octaves relative to the core (thirdsOf)
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
        RenderMetrics original;            // the input as captured (loudness, peak, clipping as recorded; the rest at the working level)
        RenderMetrics processed;           // the final plan
        int iterations = 0;
        std::vector<juce::String> adjustments;   // what the verification changed
        std::vector<CandidateResult> candidates; // every candidate of the search (index 0 = base plan)
        int chosenCandidate = 0;
        double seconds = 0.0;                    // analysis + all renders
        MixIntent intent;                        // 0.8: the genre / vibe it mixed for
        std::vector<Segment> segments;           // the analysed segments of the capture
        std::vector<VocalProfile> segmentProfiles;   // the profile of each segment (the profile aggregates them)
        std::vector<int> excerptSegments;        // the segments the renders played, in song order (indices into `segments`)
        juce::String storeNote;                  // set when the analysis JSON could not be written
    };

    // analyse -> plan -> render -> measure -> adjust (max. 4 rounds) -> set the
    // output level. Runs entirely off the audio thread on a copy of the audio,
    // through a private VocalChain with exactly the plugin's parameter mapping.
    class AutoMixEngine
    {
    public:
        using Progress = std::function<void (float fraction, const juce::String& stage)>;

        static AutoMixResult run (const CapturedAudio& audio, const std::map<juce::String, float>& current,
                                  const std::function<bool()>& shouldStop = {}, const Progress& progress = {},
                                  const MixIntent& intent = {});

        // Renders `audio` with `current` overridden by `values` and measures the result.
        // `output` (optional) receives the processed audio (clip guard off).
        static RenderMetrics render (const CapturedAudio& audio, const std::map<juce::String, float>& current,
                                     const std::map<juce::String, float>& values, const dsp::Routing& routing,
                                     CapturedAudio* output = nullptr);

        // `scaleDb`: `audio` is the capture scaled by this gain; lufs, peak and clipping describe the capture
        static RenderMetrics measure (const CapturedAudio& audio, float scaleDb = 0.0f);

        // The audio the renders play: the segments ranked by voiced seconds (ties: the earlier one), taken until 30 s
        // are reached, laid end to end in song order with 20 ms fades where two of them are not neighbours in the
        // capture. The length of every segment is kept. Without segments: the capture itself.
        static std::vector<int> excerptSegments (const std::vector<Segment>& segments, double sampleRate);
        static CapturedAudio excerptFor (const CapturedAudio& audio, const std::vector<Segment>& segments);

        // The input gain that brings the capture to workingLevelDb: working level minus the median RMS of the voiced
        // 50 ms blocks (mid signal; a block counts as voiced from 35 dB under the loud blocks - relative only, so
        // a quiet take is measured like a loud one), limited to the input gain's range. Deterministic; 0 without
        // any voiced block. `wantedDb` (optional) receives the value before the limit.
        static float workingGainDb (const CapturedAudio& audio, float* wantedDb = nullptr);

        // 0.8 (spec: "Kompressor gezielt"): renders compressor settings around the plan's (threshold -4 .. +4 dB,
        // ratio 1.8 / 2.5 / 3.5 / 5, greedy, at most `maxRenders`) and keeps the one whose phrase spread and crest
        // land closest to the target's dynamics window; then the Vocal Smash blend (0, 1/2, 1, 1.5 x the density
        // target, at most 2 renders) for the crest. Returns the renders used; the plan's texts follow.
        static int tuneDynamics (const CapturedAudio& audio, const std::map<juce::String, float>& current, MixPlan& plan,
                                 const VocalProfile& profile, const MixTarget& target, int maxRenders = 8);

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
        // `current`: the values the plan overlays (EQ role bands find free slots there; empty = defaults).
        static std::vector<std::pair<juce::String, MixPlan>> candidatesFor (const MixPlan& base, const VocalProfile& profile,
                                                                             const std::map<juce::String, float>& current = {},
                                                                             const MixTarget& target = targetFor (Genre::party, Vibe::punchy));
    };
}
