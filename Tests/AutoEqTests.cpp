// 0.6.1 auto-mix EQ (spec item 7): sustained resonance detection, the tonal model, the move
// planner (narrow notches, broad tonal moves, limits) and its integration into the planner and
// the engine (role bands only, Vocal Check and user bands untouched).

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "TargetHelpers.h"
#include "AutoMix/AutoMixEngine.h"
#include "AutoMix/AutoEq.h"
#include "Parameters/EqBands.h"
#include "Parameters/ParameterLayout.h"
#include "Text/I18n.h"

using namespace vocalypse;
using namespace vocalypse::test;
using namespace vocalypse::automix;

namespace
{
    automix::CapturedAudio concat (const automix::CapturedAudio& a, const automix::CapturedAudio& b)
    {
        auto out = a;
        for (size_t ch = 0; ch < out.vocal.size(); ++ch)
            out.vocal[ch].insert (out.vocal[ch].end(), b.vocal[std::min (ch, b.vocal.size() - 1)].begin(),
                                  b.vocal[std::min (ch, b.vocal.size() - 1)].end());
        return out;
    }

    bool near (float hz, float target, float octaves) { return std::abs (std::log2 (hz / target)) < octaves; }
}

class AutoEqTests final : public juce::UnitTest
{
public:
    AutoEqTests() : juce::UnitTest ("Vocalypse auto EQ", "Vocalypse") {}

    void runTest() override
    {
        analysisTests();
        modelTests();
        moveTests();
        plannerTests();
        engineTests();
    }

    void engineTests()
    {
        beginTest ("Engine: the verification moves the tone towards the target curve, within the limits, explained");
        {
            const auto cur = defaults();
            for (auto spec : { [] { VocalSpec s; s.seconds = 12.0; s.tiltDb = -5.0f; s.f0 = 120.0f; return s; }(),
                               [] { VocalSpec s; s.seconds = 12.0; s.tiltDb = 4.0f; s.sibilance = 1.6f; s.f0 = 300.0f; return s; }(),
                               [] { VocalSpec s; s.seconds = 12.0; s.resonanceHz = 1800.0f; s.resonanceDb = 16.0f; return s; }() })
            {
                const auto audio = makeVocal (spec);
                const auto r = AutoMixEngine::run (audio, cur);
                expect (r.ok, r.error);
                if (! r.ok || r.candidates.empty())
                    continue;

                // The planner's open-loop plan, rendered and scored the same way
                const auto open = AutoMixPlanner::plan (r.profile, cur);
                const auto mo = AutoMixEngine::renderChecked (audio, cur, open, r.profile);
                const auto target = AutoMixEngine::targetLufs (r.original.lufs);
                // tone and S balance: the verification trades them (a de-esser it strengthens darkens the top,
                // and with the S sounds still strong the EQ may not lift it back)
                const auto so = evaluate (mo, r.original, r.profile, target, true, true);
                const float tonalOpen = so.tonal + so.sibilance;
                const float tonalBase = r.candidates.front().score.tonal + r.candidates.front().score.sibilance;
                logMessage ("    tilt " + juce::String (spec.tiltDb, 0) + ": tonal open loop " + juce::String (tonalOpen, 2)
                            + " -> verified " + juce::String (tonalBase, 2));
                expect (tonalBase <= tonalOpen + 0.05f, "the verified plan is not further from the target curve");

                const auto bands = autoBands (r.plan, cur);
                expect ((int) bands.size() <= autoeq::maxBands, "at most 6 auto-mix bands");
                for (auto& b : bands)
                {
                    expect (autoeq::withinLimits (b), "limits: " + autoeq::titleOf (b.bandRole(), b));
                    if (b.bandRole() != dsp::EqRole::lowCut)
                        expect (std::any_of (r.plan.decisions.begin(), r.plan.decisions.end(), [&] (const Decision& d)
                                             { return d.module == dsp::ModuleId::eq && d.title == autoeq::titleOf (b.bandRole(), b); }),
                                "the overview shows " + autoeq::titleOf (b.bandRole(), b));
                }
            }
        }
    }

    static std::map<juce::String, float> defaults()
    {
        std::map<juce::String, float> v;
        for (auto& s : getParameterSpecs())
            v[s.id] = s.defaultValue;
        return v;
    }

    static void putBand (std::map<juce::String, float>& v, int band, const dsp::EqBand& b)
    {
        auto access = eqbands::accessFor (v);
        eqbands::writeBand (access, 0, band, b);
    }

    // Auto-mix bands of EQ 1 in a plan (over `current`)
    static std::vector<dsp::EqBand> autoBands (const MixPlan& plan, const std::map<juce::String, float>& current)
    {
        auto values = plan.values;
        const auto access = eqbands::accessFor (values, &current);
        std::vector<dsp::EqBand> out;
        for (int i = 0; i < dsp::maxEqBands; ++i)
        {
            const auto b = eqbands::readBand (access, 0, i);
            if (b.on && autoeq::ownedRole (b.bandRole()) && (b.isCut() || std::abs (b.gain) > 1.0e-3f))
                out.push_back (b);
        }
        return out;
    }

    void plannerTests()
    {
        const auto planOf = [] (const VocalSpec& s, const std::map<juce::String, float>& current)
        {
            return AutoMixPlanner::plan (VocalProfiler::analyse (makeVocal (s)), current);
        };
        VocalSpec res; res.seconds = 14.0; res.resonanceHz = 1800.0f; res.resonanceDb = 16.0f;

        beginTest ("Planner: a resonance gets a notch in an auto-mix role band, explained; SMOOTH stays gentle");
        {
            const auto cur = defaults();
            const auto plan = planOf (res, cur);
            const auto notch = roleBand (plan, cur, dsp::EqRole::autoNotch1);
            expect (notch.has_value() && notch->on, "notch band");
            if (notch.has_value())
            {
                expect (near (notch->freq, 1800.0f, 1.0f / 6.0f) && notch->gain <= -3.0f && notch->q >= 10.0f, "narrow cut at the resonance");
                expect (autoeq::withinLimits (*notch));
            }
            expect (std::any_of (plan.decisions.begin(), plan.decisions.end(), [] (const Decision& d)
                                 { return d.module == dsp::ModuleId::eq && i18n::show (d.title).startsWith ("Resonanz"); }), "the overview names it");
            expect (std::any_of (plan.decisions.begin(), plan.decisions.end(), [] (const Decision& d)
                                 { return d.module == dsp::ModuleId::eq && i18n::show (d.title, i18n::Language::english).startsWith ("Resonance")
                                          && i18n::show (d.title, i18n::Language::english).contains ("narrow"); }), "and in English");
            // 2026-10-29: SMOOTH runs on every vocal as basic care; a resonance the EQ already notches does not raise it
            expect (plan.on (pid::smoothOn) && std::abs (plan.get (pid::smoothAmount) - 20.0f) < 0.01f,
                    "SMOOTH only gently (20 %) for a resonance the EQ already notches (not harsh)");
            for (auto& b : autoBands (plan, cur))
                expect (autoeq::withinLimits (b), "limits: " + autoeq::titleOf (b.bandRole(), b));
            expect ((int) autoBands (plan, cur).size() <= autoeq::maxBands, "at most 6 auto-mix bands");
        }

        beginTest ("Planner: a re-run clears the notches of an earlier run");
        {
            auto cur = defaults();
            for (auto& [id, v] : planOf (res, defaults()).values)
                cur[id] = v;
            VocalSpec clean; clean.seconds = 14.0;
            const auto plan = planOf (clean, cur);
            const auto notch = roleBand (plan, cur, dsp::EqRole::autoNotch1);
            expect (! notch.has_value() || ! notch->on || std::abs (notch->gain) < 1.0e-3f, "old notch switched off");
        }

        // Task 3 (U5): the plan starts from the defaults, so bands in the live state (Vocal Check's, the user's) are
        // neither kept nor counted: they are overwritten with the defaults - every auto mix gives the same EQ
        beginTest ("Planner: bands in the live state (Vocal Check, user) do not reach the plan; it holds every band at its default or its own value");
        {
            auto cur = defaults();
            dsp::EqBand check = band (dsp::EqBandType::bell, 1800.0f, -8.0f, 6.0f);
            check.role = (int) dsp::EqRole::presence;
            putBand (cur, 0, check);
            dsp::EqBand user = band (dsp::EqBandType::bell, 600.0f, 2.0f, 1.0f);
            putBand (cur, 1, user);   // role none
            const auto plan = planOf (res, cur);
            const auto fresh = planOf (res, defaults());
            expect (plan.values == fresh.values, "the same plan as from the defaults");
            for (int b : { 0, 1 })
                for (auto* f : { pid::eqband::on, pid::eqband::type, pid::eqband::freq, pid::eqband::gain, pid::eqband::q, pid::eqband::role })
                    expect (plan.values.count (eqbands::bandParamId (0, b, f)) == 1, "band " + juce::String (b) + " written: " + f);
            const auto notch = roleBand (plan, cur, dsp::EqRole::autoNotch1);
            expect (notch.has_value() && notch->on, "the resonance gets its own notch (the old Vocal Check cut is gone)");

            // All 16 slots filled in the live state: the plan still has its bands
            auto full = defaults();
            for (int b = 0; b < dsp::maxEqBands; ++b)
                putBand (full, b, band (dsp::EqBandType::bell, 200.0f * (float) (b + 1), 0.5f, 1.0f));
            VocalSpec dark; dark.seconds = 14.0; dark.tiltDb = -5.0f; dark.f0 = 120.0f;
            const auto pf = planOf (dark, full);
            expect (pf.values == planOf (dark, defaults()).values, "full live EQ: the same plan as from the defaults");
            expect (pf.routing.isValid());
        }

        beginTest ("Planner: tone moves stay within the limits on every kind of take");
        {
            std::vector<VocalSpec> takes;
            { VocalSpec s; s.seconds = 14.0; takes.push_back (s); }
            { VocalSpec s; s.seconds = 14.0; s.tiltDb = -5.0f; s.f0 = 120.0f; takes.push_back (s); }
            { VocalSpec s; s.seconds = 14.0; s.tiltDb = 4.0f; s.sibilance = 1.6f; s.f0 = 300.0f; takes.push_back (s); }
            { VocalSpec s = res; s.tiltDb = -3.0f; takes.push_back (s); }
            { VocalSpec s; s.seconds = 6.0; s.resonanceHz = 900.0f; s.resonanceDb = 18.0f; takes.push_back (s); }
            const auto cur = defaults();
            for (auto& s : takes)
            {
                const auto plan = planOf (s, cur);
                const auto bands = autoBands (plan, cur);
                expect ((int) bands.size() <= autoeq::maxBands, "at most 6 auto-mix bands: " + juce::String ((int) bands.size()));
                for (auto& b : bands)
                    expect (autoeq::withinLimits (b), "limits: " + autoeq::titleOf (b.bandRole(), b));
            }
        }
    }

    static dsp::EqBand band (dsp::EqBandType t, float f, float g, float q)
    {
        dsp::EqBand b;
        b.on = true; b.type = (int) t; b.freq = f; b.gain = g; b.q = q;
        return b;
    }

    void modelTests()
    {
        beginTest ("Tonal model: a shelf moves the top where there is content, not on a band-limited take");
        {
            std::vector<float> flat ((size_t) VocalProfiler::numBands, -40.0f);
            const auto l0 = autoeq::levelsOf (flat, {});
            const auto l1 = autoeq::levelsOf (flat, { band (dsp::EqBandType::highShelf, 3000.0f, 3.0f, 0.7071f) });
            expect (l1.top - l0.top > 2.0f && l1.top - l0.top < 3.1f, "3 kHz shelf +3 dB: top +" + juce::String (l1.top - l0.top, 2));
            expectWithinAbsoluteError (l1.body, l0.body, 0.3f, "body nearly unchanged");

            auto limited = flat;
            for (int b = 0; b < VocalProfiler::numBands; ++b)
                if (VocalProfiler::bandCentre (b) > 8000.0f)
                    limited[(size_t) b] = -200.0f;
            const auto m0 = autoeq::levelsOf (limited, {});
            const auto m1 = autoeq::levelsOf (limited, { band (dsp::EqBandType::highShelf, 9000.0f, 3.0f, 0.7071f) });
            expect (m1.top - m0.top < 0.5f, "9 kHz shelf on a take without content above 8 kHz: " + juce::String (m1.top - m0.top, 2));

            // The model reads the same band levels as the profiler
            for (float tilt : { -5.0f, 0.0f, 4.0f })
            {
                VocalSpec s; s.seconds = 12.0; s.tiltDb = tilt;
                const auto p = VocalProfiler::analyse (makeVocal (s));
                const auto l = autoeq::levelsOf (p.spectrumDb, {});
                const juce::String t = " (tilt " + juce::String (tilt, 0) + ")";
                expectWithinAbsoluteError (l.body, p.bodyDb, 1.0f, "body" + t);
                expectWithinAbsoluteError (l.lowMid, p.lowMidDb, 1.0f, "low mids" + t);
                expectWithinAbsoluteError (l.presence, p.presenceDb, 1.0f, "presence" + t);
                expectWithinAbsoluteError (l.top, p.topDb, 1.0f, "top" + t);
                expectWithinAbsoluteError (l.air, p.airDb, 1.5f, "air" + t);
            }

            const auto disabled = [] { auto b = band (dsp::EqBandType::bell, 1000.0f, 12.0f, 1.0f); b.on = false; return b; }();
            expectWithinAbsoluteError (autoeq::levelsOf (flat, { disabled }).top, l0.top, 1.0e-4f, "bands that are off do nothing");
        }
    }

    // The profile of a synthetic take, with optional changes to the long-term spectrum
    static VocalSpec clean14() { VocalSpec s; s.seconds = 14.0; return s; }

    static VocalProfile profileOf (const VocalSpec& s) { return VocalProfiler::analyse (makeVocal (s)); }

    static void addBump (VocalProfile& p, float lo, float hi, float db)
    {
        for (int b = 0; b < VocalProfiler::numBands; ++b)
        {
            const float c = VocalProfiler::bandCentre (b);
            if (c >= lo && c < hi && p.spectrumDb[(size_t) b] > -190.0f)
                p.spectrumDb[(size_t) b] += db;
        }
    }

    void checkLimits (const std::vector<autoeq::Move>& moves, int budget, const juce::String& what)
    {
        expect ((int) moves.size() <= budget, what + ": within the budget");
        int notches = 0;
        for (auto& m : moves)
        {
            expect (autoeq::withinLimits (m.band), what + ": within the limits: " + m.title);
            expect (autoeq::ownedRole (m.role), what + ": an auto-mix role");
            if (m.narrow)
            {
                ++notches;
                expect (m.band.gain < 0.0f && m.band.q >= 10.0f && m.band.q <= 16.0f, what + ": narrow = cut, Q 10-16");
            }
            else
            {
                expect (m.band.q < 10.0f, what + ": broad");
            }
            expect (m.title.isNotEmpty() && m.reason.isNotEmpty(), what + ": explained");
        }
        expect (notches <= autoeq::maxNotches);
    }

    void moveTests()
    {
        const auto ctxFor = [] (const VocalProfile& p)
        {
            autoeq::Context c;
            c.profile = &p;
            c.harsh = p.hasIssue ("harsh");
            c.sibilant = p.hasIssue ("sibilance");
            c.noisy = p.snrValid && p.snrDb < 28.0f;
            return c;
        };
        const auto find = [] (const std::vector<autoeq::Move>& moves, dsp::EqRole role) -> const autoeq::Move*
        {
            for (auto& m : moves)
                if (m.role == role)
                    return &m;
            return nullptr;
        };

        beginTest ("Auto EQ moves: a sustained resonance gets a narrow cut, never a narrow boost");
        {
            VocalSpec s; s.seconds = 14.0; s.resonanceHz = 1800.0f; s.resonanceDb = 16.0f;
            const auto p = profileOf (s);
            const auto moves = autoeq::planCurveMoves (ctxFor (p));
            checkLimits (moves, autoeq::maxBands - 1, "resonance");
            const auto* n = find (moves, dsp::EqRole::autoNotch1);
            expect (n != nullptr, "notch planned");
            if (n != nullptr)
            {
                expect (near (n->band.freq, 1800.0f, 1.0f / 6.0f), "at the resonance: " + juce::String (n->band.freq));
                expect (n->band.gain <= -3.0f && n->band.gain >= -9.0f, "depth " + juce::String (n->band.gain));
                expect (n->band.bandType() == dsp::EqBandType::bell && n->narrow);
            }

            // A kept band (Vocal Check / user) that already cuts there: no second cut on top
            auto ctx = ctxFor (p);
            ctx.fixed.push_back (band (dsp::EqBandType::bell, 1800.0f, -10.0f, 8.0f));
            expect (find (autoeq::planCurveMoves (ctx), dsp::EqRole::autoNotch1) == nullptr, "no notch where a kept band already cuts");

            // Conservative: gentler
            auto cons = ctxFor (p);
            cons.conservative = true;
            const auto* nc = find (autoeq::planCurveMoves (cons), dsp::EqRole::autoNotch1);
            expect (n == nullptr || nc == nullptr || nc->band.gain > n->band.gain, "conservative cuts less");

            VocalSpec clean; clean.seconds = 14.0;
            const auto pc = profileOf (clean);
            for (auto& m : autoeq::planCurveMoves (ctxFor (pc)))
                expect (! m.narrow, "a clean take gets no notch: " + m.title);
        }

        // ---- 0.8: curve fit against the genre / vibe target (thirds, MixTarget)
        const auto party = targetFor (Genre::party, Vibe::punchy);
        const auto curveCtx = [&ctxFor] (const VocalProfile& p, const MixTarget& t)
        {
            auto c = ctxFor (p);
            c.target = &t;
            return c;
        };
        const auto modelAt = [] (const std::vector<autoeq::Move>& moves, float hz)
        {
            std::vector<dsp::EqBand> bands;
            for (auto& m : moves)
                bands.push_back (m.band);
            return autoeq::responseDb (bands, hz);
        };
        const auto titles = [] (const std::vector<autoeq::Move>& moves)
        {
            juce::String all;
            for (auto& m : moves)
                all << i18n::show (m.title, i18n::Language::german) << "; ";
            return all;
        };
        const auto broadOf = [] (const std::vector<autoeq::Move>& moves)
        {
            std::vector<autoeq::Move> out;
            for (auto& m : moves)
                if (! m.narrow)
                    out.push_back (m);
            return out;
        };

        beginTest ("Curve fit: a mud bump gets a cut where it is");
        {
            auto p = onTarget (profileOf (clean14()), party, [] (float hz) { return hz > 250.0f && hz < 480.0f ? 5.0f : 0.0f; });
            const auto moves = broadOf (autoeq::planCurveMoves (curveCtx (p, party)));
            logMessage ("    mud: " + titles (moves));
            checkLimits (moves, autoeq::maxBands - 1, "mud");
            const auto it = std::find_if (moves.begin(), moves.end(), [] (const autoeq::Move& m) { return m.band.freq >= 280.0f && m.band.freq <= 440.0f; });
            expect (it != moves.end(), "a move at the bump: " + titles (moves));
            if (it != moves.end())
            {
                expect (it->band.gain <= -2.0f && it->band.gain >= -5.0f, "cut " + juce::String (it->band.gain, 1));
                expect (i18n::show (it->title, i18n::Language::german).startsWith ("Dröhnen"), "named: " + it->title);
                expect (i18n::show (it->title, i18n::Language::english).startsWith ("Boomy"), "named: " + it->title);
            }
        }

        beginTest ("Curve fit: missing presence and air are lifted where they lack");
        {
            auto p = onTarget (profileOf (clean14()), party, [] (float hz) { return hz >= 2400.0f ? -5.0f : 0.0f; });
            const auto moves = broadOf (autoeq::planCurveMoves (curveCtx (p, party)));
            logMessage ("    dull: " + titles (moves));
            checkLimits (moves, autoeq::maxBands - 1, "dull");
            for (auto& m : moves)
                expect (m.band.gain <= 0.0f || m.band.freq >= 2000.0f, "lifts only up there: " + m.title);
            const float at4k = modelAt (moves, 4000.0f);
            expect (at4k >= 2.0f && at4k <= 4.0f, "4 kHz lifted " + juce::String (at4k, 1));
        }

        beginTest ("Curve fit: a clean dark take gets real brilliance");
        {
            // Bench 2026-11-02: dry takes lie 8-17 dB under finished party vocals above 4 kHz - a clean one gets up to 6 dB there
            auto p = onTarget (profileOf (clean14()), party, [] (float hz) { return hz >= 5000.0f ? -10.0f : 0.0f; });
            const auto moves = autoeq::planCurveMoves (curveCtx (p, party));
            logMessage ("    clean dark: " + titles (moves));
            expect (modelAt (moves, 10000.0f) >= 4.0f, "10 kHz lifted " + juce::String (modelAt (moves, 10000.0f), 1));
            for (auto& m : moves)
                expect (autoeq::withinLimits (m.band) && m.band.gain <= 6.0f, m.title);
            auto ctx = curveCtx (p, party);
            ctx.sibilant = true;
            expect (modelAt (autoeq::planCurveMoves (ctx), 10000.0f) <= 4.0f + 1.0e-3f, "sibilant: at most 4 dB up there");
        }

        beginTest ("Curve fit: a notched resonance does not block the lift around it");
        {
            auto p = onTarget (profileOf (clean14()), party, [] (float hz) { return hz >= 5000.0f ? -10.0f : 0.0f; });
            Resonance r;
            r.hz = 7200.0f; r.excessDb = 12.0f; r.strong = true; r.persistence = 1.0f; r.sustained = true; r.q = 14.0f;
            p.resonances.push_back (r);
            const auto moves = autoeq::planCurveMoves (curveCtx (p, party));
            logMessage ("    notched + dark: " + titles (moves));
            expect (std::any_of (moves.begin(), moves.end(), [] (const autoeq::Move& m) { return m.narrow && std::abs (m.band.freq - 7200.0f) < 1.0f; }), "the notch");
            expect (modelAt (moves, 11000.0f) >= 3.0f, "and the brilliance above it: " + juce::String (modelAt (moves, 11000.0f), 1));
        }

        beginTest ("Curve fit: a voice on the target is left alone");
        {
            juce::Random r (7);
            auto p = onTarget (profileOf (clean14()), party, [&r] (float) { return r.nextFloat() - 0.5f; });
            const auto moves = broadOf (autoeq::planCurveMoves (curveCtx (p, party)));
            logMessage ("    on target: " + titles (moves));
            expect (moves.size() <= 1, "at most one broad move: " + titles (moves));
            for (auto& m : moves)
                expect (std::abs (m.band.gain) <= 1.5f, "small: " + m.title);
        }

        beginTest ("Curve fit: a noisy dark take is lifted at most 2 dB above 5 kHz");
        {
            auto p = onTarget (profileOf (clean14()), party, [] (float hz) { return hz >= 5000.0f ? -8.0f : 0.0f; });
            auto ctx = curveCtx (p, party);
            ctx.noisy = true;
            const auto moves = autoeq::planCurveMoves (ctx);
            logMessage ("    noisy dark: " + titles (moves));
            expect (modelAt (moves, 8000.0f) <= 2.0f + 1.0e-3f && modelAt (moves, 12000.0f) <= 2.0f + 1.0e-3f, "8 / 12 kHz: "
                    + juce::String (modelAt (moves, 8000.0f), 1) + " / " + juce::String (modelAt (moves, 12000.0f), 1));
        }

        beginTest ("Curve fit: different voices get different EQs");
        {
            VocalSpec dark; dark.seconds = 14.0; dark.tiltDb = -5.0f; dark.f0 = 120.0f;
            VocalSpec bright; bright.seconds = 14.0; bright.tiltDb = 4.0f; bright.f0 = 300.0f;
            const auto nasal = onTarget (profileOf (clean14()), party, [] (float hz) { return hz >= 900.0f && hz <= 1400.0f ? 6.0f : 0.0f; });
            const std::vector<VocalProfile> voices { profileOf (dark), profileOf (bright), nasal };
            std::vector<std::vector<autoeq::Move>> fits;
            for (auto& v : voices)
            {
                fits.push_back (broadOf (autoeq::planCurveMoves (curveCtx (v, party))));
                logMessage ("    " + titles (fits.back()));
                checkLimits (fits.back(), autoeq::maxBands - 1, "voice");
            }
            const auto differ = [] (const std::vector<autoeq::Move>& a, const std::vector<autoeq::Move>& b)
            {
                for (auto& x : a)
                {
                    bool matched = false;
                    for (auto& y : b)
                        matched = matched || (std::abs (std::log2 (x.band.freq / y.band.freq)) < 1.0f / 3.0f && (x.band.gain > 0.0f) == (y.band.gain > 0.0f));
                    if (! matched)
                        return true;
                }
                return a.size() != b.size();
            };
            expect (differ (fits[0], fits[1]) && differ (fits[1], fits[2]) && differ (fits[0], fits[2]), "no fixed pattern");
            // the nasal one is cut in its nasal range
            expect (std::any_of (fits[2].begin(), fits[2].end(), [] (const autoeq::Move& m)
                                 { return m.band.gain < 0.0f && m.band.freq >= 800.0f && m.band.freq <= 1600.0f; }), "nasal cut");
        }

        beginTest ("Curve fit: the vibe moves the result");
        {
            const auto p = onTarget (profileOf (clean14()), party, [] (float) { return 0.0f; });
            const auto warm = targetFor (Genre::party, Vibe::warm), aggressive = targetFor (Genre::party, Vibe::aggressive);
            const auto a = autoeq::planCurveMoves (curveCtx (p, warm)), b = autoeq::planCurveMoves (curveCtx (p, aggressive));
            logMessage ("    warm: " + titles (a) + " | aggressive: " + titles (b));
            const auto bite = [&modelAt] (const std::vector<autoeq::Move>& m)
            {
                return (modelAt (m, 2520.0f) + modelAt (m, 3175.0f) + modelAt (m, 4000.0f)) / 3.0f;
            };
            expect (bite (b) >= bite (a) + 1.0f, "aggressive has more 2.5 - 4 kHz: " + juce::String (bite (b), 1) + " vs " + juce::String (bite (a), 1));
        }

        beginTest ("Curve fit: limits and budget");
        {
            VocalSpec s; s.seconds = 14.0; s.resonanceHz = 1800.0f; s.resonanceDb = 16.0f; s.tiltDb = -5.0f; s.f0 = 120.0f;
            auto p = profileOf (s);
            addBump (p, 220.0f, 520.0f, 9.0f);
            for (int budget : { 0, 1, 2, 5, 7 })
            {
                auto ctx = curveCtx (p, party);
                ctx.budget = budget;
                checkLimits (autoeq::planCurveMoves (ctx), budget, "budget " + juce::String (budget));
            }
            expectEquals (autoeq::maxBands, 8);
        }

        beginTest ("Closed loop: the render decides what is still off the target curve");
        {
            const auto p = onTarget (profileOf (clean14()), party, [] (float) { return 0.0f; });
            auto ctx = curveCtx (p, party);
            const auto curve = [&party] (const std::function<float (float)>& delta)
            {
                std::vector<float> t ((size_t) numThirds);
                for (int k = 0; k < numThirds; ++k)
                    t[(size_t) k] = party.curveDb[(size_t) k] + delta (thirdCentre (k));
                return t;
            };
            expect (autoeq::correctCurve (curve ([] (float) { return 0.0f; }), ctx, {}).empty(), "on target: nothing to correct");

            const auto dull = curve ([] (float hz) { return hz >= 2400.0f ? -5.0f : 0.0f; });
            const auto c1 = autoeq::correctCurve (dull, ctx, {});
            logMessage ("    dull render: " + titles (c1));
            checkLimits (c1, autoeq::maxBands - 1, "correction");
            expect (std::any_of (c1.begin(), c1.end(), [] (const autoeq::Move& m) { return m.band.gain > 0.0f && m.band.freq >= 2000.0f; }), "lift up there");

            const auto muddy = curve ([] (float hz) { return hz > 250.0f && hz < 480.0f ? 5.0f : 0.0f; });
            autoeq::Move planned { dsp::EqRole::autoMix, band (dsp::EqBandType::bell, 320.0f, -2.0f, 2.0f), "x", "y", false };
            const auto c2 = autoeq::correctCurve (muddy, ctx, { planned });
            logMessage ("    muddy render: " + titles (c2));
            const auto* mid = find (c2, dsp::EqRole::autoMix);
            expect (mid != nullptr && mid->band.gain < -2.0f && mid->band.gain >= -autoeq::maxBroadCutDb, "deeper cut");
            expect (mid == nullptr || std::abs (mid->band.freq - 320.0f) < 0.01f, "at the planned frequency");

            auto none = ctx;
            none.budget = 0;
            expect (autoeq::correctCurve (dull, none, {}).empty(), "no budget: no new band");

            const auto bright = curve ([] (float hz) { return hz >= 7000.0f ? 4.0f : 0.0f; });
            autoeq::Move lift { dsp::EqRole::air, band (dsp::EqBandType::highShelf, 9000.0f, 3.0f, 0.7071f), "x", "y", false };
            const auto c3 = autoeq::correctCurve (bright, ctx, { lift });
            const auto* a3 = find (c3, dsp::EqRole::air);
            expect (a3 != nullptr && a3->band.gain < 3.0f, "overshoot -> less lift");
        }

        beginTest ("Objective: the tonal term is the distance to the target curve");
        {
            const auto p = onTarget (profileOf (clean14()), party, [] (float) { return 0.0f; });
            RenderMetrics m;
            m.valid = true;
            m.lufs = -18.0f;
            m.peakDb = -6.0f;
            m.crestDb = 9.0f;
            m.thirdsDb = p.thirdsDb;
            const auto shifted = [&] (float dbAbove2k)
            {
                auto x = m;
                for (int k = 0; k < numThirds; ++k)
                    if (thirdCentre (k) >= 2000.0f)
                        x.thirdsDb[(size_t) k] += dbAbove2k;
                return evaluate (x, m, p, -18.0f, true, false, party).tonal;
            };
            expectWithinAbsoluteError (evaluate (m, m, p, -18.0f, true, false, party).tonal, 0.0f, 0.05f);
            expect (shifted (4.0f) > shifted (2.0f) && shifted (2.0f) >= 0.0f && shifted (4.0f) > 0.1f,
                    "+2 / +4: " + juce::String (shifted (2.0f), 2) + " / " + juce::String (shifted (4.0f), 2));
        }

        beginTest ("Objective: a beat that covers the words costs");
        {
            auto p = onTarget (profileOf (clean14()), party, [] (float) { return 0.0f; });
            RenderMetrics m;
            m.valid = true;
            m.lufs = -18.0f;
            m.peakDb = -6.0f;
            m.crestDb = 9.0f;
            m.thirdsDb = p.thirdsDb;
            expectEquals (evaluate (m, m, p, -18.0f, true, false, party).beat, 0.0f);
            p.beatPresent = true;
            p.vocalOverBeatLu = autoeq::leadOverBeatLu;
            p.beatOverVocalDb.assign ((size_t) VocalProfiler::numBands, -10.0f);
            for (int b = 0; b < VocalProfiler::numBands; ++b)
                if (VocalProfiler::bandCentre (b) >= 2000.0f && VocalProfiler::bandCentre (b) <= 4000.0f)
                    p.beatOverVocalDb[(size_t) b] = 8.0f;
            const auto covered = evaluate (m, m, p, -18.0f, true, false, party);
            expect (covered.beat > 0.0f, "covered: " + covered.describe());
            // lifting the voice there helps
            auto lifted = m;
            for (int k = 0; k < numThirds; ++k)
                if (thirdCentre (k) >= 2000.0f && thirdCentre (k) <= 4000.0f)
                    lifted.thirdsDb[(size_t) k] += 3.0f;
            expect (evaluate (lifted, m, p, -18.0f, true, false, party).beat < covered.beat);
        }

        beginTest ("Curve fit: budget");
        {
            // Budget: never more moves than allowed
            {
                VocalSpec s; s.seconds = 14.0; s.resonanceHz = 1800.0f; s.resonanceDb = 16.0f; s.tiltDb = -5.0f; s.f0 = 120.0f;
                auto p = profileOf (s);
                addBump (p, 220.0f, 520.0f, 9.0f);
                for (int budget : { 0, 1, 2, 5 })
                {
                    auto ctx = ctxFor (p);
                    ctx.budget = budget;
                    checkLimits (autoeq::planCurveMoves (ctx), budget, "budget " + juce::String (budget));
                }
            }
        }
    }

    void analysisTests()
    {
        beginTest ("Analysis: a fixed resonance is sustained and narrow; a peak in one part of the take is not");
        {
            VocalSpec s; s.seconds = 14.0; s.resonanceHz = 1800.0f; s.resonanceDb = 16.0f;
            const auto p = VocalProfiler::analyse (makeVocal (s));
            const Resonance* found = nullptr;
            for (auto& r : p.resonances)
                if (near (r.hz, 1800.0f, 1.0f / 6.0f))
                    found = &r;
            expect (found != nullptr, "resonance at 1.8 kHz found");
            if (found != nullptr)
            {
                expect (found->sustained, "sustained: persistence " + juce::String (found->persistence, 2));
                expect (found->q >= 10.0f && found->q <= 16.0f, "narrow Q " + juce::String (found->q, 1));
            }

            VocalSpec part = s; part.seconds = 4.0; part.resonanceDb = 22.0f;
            VocalSpec rest; rest.seconds = 12.0; rest.seed = 7;
            const auto pp = VocalProfiler::analyse (concat (makeVocal (part), makeVocal (rest)));
            bool seen = false;
            for (auto& r : pp.resonances)
                seen = seen || near (r.hz, 1800.0f, 1.0f / 6.0f);
            expect (seen, "the long-term spectrum still shows the peak (the persistence decides)");
            for (auto& r : pp.resonances)
                if (near (r.hz, 1800.0f, 1.0f / 6.0f))
                    expect (! r.sustained, "only in the first quarter: not sustained (persistence " + juce::String (r.persistence, 2) + ")");

            VocalSpec clean; clean.seconds = 14.0;
            for (auto& r : VocalProfiler::analyse (makeVocal (clean)).resonances)
                expect (! r.sustained || r.excessDb < 8.0f, "a clean take has no strong sustained resonance: " + juce::String (r.hz));
        }
    }
};

static AutoEqTests autoEqTests;
