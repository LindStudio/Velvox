#pragma once

#include "AutoMixEngine.h"

namespace vocalypse::automix
{
    // The analysis of every auto mix is kept as JSON (plan 2026-11-04-automix-konsistenz, U1): what was captured, every
    // segment's profile, the aggregated profile, the decision log of every module, the values the plan writes and the
    // routing. The newest 50 files are kept. NaN / infinite numbers are written as null, so the file is always valid JSON.
    //
    //   <directory>/<yyyyMMdd_HHmmss>_<fnv1a of the capture>.json
    //   { version, intent, capture { seconds, voicedSeconds, hash, ppqStart }, segments [profile...], profile, log [...],
    //     values {...}, routing }
    class AnalysisStore
    {
    public:
        static constexpr int version = 1;
        static constexpr int keepFiles = 50;

        // userApplicationDataDirectory/Lind Audio/VOCALYPSE/AutoMix
        static juce::File defaultDirectory();
        // Where write() puts the files. Tests and the command line set a temporary folder; an empty file = the default.
        static void setDirectory (const juce::File& directory);
        static juce::File directory();

        // `hash`: hashOf (captured) when the caller has it already
        static juce::var toVar (const AutoMixResult&, const CapturedAudio&, const juce::String& hash = {});
        static juce::String toJson (const AutoMixResult&, const CapturedAudio&, const juce::String& hash = {});   // valid JSON, never NaN / inf
        // FNV-1a (64 bit, hex) over the captured samples
        static juce::String hashOf (const CapturedAudio&);

        // Writes into directory() and keeps the newest `keepFiles`; returns the file (invalid when writing failed)
        static juce::File write (const AutoMixResult&, const CapturedAudio&);
        static juce::File writeTo (const juce::File& directory, const AutoMixResult&, const CapturedAudio&);
        // Deletes the oldest files of the store's own name pattern (<date>_<time>_<hash>.json) beyond keepFiles; other files stay
        static void prune (const juce::File& directory);
    };
}
