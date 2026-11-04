#pragma once

// AUTO MIX CONSISTENCY HARNESS (plan 2026-11-04-automix-konsistenz, Task 1)
//
// Runs the auto mix the way the plugin does (capture from a file in 512-sample blocks, then the engine from a
// given parameter state), reduces a plan to a fingerprint (EQ bands by ROLE, never by slot) and compares two
// fingerprints under the tolerances of the plan: exact (repeats, prior state, sequential runs), gain shift,
// perturbation (noise, start point). The command line mode `VocalypseTests --consistency` runs all variants
// on real takes and writes consistency.csv / consistency_summary.txt.

#include "AutoMix/AutoMixEngine.h"
#include "Parameters/ParameterLayout.h"
#include <map>
#include <set>
#include <vector>

namespace vocalypse::test
{
    struct PlanFingerprint
    {
        std::map<juce::String, float> values;                       // every parameter except EQ band slots
        std::map<std::pair<int, int>, dsp::EqBand> roleBands;       // (instance, EqRole) -> band, roles != none
        std::set<juce::String> modulesOn;                            // "<moduleKey>#<instance>" in the routing and switched on
        juce::String routing;                                        // Routing::toString()
    };
    // The effective state: `prior` (the plugin's parameters before the mix) overlaid with what the plan writes, so a
    // parameter the plan leaves alone shows the state it leaked from. The one-argument form overlays the defaults.
    PlanFingerprint fingerprint (const automix::MixPlan& plan, const std::map<juce::String, float>& prior);
    PlanFingerprint fingerprint (const automix::MixPlan& plan);

    // The tolerances of the plan's Global Constraints, as named fields (later tasks tighten them)
    struct Tolerances
    {
        // gain shift (+-0.5 / 1 / 2 dB of input level): no module flip,
        float inputGainDb = 0.05f;        // inputGain = original - shift +- this
        float outputGainDb = 0.3f;        // outputGain = original + shift +- this
        float relative = 0.02f;           // every other value: within this share ...
        float absolute = 0.1f;            // ... or this many units (the thresholds too: the plan is made at the working level)

        // perturbation (noise -70 dBFS, start offsets 3 / 6 s): a flip only at low intensity, and
        float eqBandGainDb = 1.5f;        // EQ role band gain
        float freqOctaves = 1.0f / 6.0f;  // EQ role band / de-esser frequency
        float compThresholdDb = 2.0f;
        float compRatioRel = 0.20f;
        float compTimeRel = 0.25f;        // attack / release
        float deessRangeDb = 2.0f;
        float mixPoints = 6.0f;           // wet / mix / blend percentages
        float satDriveDb = 1.5f;
    };

    // "lowIntensity" per module: a module may flip under perturbation only when, in the run that has it on,
    // its intensity parameter is <= value (a compressor never counts as low)
    struct LowIntensity { const char* module; const char* param; float value; };
    const std::vector<LowIntensity>& lowIntensityTable();

    enum class Check { exact, gainShift, perturbation };

    struct PlanDiff
    {
        int differing = 0;                         // flips + every value / band that is not identical
        std::vector<juce::String> flips;           // modules that are on in one plan only ("+deess#0": on in `other`)
        std::vector<juce::String> overTolerance;   // what violates the check
        bool pass = true;
    };
    // `gainShiftDb`: the level shift of the input in `other` (only for Check::gainShift)
    PlanDiff compare (const PlanFingerprint& reference, const PlanFingerprint& other, Check check, float gainShiftDb, const Tolerances&);

    // Plays `file` from `startSeconds` through the CaptureBuffer like the plugin and copies the capture out.
    // False when there was not enough signal.
    // `withSongPosition`: the host reports 120 BPM and the song position (the file starts at ppq 0, so a start at 6 s is ppq 12).
    bool captureLikePlugin (const automix::CapturedAudio& file, double startSeconds, automix::CapturedAudio& captured,
                            bool withSongPosition = false);

    // Simulates the plugin: plays `file` from `startSeconds` through CaptureBuffer in 512-sample blocks and runs
    // the auto mix from the `prior` parameter values. `withSongPosition` (120 BPM, ppq = file time * 2) is fed
    // to the capture like a host does (Task 5).
    automix::AutoMixResult mixFile (const automix::CapturedAudio& file, double startSeconds, bool withSongPosition,
                                    const std::map<juce::String, float>& prior, const automix::MixIntent&);

    std::map<juce::String, float> defaultValues();
    std::map<juce::String, float> scrambledValues (juce::uint32 seed);   // every parameter at a seeded random legal value

    // Command line: VocalypseTests --consistency <takes.txt|dir> <outDir> [genre vibe] [--repeats N]
    // Takes run in parallel threads (one per take); returns 1 when any check fails.
    int runConsistency (const juce::File& takes, const juce::File& outDir, const automix::MixIntent& intent, int repeats);
}
