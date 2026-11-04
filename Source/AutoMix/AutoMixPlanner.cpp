#include "AutoMixPlanner.h"
#include "../Parameters/ParameterLayout.h"
#include "../Parameters/SettingsReader.h"
#include "../Parameters/RoutingMigration.h"
#include "../Parameters/EqBands.h"
#include "AutoEq.h"
#include "AutoMixObjective.h"
#include "ReportText.h"

namespace vocalypse::automix
{
    using M = dsp::ModuleId;

    namespace
    {
        // Decision titles, reasons and notes are saved in the report: both languages
        using txt::both;
        using txt::db;
        using txt::hz;
        using txt::num;
        float clamp (float v, float lo, float hi) { return juce::jlimit (lo, hi, v); }

        bool likely (const VocalProfile& p, const char* key)
        {
            const auto* i = p.issue (key);
            return i != nullptr && i->confidence >= Confidence::likely;
        }
    }

    //==========================================================================
    const std::set<juce::String>& sessionParameters()
    {
        static const std::set<juce::String> ids { pid::bypass, pid::levelMatchWindow, pid::levelMatchRange, pid::checkOn, pid::checkMode };
        return ids;
    }

    const std::map<juce::String, float>& defaultParameterValues()
    {
        static const auto values = []
        {
            std::map<juce::String, float> v;
            for (auto& s : getParameterSpecs())
                v[s.id] = s.defaultValue;
            return v;
        }();
        return values;
    }

    std::map<juce::String, float> startValues (const std::map<juce::String, float>& live)
    {
        auto values = defaultParameterValues();
        for (auto& id : sessionParameters())
            if (const auto it = live.find (id); it != live.end())
                values[id] = it->second;
        return values;
    }

    void MixPlan::set (const juce::String& id, float value)
    {
        const auto* spec = findParameterSpec (id);
        if (spec == nullptr || ! std::isfinite (value))
        {
            jassertfalse;
            return;
        }
        float v = juce::jlimit (spec->minValue, spec->maxValue, value);
        if (spec->type != ParameterSpec::Type::Float)
            v = (float) juce::roundToInt (v);
        else if (spec->interval > 0.0f)
            v = spec->minValue + spec->interval * std::round ((v - spec->minValue) / spec->interval);
        values[id] = v;
    }

    float MixPlan::get (const juce::String& id, float fallback) const
    {
        auto it = values.find (id);
        return it != values.end() ? it->second : fallback;
    }

    std::set<dsp::ModuleId> MixPlan::activeModules() const
    {
        std::set<dsp::ModuleId> s;
        const std::pair<const char*, M> switches[] {
            { pid::eqOn, M::eq }, { pid::rideOn, M::ride }, { pid::compOn, M::comp }, { pid::smashOn, M::smash },
            { pid::deessOn, M::deess }, { pid::smoothOn, M::smooth }, { pid::satOn, M::sat }, { pid::doublerOn, M::doubler },
            { pid::delayOn, M::delay }, { pid::reverbOn, M::reverb }
        };
        for (auto& [id, m] : switches)
            if (on (id))
                s.insert (m);
        return s;
    }

    ModuleLog& MixPlan::logFor (dsp::ModuleId module, int instance)
    {
        for (auto& l : log)
            if (! l.globalStage && l.module == module && l.instance == instance)
                return l;
        ModuleLog l;
        l.module = module;
        l.instance = instance;
        log.push_back (std::move (l));
        return log.back();
    }

    Decision* MixPlan::find (dsp::ModuleId m)
    {
        for (auto& d : decisions)
            if (! d.outputStage && d.module == m)
                return &d;
        return nullptr;
    }

    //==========================================================================
    dsp::EqBand eqBand (dsp::EqBandType type, float freq, float gain, float q, int slope)
    {
        dsp::EqBand b;
        b.on = true;
        b.type = (int) type;
        b.freq = freq;
        b.gain = gain;
        b.q = q;
        b.slope = slope;
        return b;
    }

    std::optional<dsp::EqBand> roleBand (const MixPlan& plan, const std::map<juce::String, float>& current, dsp::EqRole role, int instance)
    {
        auto values = plan.values;
        const auto access = eqbands::accessFor (values, current.empty() ? nullptr : &current);
        const int i = eqbands::findBand (access, instance, role);
        if (i < 0)
            return std::nullopt;
        return eqbands::readBand (access, instance, i);
    }

    float roleGain (const MixPlan& plan, const std::map<juce::String, float>& current, dsp::EqRole role, int instance)
    {
        const auto b = roleBand (plan, current, role, instance);
        return b.has_value() && b->on ? b->gain : 0.0f;
    }

    std::map<juce::String, float> sliderPositions (const std::map<juce::String, float>& values)
    {
        const auto get = [&values] (const char* id) { const auto it = values.find (id); return it != values.end() ? it->second : 0.0f; };
        const auto pct = [] (float fraction) { return juce::jlimit (-100.0f, 100.0f, fraction * 100.0f); };   // bipolar faders
        const bool comp = get (pid::compOn) > 0.5f;
        return {
            { pid::macroPresence,   pct (roleGain ({}, values, dsp::EqRole::air) / 5.0f) },
            { pid::macroWarmth,     pct (roleGain ({}, values, dsp::EqRole::warmth) / 3.0f) },
            { pid::macroSpace,      pct (get (pid::reverbOn) > 0.5f ? get (pid::reverbMix) / 100.0f / dsp::spaceReverbPerTravel : 0.0f) },
            { pid::macroWidth,      pct (get (pid::doublerOn) > 0.5f ? get (pid::doublerMix) / 100.0f / 0.6f : 0.0f) },
            { pid::macroAggression, pct (comp ? (get (pid::compRatio) - 2.0f) / 4.0f : 0.0f) },
        };
    }

    int setRoleBand (MixPlan& plan, const std::map<juce::String, float>& current, int instance, dsp::EqRole role,
                     const dsp::EqBand& value, bool createIfMissing)
    {
        const auto access = eqbands::accessFor (plan.values, current.empty() ? nullptr : &current);
        int i = eqbands::findBand (access, instance, role);
        if (i < 0)
        {
            if (! createIfMissing)
                return -1;
            i = eqbands::findFreeBand (access, instance, role);   // the role's fixed slot when free
            if (i < 0)
                return -1;
        }
        auto b = value;
        b.role = (int) role;
        eqbands::writeBand (access, instance, i, b);
        return i;
    }

    AutoEqState autoEqState (const MixPlan& plan, const std::map<juce::String, float>& current, const VocalProfile& p, bool fresh)
    {
        AutoEqState s;
        auto values = plan.values;
        const auto access = eqbands::accessFor (values, current.empty() ? nullptr : &current);
        auto& ctx = s.context;
        ctx.profile = &p;
        ctx.conservative = plan.conservative;
        // Soft weights of the measurements (Task 7), not the profile's yes / no issues: nothing flips at one number
        ctx.noisy = autoeq::noisyWeight (p);
        ctx.sibilant = autoeq::sibilantWeight (p);
        ctx.harsh = autoeq::harshWeight (p);
        int inUse = 0, available = 0;
        for (int i = 0; i < dsp::maxEqBands; ++i)
        {
            const auto b = eqbands::readBand (access, 0, i);
            const auto role = b.bandRole();
            if (role == dsp::EqRole::lowCut || role == dsp::EqRole::highCut)
            {
                if (b.on)
                {
                    ctx.fixed.push_back (b);
                    ++inUse;
                }
                else
                    ++available;
            }
            else if (autoeq::ownedRole (role))
            {
                const bool active = b.on && std::abs (b.gain) > 1.0e-3f;
                if (active && ! fresh)
                {
                    const bool narrow = b.q >= autoeq::minNarrowQ - 1.0e-3f;
                    s.owned.push_back ({ role, b, autoeq::titleOf (role, b), {}, narrow });
                    ++inUse;
                }
                else
                    ++available;
            }
            else if (b.isFree())
                ++available;
            else if (b.on)
                ctx.fixed.push_back (b);
        }
        ctx.budget = juce::jlimit (0, available, autoeq::maxBands - inUse);
        return s;
    }

    void AutoMixPlanner::configureDeEsser (MixPlan& plan, const VocalProfile& p, float inGain, float range)
    {
        // Threshold between the detector level on vowels (90th percentile) and on
        // S sounds (median): S sounds are reduced, vowels stay untouched
        const float vowel = p.sibBandVowelP90Db + inGain, sib = p.sibBandSibilantP50Db + inGain;
        const float threshold = p.sibBandSibilantP50Db > -99.0f ? std::max (0.5f * (vowel + sib), vowel + 1.0f) : -30.0f;
        plan.set (pid::deessOn, 1.0f);
        plan.set (pid::deessMode, 0.0f);
        plan.set (pid::deessFreq, p.deessDetectorHz);
        plan.set (pid::deessThreshold, juce::jlimit (-60.0f, -3.0f, threshold));
        plan.set (pid::deessRange, range);
    }

    dsp::Routing AutoMixPlanner::standardRouting()
    {
        // EQ (clean-up) -> Ride -> Comp -> Smash -> De-esser -> Smooth -> Sat ->
        // Doubler, with delay and reverb as parallel sends. This is the default
        // order of the plugin, which keeps the visible chain familiar.
        return dsp::Routing::legacyFull();
    }

    //==========================================================================
    MixPlan AutoMixPlanner::plan (const VocalProfile& p, const std::map<juce::String, float>& live, const MixIntent& intent)
    {
        const auto target = targetFor (intent.genre, intent.vibe);
        // Every auto mix starts from the defaults (the live state is read for the session parameters only):
        // nothing a user or an earlier run left behind reaches the plan. The plan holds every parameter.
        const auto current = startValues (live);
        MixPlan plan;
        for (auto& [id, value] : current)
            if (sessionParameters().count (id) == 0)
                plan.values[id] = value;
        plan.routing = standardRouting();
        plan.conservative = ! p.enoughMaterial || p.voicedSeconds < 8.0f;

        const auto cur = [&current] (const char* id, float fallback = 0.0f)
        {
            auto it = current.find (id);
            return it != current.end() ? it->second : fallback;
        };
        const auto decide = [&plan] (M m, juce::String title, juce::String reason, bool measured)
        {
            plan.decisions.push_back ({ m, false, std::move (title), std::move (reason), measured });
        };

        // The structured log (one entry per module decided on): need is 1 / 0 for now (the soft decisions come
        // with later tasks), parameters are filled from plan.values once the plan is complete
        const auto note = [&plan] (M m, bool active, const char* problem, std::initializer_list<Measured> measured, float intensity, const juce::String& skip)
        {
            auto& l = plan.logFor (m);
            l.active = active;
            l.problem = problem;
            l.measurements.clear();
            for (auto& x : measured)
                if (std::isfinite (x.value))
                    l.measurements.push_back (x);
            l.need = active ? 1.0f : 0.0f;
            l.intensity = active ? juce::jlimit (0.0f, 1.0f, intensity) : 0.0f;
            l.skipReason = active ? juce::String() : skip;
        };

        // Levels as the modules see them. The engine analyses a copy of the capture scaled to the working level and
        // sets the input gain to that scaling, so the profile already is what the chain receives: no input gain
        // is added here, and every level below is relative to the working level (plan 2026-11-04, U3)
        constexpr float inGain = 0.0f;
        const float voice = p.voiceRmsDb + inGain;              // median voiced RMS (dBFS)
        const bool noisy = p.snrValid && p.snrDb < 28.0f;
        const bool harsh = p.hasIssue ("harsh");
        const bool dull = p.hasIssue ("dull");
        const bool compressed = p.heavilyCompressed;
        const bool roomy = likely (p, "reverberant");
        const bool someRoom = p.reverberant;

        // ================= Clean-up first: room noise and breaths (0.7) =================
        plan.set (pid::denoiseOn, 0.0f);
        plan.set (pid::breathOn, 0.0f);
        // 2026-10-29: not on a take that already has room or reverb - between the lines lies its tail, not noise
        // (the benchmark set it on 7 of 8 finished professional vocals)
        // ... and gently: on sustained singing without real pauses the "floor" is often quiet singing, which a strong
        // reduction would eat (the benchmark asked for 80 % on clean VocalSet takes) - below 35 dB, 15 .. 50 %
        if (p.snrValid && p.snrDb < 35.0f && ! someRoom && ! compressed)   // a finished (heavily compressed) vocal: its floor is the mix
        {
            const float amount = (float) juce::roundToInt (juce::jlimit (15.0f, 50.0f, juce::jmap (p.snrDb, 35.0f, 20.0f, 15.0f, 50.0f)));
            plan.set (pid::denoiseOn, 1.0f);
            plan.set (pid::denoiseAmount, amount);
            plan.routing.insertDefault (M::denoise, 0);
            decide (M::denoise, both ("Raumrauschen {0} %", "Room Noise {0} %", { juce::String ((int) amount) }),
                    both ("Nur {0} dB zwischen Stimme und Rauschen - das Rauschen zwischen den Zeilen wird leiser.",
                          "Only {0} dB between voice and noise - the noise between the lines gets quieter.", { juce::String (juce::roundToInt (p.snrDb)) }),
                    true);
        }
        note (M::denoise, plan.on (pid::denoiseOn), "snr",
              { { "snr", p.snrValid ? p.snrDb : std::nanf (""), "dB" }, { "room", someRoom ? 1.0f : 0.0f, "" }, { "compressed", compressed ? 1.0f : 0.0f, "" } },
              plan.get (pid::denoiseAmount) / 100.0f,
              ! p.snrValid ? both ("Raumrauschen bleibt aus: Rauschabstand nicht messbar.", "Room noise stays off: signal-to-noise not measurable.")
                           : p.snrDb >= 35.0f ? both ("Raumrauschen bleibt aus: genug Abstand zwischen Stimme und Rauschen.", "Room noise stays off: enough distance between voice and noise.")
                                              : both ("Raumrauschen bleibt aus: Die Aufnahme hat Raum oder ist bereits komprimiert - zwischen den Zeilen liegt kein Rauschen.",
                                                      "Room noise stays off: the recording has room sound or is already compressed - between the lines is no noise."));
        // 2026-10-29: audible breaths are common - measured 0 .. 3.6 % of the blocks on 31 takes; from 1 % on it lowers them
        if (p.breathShare > 0.01f)
        {
            // 0.8: with the measured share (1 % -> 40 %, 4 % -> 70 %); rap / speech keeps a little more natural breath
            const float amount = 5.0f * std::round (clamp (30.0f + 1000.0f * p.breathShare - (p.pitchedShare < 0.45f ? 10.0f : 0.0f), 30.0f, 70.0f) / 5.0f);
            plan.set (pid::breathOn, 1.0f);
            plan.set (pid::breathAmount, amount);
            plan.routing.insertDefault (M::breath, 0);
            decide (M::breath, both ("Atem-Absenker {0} %", "Breath Control {0} %", { juce::String ((int) amount) }),
                    both ("Hörbare Atmer zwischen den Zeilen ({0} % der Aufnahme) - sie werden leiser, S-Laute bleiben.",
                          "Audible breaths between the lines ({0} % of the take) - they get quieter, S sounds stay.",
                          { juce::String (juce::roundToInt (100.0f * p.breathShare)) }),
                    true);
        }
        note (M::breath, plan.on (pid::breathOn), "breaths", { { "breathShare", 100.0f * p.breathShare, "%" } }, plan.get (pid::breathAmount) / 100.0f,
              both ("Atem-Absenker bleibt aus: kaum hörbare Atmer.", "Breath control stays off: hardly any audible breaths."));

        // ---- Macros off (the SIMPLE sliders are placed by the controller afterwards: sliderPositions)
        for (auto* id : { pid::macroPresence, pid::macroWarmth, pid::macroAggression, pid::macroWidth, pid::macroSpace, pid::macroDensity })
            plan.set (id, 0.0f);

        plan.set (pid::deessListen, 0.0f);
        plan.set (pid::smoothDelta, 0.0f);
        plan.set (pid::safetyClip, 1.0f);
        plan.set (pid::stereoWidth, 100.0f);

        if (plan.conservative)
            plan.notes.push_back (both ("Es wurde wenig Gesang erfasst - VOCALYPSE verwendet deshalb nur vorsichtige Einstellungen.",
                                        "Little singing was captured - so VOCALYPSE uses only cautious settings."));

        // ================= EQ: clean-up, not colour =================
        plan.set (pid::eqOn, 1.0f);
        plan.set (pid::eqMix, 100.0f);
        // EQ decisions live in role bands of EQ 1 (spec 8, Q5): user bands are never touched
        {
            auto highCut = roleBand (plan, current, dsp::EqRole::highCut);
            if (highCut.has_value() && highCut->on)
            {
                highCut->on = false;
                setRoleBand (plan, current, 0, dsp::EqRole::highCut, *highCut, false);
            }
        }
        {
            float fc = 80.0f;
            juce::String reason = both ("Keine sichere Tonhöhe gemessen - Standard-Trennfrequenz für Gesang.",
                                        "No reliable pitch measured - standard cutoff for vocals.");
            bool measured = false;
            if (p.pitchValid)
            {
                // Well below the lowest sung fundamental: only rumble and handling noise
                // live there, and the 12 dB/oct slope stays clear of the voice
                fc = clamp (0.6f * p.f0LowHz, 70.0f, 150.0f);
                reason = both ("Tiefe gesungene Töne liegen um {0} - darunter liegt nur Rumpeln.",
                               "The lowest sung notes are around {0} - below that is only rumble.", { hz (p.f0LowHz) });
                measured = true;
            }
            // Rumble (Task 7): the weight grows from the issue's edge (-24 dB) to "likely" (-16 dB); the cutoff moves
            // with it and the slope steps 12 -> 18 -> 24 dB/oct as a choice with margin (the 12 dB slope keeps the
            // choice until the weight clearly asks for more), no longer one jump from 12 to 24
            const float rumbleW = soft (p.rumbleDb, -24.0f, -16.0f);
            if (rumbleW > 0.0f)
            {
                const float fcRumble = clamp (std::min (p.pitchValid ? 0.75f * p.f0LowHz : 110.0f, fc * 1.2f), 70.0f, 170.0f);
                fc = blend (rumbleW, fc, fcRumble);
                reason << both (" Zusätzlich viel Energie unter 90 Hz ({0}).", " Also a lot of energy below 90 Hz ({0}).", { db (p.rumbleDb) });
            }
            const int slopeIndex = 1 + chooseWithMargin ({ -std::abs (2.0f * rumbleW), -std::abs (2.0f * rumbleW - 1.0f), -std::abs (2.0f * rumbleW - 2.0f) }, 0, 0.25f);
            setRoleBand (plan, current, 0, dsp::EqRole::lowCut, eqBand (dsp::EqBandType::lowCut, fc, 0.0f, 0.7071f, slopeIndex), true);
            decide (M::eq, both ("Low Cut {0}, {1} dB/Okt.", "Low Cut {0}, {1} dB/oct", { hz (fc), juce::String (dsp::eqSlopeDbPerOct (slopeIndex)) }), reason, measured);
        }

        // 0.6.1 (spec item 7): narrow notches on sustained resonances and broad tonal moves
        // towards the target curve (AutoEq), in auto-mix role bands of EQ 1. Bands the auto-mix
        // does not own (user bands, Vocal Check's presence / mud) stay as they are and count in
        // the model: a resonance Vocal Check already cut is not cut twice.
        auto eqContext = autoEqState (plan, current, p, true).context;
        eqContext.target = &target;
        eqContext.unmaskDb = autoeq::unmaskOf (p, plan.conservative);
        const auto eqMoves = autoeq::planCurveMoves (eqContext);
        const auto moveFor = [&eqMoves] (dsp::EqRole role) -> const autoeq::Move*
        {
            for (auto& m : eqMoves)
                if (m.role == role)
                    return &m;
            return nullptr;
        };
        for (auto& m : eqMoves)
            if (setRoleBand (plan, current, 0, m.role, m.band, true) >= 0)
                decide (M::eq, m.title, m.reason, true);
        note (M::eq, true, p.rumbleDb > -16.0f ? "rumble" : "cleanUp",
              { { "f0Low", p.pitchValid ? p.f0LowHz : std::nanf (""), "Hz" }, { "rumble", p.rumbleDb, "dB" }, { "presence", p.presenceDb, "dB" },
                { "curveMoves", (float) eqMoves.size(), "" }, { "harshWeight", eqContext.harsh, "" }, { "dullWeight", autoeq::dullWeight (p), "" },
                { "sibilantWeight", eqContext.sibilant, "" }, { "noisyWeight", eqContext.noisy, "" } }, 1.0f, {});
        {
            // need: how much the take asks for EQ (a move of 1 dB counts half, 2 dB fully; the rumble weight);
            // intensity: the moves' total size against 18 dB (3 bands at their usual limit)
            float biggest = 0.0f, total = 0.0f;
            for (auto& m : eqMoves)
            {
                biggest = std::max (biggest, std::abs (m.band.gain));
                total += std::abs (m.band.gain);
            }
            auto& l = plan.logFor (M::eq);
            l.need = std::max (soft (p.rumbleDb, -24.0f, -16.0f), soft (biggest, 0.5f, 2.0f));
            l.intensity = juce::jlimit (0.0f, 1.0f, total / 18.0f);
        }
        // Roles without a move: flat (0.5 bands keep their place), notches and the tone bell off
        {
            const std::pair<dsp::EqRole, dsp::EqBand> flat[] {
                { dsp::EqRole::warmth, eqBand (dsp::EqBandType::lowShelf, 180.0f, 0.0f, 0.7071f) },
                { dsp::EqRole::autoMix, eqBand (dsp::EqBandType::bell, 1500.0f, 0.0f, 1.0f) },
                { dsp::EqRole::air, eqBand (dsp::EqBandType::highShelf, 10000.0f, 0.0f, 0.7071f) } };
            for (auto& [role, b] : flat)
                if (moveFor (role) == nullptr)
                    setRoleBand (plan, current, 0, role, b, false);
            for (auto role : { dsp::EqRole::autoTone, dsp::EqRole::autoNotch1, dsp::EqRole::autoNotch2, dsp::EqRole::autoNotch3 })
                if (moveFor (role) == nullptr)
                    if (auto b = roleBand (plan, current, role); b.has_value() && (b->on || b->gain != 0.0f))
                    {
                        b->on = false;
                        b->gain = 0.0f;
                        setRoleBand (plan, current, 0, role, *b, false);
                    }
        }
        if (const auto* air = moveFor (dsp::EqRole::air); (air == nullptr || air->band.gain <= 0.0f) && (harsh || p.presenceDb > autoeq::presenceWindow.hi))
            plan.notes.push_back (both ("Keine Höhenanhebung: Die Aufnahme ist bereits hell genug.",
                                        "No high boost: the recording is already bright enough."));
        if (const auto mask = autoeq::maskingOf (p); harsh && autoeq::unmaskGainDb (mask.presenceDb, false) > 0.0f)
            plan.notes.push_back (both ("Der Beat verdeckt die Stimme bei {0} ({1}), aber sie ist dort schon scharf - keine Anhebung.",
                                        "The beat covers the voice at {0} ({1}), but it is already sharp there - no boost.",
                                        { hz (mask.hz), db (mask.presenceDb) }));

        // ================= Level Ride: phrase levels =================
        // Also on wet hooks with a small loudness range: in the blind listening test of
        // 2026-10-24 the ridden version was the more intelligible one (quiet syllables, not
        // only reverb tails). The onsets it lifts are caught by a faster compressor attack.
        // 0.8: from the genre / vibe's phrase window (party: above 8 dB as before)
        const bool rideWanted = p.dynamicsValid && p.dynamicsDb > target.dynamics.phraseHi + 2.0f && ! compressed && ! plan.conservative;
        plan.set (pid::rideOn, rideWanted ? 1.0f : 0.0f);
        if (rideWanted)
        {
            // 2026-11-03: the style's phrase window scales the ride (party 3-6 dB = x1; Natural / Ballad ride less,
            // Aggressive / Rap more) - with one amount for all, every style ended at the same dynamics
            const float styleScale = 4.5f / std::max (2.0f, 0.5f * (target.dynamics.phraseLo + target.dynamics.phraseHi));
            const float amount = std::round (clamp ((40.0f + 4.0f * (p.dynamicsDb - 8.0f)) * styleScale, 25.0f, 85.0f));
            const float boost = noisy ? 3.0f : 6.0f;
            float silence = p.voiceRmsLowDb + inGain - 12.0f;
            if (p.snrValid)
                silence = std::max (silence, p.noiseFloorDb + inGain + 6.0f);
            plan.set (pid::rideTarget, clamp (voice, -40.0f, -6.0f));
            plan.set (pid::rideAmount, amount);
            plan.set (pid::rideMaxBoost, boost);
            plan.set (pid::rideMaxCut, 8.0f);
            plan.set (pid::rideResponse, 45.0f);
            plan.set (pid::ridePhrase, 40.0f);
            plan.set (pid::rideTransient, 60.0f);
            plan.set (pid::rideSilence, clamp (silence, -70.0f, -35.0f));
            decide (M::ride, "Level Ride " + juce::String (juce::roundToInt (amount)) + " %, max. +" + juce::String ((int) boost) + " dB",
                    both ("Laute und leise Wörter liegen {0} dB auseinander - leise Wörter werden angehoben, bevor der Kompressor arbeitet.",
                          "Loud and quiet words are {0} dB apart - quiet words are lifted before the compressor works.", { num (p.dynamicsDb, 1) })
                        + (noisy ? both (" Anhebung begrenzt (Rauschen).", " Lift limited (noise).") : juce::String()), true);
        }
        note (M::ride, rideWanted, "phraseSpread", { { "phraseSpread", p.dynamicsValid ? p.dynamicsDb : std::nanf (""), "dB" }, { "phraseHi", target.dynamics.phraseHi, "dB" } },
              plan.get (pid::rideAmount) / 100.0f,
              plan.conservative ? both ("Kein Level Ride: Es wurde wenig Gesang erfasst.", "No Level Ride: little singing was captured.")
                                : compressed ? both ("Kein Level Ride: Die Aufnahme ist bereits dicht.", "No Level Ride: the recording is already dense.")
                                             : both ("Kein Level Ride: Die Phrasen liegen nah genug beieinander.", "No Level Ride: the phrase levels are close enough."));

        // ================= Compressor: steady level =================
        {
            // After the rider, words sit around the rider target
            const float level = rideWanted ? plan.get (pid::rideTarget) : voice;
            // 0.8: the start of the engine's search follows this voice - the ratio from how far its phrase
            // spread (after the ride) is from the middle of the target window, the threshold from its crest
            // (peaky words: deeper), on the search's ratio steps 1.8 / 2.5 / 3.5 / 5
            const float mid = 0.5f * (target.dynamics.phraseLo + target.dynamics.phraseHi);
            const float spread = rideWanted ? target.dynamics.phraseHi : (p.dynamicsValid ? p.dynamicsDb : mid + 2.0f);
            const float wanted = clamp (spread / mid, 1.5f, 5.0f);
            float ratio = wanted < 2.15f ? 1.8f : (wanted < 3.0f ? 2.5f : (wanted < 4.25f ? 3.5f : 5.0f));
            float below = clamp (3.0f + 0.5f * (p.crestDb - target.dynamics.crestHi), 1.5f, 8.0f);   // below the median word (RMS detector reads +3 dB for a sine)
            if (compressed)       { ratio = 1.8f; below = 1.0f; }
            if (plan.conservative) { ratio = std::min (ratio, 2.5f); below = std::min (below, 3.0f); }

            const float threshold = clamp (level + 3.0f - below, -60.0f, -6.0f);
            const float expectedGr = below * (1.0f - 1.0f / ratio);
            plan.set (pid::compOn, 1.0f);
            plan.set (pid::compDetector, 1.0f);   // RMS: follows the voice, not single consonants
            plan.set (pid::compThreshold, threshold);
            plan.set (pid::compRatio, ratio);
            // 0.8 (spec: "Kompressor gezielt"): the attack follows the consonants - soft onsets (consonant peak at
            // most level with the vowel after it) keep 9 ms of transient, sharp ones (6 dB over it) are caught at
            // 3 ms, in between on a sliding scale; aggressive -2 ms,
            // floaty +8 ms. A ridden take keeps the fast attack for the onsets the ride lifts (3 ms: about
            // 1 dB less peak-to-loudness on ridden hooks than 12 ms). The release follows the syllable rate,
            // so the compressor is back between the syllables (60 % of their spacing, 40 .. 250 ms).
            float attack = p.onsetsValid ? juce::jmap (clamp (p.onsetSharpnessDb, 0.0f, 6.0f), 0.0f, 6.0f, 9.0f, 3.0f) : (p.crestDb > 15.0f ? 5.0f : 8.0f);
            attack = std::round (attack);
            if (rideWanted)
                attack = std::min (attack, 3.0f);
            attack += target.vibe == Vibe::aggressive ? -2.0f : (target.vibe == Vibe::floaty ? 8.0f : 0.0f);
            attack = clamp (attack, 1.0f, 30.0f);
            const float release = p.onsetsValid && p.onsetRate > 0.5f ? clamp (600.0f / p.onsetRate, 40.0f, 250.0f) : 110.0f;
            plan.set (pid::compAttack, attack);
            plan.set (pid::compRelease, release);
            plan.set (pid::compKnee, 6.0f);
            plan.set (pid::compMakeup, clamp (std::round (expectedGr * 2.0f) / 2.0f, 0.0f, 6.0f));
            plan.set (pid::compMix, 100.0f);
            plan.set (pid::compScHpf, 100.0f);
            decide (M::comp, both ("Kompressor {0}:1, Schwelle {1}", "Compressor {0}:1, threshold {1}", { num (ratio, 1), db (threshold) }),
                    (compressed ? both ("Die Aufnahme ist bereits dicht - nur sanfte Kontrolle.", "The recording is already dense - only gentle control.")
                                : both ("Etwa {0} dB Reduktion auf typischen Wörtern für einen gleichmäßigen, durchsetzungsfähigen Pegel.",
                                        "About {0} dB of reduction on typical words for an even level that cuts through.", { num (expectedGr, 1) }))
                        + (p.onsetsValid ? both (" Attack {0} ms ({1}), Release {2} ms ({3} Silben/s).", " Attack {0} ms ({1}), release {2} ms ({3} syllables/s).",
                                                 { num (attack, 0), p.onsetSharpnessDb >= 3.0f ? both ("scharfe Konsonanten", "sharp consonants") : both ("weiche Einsätze", "soft onsets"),
                                                   num (release, 0), num (p.onsetRate, 1) })
                                         : juce::String()),
                    true);
            note (M::comp, true, "phraseSpread", { { "phraseSpread", p.dynamicsValid ? p.dynamicsDb : std::nanf (""), "dB" }, { "crest", p.crestDb, "dB" },
                                                   { "expectedGr", expectedGr, "dB" } }, 1.0f, {});
        }

        // ================= Vocal Smash: density (parallel) =================
        // 0.8: the blend of the genre / vibe's density target (the engine tunes it on the render; natural = none)
        const bool smash = ! compressed && ! plan.conservative && ! p.distortionLikely && target.densityBlend > 0.0f;
        plan.set (pid::smashOn, smash ? 1.0f : 0.0f);
        if (smash)
        {
            plan.set (pid::smashAmount, 40.0f);
            plan.set (pid::smashBlend, target.densityBlend);
            plan.set (pid::smashAttack, 2.0f);
            plan.set (pid::smashRelease, 90.0f);
            plan.set (pid::smashTone, harsh ? -10.0f : (dull ? 25.0f : 10.0f));
            plan.set (pid::smashOutput, 0.0f);
            decide (M::smash, both ("Parallelkompression {0} % Blend", "Parallel compression {0} % blend", { num (target.densityBlend, 0) }),
                    both ("Ein kleiner Anteil stark komprimierter Stimme unter dem Original gibt Dichte, ohne die Transienten zu verlieren.",
                          "A little heavily compressed voice under the original adds density without losing the transients."), false);
        }
        else if (compressed)
        {
            plan.notes.push_back (both ("Keine Parallelkompression: Die Aufnahme ist bereits stark komprimiert.",
                                        "No parallel compression: the recording is already heavily compressed."));
        }
        note (M::smash, smash, "density", { { "densityBlend", target.densityBlend, "%" }, { "compressed", compressed ? 1.0f : 0.0f, "" } },
              plan.get (pid::smashBlend) / 100.0f,
              compressed ? both ("Keine Parallelkompression: Die Aufnahme ist bereits stark komprimiert.", "No parallel compression: the recording is already heavily compressed.")
                         : plan.conservative || p.distortionLikely ? both ("Keine Parallelkompression: zu wenig oder übersteuertes Material.", "No parallel compression: too little or clipped material.")
                                                                   : both ("Keine Parallelkompression: Der Stil braucht keine zusätzliche Dichte.", "No parallel compression: the style needs no extra density."));

        // ================= De-esser =================
        const bool deess = p.sibilanceValid && p.sibilanceVsVowelDb > -9.0f;
        plan.set (pid::deessOn, deess ? 1.0f : 0.0f);
        if (deess)
        {
            const bool strong = p.hasIssue ("sibilance");
            const float range = p.sibilanceVsVowelDb > 0.0f ? 8.0f : (likely (p, "sibilance") ? 6.0f : (strong ? 5.0f : 3.0f));
            configureDeEsser (plan, p, inGain, range);
            decide (M::deess, both ("De-Esser ab {0}, max. {1} dB", "De-Esser from {0}, max. {1} dB", { hz (p.deessDetectorHz), juce::String ((int) range) }),
                    both ("S-Laute {0} gegenüber den Vokalen. ", "Sibilance {0} against the vowels. ", { db (p.sibilanceVsVowelDb) })
                        + (strong ? both ("Schwelle zwischen Vokal- und S-Pegel, damit nur S-Laute reduziert werden.",
                                          "Threshold between vowel and S level, so only the S sounds are reduced.")
                                  : both ("Vorsorglich dezent: Kompression und Präsenz heben S-Laute an.",
                                          "Gentle, as a precaution: compression and presence lift S sounds.")), true);
        }
        else
        {
            plan.notes.push_back (p.sibilanceValid ? both ("De-Esser bleibt aus: S-Laute sind unauffällig.", "De-Esser stays off: the sibilance is unremarkable.")
                                                   : both ("De-Esser bleibt aus: zu wenige S-Laute zum Messen.", "De-Esser stays off: too few S sounds to measure."));
        }
        note (M::deess, deess, "sibilance",
              { { "sibilanceVsVowel", p.sibilanceValid ? p.sibilanceVsVowelDb : std::nanf (""), "dB" }, { "deessFreq", p.deessDetectorHz, "Hz" } },
              plan.get (pid::deessRange) / 10.0f, plan.notes.empty() ? juce::String() : plan.notes.back());

        // ================= Smooth: adaptive resonance control =================
        // One problem, one tool: a resonance the EQ notches (0.6.1) is no reason for SMOOTH
        const auto notched = [&eqMoves] (const Resonance& r)
        {
            return std::any_of (eqMoves.begin(), eqMoves.end(), [&r] (const autoeq::Move& m)
                                { return m.narrow && std::abs (std::log2 (m.band.freq / r.hz)) < 1.0f / 12.0f; });
        };
        const Resonance* highRes = nullptr;
        for (auto& r : p.resonances)
            if (r.hz >= 1200.0f && r.excessDb > 7.0f && ! notched (r))
            {
                highRes = &r;
                break;
            }
        // 2026-10-29 (Jakob: "Smooth hilft bei jeder Vocal"): SMOOTH only takes down what sticks out, so it runs on
        // every vocal with enough material - gently (20 %) where nothing stands out, more on harsh or resonant ones
        const bool smooth = p.enoughMaterial;
        plan.set (pid::smoothOn, smooth ? 1.0f : 0.0f);
        if (smooth)
        {
            const bool marked = harsh || highRes != nullptr;
            const float amount = likely (p, "harsh") || (highRes != nullptr && highRes->strong) ? 45.0f : (marked ? 30.0f : 20.0f);
            plan.set (pid::smoothAmount, amount);
            plan.set (pid::smoothSensitivity, 50.0f);
            plan.set (pid::smoothLow, 1000.0f);
            plan.set (pid::smoothHigh, 10000.0f);
            plan.set (pid::smoothAttack, 5.0f);
            plan.set (pid::smoothRelease, 80.0f);
            plan.set (pid::smoothMode, 0.0f);
            plan.set (pid::smoothOutput, 0.0f);
            decide (M::smooth, both ("Resonanzkontrolle {0} %", "Resonance control {0} %", { juce::String ((int) amount) }),
                    highRes != nullptr ? both ("Schmale Spitze um {0} - SMOOTH senkt sie nur, wenn sie hervorsticht.",
                                               "Narrow peak around {0} - SMOOTH lowers it only when it sticks out.", { hz (highRes->hz) })
                                       : harsh ? both ("Betonte obere Mitten ({0}) - SMOOTH glättet nur die herausstechenden Bänder.",
                                                       "Emphasized upper mids ({0}) - SMOOTH smooths only the bands that stick out.", { db (p.presenceDb) })
                                               : both ("Sanft als Grundpflege: SMOOTH glättet nur Resonanzen, die beim Singen kurz hervorstechen.",
                                                       "Gentle, as basic care: SMOOTH only smooths resonances that briefly stick out while singing."),
                    marked);
        }
        note (M::smooth, smooth, highRes != nullptr ? "resonance" : (harsh ? "harsh" : "basicCare"),
              { { "presence", p.presenceDb, "dB" }, { "resonanceHz", highRes != nullptr ? highRes->hz : std::nanf (""), "Hz" },
                { "resonanceExcess", highRes != nullptr ? highRes->excessDb : std::nanf (""), "dB" } },
              plan.get (pid::smoothAmount) / 100.0f,
              both ("Resonanzkontrolle bleibt aus: zu wenig Material.", "Resonance control stays off: too little material."));

        // ================= Saturation: density and colour =================
        const bool sat = ! p.distortionLikely && ! (compressed && harsh);
        plan.set (pid::satOn, sat ? 1.0f : 0.0f);
        if (sat)
        {
            // 0.8: the character of the vibe (aggressive Tube, warm Tape ...); a harsh voice gets the soft Warm; a
            // voice whose body (125 - 250 Hz) lies on average more than 3 dB under the target curve gets Tube and 10 %
            // more for body (the plain deviation: the finished vocals spread widely down there)
            float bodyLack = 0.0f;
            int bodyThirds = 0;
            for (int k = 0; k < numThirds && p.thirdsDb.size() == (size_t) numThirds; ++k)
                if (thirdCentre (k) >= 125.0f * 0.99f && thirdCentre (k) <= 250.0f * 1.01f && p.thirdsDb[(size_t) k] > -190.0f)
                {
                    bodyLack += p.thirdsDb[(size_t) k] - target.curveDb[(size_t) k];
                    ++bodyThirds;
                }
            const bool thin = bodyThirds > 0 && bodyLack / (float) bodyThirds < -3.0f && ! (p.pitchValid && p.f0LowHz > 250.0f);
            juce::ignoreUnused (dull);
            const int character = harsh ? 0 : (thin ? 2 : target.satCharacter);   // Warm / Tape / Tube / Hard
            const float drive = plan.conservative ? 3.0f : target.satDrive;
            const float mix = (plan.conservative ? 20.0f : target.satMix) + (thin && ! harsh ? 10.0f : 0.0f);
            plan.set (pid::satCharacter, (float) character);
            plan.set (pid::satDrive, drive);
            plan.set (pid::satMix, mix);
            plan.set (pid::satOutput, 0.0f);
            static const char* names[] { "Warm", "Tape", "Tube", "Hard" };
            decide (M::sat, both ("Sättigung {0}, {1} %", "Saturation {0}, {1} %", { names[character], juce::String ((int) mix) }),
                    harsh ? both ("Dezente, weiche Obertöne für Dichte - ohne die Schärfe zu betonen.",
                                  "Subtle, soft harmonics for density - without emphasizing the harshness.")
                          : thin ? both ("Wenig Körper (125-250 Hz unter dem Ziel) - Tube-Obertöne machen die Stimme voller.",
                                         "Little body (125-250 Hz under the target) - Tube harmonics make the voice fuller.")
                                 : both ("Obertöne für Dichte, Charakter für {0} · {1}.", "Harmonics for density, character for {0} · {1}.",
                                         { genreName (target.genre), vibeName (target.vibe) }), true);
        }
        else
        {
            plan.notes.push_back (p.distortionLikely ? both ("Keine Sättigung: Die Aufnahme zeigt bereits Übersteuerung.",
                                                             "No saturation: the recording already shows clipping.")
                                                     : both ("Keine Sättigung: bereits dicht und hell.", "No saturation: already dense and bright."));
        }
        note (M::sat, sat, p.distortionLikely ? "distortion" : "density",
              { { "distortion", p.distortionLikely ? 1.0f : 0.0f, "" }, { "harsh", harsh ? 1.0f : 0.0f, "" }, { "compressed", compressed ? 1.0f : 0.0f, "" } },
              plan.get (pid::satMix) / 100.0f, plan.notes.empty() ? juce::String() : plan.notes.back());

        // ================= Width: doubler, mono-safe =================
        const bool alreadyWide = p.stereoContent && p.correlation < 0.8f;
        // 0.8: the width of the genre / vibe; a rap lead (few held notes) only a little
        // ... and more of it the more the voice holds notes (0.45 -> x 0.7, 0.95 -> x 1.2)
        const float doublerMix = p.pitchedShare < 0.45f ? std::min (target.space.doublerMix, 8.0f)
                                                         : std::round (target.space.doublerMix * juce::jmap (clamp (p.pitchedShare, 0.45f, 0.95f), 0.45f, 0.95f, 0.7f,
                                                                                                             // a genre with little width (rap: 8 %) never gets more
                                                                                                             target.space.doublerMix <= 8.0f ? 1.0f : 1.2f));
        const bool doubler = ! alreadyWide && ! plan.conservative && doublerMix >= 5.0f;
        plan.set (pid::doublerOn, doubler ? 1.0f : 0.0f);
        if (doubler)
        {
            plan.set (pid::doublerMix, doublerMix);
            plan.set (pid::doublerWidth, 70.0f);
            plan.set (pid::doublerDelay, 14.0f);
            plan.set (pid::doublerDetune, 7.0f);
            plan.set (pid::doublerMonoSafe, 100.0f);   // mono sum stays exactly the original
            plan.set (pid::doublerTone, 8000.0f);
            decide (M::doubler, both ("Breite: Doubler {0} %, mono-sicher", "Width: Doubler {0} %, mono-safe", { num (doublerMix, 0) }),
                    both ("Doppelung links/rechts für {0} · {1}. \"Mono safe\" 100 %: In Mono klingt die Stimme exakt wie ohne Doubler.",
                          "Doubling left/right for {0} · {1}. \"Mono safe\" 100 %: in mono the voice sounds exactly as without the Doubler.",
                          { genreName (target.genre), vibeName (target.vibe) }), true);
        }
        else if (alreadyWide)
        {
            plan.notes.push_back (both ("Kein Doubler: Die Aufnahme ist bereits breit (Korrelation {0}).",
                                        "No Doubler: the recording is already wide (correlation {0}).", { num (p.correlation, 2) }));
        }
        note (M::doubler, doubler, "width",
              { { "correlation", p.stereoContent ? p.correlation : std::nanf (""), "" }, { "pitchedShare", 100.0f * p.pitchedShare, "%" }, { "doublerMix", doublerMix, "%" } },
              plan.get (pid::doublerMix) / 100.0f,
              alreadyWide ? both ("Kein Doubler: Die Aufnahme ist bereits breit.", "No Doubler: the recording is already wide.")
                          : plan.conservative ? both ("Kein Doubler: Es wurde wenig Gesang erfasst.", "No Doubler: little singing was captured.")
                                              : both ("Kein Doubler: Der Stil braucht hier kaum Breite.", "No Doubler: the style needs hardly any width here."));
        plan.set (pid::monoBelow, 120.0f);

        // ================= Space: reverb and delay as sends, ducked =================
        const float duckThreshold = clamp (voice - 10.0f, -60.0f, -10.0f);
        plan.set (pid::duckSource, 0.0f);
        if (roomy)
        {
            plan.set (pid::reverbOn, 0.0f);
            plan.set (pid::delayOn, 0.0f);
            plan.notes.push_back (both ("Kein Hall und kein Delay: Die Aufnahme hat bereits deutlichen Raum ({0} s Ausklang).",
                                        "No reverb and no delay: the recording already has a clear room sound ({0} s decay).", { num (p.tailSeconds, 1) }));
            for (auto m : { M::reverb, M::delay })
                note (m, false, "room", { { "tail", p.tailSeconds, "s" } }, 0.0f, plan.notes.back());
        }
        else
        {
            // Character from the pitch measurement: rhythmic speech / rap (few pitched
            // frames) stays drier and closer, long sung melodies get the Large Hall (a take
            // with some room of its own keeps the shorter plate)
            const bool spoken = p.pitchedShare < 0.45f;
            const bool sustained = p.pitchedShare > 0.75f && p.pitchSpreadSemitones > 2.0f;
            const bool largeHall = sustained && ! someRoom;
            // 2026-11-03: calibrated on 11 SONOVOX dry / producer-wet lead pairs - the producers' tails sit
            // -25 / -29 / -41 dB under the phrases 0.15-0.4 / 0.4-0.8 / 0.8-1.6 s after them; the old 14 %
            // left them 10 dB quieter ("Hall und Delay hört man gar nicht"). Measured through the whole chain
            // (ride, compressor, ducking) reverb ~42 % + delay ~20 % with a 2 s plate hit it.
            float revMix = someRoom ? 18.0f : (plan.conservative ? 26.0f : 42.0f);
            float decay = 2.0f, preDelay = 30.0f;
            int type = 1;   // plate
            if (spoken)         { revMix = std::min (revMix, 20.0f); decay = 1.2f; type = 0; }
            else if (largeHall) { decay = 2.6f; preDelay = 40.0f; type = 3; }   // the long tail already reads wetter
            else if (sustained) { revMix = std::min (revMix + 4.0f, 48.0f); decay = 2.3f; }
            // 0.8: the amount of the genre / vibe; with the song tempo the tail lasts a number of bars and the
            // pre-delay is a 1/64 note (no tempo: the times above)
            revMix = std::round (clamp (revMix * target.space.reverbScale, 4.0f, 70.0f));
            decay = clamp (decay * target.space.decayBars, 0.8f, 3.5f);   // without tempo: the style's length too
            if (p.hostBpm > 0.0)
            {
                const float bar = 240.0f / (float) p.hostBpm;
                decay = clamp (target.space.decayBars * bar * (spoken ? 0.6f : (largeHall ? 1.2f : 1.0f)), 0.8f, 3.0f);
                preDelay = std::round (clamp (60000.0f / (float) p.hostBpm / 16.0f, 15.0f, 60.0f));
            }
            // 2026-11-03: the vibe also colours the room (Floaty and Ballade · Warm had the same amount and length):
            // Floaty an open, bright hall with a later onset, Warm a darker, smaller plate, Aggressive a tight one
            float damping = harsh ? 5500.0f : 6500.0f, size = 55.0f;
            if (! spoken && ! someRoom)
            {
                if (target.vibe == Vibe::floaty)
                {
                    damping = harsh ? 7000.0f : 9500.0f;
                    size = 80.0f;
                    preDelay += 20.0f;
                    if (type == 1)
                        type = 2;   // plate -> hall
                }
                else if (target.vibe == Vibe::warm)
                {
                    damping = 4200.0f;
                    size = 45.0f;
                }
                else if (target.vibe == Vibe::aggressive)
                {
                    damping = harsh ? 5000.0f : 6000.0f;
                    size = 40.0f;
                }
            }
            // The beat on the sidechain: a dense one leaves no gaps for the tail, one with gaps leaves room
            // (not for a take with a room of its own)
            const auto density = someRoom && beatDensityOf (p) == BeatDensity::sparse ? BeatDensity::medium : beatDensityOf (p);
            if (density == BeatDensity::dense)
                revMix = std::round (revMix * 0.7f);
            else if (density == BeatDensity::sparse)
                revMix = std::min (revMix + 4.0f, 70.0f);
            const juce::String beatReason = (density == BeatDensity::dense
                ? both (" Dichter Beat (kaum Lücken gemessen): weniger Hall, damit die Stimme vorne bleibt.",
                        " Dense beat (hardly any gaps measured): less reverb so the voice stays in front.")
                : density == BeatDensity::sparse ? both (" Beat mit Lücken: etwas mehr Raum.", " Beat with gaps: a little more room.")
                                                 : juce::String())
                + both (" Raum für {0} · {1}", " Space for {0} · {1}", { genreName (target.genre), vibeName (target.vibe) })
                + (p.hostBpm > 0.0 ? both (", Hallzeit und Pre-Delay nach dem Songtempo ({0} BPM).", ", reverb time and pre-delay from the song tempo ({0} BPM).",
                                           { num ((float) p.hostBpm, 0) })
                                   : juce::String ("."));
            plan.set (pid::reverbOn, 1.0f);
            plan.set (pid::reverbType, (float) type);
            plan.set (pid::reverbMix, revMix);
            plan.set (pid::reverbDecay, decay);
            plan.set (pid::reverbPreDelay, preDelay);
            plan.set (pid::reverbSize, size);
            plan.set (pid::reverbDamping, damping);
            plan.set (pid::reverbLowCut, 300.0f);
            plan.set (pid::reverbWidth, 100.0f);
            plan.set (pid::reverbDuckAmount, 6.0f);
            plan.set (pid::reverbDuckThreshold, duckThreshold);
            plan.set (pid::reverbDuckAttack, 20.0f);
            plan.set (pid::reverbDuckRelease, 500.0f);
            decide (M::reverb, both ("{0} {1} %, {2} s, geduckt", "{0} {1} %, {2} s, ducked",
                                     { txt::reverbType (type), juce::String ((int) revMix), num (decay, 1) }),
                    someRoom ? both ("Die Aufnahme hat etwas Raum - deshalb nur wenig Hall.", "The recording has some room sound - so only a little reverb.")
                             : spoken ? both ("Sprechgesang/Rap (wenig gehaltene Töne gemessen): kurzer, trockener Raum, damit der Text vorne bleibt.",
                                              "Spoken vocal/rap (few held notes measured): a short, dry room keeps the lyrics up front.")
                                      : largeHall ? both ("Lange, gehaltene Melodiebögen: Large Hall mit 40 ms Pre-Delay trägt die Töne; Ducking hält die Wörter vorne.",
                                                          "Long, held melodic lines: a Large Hall with 40 ms pre-delay carries the notes; ducking keeps the words up front.")
                                      : sustained ? both ("Viele gehaltene Töne: etwas längere Plate für Tiefe; Ducking hält die Wörter vorne.",
                                                          "Many held notes: a slightly longer plate for depth; ducking keeps the words up front.")
                                                  : both ("Räumliche Tiefe; 30 ms Pre-Delay und Ducking halten die Wörter vorne.",
                                                          "Spatial depth; 30 ms pre-delay and ducking keep the words up front.")
                        + beatReason, true);
            note (M::reverb, true, someRoom ? "room" : (spoken ? "spoken" : "style"),
                  { { "tail", p.tailSeconds, "s" }, { "pitchedShare", 100.0f * p.pitchedShare, "%" }, { "beatDensity", (float) density, "" },
                    { "hostBpm", (float) p.hostBpm, "BPM" } }, revMix / 70.0f, {});

            const bool delay = ! someRoom && ! plan.conservative;   // echoes on a roomy take only add wash
            plan.set (pid::delayOn, delay ? 1.0f : 0.0f);
            if (delay)
            {
                plan.set (pid::delaySync, 1.0f);
                // 1/4 for spoken, 1/8 dotted for sung; 0.8: floaty 1/4 dotted, aggressive 1/16
                plan.set (pid::delayDivision, target.vibe == Vibe::floaty ? 10.0f : (target.vibe == Vibe::aggressive ? 3.0f : (spoken ? 9.0f : 7.0f)));
                plan.set (pid::delayTime, spoken ? 400.0f : 300.0f);  // used only without host tempo
                plan.set (pid::delayFeedback, spoken ? 12.0f : 22.0f);
                // 0.8: the echo needs the pauses - singing without gaps gets less, phrases with room more
                const float singing = p.capturedSeconds > 0.0f ? p.voicedSeconds / p.capturedSeconds : 0.8f;
                const float gaps = juce::jmap (clamp (singing, 0.6f, 0.95f), 0.6f, 0.95f, 1.3f, 0.8f);
                // 2026-11-03: x2.2 (SONOVOX calibration, see the reverb above)
                const float delayMix = std::round (clamp (((spoken ? 13.0f : 20.0f) - (density == BeatDensity::dense ? 4.0f : 0.0f)) * target.space.delayScale * gaps, 3.0f, 40.0f));
                plan.set (pid::delayMix, delayMix);
                plan.set (pid::delayLowCut, 350.0f);
                plan.set (pid::delayHighCut, 5000.0f);
                plan.set (pid::delayPingPong, 1.0f);
                plan.set (pid::delayDuckAmount, 9.0f);
                plan.set (pid::delayDuckThreshold, duckThreshold);
                plan.set (pid::delayDuckAttack, 10.0f);
                plan.set (pid::delayDuckRelease, 300.0f);
                plan.set (pid::delayToReverb, 15.0f);
                decide (M::delay, both ("Delay {0}, {1} %, geduckt", "Delay {0}, {1} %, ducked",
                                        { delaydiv::names()[(int) plan.get (pid::delayDivision)], juce::String ((int) delayMix) }),
                        both ("Tempo-synchrones Echo (folgt dem Songtempo, ohne Tempo-Info {0} ms). Es kommt erst in den Pausen hervor.",
                              "Tempo-synced echo (follows the song tempo, {0} ms without tempo info). It comes out only in the gaps.",
                              { spoken ? "400" : "300" })
                            + (density == BeatDensity::dense ? both (" Dichter Beat: leiser.", " Dense beat: quieter.") : juce::String()), false);
            }
            note (M::delay, delay, someRoom ? "room" : "style", { { "tail", p.tailSeconds, "s" }, { "beatDensity", (float) density, "" } },
                  plan.get (pid::delayMix) / 40.0f,
                  someRoom ? both ("Kein Delay: Die Aufnahme hat etwas Raum - Echos würden nur verschmieren.", "No delay: the recording has some room sound - echoes would only add wash.")
                           : both ("Kein Delay: Es wurde wenig Gesang erfasst.", "No delay: little singing was captured."));
        }

        // Output gain is set by the engine after measuring the processed result (it starts at its default)
        plan.set (pid::outputGain, cur (pid::outputGain));
        {
            ModuleLog out;
            out.globalStage = true;
            out.active = true;
            out.problem = "level";
            out.need = 1.0f;
            out.intensity = 0.0f;   // the engine sets the level on the render
            plan.log.push_back (std::move (out));
        }

        // Fair comparison: the bypass plays at the loudness of the mix
        if (cur (pid::levelMatchMode) < 0.5f)
            plan.set (pid::levelMatchMode, 1.0f);

        // The chain holds exactly what the plan uses (in the usual order)
        OfflineParameters params;
        params.setAll (current);
        params.setAll (plan.values);
        plan.routing = pruneInaudible (plan.routing, params.read (plan.routing));
        // A module that is not in the chain is off, explicitly (instances the auto mix never uses stay at 0, too)
        for (auto m : { M::eq, M::ride, M::comp, M::smash, M::deess, M::smooth, M::sat, M::doubler, M::delay, M::reverb, M::denoise, M::breath })
            for (int instance = 0; instance < dsp::maxInstances (m); ++instance)
                if (! plan.routing.contains (m, instance))
                    plan.set (dsp::instanceParamId (juce::String (dsp::moduleKey (m)) + "On", instance), 0.0f);
        fillParameters (plan.log, plan.values);
        return plan;
    }
}
