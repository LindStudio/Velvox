#include "AutoMixPlanner.h"
#include "../Parameters/ParameterLayout.h"
#include "../Parameters/SettingsReader.h"
#include "../Parameters/RoutingMigration.h"
#include "../Parameters/EqBands.h"
#include "AutoEq.h"
#include "ReportText.h"

namespace velvox::automix
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

    int setRoleBand (MixPlan& plan, const std::map<juce::String, float>& current, int instance, dsp::EqRole role,
                     const dsp::EqBand& value, bool createIfMissing)
    {
        const auto access = eqbands::accessFor (plan.values, current.empty() ? nullptr : &current);
        int i = eqbands::findBand (access, instance, role);
        if (i < 0)
        {
            if (! createIfMissing)
                return -1;
            i = eqbands::findFreeBand (access, instance);
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
        ctx.noisy = p.snrValid && p.snrDb < 28.0f;
        ctx.sibilant = p.hasIssue ("sibilance");
        ctx.harsh = p.hasIssue ("harsh");
        ctx.masking = p.maskingHint;
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
    MixPlan AutoMixPlanner::plan (const VocalProfile& p, const std::map<juce::String, float>& current)
    {
        MixPlan plan;
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

        // Levels as the modules see them: the input gain is applied before the chain
        const float inGain = cur (pid::inputGain);
        const float voice = p.voiceRmsDb + inGain;              // median voiced RMS (dBFS)
        const bool noisy = p.snrValid && p.snrDb < 28.0f;
        const bool harsh = p.hasIssue ("harsh");
        const bool sibilant = p.hasIssue ("sibilance");
        const bool dull = p.hasIssue ("dull");
        const bool compressed = p.heavilyCompressed;
        const bool roomy = likely (p, "reverberant");
        const bool someRoom = p.reverberant;

        // ---- Start from a defined state: creative effects and macros off
        int creativeWasOn = 0;
        for (auto* id : { pid::pitchOn, pid::chopOn, pid::pulseOn, pid::choirOn })
        {
            creativeWasOn += cur (id) > 0.5f ? 1 : 0;
            plan.set (id, 0.0f);
        }
        if (creativeWasOn > 0)
            plan.notes.push_back (both ("Kreativ-Effekte (Pitch, Stutter, Pulse, Choir) wurden für den Grundmix ausgeschaltet. Der vorherige Zustand lässt sich wiederherstellen.",
                                        "Creative effects (Pitch, Stutter, Pulse, Choir) were turned off for the basic mix. The previous state can be restored."));

        bool macrosWereSet = false;
        for (auto* id : { pid::macroPresence, pid::macroWarmth, pid::macroAggression, pid::macroWidth, pid::macroSpace, pid::macroDensity })
        {
            macrosWereSet = macrosWereSet || cur (id) > 0.5f;
            plan.set (id, 0.0f);
        }
        if (macrosWereSet)
            plan.notes.push_back (both ("Die SIMPEL-Regler starten wieder bei 0 - von dort aus verfeinern sie den neuen Mix.",
                                        "The SIMPLE controls start at 0 again - from there they fine-tune the new mix."));

        plan.set (pid::deessListen, 0.0f);
        plan.set (pid::smoothDelta, 0.0f);
        plan.set (pid::safetyClip, 1.0f);
        plan.set (pid::stereoWidth, 100.0f);

        if (plan.conservative)
            plan.notes.push_back (both ("Es wurde wenig Gesang erfasst - Headliner verwendet deshalb nur vorsichtige Einstellungen.",
                                        "Little singing was captured - so Headliner uses only cautious settings."));

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
            const bool rumble = p.hasIssue ("rumble");
            if (rumble)
            {
                fc = std::min (p.pitchValid ? 0.75f * p.f0LowHz : 110.0f, fc * 1.2f);
                fc = clamp (fc, 70.0f, 170.0f);
                reason << both (" Zusätzlich viel Energie unter 90 Hz ({0}).", " Also a lot of energy below 90 Hz ({0}).", { db (p.rumbleDb) });
            }
            setRoleBand (plan, current, 0, dsp::EqRole::lowCut, eqBand (dsp::EqBandType::lowCut, fc, 0.0f, 0.7071f, rumble ? 3 : 1), true);
            decide (M::eq, rumble ? both ("Low Cut {0}, 24 dB/Okt.", "Low Cut {0}, 24 dB/oct", { hz (fc) })
                                  : both ("Low Cut {0}, 12 dB/Okt.", "Low Cut {0}, 12 dB/oct", { hz (fc) }), reason, measured);
        }

        // 0.6.1 (spec item 7): narrow notches on sustained resonances and broad tonal moves
        // towards the target curve (AutoEq), in auto-mix role bands of EQ 1. Bands the auto-mix
        // does not own (user bands, Vocal Check's presence / mud) stay as they are and count in
        // the model: a resonance Vocal Check already cut is not cut twice.
        const auto eqMoves = autoeq::planMoves (autoEqState (plan, current, p, true).context);
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
        if (const auto* air = moveFor (dsp::EqRole::air); (air == nullptr || air->band.gain <= 0.0f) && (harsh || p.presenceDb > -4.0f))
            plan.notes.push_back (both ("Keine Höhenanhebung: Die Aufnahme ist bereits hell genug.",
                                        "No high boost: the recording is already bright enough."));

        // ================= Level Ride: phrase levels =================
        // Also on wet hooks with a small loudness range: in the blind listening test of
        // 2026-10-24 the ridden version was the more intelligible one (quiet syllables, not
        // only reverb tails). The onsets it lifts are caught by a faster compressor attack.
        const bool rideWanted = p.dynamicsValid && p.dynamicsDb > 8.0f && ! compressed && ! plan.conservative;
        plan.set (pid::rideOn, rideWanted ? 1.0f : 0.0f);
        if (rideWanted)
        {
            const float amount = clamp (40.0f + 4.0f * (p.dynamicsDb - 8.0f), 40.0f, 80.0f);
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

        // ================= Compressor: steady level =================
        {
            // After the rider, words sit around the rider target
            const float level = rideWanted ? plan.get (pid::rideTarget) : voice;
            float ratio = 3.0f;
            float below = 4.0f;          // threshold below the median word (RMS detector reads +3 dB for a sine)
            if (compressed)       { ratio = 1.8f; below = 1.0f; }
            else if (! rideWanted && p.dynamicsValid && p.dynamicsDb > 14.0f) { ratio = 4.0f; below = 6.0f; }
            if (plan.conservative) { ratio = std::min (ratio, 2.5f); below = std::min (below, 3.0f); }

            const float threshold = clamp (level + 3.0f - below, -60.0f, -6.0f);
            const float expectedGr = below * (1.0f - 1.0f / ratio);
            plan.set (pid::compOn, 1.0f);
            plan.set (pid::compDetector, 1.0f);   // RMS: follows the voice, not single consonants
            plan.set (pid::compThreshold, threshold);
            plan.set (pid::compRatio, ratio);
            // Fast enough to catch the onsets the ride lifts (3 ms: about 1 dB less peak-to-
            // loudness on ridden hooks than 12 ms); upfront and dense was preferred in the
            // listening test, so the default is quicker than a "natural" 12 ms
            plan.set (pid::compAttack, rideWanted ? 3.0f : (p.crestDb > 15.0f ? 5.0f : 8.0f));
            plan.set (pid::compRelease, 110.0f);
            plan.set (pid::compKnee, 6.0f);
            plan.set (pid::compMakeup, clamp (std::round (expectedGr * 2.0f) / 2.0f, 0.0f, 6.0f));
            plan.set (pid::compMix, 100.0f);
            plan.set (pid::compScHpf, 100.0f);
            decide (M::comp, both ("Kompressor {0}:1, Schwelle {1}", "Compressor {0}:1, threshold {1}", { num (ratio, 1), db (threshold) }),
                    compressed ? both ("Die Aufnahme ist bereits dicht - nur sanfte Kontrolle.", "The recording is already dense - only gentle control.")
                               : both ("Etwa {0} dB Reduktion auf typischen Wörtern für einen gleichmäßigen, durchsetzungsfähigen Pegel.",
                                       "About {0} dB of reduction on typical words for an even level that cuts through.", { num (expectedGr, 1) }),
                    true);
        }

        // ================= Vocal Smash: density (parallel) =================
        const bool smash = ! compressed && ! plan.conservative && ! p.distortionLikely;
        plan.set (pid::smashOn, smash ? 1.0f : 0.0f);
        if (smash)
        {
            plan.set (pid::smashAmount, 40.0f);
            plan.set (pid::smashBlend, 18.0f);
            plan.set (pid::smashAttack, 2.0f);
            plan.set (pid::smashRelease, 90.0f);
            plan.set (pid::smashTone, harsh ? -10.0f : (dull ? 25.0f : 10.0f));
            plan.set (pid::smashOutput, 0.0f);
            decide (M::smash, both ("Parallelkompression 18 % Blend", "Parallel compression 18 % blend"),
                    both ("Ein kleiner Anteil stark komprimierter Stimme unter dem Original gibt Dichte, ohne die Transienten zu verlieren.",
                          "A little heavily compressed voice under the original adds density without losing the transients."), false);
        }
        else if (compressed)
        {
            plan.notes.push_back (both ("Keine Parallelkompression: Die Aufnahme ist bereits stark komprimiert.",
                                        "No parallel compression: the recording is already heavily compressed."));
        }

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
        const bool smooth = p.enoughMaterial && (harsh || highRes != nullptr);
        plan.set (pid::smoothOn, smooth ? 1.0f : 0.0f);
        if (smooth)
        {
            const float amount = likely (p, "harsh") || (highRes != nullptr && highRes->strong) ? 45.0f : 30.0f;
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
                                       : both ("Betonte obere Mitten ({0}) - SMOOTH glättet nur die herausstechenden Bänder.",
                                               "Emphasized upper mids ({0}) - SMOOTH smooths only the bands that stick out.", { db (p.presenceDb) }),
                    true);
        }

        // ================= Saturation: density and colour =================
        const bool sat = ! p.distortionLikely && ! (compressed && harsh);
        plan.set (pid::satOn, sat ? 1.0f : 0.0f);
        if (sat)
        {
            const int character = harsh ? 0 : (dull || p.tiltDbPerOctave < -7.0f ? 2 : 1);   // Warm / Tube / Tape
            const float drive = plan.conservative ? 3.0f : 5.0f;
            const float mix = plan.conservative ? 20.0f : 30.0f;
            plan.set (pid::satCharacter, (float) character);
            plan.set (pid::satDrive, drive);
            plan.set (pid::satMix, mix);
            plan.set (pid::satOutput, 0.0f);
            static const char* names[] { "Warm", "Tape", "Tube", "Hard" };
            decide (M::sat, both ("Sättigung {0}, {1} %", "Saturation {0}, {1} %", { names[character], juce::String ((int) mix) }),
                    harsh ? both ("Dezente, weiche Obertöne für Dichte - ohne die Schärfe zu betonen.",
                                  "Subtle, soft harmonics for density - without emphasizing the harshness.")
                          : dull ? both ("Obertöne bringen eine dunkle Stimme nach vorne.", "Harmonics bring a dark voice forward.")
                                 : both ("Leichte Obertöne für Dichte und Durchsetzungskraft.", "Light harmonics for density and punch."), false);
        }
        else
        {
            plan.notes.push_back (p.distortionLikely ? both ("Keine Sättigung: Die Aufnahme zeigt bereits Übersteuerung.",
                                                             "No saturation: the recording already shows clipping.")
                                                     : both ("Keine Sättigung: bereits dicht und hell.", "No saturation: already dense and bright."));
        }

        // ================= Width: doubler, mono-safe =================
        const bool alreadyWide = p.stereoContent && p.correlation < 0.8f;
        const bool doubler = ! alreadyWide && ! plan.conservative && p.pitchedShare >= 0.45f;
        plan.set (pid::doublerOn, doubler ? 1.0f : 0.0f);
        if (doubler)
        {
            plan.set (pid::doublerMix, 20.0f);
            plan.set (pid::doublerWidth, 70.0f);
            plan.set (pid::doublerDelay, 14.0f);
            plan.set (pid::doublerDetune, 7.0f);
            plan.set (pid::doublerMonoSafe, 100.0f);   // mono sum stays exactly the original
            plan.set (pid::doublerTone, 8000.0f);
            decide (M::doubler, both ("Breite: Doubler 20 %, mono-sicher", "Width: Doubler 20 %, mono-safe"),
                    both ("Leichte Doppelung links/rechts. \"Mono safe\" 100 %: In Mono klingt die Stimme exakt wie ohne Doubler.",
                          "Light doubling left/right. \"Mono safe\" 100 %: in mono the voice sounds exactly as without the Doubler."), false);
        }
        else if (alreadyWide)
        {
            plan.notes.push_back (both ("Kein Doubler: Die Aufnahme ist bereits breit (Korrelation {0}).",
                                        "No Doubler: the recording is already wide (correlation {0}).", { num (p.correlation, 2) }));
        }
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
        }
        else
        {
            // Character from the pitch measurement: rhythmic speech / rap (few pitched
            // frames) stays drier and closer, long sung melodies get the Large Hall (a take
            // with some room of its own keeps the shorter plate)
            const bool spoken = p.pitchedShare < 0.45f;
            const bool sustained = p.pitchedShare > 0.75f && p.pitchSpreadSemitones > 2.0f;
            const bool largeHall = sustained && ! someRoom;
            float revMix = someRoom ? 7.0f : (plan.conservative ? 10.0f : 14.0f);
            float decay = 1.6f, preDelay = 30.0f;
            int type = 1;   // plate
            if (spoken)         { revMix = std::min (revMix, 8.0f); decay = 1.0f; type = 0; }
            else if (largeHall) { decay = 2.3f; preDelay = 40.0f; type = 3; }   // the long tail already reads wetter
            else if (sustained) { revMix = std::min (revMix + 2.0f, 16.0f); decay = 1.9f; }
            plan.set (pid::reverbOn, 1.0f);
            plan.set (pid::reverbType, (float) type);
            plan.set (pid::reverbMix, revMix);
            plan.set (pid::reverbDecay, decay);
            plan.set (pid::reverbPreDelay, preDelay);
            plan.set (pid::reverbSize, 55.0f);
            plan.set (pid::reverbDamping, harsh ? 5500.0f : 6500.0f);
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
                                                          "Spatial depth; 30 ms pre-delay and ducking keep the words up front."), true);

            const bool delay = ! someRoom && ! plan.conservative;   // echoes on a roomy take only add wash
            plan.set (pid::delayOn, delay ? 1.0f : 0.0f);
            if (delay)
            {
                plan.set (pid::delaySync, 1.0f);
                plan.set (pid::delayDivision, spoken ? 9.0f : 7.0f);   // 1/4 for spoken, 1/8 dotted for sung
                plan.set (pid::delayTime, spoken ? 400.0f : 300.0f);  // used only without host tempo
                plan.set (pid::delayFeedback, spoken ? 12.0f : 22.0f);
                plan.set (pid::delayMix, spoken ? 6.0f : 9.0f);
                plan.set (pid::delayLowCut, 350.0f);
                plan.set (pid::delayHighCut, 5000.0f);
                plan.set (pid::delayPingPong, 1.0f);
                plan.set (pid::delayDuckAmount, 9.0f);
                plan.set (pid::delayDuckThreshold, duckThreshold);
                plan.set (pid::delayDuckAttack, 10.0f);
                plan.set (pid::delayDuckRelease, 300.0f);
                plan.set (pid::delayToReverb, 15.0f);
                decide (M::delay, spoken ? both ("Delay 1/4, 6 %, geduckt", "Delay 1/4, 6 %, ducked")
                                         : both ("Delay 1/8 punktiert, 9 %, geduckt", "Delay 1/8 dotted, 9 %, ducked"),
                        both ("Tempo-synchrones Echo (folgt dem Songtempo, ohne Tempo-Info {0} ms). Es kommt erst in den Pausen hervor.",
                              "Tempo-synced echo (follows the song tempo, {0} ms without tempo info). It comes out only in the gaps.",
                              { spoken ? "400" : "300" }), false);
            }
        }

        // Output gain is set by the engine after measuring the processed result
        plan.set (pid::outputGain, cur (pid::outputGain));

        // Fair comparison: the bypass plays at the loudness of the mix
        if (cur (pid::levelMatchMode) < 0.5f)
            plan.set (pid::levelMatchMode, 1.0f);

        // The chain holds exactly what the plan uses (in the usual order)
        OfflineParameters params;
        params.setAll (current);
        params.setAll (plan.values);
        plan.routing = pruneInaudible (plan.routing, params.read (plan.routing));
        return plan;
    }
}
