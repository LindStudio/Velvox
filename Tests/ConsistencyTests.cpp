// AUTO MIX CONSISTENCY (plan 2026-11-04-automix-konsistenz, Task 1): the harness that compares auto-mix plans
// (fingerprints) - exact for repeats, with tolerances for level shifts and small perturbations - and the
// determinism of the engine on a synthetic vocal.

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "ConsistencyHarness.h"
#include "Parameters/EqBands.h"

using namespace vocalypse;
using namespace vocalypse::test;
using namespace vocalypse::automix;

namespace
{
    MixPlan basePlan()
    {
        MixPlan p;
        p.routing = dsp::Routing::defaults();
        p.set (pid::inputGain, 0.0f);
        p.set (pid::outputGain, 1.0f);
        p.set (pid::compOn, 1.0f);
        p.set (pid::compThreshold, -18.0f);
        p.set (pid::compRatio, 3.0f);
        p.set (pid::deessOn, 1.0f);
        p.set (pid::deessRange, 6.0f);
        p.set (pid::reverbOn, 0.0f);
        p.set (pid::reverbMix, 5.0f);
        return p;
    }

    void putBand (MixPlan& p, int slot, dsp::EqRole role, float freq, float gain)
    {
        const auto access = eqbands::accessFor (p.values);
        dsp::EqBand b;
        b.on = true;
        b.type = (int) dsp::EqBandType::bell;
        b.freq = freq;
        b.gain = gain;
        b.role = (int) role;
        eqbands::writeBand (access, 0, slot, b);
    }
}

class ConsistencyTests final : public juce::UnitTest
{
public:
    ConsistencyTests() : juce::UnitTest ("Auto mix consistency", "VOCALYPSE") {}

    void runTest() override
    {
        const Tolerances tol;

        beginTest ("A fingerprint compared with itself passes the exact check");
        {
            auto p = basePlan();
            putBand (p, 2, dsp::EqRole::presence, 3500.0f, 2.0f);
            const auto fp = fingerprint (p);
            const auto d = compare (fp, fp, Check::exact, 0.0f, tol);
            expect (d.pass);
            expectEquals (d.differing, 0);
            expect (fp.modulesOn.count ("deess#0") == 1);
            expect (fp.modulesOn.count ("reverb#0") == 0);
            expect (fp.roleBands.size() == 1);
        }

        beginTest ("A changed deessOn is a flip");
        {
            auto a = basePlan(), b = basePlan();
            b.set (pid::deessOn, 0.0f);
            const auto d = compare (fingerprint (a), fingerprint (b), Check::exact, 0.0f, tol);
            expect (! d.pass);
            expectEquals ((int) d.flips.size(), 1);
            expect (d.flips[0].contains ("deess"));
        }

        beginTest ("An EQ band moved to another slot with the same role is not a difference");
        {
            auto a = basePlan(), b = basePlan();
            putBand (a, 0, dsp::EqRole::presence, 3500.0f, 2.0f);
            putBand (b, 7, dsp::EqRole::presence, 3500.0f, 2.0f);
            expect (fingerprint (a).values == fingerprint (b).values);   // slots are not part of the values
            const auto d = compare (fingerprint (a), fingerprint (b), Check::exact, 0.0f, tol);
            expect (d.pass);
            expectEquals (d.differing, 0);

            putBand (b, 7, dsp::EqRole::presence, 3500.0f, 3.0f);        // same role, another gain: a difference
            expect (! compare (fingerprint (a), fingerprint (b), Check::exact, 0.0f, tol).pass);
        }

        beginTest ("Gain shift: 2 dB threshold shift passes at shift 2, the gain compensation is checked");
        {
            auto a = basePlan(), b = basePlan();
            b.set (pid::compThreshold, -20.0f);
            b.set (pid::inputGain, -2.0f);
            b.set (pid::outputGain, 3.0f);
            expect (compare (fingerprint (a), fingerprint (b), Check::gainShift, 2.0f, tol).pass);

            auto c = b;
            c.set (pid::inputGain, -1.0f);                                  // compensation missing
            expect (! compare (fingerprint (a), fingerprint (c), Check::gainShift, 2.0f, tol).pass);

            auto e = b;
            e.set (pid::compRatio, 4.0f);                                   // another value moved by 33 %
            expect (! compare (fingerprint (a), fingerprint (e), Check::gainShift, 2.0f, tol).pass);

            auto f = b;
            f.set (pid::deessOn, 0.0f);                                     // no module flip allowed
            expect (! compare (fingerprint (a), fingerprint (f), Check::gainShift, 2.0f, tol).pass);
        }

        beginTest ("Perturbation: flips only at low intensity, values within their tolerances");
        {
            auto a = basePlan(), b = basePlan();
            a.set (pid::reverbOn, 1.0f);                                    // reverb mix 5 % <= 8 %: may flip
            expect (compare (fingerprint (a), fingerprint (b), Check::perturbation, 0.0f, tol).pass);
            a.set (pid::reverbMix, 15.0f);                                  // 15 % > 8 %: must not
            expect (! compare (fingerprint (a), fingerprint (b), Check::perturbation, 0.0f, tol).pass);

            auto c = basePlan(), d = basePlan();
            d.set (pid::compOn, 0.0f);                                      // a compressor never flips
            expect (! compare (fingerprint (c), fingerprint (d), Check::perturbation, 0.0f, tol).pass);

            auto e = basePlan(), f = basePlan();
            f.set (pid::compThreshold, -16.5f);                             // 1.5 dB <= 2 dB
            f.set (pid::deessRange, 7.5f);
            expect (compare (fingerprint (e), fingerprint (f), Check::perturbation, 0.0f, tol).pass);
            f.set (pid::compThreshold, -15.0f);                             // 3 dB > 2 dB
            expect (! compare (fingerprint (e), fingerprint (f), Check::perturbation, 0.0f, tol).pass);

            auto g = basePlan(), h = basePlan();
            putBand (g, 0, dsp::EqRole::air, 12000.0f, 2.0f);
            putBand (h, 4, dsp::EqRole::air, 12000.0f * 1.1f, 3.0f);        // 1 dB, 0.14 octave
            expect (compare (fingerprint (g), fingerprint (h), Check::perturbation, 0.0f, tol).pass);
            putBand (h, 4, dsp::EqRole::air, 12000.0f * 1.3f, 3.0f);        // 0.38 octave
            expect (! compare (fingerprint (g), fingerprint (h), Check::perturbation, 0.0f, tol).pass);
        }

        beginTest ("Prior states: defaults and seeded random values are legal and reproducible");
        {
            const auto d = defaultValues();
            expect (d.size() > 100);
            const auto s1 = scrambledValues (1), s1b = scrambledValues (1), s2 = scrambledValues (2);
            expect (s1 == s1b);
            expect (s1 != s2);
            expect (s1.size() == d.size());
            for (auto& [id, v] : s1)
            {
                const auto* spec = findParameterSpec (id);
                expect (spec != nullptr && v >= spec->minValue - 1.0e-4f && v <= spec->maxValue + 1.0e-4f, id);
            }
        }

        beginTest ("10 runs from the default state give identical fingerprints (synthetic vocal, 16 s)");
        {
            const auto vocal = makeVocal ({});
            const auto start = defaultValues();
            std::optional<PlanFingerprint> first;
            int identical = 0;
            for (int i = 0; i < 10; ++i)
            {
                const auto r = mixFile (vocal, 0.0, false, start, MixIntent {});
                expect (r.ok, r.error);
                if (! r.ok)
                    break;
                const auto fp = fingerprint (r.plan);
                if (! first.has_value())
                    first = fp;
                const auto d = compare (*first, fp, Check::exact, 0.0f, tol);
                if (d.pass)
                    ++identical;
                else
                    logMessage ("run " + juce::String (i) + " differs: " + juce::StringArray (d.overTolerance.data(), (int) d.overTolerance.size()).joinIntoString ("; "));
            }
            expectEquals (identical, 10);
        }
    }
};

static ConsistencyTests consistencyTests;
