#include "AutoMixObjective.h"
#include "AutoMixEngine.h"

namespace velvox::automix
{
    namespace
    {
        // Squared distance outside [lo, hi], scaled so that 2 dB outside costs 1
        float window (float v, float lo, float hi, float dbPerUnit = 2.0f)
        {
            const float d = v < lo ? lo - v : (v > hi ? v - hi : 0.0f);
            return (d / dbPerUnit) * (d / dbPerUnit);
        }
    }

    juce::String ObjectiveBreakdown::describe() const
    {
        juce::StringArray parts;
        const std::pair<const char*, float> terms[] { { "tonal", tonal }, { "dynamics", dynamics }, { "sibilance", sibilance },
                                                      { "harshness", harshness }, { "stereo", stereo }, { "space", space },
                                                      { "artifacts", artifacts }, { "change", change } };
        for (auto& [name, v] : terms)
            if (v > 0.005f)
                parts.add (juce::String (name) + " " + juce::String (v, 2));
        return parts.isEmpty() ? juce::String ("0") : parts.joinIntoString (", ");
    }

    VocalStyle styleOf (const VocalProfile& p) noexcept
    {
        if (p.pitchedShare < 0.45f)
            return VocalStyle::spoken;
        if (p.pitchedShare > 0.75f && p.pitchSpreadSemitones > 2.0f)
            return VocalStyle::sustained;
        return VocalStyle::sung;
    }

    ObjectiveBreakdown evaluate (const RenderMetrics& m, const RenderMetrics& original, const VocalProfile& profile,
                                 float targetLufs, bool isBasePlan, bool effectsPlanned)
    {
        ObjectiveBreakdown b;
        if (! m.valid)
        {
            b.artifacts = 1.0e6f;
            return b;
        }

        // ---- Tonal balance (relative to the core of the voice). Windows from the analysis
        // thresholds that were calibrated on real vocals (VocalProfiler issues): inside
        // them a vocal reads as neither muddy, thin, dull nor harsh.
        b.tonal = 0.6f * window (m.bodyDb, -9.0f, -1.0f)
                + 0.8f * window (m.lowMidDb, -7.0f, 0.0f)
                + 1.0f * window (m.topDb, -16.0f, -7.0f)
                + 0.4f * window (m.airDb, -32.0f, -16.0f, 3.0f);

        // ---- Dynamics: a lead vocal in an EDM mix sits steady; over-compression is worse
        // than a little movement, so the crest factor floor is weighted strongly
        if (m.dynamicsValid)
            b.dynamics = 0.8f * window (m.dynamicsDb, 4.0f, 8.0f);
        b.dynamics += 1.5f * window (m.crestDb, 7.0f, 100.0f, 1.5f);

        // ---- Sibilance (only where the S / vowel frames could be measured)
        if (m.sibilanceValid && original.sibilanceValid)
        {
            b.sibilance = 1.2f * window (m.sibilanceVsVowelDb, -100.0f, -6.0f);
            b.sibilance += 0.8f * window (m.sibilanceVsVowelDb, original.sibilanceVsVowelDb - 10.0f, 100.0f);
        }

        // ---- Harshness: presence far forward
        b.harshness = 1.0f * window (m.presenceDb, -100.0f, -4.0f);

        // ---- Stereo / mono compatibility
        b.stereo = 2.0f * window (m.monoLossDb, -1.0f, 100.0f, 1.0f) + 1.0f * window (m.correlation, 0.5f, 1.0f, 0.25f);

        // ---- Space: effect level in the gaps after words (relative to the voice). A dry
        // take has nearly nothing there; the window depends on the style.
        if (m.gapValid)
        {
            const auto style = styleOf (profile);
            const float lo = style == VocalStyle::spoken ? -40.0f : (style == VocalStyle::sustained ? -30.0f : -34.0f);
            const float hi = style == VocalStyle::spoken ? -24.0f : (style == VocalStyle::sustained ? -13.0f : -17.0f);
            // Too wet blurs the words (a take that already has room keeps it); too dry only
            // costs when the plan adds space at all (a roomy take stays dry on purpose)
            const float origGap = original.gapValid ? original.gapDb : -120.0f;
            b.space = 0.5f * window (m.gapDb, -200.0f, std::max (hi, origGap + 1.0f), 3.0f);
            if (effectsPlanned)
                b.space += 0.2f * window (m.gapDb, lo, 200.0f, 3.0f);
        }

        // ---- Artifacts: clipping, and the peak at the loudness the output stage will set
        const float peakAtTarget = m.peakDb + (targetLufs - m.lufs);
        b.artifacts = 4.0f * (float) m.clipRuns + 1.0f * window (peakAtTarget, -100.0f, -1.0f, 1.0f);

        // ---- Leaving the base plan has to be worth it
        b.change = isBasePlan ? 0.0f : 0.25f;
        return b;
    }
}
