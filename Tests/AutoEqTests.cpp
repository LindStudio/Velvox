// 0.6.1 auto-mix EQ (spec item 7): sustained resonance detection, the tonal model, the move
// planner (narrow notches, broad tonal moves, limits) and its integration into the planner and
// the engine (role bands only, Vocal Check and user bands untouched).

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "AutoMix/AutoMixEngine.h"
#include "AutoMix/AutoEq.h"
#include "Parameters/EqBands.h"
#include "Parameters/ParameterLayout.h"
#include "Text/I18n.h"

using namespace velvox;
using namespace velvox::test;
using namespace velvox::automix;

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
    AutoEqTests() : juce::UnitTest ("Velvox auto EQ", "Velvox") {}

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
                const float tonalOpen = evaluate (mo, r.original, r.profile, target, true, true).tonal;
                const float tonalBase = r.candidates.front().score.tonal;
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

        beginTest ("Planner: a resonance gets a notch in an auto-mix role band, explained, without SMOOTH");
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
            expect (! plan.on (pid::smoothOn), "SMOOTH is not switched on for a resonance the EQ already notches (not harsh)");
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

        beginTest ("Planner: Vocal Check and user bands are kept and counted, never written");
        {
            // Vocal Check already cut the resonance (presence role bell)
            auto cur = defaults();
            dsp::EqBand check = band (dsp::EqBandType::bell, 1800.0f, -8.0f, 6.0f);
            check.role = (int) dsp::EqRole::presence;
            putBand (cur, 0, check);
            dsp::EqBand user = band (dsp::EqBandType::bell, 600.0f, 2.0f, 1.0f);
            putBand (cur, 1, user);   // role none
            const auto plan = planOf (res, cur);
            for (int b : { 0, 1 })
                for (auto* f : { pid::eqband::on, pid::eqband::type, pid::eqband::freq, pid::eqband::gain, pid::eqband::q, pid::eqband::role })
                    expect (plan.values.count (eqbands::bandParamId (0, b, f)) == 0, "kept band " + juce::String (b) + " not written: " + f);
            expect (! roleBand (plan, cur, dsp::EqRole::autoNotch1).has_value() || ! roleBand (plan, cur, dsp::EqRole::autoNotch1)->on,
                    "no second notch on the Vocal Check cut");

            // All 16 slots are user bands: nothing is written, the plan stays valid
            auto full = defaults();
            for (int b = 0; b < dsp::maxEqBands; ++b)
                putBand (full, b, band (dsp::EqBandType::bell, 200.0f * (float) (b + 1), 0.5f, 1.0f));
            VocalSpec dark; dark.seconds = 14.0; dark.tiltDb = -5.0f; dark.f0 = 120.0f;
            const auto pf = planOf (dark, full);
            for (int b = 0; b < dsp::maxEqBands; ++b)
                expect (pf.values.count (eqbands::bandParamId (0, b, pid::eqband::gain)) == 0, "user band untouched");
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
            const auto moves = autoeq::planMoves (ctxFor (p));
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
            expect (find (autoeq::planMoves (ctx), dsp::EqRole::autoNotch1) == nullptr, "no notch where a kept band already cuts");

            // Conservative: gentler
            auto cons = ctxFor (p);
            cons.conservative = true;
            const auto* nc = find (autoeq::planMoves (cons), dsp::EqRole::autoNotch1);
            expect (n == nullptr || nc == nullptr || nc->band.gain > n->band.gain, "conservative cuts less");

            VocalSpec clean; clean.seconds = 14.0;
            const auto pc = profileOf (clean);
            for (auto& m : autoeq::planMoves (ctxFor (pc)))
                expect (! m.narrow, "a clean take gets no notch: " + m.title);
        }

        beginTest ("Auto EQ moves: broad moves follow the target curve");
        {
            VocalSpec clean; clean.seconds = 14.0;
            const auto base = profileOf (clean);
            logMessage ("    clean take: body " + juce::String (base.bodyDb, 1) + ", low mids " + juce::String (base.lowMidDb, 1)
                        + ", presence " + juce::String (base.presenceDb, 1) + ", top " + juce::String (base.topDb, 1) + ", air " + juce::String (base.airDb, 1));

            // Muddy: low mids far over the window -> broad cut there
            {
                auto p = base;
                addBump (p, 220.0f, 520.0f, 9.0f);
                const auto l = autoeq::levelsOf (p.spectrumDb, {});
                expect (l.lowMid > 0.0f, "the test take is muddy: " + juce::String (l.lowMid, 1));
                const auto moves = autoeq::planMoves (ctxFor (p));
                checkLimits (moves, autoeq::maxBands - 1, "muddy");
                const auto* m = find (moves, dsp::EqRole::autoMix);
                juce::String all;
                for (auto& mv : moves) all << mv.title << "; ";
                expect (m != nullptr && m->band.gain < 0.0f && m->band.freq >= 200.0f && m->band.freq <= 500.0f, "low-mid cut: " + all);
            }

            // Dark: lift the highs (+ presence) - never beyond +4 dB broad
            {
                VocalSpec d; d.seconds = 14.0; d.tiltDb = -5.0f; d.f0 = 120.0f;
                const auto p = profileOf (d);
                const auto moves = autoeq::planMoves (ctxFor (p));
                checkLimits (moves, autoeq::maxBands - 1, "dark");
                const auto* air = find (moves, dsp::EqRole::air);
                expect (air != nullptr && air->band.gain > 1.0f && air->band.bandType() == dsp::EqBandType::highShelf, "high shelf lift");
                std::vector<dsp::EqBand> bands;
                for (auto& m : moves) bands.push_back (m.band);
                expect (autoeq::levelsOf (p.spectrumDb, bands).top > autoeq::levelsOf (p.spectrumDb, {}).top + 1.0f, "the model's top rises");
            }

            // Band-limited dark take: the shelf goes where it can act
            {
                VocalSpec d; d.seconds = 14.0; d.tiltDb = -5.0f; d.f0 = 120.0f;
                auto p = profileOf (d);
                for (int b = 0; b < VocalProfiler::numBands; ++b)
                    if (VocalProfiler::bandCentre (b) > 7000.0f)
                        p.spectrumDb[(size_t) b] = -200.0f;
                const auto moves = autoeq::planMoves (ctxFor (p));
                const auto* air = find (moves, dsp::EqRole::air);
                expect (air != nullptr && air->band.freq < 6000.0f, "lo-fi take: shelf below the missing top end");
            }

            // Bright: a broad cut, never a lift of the highs
            {
                VocalSpec b; b.seconds = 14.0; b.tiltDb = 4.0f; b.f0 = 300.0f;
                auto p = profileOf (b);
                const auto moves = autoeq::planMoves (ctxFor (p));
                checkLimits (moves, autoeq::maxBands - 1, "bright");
                for (auto& m : moves)
                    if (m.role == dsp::EqRole::air || m.role == dsp::EqRole::autoTone)
                        expect (m.band.gain < 0.0f, "bright: highs only cut: " + m.title);
            }

            // Closed loop: the render decides what is still outside the target curve
            {
                VocalSpec s; s.seconds = 14.0;
                const auto p = profileOf (s);
                auto ctx = ctxFor (p);
                autoeq::Levels inside; inside.body = -5.0f; inside.lowMid = -3.0f; inside.presence = -10.0f; inside.top = -11.0f; inside.air = -24.0f;
                expect (autoeq::correct (inside, ctx, {}).empty(), "inside the windows: nothing to correct");

                auto dull = inside; dull.top = -20.0f;
                const auto c1 = autoeq::correct (dull, ctx, {});
                const auto* air = find (c1, dsp::EqRole::air);
                expect (air != nullptr && air->band.gain >= 1.0f && air->band.gain <= 4.0f, "dull render -> top shelf lift");
                checkLimits (c1, 5, "correction");

                auto muddy = inside; muddy.lowMid = 3.0f;
                autoeq::Move planned { dsp::EqRole::autoMix, band (dsp::EqBandType::bell, 320.0f, -2.0f, 1.2f), "x", "y", false };
                const auto c2 = autoeq::correct (muddy, ctx, { planned });
                const auto* mid = find (c2, dsp::EqRole::autoMix);
                expect (mid != nullptr && mid->band.gain < -2.0f && mid->band.gain >= -4.0f, "muddy render -> deeper low-mid cut");
                expect (mid == nullptr || std::abs (mid->band.freq - 320.0f) < 0.01f, "at the planned frequency");

                autoeq::Move limit { dsp::EqRole::autoMix, band (dsp::EqBandType::bell, 320.0f, -4.0f, 1.2f), "x", "y", false };
                expect (find (autoeq::correct (muddy, ctx, { limit }), dsp::EqRole::autoMix) == nullptr, "at -4 dB: no further cut");

                auto none = ctx;
                none.budget = 0;
                expect (autoeq::correct (dull, none, {}).empty(), "no budget: no new band");

                // A planned lift that overshot is taken back
                auto bright = inside; bright.top = -5.0f;
                autoeq::Move lift { dsp::EqRole::air, band (dsp::EqBandType::highShelf, 9000.0f, 3.0f, 0.7071f), "x", "y", false };
                const auto c3 = autoeq::correct (bright, ctx, { lift });
                const auto* a3 = find (c3, dsp::EqRole::air);
                expect (a3 != nullptr && a3->band.gain < 3.0f, "overshoot -> less lift");
            }

            // Budget: never more moves than allowed
            {
                VocalSpec s; s.seconds = 14.0; s.resonanceHz = 1800.0f; s.resonanceDb = 16.0f; s.tiltDb = -5.0f; s.f0 = 120.0f;
                auto p = profileOf (s);
                addBump (p, 220.0f, 520.0f, 9.0f);
                for (int budget : { 0, 1, 2, 5 })
                {
                    auto ctx = ctxFor (p);
                    ctx.budget = budget;
                    checkLimits (autoeq::planMoves (ctx), budget, "budget " + juce::String (budget));
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
