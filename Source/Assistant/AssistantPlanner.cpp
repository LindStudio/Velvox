#include "AssistantPlanner.h"
#include "../Parameters/ParameterLayout.h"
#include "StyleProfiles.h"

// Source is compiled as UTF-8 (/utf-8): the German texts below are plain literals.
namespace velvox::assistant
{
    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }

        juce::String num (float v, int decimals = 1)
        {
            return (v > 0.0f ? "+" : "") + juce::String (v, decimals);
        }

        // Raising a parameter moves these goals (used to enforce constraints)
        struct GoalLink { const char* paramId; Goal goal; int sign; };
        const GoalLink goalLinks[] {
            { pid::lowGain, Goal::bass, 1 }, { pid::lowGain, Goal::warmth, 1 }, { pid::hpfFreq, Goal::bass, -1 },
            { pid::midGain, Goal::presence, 1 }, { pid::highGain, Goal::brightness, 1 }, { pid::highGain, Goal::harshness, 1 },
            { pid::reverbMix, Goal::reverb, 1 }, { pid::reverbOn, Goal::reverb, 1 }, { pid::macroSpace, Goal::reverb, 1 },
            { pid::delayMix, Goal::delay, 1 }, { pid::delayOn, Goal::delay, 1 },
            { pid::outputGain, Goal::level, 1 },
            { pid::stereoWidth, Goal::width, 1 }, { pid::doublerWidth, Goal::width, 1 }, { pid::doublerOn, Goal::width, 1 },
            { pid::doublerMix, Goal::width, 1 },
            { pid::smoothAmount, Goal::harshness, -1 }, { pid::smoothOn, Goal::harshness, -1 },
            { pid::deessRange, Goal::sibilance, -1 }, { pid::deessOn, Goal::sibilance, -1 },
            { pid::compRatio, Goal::compression, 1 }, { pid::compThreshold, Goal::compression, -1 },
            { pid::smashBlend, Goal::punch, 1 }, { pid::smashOn, Goal::punch, 1 },
            { pid::satDrive, Goal::warmth, 1 }, { pid::satOn, Goal::warmth, 1 },
            { pid::macroPresence, Goal::presence, 1 },
        };

        class Builder
        {
        public:
            Builder (const ParsedRequest& r, const AssistantContext& c) : req (r), ctx (c) {}

            const ParsedRequest& req;
            const AssistantContext& ctx;
            Plan plan;
            juce::String source;   // style profile of the wish being planned (empty = typed)

            float original (const juce::String& id) const
            {
                const auto* spec = findParameterSpec (id);
                return ctx.get (id, spec != nullptr ? spec->defaultValue : 0.0f);
            }

            // The value the plan currently proposes (or the current one)
            float value (const juce::String& id) const
            {
                for (auto& ch : plan.changes)
                    if (ch.paramId == id)
                        return ch.proposed;
                return original (id);
            }

            bool on (const char* id) const { return value (id) > 0.5f; }

            Change* find (const juce::String& id)
            {
                for (auto& ch : plan.changes)
                    if (ch.paramId == id)
                        return &ch;
                return nullptr;
            }

            void note (Note::Kind kind, const juce::String& text)
            {
                for (auto& n : plan.notes)
                    if (n.text == text)
                        return;
                plan.notes.push_back ({ kind, text });
            }

            // Proposes `target` for a parameter. Returns false when nothing changes
            // (already there, or already at the end of its range).
            bool set (const char* id, float target, const juce::String& title, const juce::String& reason,
                      bool measured = false, bool selected = true, const char* group = "")
            {
                const auto* spec = findParameterSpec (id);
                if (spec == nullptr)
                {
                    note (Note::Kind::unavailable, de ("Interner Fehler: Parameter ") + id + de (" existiert nicht - übersprungen."));
                    return false;
                }

                const float orig = original (id);
                const float before = value (id);
                const float legal = legaliseValue (id, target);
                if (std::abs (legal - before) < 1.0e-4f)
                {
                    if (std::abs (target - before) > 1.0e-4f)
                        note (Note::Kind::info, juce::String (spec->name) + de (" ist bereits am Ende seines Bereichs (")
                                                    + displayValue (id, before) + de (") - weiter geht es nicht."));
                    return false;
                }

                if (auto* existing = find (id))
                {
                    existing->proposed = legal;
                    existing->proposedText = displayValue (id, legal);
                    if (! existing->reason.contains (reason))
                        existing->reason << " " << reason;
                    existing->measured = existing->measured || measured;
                    if (std::abs (legal - orig) < 1.0e-4f)
                        plan.changes.erase (plan.changes.begin() + (existing - plan.changes.data()));
                    return true;
                }

                Change ch;
                ch.paramId = id;
                if (source.isNotEmpty())
                    ch.title << de (" (") << source << ")";
                ch.module = moduleLabelFor (id);
                ch.title = title;
                ch.paramName = spec->name;
                ch.current = orig;
                ch.proposed = legal;
                ch.currentText = displayValue (id, orig);
                ch.proposedText = displayValue (id, legal);
                ch.reason = source.isNotEmpty() ? de ("Stilprofil ") + source + ": " + reason : reason;
                ch.measured = measured;
                ch.selected = selected;
                ch.group = group;
                plan.changes.push_back (ch);
                return true;
            }

            // Start value for a module that is being switched on. Its stored value was
            // inaudible so far: keep it when it is close to the target, otherwise say why it moves.
            void setStart (const char* id, float target, const juce::String& title, const juce::String& reason, const char* group, bool selected = true)
            {
                const float stored = value (id);
                if (std::abs (stored - target) <= std::max (2.0f, std::abs (target) * 0.3f))
                    return;
                set (id, target, title, reason + de (" Der bisherige Wert (") + displayValue (id, stored)
                                          + de (") war nicht hörbar, weil das Modul aus war."), false, selected, group);
            }

            void ensureOn (const char* onId, const juce::String& module, const char* group)
            {
                if (on (onId))
                    return;
                set (onId, 1.0f, module + de (" einschalten"),
                     de ("Das Modul ") + module + de (" ist aus - ohne es wäre die folgende Änderung nicht hörbar."), false, true, group);
            }

            // ------------------------------------------------------------------
            // Rules per goal

            void bass (const Intent& in)
            {
                const float k = AssistantPlanner::factor (in.intensity);
                const bool keepWarmth = req.hasConstraint (Goal::warmth, -1)
                                        || std::any_of (req.intents.begin(), req.intents.end(), [] (const Intent& i) { return i.goal == Goal::warmth && i.direction > 0; });

                if (in.direction < 0)
                {
                    juce::String measuredText;
                    bool measured = false;
                    float hpfExtra = 0.0f;
                    if (ctx.analysisValid)
                    {
                        measuredText = de (" Gemessen (Vocal Check, ") + juce::String (ctx.voicedSeconds, 0) + de (" s Stimme): 40-100 Hz liegen bei ")
                                       + num (ctx.bandDb[0]) + de (" dB, 100-300 Hz bei ") + num (ctx.bandDb[1]) + de (" dB gegenüber dem Kern der Stimme.");
                        measured = true;
                        if (ctx.bandDb[0] > -10.0f)
                            hpfExtra = 15.0f;   // clearly measurable rumble
                    }

                    ensureOn (pid::eqOn, "EQ", "eq");
                    const float hpf = on (pid::hpfOn) ? value (pid::hpfFreq) + 25.0f * k + hpfExtra
                                                      : std::max (value (pid::hpfFreq), 80.0f + 15.0f * k + hpfExtra);
                    if (! on (pid::hpfOn))
                        set (pid::hpfOn, 1.0f, de ("Low Cut einschalten"), de ("Ein Low Cut entfernt Rumpeln und Tiefbass, den eine Stimme selten braucht."), false, true, "eq");
                    set (pid::hpfFreq, std::min (hpf, 200.0f), de ("Low Cut höher setzen"),
                         de ("Du möchtest weniger tieffrequente Energie. Der Low Cut nimmt alles unterhalb dieser Frequenz weg (höchstens 200 Hz, damit die Stimme nicht dünn wird).") + measuredText,
                         measured, true, "eq");

                    if (keepWarmth)
                    {
                        note (Note::Kind::info, de ("Die Wärme soll erhalten bleiben: Die Tiefen werden nur mit dem Low Cut reduziert, nicht mit dem Low Shelf."));
                        return;
                    }
                    set (pid::lowGain, std::max (value (pid::lowGain) - 3.0f * k, -12.0f), de ("Tiefen reduzieren"),
                         de ("Senkt den Bereich um ") + displayValue (pid::lowFreq, value (pid::lowFreq)) + de (" und darunter ab - weniger Wummern, mehr Klarheit."),
                         false, true, "eq");
                }
                else
                {
                    ensureOn (pid::eqOn, "EQ", "eq");
                    set (pid::lowGain, std::min (value (pid::lowGain) + 2.5f * k, 9.0f), de ("Tiefen anheben"),
                         de ("Du möchtest mehr Tiefen: Der Low Shelf hebt den unteren Bereich der Stimme an."), false, true, "eq");
                    if (on (pid::hpfOn) && value (pid::hpfFreq) > 100.0f)
                        set (pid::hpfFreq, std::max (60.0f, value (pid::hpfFreq) - 30.0f * k), de ("Low Cut tiefer setzen"),
                             de ("Der Low Cut liegt recht hoch und würde die zusätzlichen Tiefen wieder wegnehmen."), false, true, "eq");
                }
            }

            void presence (const Intent& in, bool fromBeat = false)
            {
                const float k = AssistantPlanner::factor (in.intensity) * (fromBeat ? 0.75f : 1.0f);
                const bool noHarsher = req.hasConstraint (Goal::harshness, 1) || req.hasConstraint (Goal::brightness, 1);
                const float midGain = value (pid::midGain), midFreq = value (pid::midFreq);
                const bool midFree = std::abs (midGain) < 0.5f;
                const bool inPresence = midFreq >= 1800.0f && midFreq <= 6000.0f;

                juce::String measuredText;
                if (ctx.analysisValid)
                    measuredText = de (" Zum Vergleich gemessen (Vocal Check): 2-5 kHz liegen bei ") + num (ctx.bandDb[3])
                                   + de (" dB gegenüber dem Kern der Stimme (300 Hz - 2 kHz).");

                if (in.direction > 0)
                {
                    const juce::String why = fromBeat
                        ? de ("Damit sich die Vocal gegen den Beat durchsetzt, hilft meist etwas mehr Präsenz (2-4 kHz) - der Bereich, in dem das Ohr Sprache versteht. Allgemeine Regel, keine Messung des Beats.")
                        : de ("Du möchtest, dass die Vocal besser wahrgenommen wird. Eine moderate Anhebung im Präsenzbereich bringt die Stimme nach vorne.");

                    if (! midFree && ! inPresence)
                    {
                        note (Note::Kind::info, de ("Das Mitten-Band des EQ wird bereits bei ") + displayValue (pid::midFreq, midFreq)
                                                    + de (" genutzt - statt es zu verbiegen, schlägt der Assistant das Presence-Makro vor."));
                        set (pid::macroPresence, std::min (100.0f, value (pid::macroPresence) + 20.0f * k), de ("Presence-Makro erhöhen"),
                             why + de (" Das Makro wirkt zusätzlich auf EQ und De-Esser.") + measuredText, false, true, "macro");
                        return;
                    }

                    ensureOn (pid::eqOn, "EQ", "eq");
                    const float freq = inPresence ? midFreq : (noHarsher ? 2200.0f : 3000.0f);
                    if (std::abs (freq - midFreq) > 1.0f)
                        set (pid::midFreq, freq, de ("Mitten-Band auf den Präsenzbereich legen"),
                             de ("Das Mitten-Band ist noch ungenutzt und wird für die Präsenz auf ") + displayValue (pid::midFreq, freq)
                                 + (noHarsher ? de (" gesetzt - bewusst etwas tiefer, damit die Stimme nicht schärfer wird.") : de (" gesetzt.")),
                             false, true, "eq");
                    const float step = (noHarsher ? 1.5f : 2.5f) * k;
                    set (pid::midGain, std::min (midGain + step, noHarsher ? 3.0f : 6.0f), de ("Präsenz anheben"), why + measuredText, false, true, "eq");
                    if (noHarsher)
                        note (Note::Kind::info, de ("Damit die Stimme nicht schärfer wird, bleiben die Höhen unverändert und die Präsenz-Anhebung ist begrenzt."));
                }
                else
                {
                    if (! midFree && ! inPresence)
                    {
                        note (Note::Kind::info, de ("Das Mitten-Band liegt nicht im Präsenzbereich - für weniger Präsenz wird es nicht umgebogen. Versuche „weniger Höhen“ oder „weicher“."));
                        return;
                    }
                    ensureOn (pid::eqOn, "EQ", "eq");
                    if (midFree && ! inPresence)
                        set (pid::midFreq, 3000.0f, de ("Mitten-Band auf den Präsenzbereich legen"), de ("Das ungenutzte Mitten-Band wird für die Absenkung genutzt."), false, true, "eq");
                    set (pid::midGain, std::max (midGain - 2.0f * AssistantPlanner::factor (in.intensity), -8.0f), de ("Präsenz reduzieren"),
                         de ("Du möchtest die Stimme weniger präsent: Der Bereich um 3 kHz wird etwas abgesenkt.") + measuredText, false, true, "eq");
                }
            }

            void brightness (const Intent& in)
            {
                const float k = AssistantPlanner::factor (in.intensity);
                ensureOn (pid::eqOn, "EQ", "eq");
                if (in.direction > 0)
                {
                    const bool careful = req.hasConstraint (Goal::harshness, 1);
                    set (pid::highGain, std::min (value (pid::highGain) + (careful ? 1.0f : 2.0f) * k, careful ? 3.0f : 8.0f), de ("Höhen anheben"),
                         de ("Du möchtest mehr Höhen / Luft: Der High Shelf ab ") + displayValue (pid::highFreq, value (pid::highFreq)) + de (" hebt den oberen Bereich an."),
                         false, true, "eq");
                    if (! on (pid::deessOn))
                        note (Note::Kind::info, de ("Mehr Höhen machen auch S-Laute lauter - der De-Esser ist aus. Bei Zischen: „weniger S-Laute“."));
                }
                else
                {
                    set (pid::highGain, std::max (value (pid::highGain) - 2.0f * k, -9.0f), de ("Höhen absenken"),
                         de ("Du möchtest weniger Höhen: Der High Shelf senkt den oberen Bereich ab."), false, true, "eq");
                }
            }

            void reverb (const Intent& in)
            {
                const float k = AssistantPlanner::factor (in.intensity);
                if (in.direction > 0)
                {
                    if (! on (pid::reverbOn))
                    {
                        set (pid::reverbOn, 1.0f, de ("Reverb einschalten"), de ("Der Reverb ist aus - für mehr Raum wird er eingeschaltet."), false, true, "reverb");
                        setStart (pid::reverbMix, 8.0f + 6.0f * k, de ("Reverb-Anteil setzen"),
                                  de ("Du möchtest mehr räumlichen Nachhall. Startwert bewusst dezent, damit die Stimme vorne bleibt."), "reverb");
                    }
                    else
                    {
                        set (pid::reverbMix, std::min (value (pid::reverbMix) + 5.0f * k, 50.0f), de ("Reverb-Anteil erhöhen"),
                             de ("Du möchtest mehr räumlichen Nachhall: Der Anteil des Reverbs wird erhöht."), false, true, "reverb");
                    }
                    if (in.intensity == Intensity::strong)
                        set (pid::reverbDecay, std::min (value (pid::reverbDecay) + 0.5f, 4.0f), de ("Nachhall verlängern"),
                             de ("Für deutlich mehr Raum klingt der Hall zusätzlich etwas länger nach."), false, true, "reverb");
                    if (ctx.get (pid::macroSpace) > 0.0f)
                        note (Note::Kind::info, de ("Das Space-Makro ist aktiv und fügt zusätzlich Hall hinzu."));
                }
                else
                {
                    const bool macro = value (pid::macroSpace) > 0.0f;
                    if (! on (pid::reverbOn) && ! macro)
                    {
                        note (Note::Kind::info, de ("Der Reverb ist bereits aus - es gibt keinen Hall zu reduzieren."));
                        return;
                    }
                    if (on (pid::reverbOn))
                    {
                        const float mix = value (pid::reverbMix) - 5.0f * k;
                        set (pid::reverbMix, std::max (mix, 0.0f), de ("Reverb-Anteil reduzieren"),
                             de ("Du möchtest weniger Hall: Der Anteil des Reverbs wird verringert."), false, true, "reverb");
                        if (mix <= 2.0f)
                            set (pid::reverbOn, 0.0f, de ("Reverb ausschalten"), de ("Bei so wenig Anteil ist es sauberer, den Reverb ganz auszuschalten."), false, true, "reverb");
                    }
                    if (macro)
                        set (pid::macroSpace, std::max (0.0f, value (pid::macroSpace) - 20.0f * k), de ("Space-Makro verringern"),
                             de ("Das Space-Makro fügt zusätzlich Hall hinzu - es wird mit reduziert, sonst bliebe der Hall hörbar."), false, true, "macro");
                }
            }

            void delay (const Intent& in)
            {
                const float k = AssistantPlanner::factor (in.intensity);
                if (in.direction > 0)
                {
                    if (! on (pid::delayOn))
                    {
                        set (pid::delayOn, 1.0f, de ("Delay einschalten"), de ("Das Delay ist aus - für Echos wird es eingeschaltet."), false, true, "delay");
                        setStart (pid::delayMix, 8.0f + 5.0f * k, de ("Delay-Anteil setzen"), de ("Du möchtest Echos. Startwert dezent."), "delay");
                    }
                    else
                    {
                        set (pid::delayMix, std::min (value (pid::delayMix) + 5.0f * k, 40.0f), de ("Delay-Anteil erhöhen"),
                             de ("Du möchtest mehr Echo: Der Anteil des Delays wird erhöht."), false, true, "delay");
                    }
                }
                else
                {
                    if (! on (pid::delayOn))
                    {
                        note (Note::Kind::info, de ("Das Delay ist bereits aus."));
                        return;
                    }
                    const float mix = value (pid::delayMix) - 5.0f * k;
                    set (pid::delayMix, std::max (mix, 0.0f), de ("Delay-Anteil reduzieren"), de ("Du möchtest weniger Echo."), false, true, "delay");
                    if (mix <= 2.0f)
                        set (pid::delayOn, 0.0f, de ("Delay ausschalten"), de ("Bei so wenig Anteil ist es sauberer, das Delay auszuschalten."), false, true, "delay");
                }
            }

            void level (const Intent& in)
            {
                const float k = AssistantPlanner::factor (in.intensity);
                const float step = 2.0f * k * (float) in.direction;
                set (pid::outputGain, juce::jlimit (-12.0f, 12.0f, value (pid::outputGain) + step),
                     in.direction > 0 ? de ("Vocal lauter") : de ("Vocal leiser"),
                     de ("Ändert den Ausgangspegel des Plugins um ") + num (step) + de (" dB.")
                         + (in.direction > 0 ? de (" Clip Guard schützt vor Übersteuerung über 0 dBFS.") : juce::String()),
                     false, true, "output");
            }

            void width (const Intent& in)
            {
                const float k = AssistantPlanner::factor (in.intensity);
                const bool mono = req.hasConstraint (Goal::width, 0);

                if (in.direction > 0)
                {
                    bool risky = false;
                    if (ctx.correlationKnown)
                    {
                        risky = ctx.correlation < 0.3f;
                        note (risky ? Note::Kind::warning : Note::Kind::info,
                              de ("Gemessen: Phasenkorrelation am Ausgang aktuell ") + juce::String (ctx.correlation, 2)
                                  + (risky ? de (" - schon jetzt nah an Auslöschungen in Mono. Die Verbreiterung wird deshalb begrenzt.")
                                           : de (" (+1 = mono-sicher).")));
                    }
                    const float limit = (mono || risky) ? 0.6f : 1.0f;

                    if (! on (pid::doublerOn))
                    {
                        set (pid::doublerOn, 1.0f, de ("Doubler einschalten"),
                             de ("Breite entsteht bei einer Vocal am natürlichsten über gedoppelte Stimmen links und rechts."), false, true, "doubler");
                        setStart (pid::doublerMix, 28.0f + 8.0f * k * limit, de ("Doubler-Anteil setzen"), de ("Dezent, damit die Originalstimme im Zentrum führt."), "doubler");
                        setStart (pid::doublerWidth, 55.0f + 15.0f * k * limit, de ("Doubler-Breite setzen"), de ("Wie weit die gedoppelten Stimmen nach links und rechts gehen."), "doubler");
                    }
                    else
                    {
                        set (pid::doublerWidth, std::min (value (pid::doublerWidth) + 15.0f * k * limit, mono ? 80.0f : 100.0f), de ("Doubler breiter"),
                             de ("Du möchtest die Vocal breiter: Die gedoppelten Stimmen gehen weiter nach außen."), false, true, "doubler");
                    }

                    if (ctx.stereoKnown && ctx.stereoSource)
                        set (pid::stereoWidth, std::min (value (pid::stereoWidth) + 20.0f * k * limit, mono ? 130.0f : 170.0f), de ("Stereobreite erhöhen"),
                             de ("Das Eingangssignal ist stereo (gemessen) - die Stereobreite verstärkt den Seitenanteil."), true, true, "output");
                    else
                        note (Note::Kind::info, de ("Stereo Width wirkt nur auf ein Stereosignal. Ob die Aufnahme stereo ist, ist nicht gemessen (Vocal Check aus oder zu wenig Material) - die Breite entsteht deshalb über den Doubler."));

                    if (mono)
                    {
                        set (pid::doublerMonoSafe, std::max (value (pid::doublerMonoSafe), 75.0f), de ("Doubler mono-sicherer"),
                             de ("Du möchtest Mono-Kompatibilität: Der Doubler hält mehr Monoanteil, damit in Mono nichts ausgelöscht wird."), false, true, "doubler");
                        set (pid::monoBelow, std::max (value (pid::monoBelow), 150.0f), de ("Tiefen mono halten"),
                             de ("Unterhalb dieser Frequenz bleibt die Vocal mono - stabil auf Club-Anlagen und Handys."), false, true, "output");
                        note (Note::Kind::info, de ("Mono-Kompatibilität: Prüfe das Ergebnis mit dem Mono-Schalter in der rechten Leiste und beobachte die Korrelationsanzeige."));
                    }
                }
                else
                {
                    if (on (pid::doublerOn))
                        set (pid::doublerWidth, std::max (value (pid::doublerWidth) - 20.0f * k, 0.0f), de ("Doubler schmaler"),
                             de ("Du möchtest die Vocal schmaler: Die gedoppelten Stimmen rücken zur Mitte."), false, true, "doubler");
                    set (pid::stereoWidth, std::max (value (pid::stereoWidth) - 20.0f * k, 50.0f), de ("Stereobreite reduzieren"),
                         de ("Verringert den Seitenanteil des Ausgangs (100 % = unverändert)."), false, true, "output");
                }
            }

            void warmth (const Intent& in)
            {
                const float k = AssistantPlanner::factor (in.intensity);
                const bool bassDown = std::any_of (req.intents.begin(), req.intents.end(), [] (const Intent& i) { return i.goal == Goal::bass && i.direction < 0; });
                if (in.direction > 0)
                {
                    if (! bassDown && ! req.hasConstraint (Goal::bass, 1))
                    {
                        ensureOn (pid::eqOn, "EQ", "eq");
                        set (pid::lowGain, std::min (value (pid::lowGain) + 1.5f * k, 6.0f), de ("Wärme im Low Shelf"),
                             de ("Etwas mehr Körper im unteren Bereich macht die Stimme wärmer."), false, true, "eq");
                    }
                    const bool satSelected = in.intensity == Intensity::strong || bassDown;
                    if (! on (pid::satOn))
                    {
                        set (pid::satOn, 1.0f, de ("Sättigung einschalten (optional)"),
                             de ("Sanfte Sättigung fügt warme Obertöne hinzu.") + (satSelected ? juce::String() : de (" Optional - aktiviere es, wenn der EQ allein nicht reicht.")),
                             false, satSelected, "sat");
                        set (pid::satCharacter, 0.0f, de ("Charakter Warm"), de ("Der weichste Sättigungscharakter."), false, satSelected, "sat");
                        setStart (pid::satMix, 35.0f + 10.0f * k, de ("Sättigung dezent beimischen"), de ("Nur ein Teil des Signals wird gesättigt - die Stimme bleibt natürlich."), "sat", satSelected);
                    }
                    if (bassDown)
                        note (Note::Kind::info, de ("Weniger Tiefen und mehr Wärme zugleich: Die Wärme kommt deshalb aus der Sättigung, nicht aus dem Low Shelf."));
                }
                else
                {
                    ensureOn (pid::eqOn, "EQ", "eq");
                    set (pid::lowGain, std::max (value (pid::lowGain) - 1.5f * k, -9.0f), de ("Weniger Wärme"),
                         de ("Etwas weniger Körper im unteren Bereich macht die Stimme schlanker."), false, true, "eq");
                    if (on (pid::satOn))
                        set (pid::satMix, std::max (value (pid::satMix) - 15.0f * k, 0.0f), de ("Weniger Sättigung"),
                             de ("Die Sättigung färbt die Stimme - ihr Anteil wird reduziert."), false, true, "sat");
                }
            }

            void harshness (const Intent& in)
            {
                const float k = AssistantPlanner::factor (in.intensity);
                if (in.direction > 0)
                {
                    note (Note::Kind::unavailable, de ("Gezielt „mehr Schärfe“ bietet Velvox nicht an. Für mehr Biss: „mehr Präsenz“ oder „mehr Druck“."));
                    return;
                }
                // Typed wishes for presence / highs are never taken back by this rule
                const bool keepClarity = req.hasConstraint (Goal::presence, -1) || req.hasConstraint (Goal::brightness, -1)
                                         || std::any_of (req.intents.begin(), req.intents.end(), [] (const Intent& i)
                                                         { return (i.goal == Goal::presence || i.goal == Goal::brightness) && i.direction > 0; });

                juce::String measuredText;
                bool measured = false;
                if (ctx.analysisValid)
                {
                    measuredText = de (" Gemessen (Vocal Check): 2-5 kHz liegen bei ") + num (ctx.bandDb[3]) + de (" dB gegenüber dem Kern der Stimme.");
                    measured = true;
                }

                if (! on (pid::smoothOn))
                {
                    set (pid::smoothOn, 1.0f, de ("SMOOTH einschalten"),
                         de ("SMOOTH erkennt störende Resonanzen und senkt nur diese ab - die Stimme bleibt sonst unverändert."), false, true, "smooth");
                    setStart (pid::smoothAmount, 35.0f + 12.0f * k, de ("Resonanzen glätten"),
                              de ("Du möchtest weniger Schärfe. SMOOTH reduziert harte Spitzen adaptiv statt pauschal.") + measuredText, "smooth");
                }
                else
                {
                    set (pid::smoothAmount, std::min (value (pid::smoothAmount) + 15.0f * k, 90.0f), de ("Resonanzen stärker glätten"),
                         de ("Du möchtest weniger Schärfe: SMOOTH greift stärker ein.") + measuredText, measured, true, "smooth");
                }

                if (keepClarity)
                {
                    note (Note::Kind::info, de ("Die Stimme soll klar bleiben: Präsenz und Höhen werden nicht pauschal abgesenkt - SMOOTH greift nur bei störenden Resonanzen ein."));
                    return;
                }
                const float midGain = value (pid::midGain), midFreq = value (pid::midFreq);
                if (midGain > 0.5f && midFreq >= 2000.0f && midFreq <= 6000.0f)
                    set (pid::midGain, std::max (0.0f, midGain - 1.5f * k), de ("Präsenz-Anhebung zurücknehmen"),
                         de ("Der EQ hebt bei ") + displayValue (pid::midFreq, midFreq) + de (" an - genau dort entsteht Schärfe."), false, true, "eq");
                if (value (pid::highGain) > 3.0f)
                    set (pid::highGain, value (pid::highGain) - 1.0f * k, de ("Höhen-Anhebung etwas zurücknehmen"),
                         de ("Der High Shelf ist deutlich angehoben und trägt zur Schärfe bei."), false, true, "eq");
            }

            void sibilance (const Intent& in)
            {
                const float k = AssistantPlanner::factor (in.intensity);
                if (in.direction > 0)
                {
                    if (! on (pid::deessOn))
                    {
                        note (Note::Kind::info, de ("Der De-Esser ist aus - die S-Laute werden bereits nicht reduziert."));
                        return;
                    }
                    set (pid::deessRange, std::max (value (pid::deessRange) - 3.0f * k, 0.0f), de ("De-Esser schwächer"),
                         de ("Der De-Esser reduziert die S-Laute weniger stark."), false, true, "deess");
                    return;
                }

                juce::String measuredText;
                bool measured = false;
                if (ctx.analysisValid && ctx.sibilanceVsVowelDb > -99.0f)
                {
                    measuredText = de (" Gemessen (Vocal Check): S-Laute ") + num (ctx.sibilanceVsVowelDb) + de (" dB gegenüber den Vokalen.");
                    measured = true;
                }
                ensureOn (pid::deessOn, "De-esser", "deess");
                set (pid::deessThreshold, std::max (value (pid::deessThreshold) - 4.0f * k, -50.0f), de ("De-Esser früher eingreifen lassen"),
                     de ("Ein tieferer Threshold erfasst mehr S-Laute.") + measuredText, measured, true, "deess");
                set (pid::deessRange, std::min (value (pid::deessRange) + 3.0f * k, 15.0f), de ("S-Laute stärker reduzieren"),
                     de ("Du möchtest weniger Zischen: Die maximale Absenkung der S-Laute wird erhöht."), false, true, "deess");
            }

            void punch (const Intent& in)
            {
                const float k = AssistantPlanner::factor (in.intensity);
                if (in.direction > 0)
                {
                    if (! on (pid::smashOn))
                    {
                        set (pid::smashOn, 1.0f, de ("Vocal Smash einschalten"),
                             de ("Parallele Kompression gibt der Stimme Druck, ohne die Dynamik des Originals zu zerstören."), false, true, "smash");
                        setStart (pid::smashBlend, 18.0f + 8.0f * k, de ("Smash dezent beimischen"),
                                  de ("Der stark komprimierte Anteil liegt leise unter dem Original."), "smash");
                    }
                    else
                    {
                        set (pid::smashBlend, std::min (value (pid::smashBlend) + 10.0f * k, 60.0f), de ("Mehr Smash beimischen"),
                             de ("Du möchtest mehr Druck: Mehr vom parallel komprimierten Anteil."), false, true, "smash");
                    }
                    if (on (pid::compOn) && value (pid::compAttack) < 15.0f)
                        set (pid::compAttack, 15.0f + 5.0f * k, de ("Kompressor-Attack verlängern"),
                             de ("Eine längere Attack lässt den Anschlag der Silben durch - das wirkt druckvoller."), false, true, "comp");
                }
                else
                {
                    if (! on (pid::smashOn))
                    {
                        note (Note::Kind::info, de ("Vocal Smash ist aus. Für weniger Druck: „weniger Kompression“."));
                        return;
                    }
                    set (pid::smashBlend, std::max (value (pid::smashBlend) - 10.0f * k, 0.0f), de ("Weniger Smash"),
                         de ("Du möchtest weniger Druck: Der parallel komprimierte Anteil wird reduziert."), false, true, "smash");
                }
            }

            void compression (const Intent& in, bool fromBeat = false)
            {
                const float k = AssistantPlanner::factor (in.intensity) * (fromBeat ? 0.5f : 1.0f);
                if (in.direction > 0)
                {
                    ensureOn (pid::compOn, "Compressor", "comp");
                    set (pid::compRatio, std::min (value (pid::compRatio) + 1.0f * k, 8.0f), de ("Ratio erhöhen"),
                         fromBeat ? de ("Gleichmäßigere Lautstärke hilft der Vocal, durchgehend über dem Beat zu bleiben (allgemeine Regel).")
                                  : de ("Du möchtest mehr Kontrolle über die Dynamik: Lautere Passagen werden stärker abgesenkt."), false, true, "comp");
                    set (pid::compThreshold, std::max (value (pid::compThreshold) - 3.0f * k, -45.0f), de ("Threshold senken"),
                         de ("Der Kompressor greift früher ein."), false, true, "comp");
                }
                else
                {
                    if (! on (pid::compOn) && ! on (pid::smashOn))
                    {
                        note (Note::Kind::info, de ("Kompressor und Vocal Smash sind bereits aus."));
                        return;
                    }
                    if (on (pid::compOn))
                    {
                        set (pid::compRatio, std::max (value (pid::compRatio) - 1.0f * k, 1.5f), de ("Ratio verringern"),
                             de ("Du möchtest weniger Kompression: Die Absenkung über dem Threshold wird sanfter."), false, true, "comp");
                        set (pid::compThreshold, std::min (value (pid::compThreshold) + 3.0f * k, 0.0f), de ("Threshold anheben"),
                             de ("Der Kompressor greift später ein - mehr natürliche Dynamik bleibt erhalten."), false, true, "comp");
                    }
                    if (on (pid::smashOn))
                        set (pid::smashBlend, std::max (value (pid::smashBlend) - 10.0f * k, 0.0f), de ("Weniger Smash"),
                             de ("Vocal Smash ist parallele Kompression - auch sie wird reduziert."), false, true, "smash");
                    for (auto* macroId : { pid::macroDensity, pid::macroAggression })
                        if (value (macroId) > 0.0f)
                            set (macroId, std::max (0.0f, value (macroId) - 20.0f * k), de ("Makro verringern"),
                                 de ("Dieses Makro fügt zusätzliche Kompression hinzu."), false, true, "macro");
                }
            }

            void beat (const Intent& in)
            {
                plan.beatRequested = true;

                // Honest status of the beat measurement
                if (! ctx.sidechainConnected)
                    note (Note::Kind::warning, de ("Beat-Sidechain nicht verbunden: Die Vorschläge beruhen auf allgemeinen Regeln für Vocals, nicht auf einer Messung deines Beats. "
                                                   "In FL Studio die Beat-Spur per Sidechain an Velvox senden."));
                else if (! ctx.sidechainSignal)
                    note (Note::Kind::warning, de ("Beat-Sidechain verbunden, aber es kommt gerade kein Signal an (Beat läuft nicht?). Die Vorschläge beruhen auf allgemeinen Regeln."));
                else
                    note (Note::Kind::info, de ("Gemessen: Beat am Sidechain etwa ") + juce::String (ctx.sidechainRmsDb, 1) + de (" dBFS RMS, Vocal-Eingang etwa ")
                                                + juce::String (ctx.vocalRmsDb, 1) + de (" dBFS RMS. Das ist nur ein Pegelvergleich - ob sich Frequenzen überdecken, wird damit nicht nachgewiesen."));
                note (Note::Kind::unavailable, de ("Frequenzvergleich Vocal gegen Beat (Maskierung): noch nicht implementiert - geplant als eigener Entwicklungsschritt."));

                const bool presenceAsked = std::any_of (req.intents.begin(), req.intents.end(), [] (const Intent& i) { return i.goal == Goal::presence; });
                if (! presenceAsked && ! req.hasConstraint (Goal::presence, 1))
                    presence (in, true);

                const bool compressionAsked = std::any_of (req.intents.begin(), req.intents.end(), [] (const Intent& i) { return i.goal == Goal::compression; });
                if (! compressionAsked && ! req.hasConstraint (Goal::compression, 1))
                    compression (in, true);

                // Effects that duck while singing keep the space in front free
                if (on (pid::reverbOn) && value (pid::reverbDuckAmount) < 3.0f)
                    set (pid::reverbDuckAmount, 5.0f, de ("Hall beim Singen absenken"),
                         de ("Der Hall weicht zurück, solange du singst, und füllt die Pausen - die Worte bleiben vorne."), false, true, "duck");
                if (on (pid::delayOn) && value (pid::delayDuckAmount) < 3.0f)
                    set (pid::delayDuckAmount, 6.0f, de ("Delay beim Singen absenken"),
                         de ("Die Echos weichen zurück, solange du singst."), false, true, "duck");
            }

            // Removes changes that a constraint forbids ("nicht schaerfer", "nicht mehr Hoehen")
            void enforceConstraints()
            {
                for (auto it = plan.changes.begin(); it != plan.changes.end();)
                {
                    const float delta = it->proposed - it->current;
                    bool forbidden = false;
                    Goal which {};
                    for (auto& link : goalLinks)
                    {
                        if (it->paramId != link.paramId || std::abs (delta) < 1.0e-4f)
                            continue;
                        const int moves = (delta > 0.0f ? 1 : -1) * link.sign;
                        if (req.hasConstraint (link.goal, moves))
                        {
                            forbidden = true;
                            which = link.goal;
                        }
                    }
                    if (forbidden)
                    {
                        note (Note::Kind::info, it->title + de (" weggelassen - du möchtest nicht, dass ") + goalName (which)
                                                    + de (" in diese Richtung verändert wird."));
                        it = plan.changes.erase (it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
        };
    }

    float AssistantPlanner::factor (Intensity i) noexcept
    {
        switch (i)
        {
            case Intensity::slight:   return 0.5f;
            case Intensity::moderate: return 1.0f;
            case Intensity::strong:   return 1.8f;
        }
        return 1.0f;
    }

    float legaliseValue (const juce::String& paramId, float value)
    {
        const auto* spec = findParameterSpec (paramId);
        if (spec == nullptr || ! std::isfinite (value))
            return 0.0f;
        float v = juce::jlimit (spec->minValue, spec->maxValue, value);
        if (spec->type != ParameterSpec::Type::Float)
            v = (float) juce::roundToInt (v);
        else if (spec->interval > 0.0f)
            v = spec->minValue + spec->interval * std::round ((v - spec->minValue) / spec->interval);
        return juce::jlimit (spec->minValue, spec->maxValue, v);
    }

    juce::String displayValue (const juce::String& paramId, float value)
    {
        const auto* spec = findParameterSpec (paramId);
        if (spec == nullptr)
            return juce::String (value);
        if (spec->type == ParameterSpec::Type::Bool)
            return value > 0.5f ? de ("An") : de ("Aus");
        return formatValue (*spec, value);
    }

    juce::String moduleLabelFor (const juce::String& paramId)
    {
        const auto* spec = findParameterSpec (paramId);
        if (spec == nullptr)
            return {};
        const juce::String m (spec->module);
        if (m == "Compressor") return "Compressor";
        if (m == "De-Esser")   return "De-esser";
        if (m == "Smash")      return "Vocal Smash";
        if (m == "Ride")       return "Level Ride";
        if (m == "Stereo" || m == "Global") return "Output";
        if (m == "Ducking")    return paramId.startsWith ("delay") ? "Delay" : (paramId.startsWith ("reverb") ? "Reverb" : "Ducking");
        if (m == "Macros")     return de ("Makro");
        return m;
    }

    Plan AssistantPlanner::plan (const ParsedRequest& request, const AssistantContext& context)
    {
        Builder b (request, context);

        for (const auto& in : request.intents)
        {
            b.source = in.source;
            switch (in.goal)
            {
                case Goal::bass:        b.bass (in); break;
                case Goal::presence:    b.presence (in); break;
                case Goal::brightness:  b.brightness (in); break;
                case Goal::reverb:      b.reverb (in); break;
                case Goal::delay:       b.delay (in); break;
                case Goal::level:       b.level (in); break;
                case Goal::width:       b.width (in); break;
                case Goal::warmth:      b.warmth (in); break;
                case Goal::harshness:   b.harshness (in); break;
                case Goal::sibilance:   b.sibilance (in); break;
                case Goal::punch:       b.punch (in); break;
                case Goal::compression: b.compression (in); break;
                case Goal::beatMasking: b.beat (in); break;
                case Goal::everything:  break;
            }
        }

        // "Mono-kompatibel" without a width wish: check what is there
        if (request.hasConstraint (Goal::width, 0)
            && std::none_of (request.intents.begin(), request.intents.end(), [] (const Intent& i) { return i.goal == Goal::width; }))
        {
            if (b.on (pid::doublerOn))
                b.set (pid::doublerMonoSafe, std::max (b.value (pid::doublerMonoSafe), 75.0f), de ("Doubler mono-sicherer"),
                       de ("Du möchtest Mono-Kompatibilität: Der Doubler hält mehr Monoanteil."), false, true, "doubler");
            b.set (pid::monoBelow, std::max (b.value (pid::monoBelow), 150.0f), de ("Tiefen mono halten"),
                   de ("Unterhalb dieser Frequenz bleibt die Vocal mono."), false, true, "output");
        }

        b.source.clear();
        b.enforceConstraints();

        // Style profiles: say what they stand for - and what they are not
        if (! request.styles.isEmpty())
        {
            b.note (Note::Kind::info, de ("Stilprofile beschreiben typische, hörbare Merkmale eines Vocal-Sounds - keine Analyse und kein Nachbau der Originalaufnahmen. Was du selbst beschreibst, hat Vorrang."));
            for (const auto& label : request.styles)
                for (const auto& p : styleProfiles())
                    if (p.label == label)
                    {
                        b.note (Note::Kind::info, de ("Stilprofil „") + p.label + de ("“: ") + p.character);
                        if (p.caveat.isNotEmpty())
                            b.note (Note::Kind::unavailable, p.caveat);
                    }
        }
        for (auto g : request.styleConflicts)
            b.note (Note::Kind::info, de ("Die Stilprofile ziehen in verschiedene Richtungen (") + goalName (g).fromFirstOccurrenceOf (" ", false, false)
                                          + de (") - das bleibt deshalb unverändert. Sag es direkt, wenn du eine Richtung willst."));
        for (const auto& unknown : request.unknownStyles)
            b.note (Note::Kind::unavailable, de ("Den Stil von „") + unknown + de ("“ kenne ich nicht. Verfügbare Stilprofile: ") + styleProfileList()
                                                 + de (". Beschreibe sonst den Klang direkt, z. B. „trocken, hell, druckvoll“."));

        for (auto g : request.conflicts)
            b.note (Note::Kind::warning, de ("Widersprüchlich: ") + goalName (g) + de (" soll gleichzeitig mehr und weniger werden - dazu wird nichts geändert. Bitte entscheide dich für eine Richtung."));
        for (auto& q : request.ambiguous)
            b.note (Note::Kind::question, q);
        if (! request.unrecognised.empty() && request.intents.empty() && request.constraints.empty()
            && request.ambiguous.empty() && request.conflicts.empty())
            b.note (Note::Kind::unavailable, de ("Nicht verstanden: „") + request.original.trim() + de ("“ - dazu gibt es (noch) keine passende Funktion."));
        else
            for (auto& u : request.unrecognised)
                b.note (Note::Kind::unavailable, de ("Nicht verstanden: „") + u + de ("“ - dazu gibt es (noch) keine passende Funktion."));

        // Only limits, no wish ("nicht mehr Hoehen"): confirm instead of staying silent
        if (request.intents.empty() && ! request.constraints.empty() && request.conflicts.empty() && request.ambiguous.empty())
        {
            juce::StringArray limits;
            for (auto& c : request.constraints)
                limits.addIfNotAlreadyThere (c.direction == 0 ? de ("die Vocal bleibt mono-kompatibel")
                                                              : (c.direction > 0 ? de ("nichts, was ") + goalName (c.goal) + de (" erhöht") : de ("nichts, was ") + goalName (c.goal) + de (" verringert")));
            if (! b.plan.hasChanges())
                b.note (Note::Kind::info, de ("Verstanden: ") + limits.joinIntoString (", ") + de (". Dafür muss aktuell nichts geändert werden."));
        }

        if (context.macrosActive && b.plan.hasChanges())
            b.note (Note::Kind::info, de ("Beginner-Makros sind aktiv und wirken zusätzlich auf die Module. Die Vorschläge ändern die Modulwerte selbst."));

        return b.plan;
    }
}
