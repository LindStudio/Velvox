// 0.8 mix target (docs/superpowers/specs/2026-11-02-automix-08-tailored-design.md): genres, vibes,
// the third-octave grid and the target curve measured on finished party vocals.

#include "TestHelpers.h"
#include "AutoMix/MixTarget.h"
#include "AutoMix/VocalProfile.h"
#include "AutoMix/AutoMixPlanner.h"

using namespace velvox;
using namespace velvox::automix;

namespace
{
    // Mean of the target curve over the thirds whose centre lies in [lo, hi]
    float curveMean (const MixTarget& t, float lo, float hi)
    {
        float sum = 0.0f;
        int n = 0;
        for (int k = 0; k < numThirds; ++k)
            if (thirdCentre (k) >= lo * 0.99f && thirdCentre (k) <= hi * 1.01f)
            {
                sum += t.curveDb[(size_t) k];
                ++n;
            }
        return n > 0 ? sum / (float) n : 0.0f;
    }

    int thirdOf (float hz)
    {
        int best = 0;
        for (int k = 1; k < numThirds; ++k)
            if (std::abs (std::log2 (thirdCentre (k) / hz)) < std::abs (std::log2 (thirdCentre (best) / hz)))
                best = k;
        return best;
    }
}

class MixTargetTests final : public juce::UnitTest
{
public:
    MixTargetTests() : juce::UnitTest ("Headliner mix target", "Velvox") {}

    void runTest() override
    {
        beginTest ("Thirds: the grid runs from 80 Hz to 16 kHz");
        {
            expectWithinAbsoluteError (thirdCentre (0), 80.0f, 0.01f);
            expectWithinAbsoluteError (thirdCentre (23) / 16000.0f, 1.0f, 0.02f);
        }

        beginTest ("Thirds: a flat density rises 3 dB per octave (power per band), relative to the core");
        {
            const std::vector<float> flat ((size_t) VocalProfiler::numBands, 0.0f);
            const auto t = thirdsOf (flat);
            expectEquals ((int) t.size(), numThirds);
            for (int k = thirdOf (315.0f); k + 3 <= thirdOf (1600.0f); ++k)
                expectWithinAbsoluteError (t[(size_t) k + 3] - t[(size_t) k], 3.0f, 0.3f);
        }

        beginTest ("Thirds: an EQ band acts where it is");
        {
            const std::vector<float> flat ((size_t) VocalProfiler::numBands, 0.0f);
            const auto b = eqBand (dsp::EqBandType::bell, 1000.0f, 6.0f, 4.0f);
            const auto with = thirdsOf (flat, { b });
            const auto without = thirdsOf (flat);
            const int k1 = thirdOf (1000.0f), k125 = thirdOf (125.0f);
            // relative to the core: the bell also lifts the core a little - compare against the lowest third
            const float lift1k = (with[(size_t) k1] - with[(size_t) k125]) - (without[(size_t) k1] - without[(size_t) k125]);
            expect (lift1k >= 3.0f && lift1k <= 6.5f, "1 kHz lifted " + juce::String (lift1k, 2));
        }

        beginTest ("Target: party is measured, the others are estimates; spreads are sane");
        {
            expect (! targetFor (Genre::party, Vibe::punchy).estimated);
            expect (targetFor (Genre::rap, Vibe::punchy).estimated);
            for (int g = 0; g < 5; ++g)
                for (int v = 0; v < 5; ++v)
                {
                    const auto t = targetFor ((Genre) g, (Vibe) v);
                    for (auto s : t.spreadDb)
                        expect (s >= 1.5f);
                    for (auto c : t.curveDb)
                        expect (std::isfinite (c) && c > -60.0f && c < 20.0f);
                }
        }

        beginTest ("Target: vibes move the target");
        {
            const auto punchy = targetFor (Genre::party, Vibe::punchy);
            const auto aggressive = targetFor (Genre::party, Vibe::aggressive);
            const auto floaty = targetFor (Genre::party, Vibe::floaty);
            const auto warm = targetFor (Genre::party, Vibe::warm);
            const auto natural = targetFor (Genre::party, Vibe::natural);
            expect (curveMean (aggressive, 2500.0f, 4000.0f) >= curveMean (punchy, 2500.0f, 4000.0f) + 1.5f);
            expect (aggressive.dynamics.crestHi < punchy.dynamics.crestHi);
            expect (curveMean (floaty, 10000.0f, 16000.0f) >= curveMean (punchy, 10000.0f, 16000.0f) + 1.5f);
            expect (floaty.space.reverbScale > punchy.space.reverbScale);
            expect (curveMean (warm, 160.0f, 250.0f) >= curveMean (punchy, 160.0f, 250.0f) + 1.0f);
            expect (curveMean (warm, 10000.0f, 16000.0f) <= curveMean (punchy, 10000.0f, 16000.0f) - 1.5f);
            expect (natural.densityBlend < punchy.densityBlend);
            expect (natural.satMix < punchy.satMix);
        }

        beginTest ("Target: genres move the target");
        {
            const auto party = targetFor (Genre::party, Vibe::punchy);
            expect (targetFor (Genre::ballad, Vibe::punchy).dynamics.phraseHi > party.dynamics.phraseHi);
            expect (targetFor (Genre::rap, Vibe::punchy).space.reverbScale < party.space.reverbScale);
            expect (targetFor (Genre::edm, Vibe::punchy).space.reverbScale > party.space.reverbScale);
        }

        beginTest ("Target: names in both languages");
        {
            for (int g = 0; g < 5; ++g)
                expect (genreName ((Genre) g).isNotEmpty());
            for (int v = 0; v < 5; ++v)
                expect (vibeName ((Vibe) v).isNotEmpty());
        }
    }
};

static MixTargetTests mixTargetTests;
