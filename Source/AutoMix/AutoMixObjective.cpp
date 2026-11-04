#include "AutoMixObjective.h"
#include "AutoMixEngine.h"
#include "AutoEq.h"
#include "AutoMixPlanner.h"
#include "../DSP/Instances.h"
#include "../Parameters/EqBands.h"

namespace vocalypse::automix
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
                                                      { "artifacts", artifacts }, { "change", change }, { "beat", beat } };
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

    BeatDensity beatDensityOf (const VocalProfile& p) noexcept
    {
        if (! p.beatPresent)
            return BeatDensity::none;
        return p.beatGapShare < 0.15f ? BeatDensity::dense : (p.beatGapShare > 0.3f ? BeatDensity::sparse : BeatDensity::medium);
    }

    float planComplexity (const MixPlan& plan, float medianCompressionDb)
    {
        using M = dsp::ModuleId;
        const std::pair<M, const char*> switches[] {
            { M::eq, pid::eqOn }, { M::ride, pid::rideOn }, { M::comp, pid::compOn }, { M::smash, pid::smashOn },
            { M::deess, pid::deessOn }, { M::smooth, pid::smoothOn }, { M::sat, pid::satOn }, { M::doubler, pid::doublerOn },
            { M::delay, pid::delayOn }, { M::reverb, pid::reverbOn }, { M::denoise, pid::denoiseOn }, { M::breath, pid::breathOn }
        };
        float modules = 0.0f, eqGain = 0.0f;
        auto values = plan.values;
        const auto access = eqbands::accessFor (values, nullptr);
        for (const auto& slot : plan.routing)
        {
            for (const auto& [module, id] : switches)
            {
                if (module != slot.id || plan.get (dsp::instanceParamId (id, slot.instance)) <= 0.5f)
                    continue;
                modules += 1.0f;
                if (module == M::eq)
                    for (int b = 0; b < dsp::maxEqBands; ++b)
                        if (const auto band = eqbands::readBand (access, slot.instance, b); band.on)
                            eqGain += std::abs (band.gain);
            }
        }
        const bool comp = plan.routing.contains (M::comp) && plan.on (pid::compOn);
        return 0.3f * modules + 0.05f * eqGain + 0.1f * (comp ? std::max (0.0f, medianCompressionDb) : 0.0f);
    }

    float variantMargin (float baseScore) noexcept { return std::max (0.5f, 0.08f * baseScore); }

    bool variantEarnsChange (float incumbentScore, float incumbentComplexity, float variantScore, float variantComplexity,
                             float baseScore) noexcept
    {
        return incumbentScore - variantScore >= variantMargin (baseScore) + variantComplexity - incumbentComplexity - 1.0e-6f;
    }

    ObjectiveBreakdown evaluate (const RenderMetrics& m, const RenderMetrics& original, const VocalProfile& profile,
                                 float targetLufs, bool isBasePlan, bool effectsPlanned, const MixTarget& target)
    {
        ObjectiveBreakdown b;
        if (! m.valid)
        {
            b.artifacts = 1.0e6f;
            return b;
        }

        // ---- Tonal balance (0.8): the third octaves against the target curve of the genre / vibe, outside its
        // spread (residualDb), over the range the EQ works on; 3 dB off in every third costs 9
        {
            const auto [lo, hi] = autoeq::fitRange (profile);
            float sum = 0.0f;
            int n = 0;
            if (m.thirdsDb.size() == (size_t) numThirds)
                for (int k = lo; k <= hi; ++k)
                    if (m.thirdsDb[(size_t) k] > -190.0f)
                    {
                        const float r = residualDb (target, k, m.thirdsDb[(size_t) k]);
                        sum += r * r;
                        ++n;
                    }
            b.tonal = n > 0 ? sum / (float) n : 0.0f;
        }

        // ---- Dynamics: the window of the genre / vibe (party: calibrated on the blind listening test of
        // 2026-10-24 - the denser, more compressed versions won); squashing below crestLo - 1 costs most
        if (m.dynamicsValid)
            b.dynamics = 0.8f * window (m.dynamicsDb, target.dynamics.phraseLo, target.dynamics.phraseHi);
        b.dynamics += 1.2f * window (m.crestDb, target.dynamics.crestLo - 1.0f, 100.0f, 1.5f);

        // ---- Sibilance (only where the S / vowel frames could be measured)
        if (m.sibilanceValid && original.sibilanceValid)
        {
            b.sibilance = 1.2f * window (m.sibilanceVsVowelDb, -100.0f, target.sibilanceMax);
            b.sibilance += 0.8f * window (m.sibilanceVsVowelDb, original.sibilanceVsVowelDb - 10.0f, 100.0f);
        }

        // ---- Harshness: presence far forward (a vibe that wants more bite moves the limit with it)
        {
            float style = 0.0f;
            int n = 0;
            for (int k = 0; k < numThirds; ++k)
                if (thirdCentre (k) >= 2000.0f * 0.99f && thirdCentre (k) <= 5000.0f * 1.01f)
                {
                    style += target.styleDb[(size_t) k];
                    ++n;
                }
            b.harshness = 1.0f * window (m.presenceDb, -100.0f, autoeq::presenceWindow.hi + std::max (0.0f, n > 0 ? style / (float) n : 0.0f));
        }

        // ---- The beat on the sidechain covers the words (0.8): beat minus voice per third at 1 - 5 kHz with the
        // voice leadOverBeatLu above the beat, after the chain changed the voice there (render vs original)
        if (profile.beatPresent && profile.beatOverVocalDb.size() == (size_t) VocalProfiler::numBands
            && m.thirdsDb.size() == (size_t) numThirds && original.thirdsDb.size() == (size_t) numThirds)
        {
            const float shift = autoeq::leadOverBeatLu - profile.vocalOverBeatLu;
            for (int k = 0; k < numThirds; ++k)
            {
                if (thirdCentre (k) < 1000.0f * 0.99f || thirdCentre (k) > 5000.0f * 1.01f)
                    continue;
                float sum = 0.0f;
                int n = 0;
                for (int bb = 12 + 4 * k - 2; bb <= 12 + 4 * k + 1; ++bb)
                    if (bb >= 0 && bb < VocalProfiler::numBands)
                    {
                        sum += profile.beatOverVocalDb[(size_t) bb];
                        ++n;
                    }
                const float change = m.thirdsDb[(size_t) k] - original.thirdsDb[(size_t) k];
                const float over = (n > 0 ? sum / (float) n : -100.0f) - shift - change;
                b.beat += 0.2f * std::max (0.0f, over - autoeq::unmaskFromDb);
            }
        }

        // ---- Stereo / mono compatibility
        b.stereo = 2.0f * window (m.monoLossDb, -1.0f, 100.0f, 1.0f) + 1.0f * window (m.correlation, 0.5f, 1.0f, 0.25f);

        // ---- Space: effect level in the gaps after words (relative to the voice). A dry
        // take has nearly nothing there; the window depends on the style.
        if (m.gapValid)
        {
            // Over a dense beat a tail has nowhere to go: 3 dB drier; a beat with gaps leaves 2 dB more room
            const auto density = beatDensityOf (profile);
            const float beatShift = density == BeatDensity::dense ? -3.0f : (density == BeatDensity::sparse ? 2.0f : 0.0f);
            const auto style = styleOf (profile);
            const float lo = (style == VocalStyle::spoken ? -40.0f : (style == VocalStyle::sustained ? -30.0f : -34.0f)) + beatShift;
            const float hi = (style == VocalStyle::spoken ? -24.0f : (style == VocalStyle::sustained ? -13.0f : -17.0f)) + beatShift;
            // Too wet blurs the words (a take that already has room keeps it); too dry only
            // costs when the plan adds space at all (a roomy take stays dry on purpose)
            const float origGap = original.gapValid ? original.gapDb : -120.0f;
            b.space = 0.5f * window (m.gapDb, -200.0f, std::max (hi, origGap + 1.0f), 3.0f);
            if (effectsPlanned)
                b.space += 0.2f * window (m.gapDb, lo, 200.0f, 3.0f);
        }

        // ---- Artifacts: peaks that the chain made stick out further than in the original
        // (peak-to-loudness growth), and the headroom at the target loudness. Both cost
        // loudness, not sound: the render is float and runs before the output stage, which
        // always turns the mix down to -1 dBFS peak, so nothing over full scale in the render
        // ever clips in the result (clip runs are reported, not scored). Measured against the
        // original, so removing energy without touching the peaks (de-essing) is no artifact.
        // Weighted mildly: at matched loudness the listening test of 2026-10-24 did not hear
        // it (the ridden hooks with +7 dB onset peaks won on intelligibility).
        const float peakAtTarget = m.peakDb + (targetLufs - m.lufs);
        const float peakGrowth = original.valid ? (m.peakDb - m.lufs) - (original.peakDb - original.lufs) : 0.0f;
        b.artifacts = 0.3f * window (peakGrowth, -100.0f, 3.0f, 2.0f)
                    + 0.25f * window (peakAtTarget, -100.0f, -1.0f, 2.0f);

        // ---- Leaving the base plan has to be worth it: that is the margin of variantEarnsChange (U4), not a cost here
        (void) isBasePlan;
        return b;
    }
}
