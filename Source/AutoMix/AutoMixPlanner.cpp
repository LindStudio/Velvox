#include "AutoMixPlanner.h"
#include "../Parameters/ParameterLayout.h"
#include "../Parameters/SettingsReader.h"
#include "../Parameters/RoutingMigration.h"

namespace velvox::automix
{
    using M = dsp::ModuleId;

    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }
        juce::String db (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }
        juce::String hz (float f)
        {
            return f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
        }
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
            plan.notes.push_back (de ("Kreativ-Effekte (Pitch, Chop, Pulse, Choir) wurden für den Grundmix ausgeschaltet. Der vorherige Zustand lässt sich wiederherstellen."));

        bool macrosWereSet = false;
        for (auto* id : { pid::macroPresence, pid::macroWarmth, pid::macroAggression, pid::macroWidth, pid::macroSpace, pid::macroDensity })
        {
            macrosWereSet = macrosWereSet || cur (id) > 0.5f;
            plan.set (id, 0.0f);
        }
        if (macrosWereSet)
            plan.notes.push_back (de ("Die Makros stehen auf 0 - sie wirken als Feinschliff auf den neuen Mix."));

        plan.set (pid::deessListen, 0.0f);
        plan.set (pid::smoothDelta, 0.0f);
        plan.set (pid::safetyClip, 1.0f);
        plan.set (pid::stereoWidth, 100.0f);

        if (plan.conservative)
            plan.notes.push_back (de ("Es wurde wenig Gesang erfasst - Headliner verwendet deshalb nur vorsichtige Einstellungen."));

        // ================= EQ: clean-up, not colour =================
        plan.set (pid::eqOn, 1.0f);
        plan.set (pid::eqMix, 100.0f);
        plan.set (pid::lpfOn, 0.0f);
        {
            float fc = 80.0f;
            juce::String reason = de ("Keine sichere Tonhöhe gemessen - Standard-Trennfrequenz für Gesang.");
            bool measured = false;
            if (p.pitchValid)
            {
                // Well below the lowest sung fundamental: only rumble and handling noise
                // live there, and the 12 dB/oct slope stays clear of the voice
                fc = clamp (0.6f * p.f0LowHz, 70.0f, 150.0f);
                reason = de ("Tiefe gesungene Töne liegen um ") + hz (p.f0LowHz) + de (" - darunter liegt nur Rumpeln.");
                measured = true;
            }
            const bool rumble = p.hasIssue ("rumble");
            if (rumble)
            {
                fc = std::min (p.pitchValid ? 0.75f * p.f0LowHz : 110.0f, fc * 1.2f);
                fc = clamp (fc, 70.0f, 170.0f);
                reason << de (" Zusätzlich viel Energie unter 90 Hz (") << db (p.rumbleDb) << ").";
            }
            plan.set (pid::hpfOn, 1.0f);
            plan.set (pid::hpfFreq, fc);
            plan.set (pid::hpfSlope, rumble ? 1.0f : 0.0f);
            decide (M::eq, de ("Low Cut ") + hz (plan.get (pid::hpfFreq)) + (rumble ? de (", 24 dB/Okt.") : de (", 12 dB/Okt.")), reason, measured);
        }

        // Low band: body
        plan.set (pid::lowType, 0.0f);
        plan.set (pid::lowQ, 0.7071f);
        plan.set (pid::lowFreq, 180.0f);
        plan.set (pid::lowGain, 0.0f);
        if (p.enoughMaterial && p.bodyDb > -3.0f && ! p.hasIssue ("mud"))
        {
            const float g = -clamp (1.5f + 0.4f * (p.bodyDb + 3.0f), 1.5f, 3.0f);
            plan.set (pid::lowFreq, 200.0f);
            plan.set (pid::lowGain, g);
            decide (M::eq, de ("Tiefen ") + db (g) + de (" (Shelf 200 Hz)"),
                    de ("Viel Grundton-Energie (100-300 Hz: ") + db (p.bodyDb) + de (") - wirkt im Club-Mix schnell dröhnend."), true);
        }
        else if (p.enoughMaterial && p.bodyDb < -16.0f && ! noisy && p.pitchValid && p.f0LowHz < 200.0f)
        {
            const float g = clamp (1.0f + 0.15f * (-16.0f - p.bodyDb), 1.0f, 2.0f);
            plan.set (pid::lowFreq, 170.0f);
            plan.set (pid::lowGain, g);
            decide (M::eq, de ("Wärme ") + db (g) + de (" (Shelf 170 Hz)"),
                    de ("Wenig Körper (100-300 Hz: ") + db (p.bodyDb) + de (") - eine kleine Anhebung gibt Wärme."), true);
        }

        // Mid bell: one clear problem at a time - a low resonance or the low-mid build-up
        plan.set (pid::midGain, 0.0f);
        plan.set (pid::midQ, 1.0f);
        plan.set (pid::midFreq, 1500.0f);
        const Resonance* lowRes = nullptr;
        for (auto& r : p.resonances)
            if (r.hz >= 250.0f && r.hz < 1200.0f && r.strong && p.pitchSpreadSemitones > 3.0f)
            {
                lowRes = &r;
                break;
            }
        if (p.enoughMaterial && lowRes != nullptr)
        {
            const float g = -clamp (0.3f * lowRes->excessDb, 2.0f, 4.0f);
            plan.set (pid::midFreq, lowRes->hz);
            plan.set (pid::midQ, 4.0f);
            plan.set (pid::midGain, g);
            decide (M::eq, de ("Resonanz ") + hz (lowRes->hz) + " " + db (g) + de (" (schmal)"),
                    de ("Schmale Spitze ") + db (lowRes->excessDb) + de (" über der Umgebung. Unter 1,2 kHz statisch mit dem EQ, darüber arbeitet SMOOTH."), true);
        }
        else if (p.enoughMaterial && p.hasIssue ("mud"))
        {
            const float g = -clamp (1.5f + 0.6f * (p.lowMidDb + 1.0f), 1.5f, 4.0f) * (plan.conservative ? 0.6f : 1.0f);
            plan.set (pid::midFreq, clamp (p.lowMidPeakHz, 200.0f, 500.0f));
            plan.set (pid::midQ, 1.2f);
            plan.set (pid::midGain, g);
            decide (M::eq, de ("Tiefmitten ") + db (g) + de (" bei ") + hz (plan.get (pid::midFreq)),
                    de ("250-500 Hz ist ") + db (p.lowMidDb) + de (" gegenüber dem Kern - leicht mulmig. Ein breiter Schnitt macht Platz für Kick und Bass."), true);
        }
        else if (p.maskingHint && ! harsh)
        {
            plan.set (pid::midFreq, 3000.0f);
            plan.set (pid::midQ, 0.9f);
            plan.set (pid::midGain, 1.0f);
            decide (M::eq, de ("Präsenz +1,0 dB bei 3 kHz"),
                    de ("Der Beat ist im Präsenzbereich lauter als die Stimme (Sidechain-Messung, nur ein Hinweis) - daher nur eine kleine Anhebung."), false);
        }

        // High band: air / brightness, never on top of harshness or strong S sounds
        plan.set (pid::highType, 0.0f);
        plan.set (pid::highQ, 0.7071f);
        plan.set (pid::highFreq, 10000.0f);
        plan.set (pid::highGain, 0.0f);
        if (p.enoughMaterial && dull && ! harsh)
        {
            float g = clamp (1.5f + 0.4f * (-16.0f - p.topDb), 1.5f, 4.5f);
            if (noisy || sibilant)
                g = std::min (g, 2.0f);
            plan.set (pid::highFreq, 9000.0f);
            plan.set (pid::highGain, g);
            decide (M::eq, de ("Höhen ") + db (g) + de (" (Shelf 9 kHz)"),
                    de ("Die Aufnahme ist dunkel (über 2 kHz: ") + db (p.topDb) + ")" + (noisy || sibilant ? de (" - begrenzt wegen Rauschen/S-Lauten.") : juce::String (".")), true);
        }
        else if (p.enoughMaterial && p.airDb < -30.0f && ! harsh && ! sibilant && ! noisy)
        {
            plan.set (pid::highFreq, 11000.0f);
            plan.set (pid::highGain, 1.5f);
            decide (M::eq, de ("Luft +1,5 dB (Shelf 11 kHz)"),
                    de ("Wenig Energie über 10 kHz (") + db (p.airDb) + de (") - etwas Luft hilft der Stimme durch dichte Dance-Mixe."), true);
        }
        else if (harsh || p.presenceDb > -4.0f)
        {
            plan.notes.push_back (de ("Keine Höhenanhebung: Die Aufnahme ist bereits hell genug."));
        }

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
            decide (M::ride, de ("Level Ride ") + juce::String (juce::roundToInt (amount)) + de (" %, max. +") + juce::String ((int) boost) + " dB",
                    de ("Laute und leise Wörter liegen ") + juce::String (p.dynamicsDb, 1) + de (" dB auseinander - leise Wörter werden angehoben, bevor der Kompressor arbeitet.")
                        + (noisy ? de (" Anhebung begrenzt (Rauschen).") : juce::String()), true);
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
            decide (M::comp, de ("Kompressor ") + juce::String (ratio, 1) + de (":1, Schwelle ") + db (threshold),
                    compressed ? de ("Die Aufnahme ist bereits dicht - nur sanfte Kontrolle.")
                               : de ("Etwa ") + juce::String (expectedGr, 1) + de (" dB Reduktion auf typischen Wörtern für einen gleichmäßigen, durchsetzungsfähigen Pegel."),
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
            decide (M::smash, de ("Parallelkompression 18 % Blend"),
                    de ("Ein kleiner Anteil stark komprimierter Stimme unter dem Original gibt Dichte, ohne die Transienten zu verlieren."), false);
        }
        else if (compressed)
        {
            plan.notes.push_back (de ("Keine Parallelkompression: Die Aufnahme ist bereits stark komprimiert."));
        }

        // ================= De-esser =================
        const bool deess = p.sibilanceValid && p.sibilanceVsVowelDb > -9.0f;
        plan.set (pid::deessOn, deess ? 1.0f : 0.0f);
        if (deess)
        {
            const bool strong = p.hasIssue ("sibilance");
            const float range = p.sibilanceVsVowelDb > 0.0f ? 8.0f : (likely (p, "sibilance") ? 6.0f : (strong ? 5.0f : 3.0f));
            configureDeEsser (plan, p, inGain, range);
            decide (M::deess, de ("De-Esser ab ") + hz (p.deessDetectorHz) + ", max. " + juce::String ((int) range) + " dB",
                    de ("S-Laute ") + db (p.sibilanceVsVowelDb) + de (" gegenüber den Vokalen. ")
                        + (strong ? de ("Schwelle zwischen Vokal- und S-Pegel, damit nur S-Laute reduziert werden.")
                                  : de ("Vorsorglich dezent: Kompression und Präsenz heben S-Laute an.")), true);
        }
        else
        {
            plan.notes.push_back (p.sibilanceValid ? de ("De-Esser bleibt aus: S-Laute sind unauffällig.")
                                                   : de ("De-Esser bleibt aus: zu wenige S-Laute zum Messen."));
        }

        // ================= Smooth: adaptive resonance control =================
        const Resonance* highRes = nullptr;
        for (auto& r : p.resonances)
            if (r.hz >= 1200.0f && r.excessDb > 7.0f)
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
            decide (M::smooth, de ("Resonanzkontrolle ") + juce::String ((int) amount) + " %",
                    highRes != nullptr ? de ("Schmale Spitze um ") + hz (highRes->hz) + de (" - SMOOTH senkt sie nur, wenn sie hervorsticht.")
                                       : de ("Betonte obere Mitten (") + db (p.presenceDb) + de (") - SMOOTH glättet nur die herausstechenden Bänder."),
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
            decide (M::sat, de ("Sättigung ") + names[character] + ", " + juce::String ((int) mix) + " %",
                    harsh ? de ("Dezente, weiche Obertöne für Dichte - ohne die Schärfe zu betonen.")
                          : dull ? de ("Obertöne bringen eine dunkle Stimme nach vorne.")
                                 : de ("Leichte Obertöne für Dichte und Durchsetzungskraft."), false);
        }
        else
        {
            plan.notes.push_back (p.distortionLikely ? de ("Keine Sättigung: Die Aufnahme zeigt bereits Übersteuerung.")
                                                     : de ("Keine Sättigung: bereits dicht und hell."));
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
            decide (M::doubler, de ("Breite: Doubler 20 %, mono-sicher"),
                    de ("Leichte Doppelung links/rechts. \"Mono safe\" 100 %: In Mono klingt die Stimme exakt wie ohne Doubler."), false);
        }
        else if (alreadyWide)
        {
            plan.notes.push_back (de ("Kein Doubler: Die Aufnahme ist bereits breit (Korrelation ") + juce::String (p.correlation, 2) + ").");
        }
        plan.set (pid::monoBelow, 120.0f);

        // ================= Space: reverb and delay as sends, ducked =================
        const float duckThreshold = clamp (voice - 10.0f, -60.0f, -10.0f);
        plan.set (pid::duckSource, 0.0f);
        if (roomy)
        {
            plan.set (pid::reverbOn, 0.0f);
            plan.set (pid::delayOn, 0.0f);
            plan.notes.push_back (de ("Kein Hall und kein Delay: Die Aufnahme hat bereits deutlichen Raum (") + juce::String (p.tailSeconds, 1) + de (" s Ausklang)."));
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
            decide (M::reverb, (type == 3 ? de ("Large Hall ") : type == 1 ? de ("Plate-Hall ") : de ("Room-Hall ")) + juce::String ((int) revMix) + " %, "
                                   + juce::String (decay, 1).replaceCharacter ('.', ',') + de (" s, geduckt"),
                    someRoom ? de ("Die Aufnahme hat etwas Raum - deshalb nur wenig Hall.")
                             : spoken ? de ("Sprechgesang/Rap (wenig gehaltene Töne gemessen): kurzer, trockener Raum, damit der Text vorne bleibt.")
                                      : largeHall ? de ("Lange, gehaltene Melodiebögen: Large Hall mit 40 ms Pre-Delay trägt die Töne; Ducking hält die Wörter vorne.")
                                      : sustained ? de ("Viele gehaltene Töne: etwas längere Plate für Tiefe; Ducking hält die Wörter vorne.")
                                                  : de ("Räumliche Tiefe; 30 ms Pre-Delay und Ducking halten die Wörter vorne."), true);

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
                decide (M::delay, spoken ? de ("Delay 1/4, 6 %, geduckt") : de ("Delay 1/8 punktiert, 9 %, geduckt"),
                        de ("Tempo-synchrones Echo (folgt dem Songtempo, ohne Tempo-Info ") + (spoken ? "400" : "300")
                            + de (" ms). Es kommt erst in den Pausen hervor."), false);
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
