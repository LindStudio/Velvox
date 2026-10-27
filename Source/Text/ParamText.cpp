#include "ParamText.h"
#include "I18n.h"

#include <cstring>
#include <unordered_map>

namespace velvox::i18n
{
    namespace
    {
        // German interface texts per parameter id. Empty fields fall back to the English text.
        // Standard audio terms (Threshold, Ratio, Attack, Release, Mix, Drive, Decay, Pre-Delay,
        // Feedback, Q, Gain, Low Cut, High Cut, Send ...) stay English in both languages.
        //
        // Numbered families share one entry: the first digits of the id become "#" ("eqBand3Gain" ->
        // "eqBand#Gain", "doublerV2On" -> "doublerV#On") and "{0}" in the name is the number.
        // Names keep the module prefix of the English name ("Comp ", "Sat ", ...): extra instances
        // ("Compressor 2: Threshold") replace it with the instance name, as the English names do.
        struct Entry
        {
            const char* name;
            const char* description;
            std::vector<const char*> choices;   // in the parameter's choice order
        };

        const std::unordered_map<std::string, Entry>& germanTable()
        {
            static const std::unordered_map<std::string, Entry> table {
                // ---------------- Global ----------------
                { "inputGain",  { "Eingangspegel", "Pegel vor der Bearbeitungskette.", {} } },
                { "outputGain", { "Ausgangspegel", "Pegel nach der Bearbeitungskette.", {} } },
                { "bypass",     { "", "Umgeht die ganze Kette (mit Latenzausgleich).", {} } },
                { "safetyClip", { "", "Sanfter Clipper ab -1 dBFS, hält den Ausgang unter 0 dBFS.", {} } },

                // ---------------- EQ ----------------
                { "eqOn",     { "EQ An", "Schaltet den Equalizer ein.", {} } },
                { "eqMix",    { "", "Dry/Wet des Equalizers.", {} } },
                { "hpfOn",    { "Low Cut An", "Schaltet den Hochpass (Low Cut) ein.", {} } },
                { "hpfFreq",  { "", "Grenzfrequenz des Hochpasses.", {} } },
                { "hpfSlope", { "Low Cut Steilheit", "Steilheit des Hochpasses.", {} } },
                { "lowFreq",  { "Low Frequenz", "Eckfrequenz des Low Shelf.", {} } },
                { "lowGain",  { "", "Gain des Low Shelf (Körper / Wärme).", {} } },
                { "midFreq",  { "Mid Frequenz", "Mittenfrequenz des Mid-Bell.", {} } },
                { "midGain",  { "", "Gain des Mid-Bell (Pappigkeit / Präsenz).", {} } },
                { "midQ",     { "", "Bandbreite des Mid-Bell; höher = schmaler.", {} } },
                { "highFreq", { "High Frequenz", "Eckfrequenz des High Shelf.", {} } },
                { "highGain", { "", "Gain des High Shelf (Luft / Helligkeit).", {} } },
                { "lowType",  { "Low Band Typ", "Low-Band als Shelf (ganzer Bassbereich) oder Bell (um die Frequenz).", {} } },
                { "lowQ",     { "", "Bandbreite des Low-Bands (Shelf: Form der Flanke).", {} } },
                { "highType", { "High Band Typ", "High-Band als Shelf (Luft) oder Bell (Präsenz).", {} } },
                { "highQ",    { "", "Bandbreite des High-Bands (Shelf: Form der Flanke).", {} } },
                { "lpfOn",    { "High Cut An", "Schaltet den Tiefpass (High Cut) ein.", {} } },
                { "lpfFreq",  { "", "Grenzfrequenz des Tiefpasses (z. B. für Telefon- oder Lo-Fi-Effekte).", {} } },
                { "lpfSlope", { "High Cut Steilheit", "Steilheit des Tiefpasses.", {} } },
                { "eqLegacyResponse", { "EQ Alte Kurven", "Intern: die Bänder klingen wie der EQ von 0.5 (gesetzt beim Laden älterer Projekte).", {} } },

                // EQ bands (0.6)
                { "eqBand#On",    { "EQ Band {0} An", "Schaltet das Band ein.", {} } },
                { "eqBand#Type",  { "EQ Band {0} Typ", "Bell, Shelves, Cuts, Notch, Band Pass oder Tilt.", {} } },
                { "eqBand#Freq",  { "EQ Band {0} Frequenz", "Mitten- bzw. Eckfrequenz des Bands.", {} } },
                { "eqBand#Gain",  { "", "Anhebung oder Absenkung des Bands (Tilt: von den Tiefen zu den Höhen).", {} } },
                { "eqBand#Q",     { "", "Bandbreite; höher = schmaler.", {} } },
                { "eqBand#Slope", { "EQ Band {0} Steilheit", "Steilheit eines Low oder High Cut.", {} } },
                { "eqBand#Role",  { "EQ Band {0} Rolle", "Intern: welche Funktion das Band steuert (Makros, Auto-Mix, Vocal Check); eigene Bänder haben keine.",
                                    { "Keine", "Präsenz", "Wärme", "Luft", "", "Dröhnen", "", "", "", "", "", "" } } },

                // ---------------- Compressor ----------------
                { "compOn",          { "Comp An", "Schaltet den Compressor ein.", {} } },
                { "compThreshold",   { "", "Pegel, ab dem die Gain-Reduktion beginnt.", {} } },
                { "compRatio",       { "", "Stärke der Kompression über dem Threshold.", {} } },
                { "compAttack",      { "", "Wie schnell die Gain-Reduktion greift.", {} } },
                { "compRelease",     { "", "Wie schnell die Gain-Reduktion wieder loslässt.", {} } },
                { "compKnee",        { "", "Breite des weichen Übergangs um den Threshold.", {} } },
                { "compMakeup",      { "", "Gain nach der Kompression.", {} } },
                { "compMix",         { "", "Dry/Wet für Parallelkompression.", {} } },
                { "compDetector",    { "", "Peak reagiert auf jede Spitze; RMS folgt dem mittleren Pegel (ruhiger).", {} } },
                { "compScHpf",       { "", "Ignoriert tiefe Frequenzen bei der Erkennung, damit Plosive und Bass nicht komprimieren (20 Hz = aus).", {} } },
                { "compAutoRelease", { "", "Release je nach Material: kurze Spitzen erholen sich schnell, lange Kompression langsam und weich.", {} } },
                { "compAutoMakeup",  { "", "Gleicht das Gain aus, das der Compressor einer Stimme bei etwa -18 dBFS nimmt, damit der Pegel ähnlich bleibt.", {} } },

                // ---------------- De-Esser ----------------
                { "deessOn",        { "De-Ess An", "Schaltet den De-Esser ein.", {} } },
                { "deessFreq",      { "De-Ess Frequenz", "Untere Kante des S-Bands, das abgesenkt wird.", {} } },
                { "deessThreshold", { "", "Pegel der S-Laute, ab dem die Absenkung beginnt.", {} } },
                { "deessRange",     { "", "Größte Absenkung des S-Bands (Stärke).", {} } },
                { "deessListen",    { "De-Ess Abhören", "Spielt nur das erkannte S-Band, zum Einstellen.", {} } },
                { "deessMode",      { "De-Ess Modus", "Split band senkt nur die S-Frequenzen ab; Wideband senkt bei jedem S die ganze Stimme.", {} } },
                { "deessDetection", { "De-Ess Erkennung", "Relativ vergleicht das S-Band mit der ganzen Stimme, so werden laute und leise Phrasen gleich behandelt.",
                                      { "Absolut", "Relativ" } } },

                // ---------------- Saturation ----------------
                { "satOn",        { "Sat An", "Schaltet die Saturation ein.", {} } },
                { "satDrive",     { "", "Stärke der harmonischen Sättigung (pegelkompensiert).", {} } },
                { "satOutput",    { "Sat Ausgang", "Ausgangspegel der Saturation.", {} } },
                { "satMix",       { "", "Dry/Wet der Saturation.", {} } },
                { "satCharacter", { "Sat Charakter", "Warm: weich, leicht asymmetrisch. Tape: sanft, symmetrisch. Tube: reiche geradzahlige Obertöne. Hard: festes Clipping.", {} } },
                { "satMultiband", { "", "Teilt die Stimme in Low / Mid / High und sättigt jedes Band für sich.", {} } },
                { "satXoverLow",  { "Sat Übergang Low", "Grenze zwischen Low- und Mid-Band.", {} } },
                { "satXoverHigh", { "Sat Übergang High", "Grenze zwischen Mid- und High-Band (mindestens 1,5 x die untere Grenze).", {} } },
                { "satLowOn",     { "Sat Low An", "Sättigt das Low-Band (aus: das Band bleibt sauber).", {} } },
                { "satLowDrive",  { "", "Sättigung des Low-Bands (pegelkompensiert).", {} } },
                { "satLowChar",   { "Sat Low Charakter", "Charakter des Low-Bands.", {} } },
                { "satMidOn",     { "Sat Mid An", "Sättigt das Mid-Band (aus: das Band bleibt sauber).", {} } },
                { "satMidDrive",  { "", "Sättigung des Mid-Bands (pegelkompensiert).", {} } },
                { "satMidChar",   { "Sat Mid Charakter", "Charakter des Mid-Bands.", {} } },
                { "satHighOn",    { "Sat High An", "Sättigt das High-Band (aus: das Band bleibt sauber).", {} } },
                { "satHighDrive", { "", "Sättigung des High-Bands (pegelkompensiert).", {} } },
                { "satHighChar",  { "Sat High Charakter", "Charakter des High-Bands.", {} } },
                { "satLink",      { "Sat Koppeln", "Ein Band-Drive bewegt alle drei mit (ihre Abstände bleiben).", {} } },

                // ---------------- Delay ----------------
                { "delayOn",        { "Delay An", "Schaltet das Delay ein (paralleler Send).", {} } },
                { "delayTime",      { "Delay Zeit", "Delay-Zeit ohne Tempo-Sync.", {} } },
                { "delaySync",      { "", "Delay-Zeit zum Host-Tempo synchronisieren.", {} } },
                { "delayDivision",  { "Delay Notenwert", "Notenwert bei Tempo-Sync.", {} } },
                { "delayFeedback",  { "", "Menge der Wiederholungen (begrenzt, damit es stabil bleibt).", {} } },
                { "delayMix",       { "", "Pegel des Delays, das zum trockenen Signal kommt.", {} } },
                { "delayLowCut",    { "", "Hochpass auf den Wiederholungen.", {} } },
                { "delayHighCut",   { "", "Tiefpass auf den Wiederholungen.", {} } },
                { "delayPingPong",  { "", "Wiederholungen wechseln zwischen links und rechts.", {} } },
                { "delaySpread",    { "", "Rechts wiederholt um so viel später als links - breite, unabhängige Stereo-Echos.", {} } },
                { "delayDrive",     { "", "Bandartige Sättigung der Wiederholungen: wärmer, jedes Echo etwas runder.", {} } },
                { "delayDiffusion", { "", "Verwischt die Wiederholungen zu einem weichen Teppich - von klaren Echos bis zu Klangwolken.", {} } },
                { "delayMod",       { "", "Langsames Leiern auf den Wiederholungen, wie ein Bandecho.", {} } },
                { "delayToReverb",  { "Delay in Reverb", "Schickt die Delay-Echos ins Reverb (wenn beide als Send laufen).", {} } },

                // ---------------- Reverb ----------------
                { "reverbOn",        { "Reverb An", "Schaltet das Reverb ein (paralleler Send).", {} } },
                { "reverbType",      { "Reverb Typ",
                                       "Charakter des Raums: Room = natürlich und nah, Plate = dicht und hell, Hall / Large Hall = Konzertsäle, "
                                       "Ambience = kurze Studio-Luft, Atmosphere = riesiger, üppiger EDM-Raum, Cinematic = dunkel und tief, Ethereal = hell, breit und schwebend.",
                                       {} } },
                { "reverbMix",       { "", "Pegel des Reverbs, das zum trockenen Signal kommt.", {} } },
                { "reverbDecay",     { "", "Nachhallzeit (T30 der ganzen Fahne, alle Frequenzen).", {} } },
                { "reverbPreDelay",  { "", "Pause, bevor der Hall einsetzt.", {} } },
                { "reverbSize",      { "Reverb Größe", "Größe des virtuellen Raums bzw. der Platte.", {} } },
                { "reverbDamping",   { "Reverb Klang", "Dämpfung der Höhen in der Hallfahne.", {} } },
                { "reverbLowCut",    { "", "Hochpass am Eingang des Reverbs.", {} } },
                { "reverbWidth",     { "", "Stereobreite des Reverbs.", {} } },
                { "reverbEarly",     { "Reverb Frühe Reflexionen", "Pegel der frühen Reflexionen - der ersten deutlichen Echos, an denen das Ohr Größe und Nähe des Raums erkennt.", {} } },
                { "reverbDiffusion", { "", "Wie schnell die Echos zu einer glatten Fahne verschmelzen. Wenig = körnig, deutlich; viel = dicht und seidig.", {} } },
                { "reverbMod",       { "Reverb Bewegung", "Langsame Bewegung in der Hallfahne. Macht lange Räume üppig und vermeidet metallisches Klingeln; 0 = statisch.", {} } },
                { "reverbBass",      { "", "Decay der Tiefen im Verhältnis zu den Mitten. Unter 1 bleiben lange Fahnen sauber.", {} } },
                { "reverbHighCut",   { "", "Tiefpass auf dem Reverb: tiefer = dunkler, weiter hinten.", {} } },

                // ---------------- Stereo ----------------
                { "stereoWidth", { "Stereobreite", "Breite des ganzen Ausgangs. Nur das Seitensignal ändert sich, die Stimme in der Mitte und die Mono-Summe bleiben unberührt.", {} } },
                { "monoBelow",   { "Mono unter", "Hält alles unter dieser Frequenz mono (20 Hz = aus).", {} } },

                // ---------------- Output: level matching ----------------
                { "levelMatchMode",   { "Level Match", "Wie Bypass: der Bypass spielt so laut wie der bearbeitete Klang (fairer A/B-Vergleich). Auto-Gain: der bearbeitete Klang bleibt so laut wie der Eingang.",
                                        { "Aus", "Wie Bypass", "Auto-Gain" } } },
                { "levelMatchWindow", { "Level-Match-Fenster", "Wie lange die Lautheit gemittelt wird, bevor angeglichen wird.",
                                        { "Schnell (0,4 s)", "Mittel (3 s)", "Langsam (10 s)" } } },
                { "levelMatchRange",  { "Max. Korrektur", "Größte Korrektur, die Level Match anwenden darf.", {} } },

                // ---------------- Doubler ----------------
                { "doublerOn",       { "Doubler An", "Schaltet den Vocal-Doubler ein.", {} } },
                { "doublerMix",      { "", "Pegel der gedoppelten Stimmen (das Original bleibt bei vollem Pegel).", {} } },
                { "doublerWidth",    { "", "Wie weit links und rechts die gedoppelten Stimmen stehen.", {} } },
                { "doublerDelay",    { "", "Zeitversatz der gedoppelten Stimmen - länger wirkt lockerer.", {} } },
                { "doublerDetune",   { "", "Tonhöhenabweichung der gedoppelten Stimmen in Cent.", {} } },
                { "doublerMonoSafe", { "Doubler Mono-sicher", "Hält die gedoppelten Stimmen aus der Mono-Summe. 100 % = Mono klingt genau wie das Original.", {} } },
                { "doublerTone",     { "Doubler Klang", "High Cut auf den gedoppelten Stimmen (dunkler = weiter hinten).", {} } },
                { "doublerVoices",   { "Doubler Stimmen", "Classic: zwei Stimmen, eingestellt mit Delay und Detune (der Doubler von 0.2-0.5). 2 / 4: jede Stimme mit eigenem Delay, Detune, Gain und Pan.", {} } },
                { "doublerV#On",     { "Doubler V{0} An", "Schaltet diese Stimme ein (2 / 4 Stimmen).", {} } },
                { "doublerV#Delay",  { "", "Wie spät diese Stimme nach dem Original einsetzt.", {} } },
                { "doublerV#Detune", { "", "Feste Tonhöhenverschiebung dieser Stimme.", {} } },
                { "doublerV#Gain",   { "", "Pegel dieser Stimme.", {} } },
                { "doublerV#Pan",    { "", "Stereoposition dieser Stimme (skaliert mit Width).", {} } },
                { "doublerModDepth", { "Doubler Mod-Tiefe", "Langsames, zufälliges Schwanken von Tonhöhe und Timing der Stimmen (2 / 4 Stimmen; Classic nutzt Detune).", {} } },
                { "doublerModRate",  { "Doubler Mod-Tempo", "Tempo des Schwankens (jede Stimme bewegt sich mit eigenem, unabhängigem Tempo darum herum).", {} } },
                { "doublerLowCut",   { "", "Hochpass auf den gedoppelten Stimmen (der Bassbereich bleibt beim Original).", {} } },
                { "doublerDry",      { "Doubler Original", "Pegel der Original-Stimme. -60 dB = aus (nur die Stimmen).", {} } },

                // ---------------- Ducking ----------------
                { "duckSource",          { "Ducking Quelle", "Was Reverb und Delay herunterdrückt: die Stimme selbst oder der Sidechain-Eingang des Plugins (z. B. eine Kick).",
                                           { "Vocal (intern)", "Sidechain-Eingang" } } },
                { "delayDuckAmount",     { "Delay Duck Stärke", "Wie weit das Delay absinkt, während die Stimme singt (0 = aus).", {} } },
                { "delayDuckThreshold",  { "", "Pegel der Stimme, ab dem das Delay-Ducking beginnt.", {} } },
                { "delayDuckAttack",     { "", "Wie schnell das Delay heruntergedrückt wird.", {} } },
                { "delayDuckRelease",    { "", "Wie schnell das Delay in den Pausen zurückkommt.", {} } },
                { "reverbDuckAmount",    { "Reverb Duck Stärke", "Wie weit das Reverb absinkt, während die Stimme singt (0 = aus).", {} } },
                { "reverbDuckThreshold", { "", "Pegel der Stimme, ab dem das Reverb-Ducking beginnt.", {} } },
                { "reverbDuckAttack",    { "", "Wie schnell das Reverb heruntergedrückt wird.", {} } },
                { "reverbDuckRelease",   { "", "Wie schnell das Reverb in den Pausen zurückkommt.", {} } },



                // ---------------- Macros ----------------
                { "macroPresence",   { "Präsenz", "Holt die Stimme nach vorn: hellere Höhen, S-Laute bleiben im Zaum.", {} } },
                { "macroWarmth",     { "Wärme", "Voller, runder Klang: mehr Körper, weichere Höhen, sanfte Sättigung.", {} } },
                { "macroAggression", { "", "Härter, intensiver: schnellere, stärkere Kompression und mehr Drive.", {} } },
                { "macroWidth",      { "", "Breitere Stimme: gedoppelte Stimmen links und rechts, breiteres Stereobild. Die Mitte bleibt stabil.", {} } },
                { "macroSpace",      { "Raum", "Mehr Raum: Reverb und Delay, die zurücktreten, während du singst.", {} } },
                { "macroDensity",    { "Dichte", "Dichter und gleichmäßiger: mehr Kompression und etwas Sättigung.", {} } },


                // ---------------- Vocal Smash ----------------
                { "smashOn",      { "Smash An", "Schaltet VOCAL SMASH ein, einen eigenen Parallel-Compressor.", {} } },
                { "smashAmount",  { "Smash Stärke", "Wie hart der parallele Weg gequetscht wird (tieferer Threshold, höhere Ratio). Das Original behält seine Dynamik.", {} } },
                { "smashBlend",   { "Smash Anteil", "Wie viel vom gequetschten Weg unter das Original kommt.", {} } },
                { "smashAttack",  { "", "Attack des gequetschten Wegs - schnell erwischt jeden Konsonanten.", {} } },
                { "smashRelease", { "", "Release des gequetschten Wegs - kurz = dichter, lang = weicher.", {} } },
                { "smashTone",    { "Smash Klang", "Färbt den gequetschten Weg: negativ = wärmer, positiv = heller und präsenter.", {} } },
                { "smashOutput",  { "Smash Ausgang", "Ausgangspegel des Moduls.", {} } },


                // ---------------- Smooth ----------------
                { "smoothOn",          { "Smooth An", "Schaltet SMOOTH ein, die adaptive Resonanz-Kontrolle.", {} } },
                { "smoothAmount",      { "Smooth Stärke", "Wie stark erkannte Resonanzen abgesenkt werden.", {} } },
                { "smoothSensitivity", { "Smooth Empfindlichkeit", "Wie weit eine Frequenz aus ihrer Umgebung herausragen muss, bis sie als Resonanz gilt.", {} } },
                { "smoothLow",         { "Smooth Tief", "Tiefste bearbeitete Frequenz.", {} } },
                { "smoothHigh",        { "Smooth Hoch", "Höchste bearbeitete Frequenz.", {} } },
                { "smoothAttack",      { "", "Wie schnell eine Absenkung reagiert.", {} } },
                { "smoothRelease",     { "", "Wie schnell eine Absenkung loslässt.", {} } },
                { "smoothMode",        { "Smooth Modus", "Soft: breite, sanfte Absenkungen. Hard: schmal, stärker und schneller - für aggressive Resonanzen.", {} } },
                { "smoothOutput",      { "Smooth Ausgang", "Ausgangspegel des Moduls.", {} } },
                { "smoothDelta",       { "", "Nur hören, was entfernt wird (zum Einstellen).", {} } },
                { "smoothW#Freq",      { "Smooth Gewichtung {0} Frequenz", "Position eines Punkts der Gewichtungskurve.", {} } },
                { "smoothW#Gain",      { "Smooth Gewichtung {0} Gain", "Mehr (+) oder weniger (-) Absenkung um diesen Punkt; überall 0 = gleichmäßig.", {} } },

                // ---------------- Vocal Check ----------------
                { "checkOn",   { "Check An", "Schaltet VOCAL CHECK ein. Es hört nur zu: das Audio bleibt unverändert.", {} } },
                { "checkMode", { "Check Modus", "Laufend: fortlaufende Analyse der letzten ~30 s. Capture: analysiert alles von Start bis Stop.",
                                 { "Laufend", "Capture" } } },

                // ---------------- Level Ride ----------------
                { "rideOn",        { "Ride An", "Schaltet LEVEL RIDE ein, den automatischen Lautstärke-Ausgleich der Stimme.", {} } },
                { "rideTarget",    { "Ride Ziel", "Pegel, den der Ausgleich anstrebt (RMS der Stimme).", {} } },
                { "rideAmount",    { "Ride Stärke", "Wie viel vom Abstand zum Ziel ausgeglichen wird.", {} } },
                { "rideMaxBoost",  { "Ride Max. Anhebung", "Größte Anhebung für leise Wörter.", {} } },
                { "rideMaxCut",    { "Ride Max. Absenkung", "Größte Absenkung für laute Wörter.", {} } },
                { "rideResponse",  { "Ride Tempo", "Tempo der Pegelbewegungen: wenig = langsam und weich, viel = Wort für Wort.", {} } },
                { "ridePhrase",    { "Ride Phrasen glätten", "Mittelt den Pegel über längere Phrasen statt einzelner Silben - der natürliche Fluss bleibt.", {} } },
                { "rideTransient", { "Ride Transienten erhalten", "Ignoriert kurze Spitzen (Konsonanten, Plosive), damit sie weder abgesenkt noch aufgepumpt werden.", {} } },
                { "rideSilence",   { "Ride Ignorieren unter", "Unter diesem Pegel gilt das Signal als Pause; Atmer und Raumgeräusche werden nie angehoben.", {} } },
            };
            return table;
        }

        struct Found
        {
            const Entry* entry = nullptr;
            juce::String number;          // "{0}" in the name of a numbered family
            bool extraInstance = false;   // "compThreshold_2"
        };

        Found find (const juce::String& paramId)
        {
            Found f;

            // Extra instances: "compThreshold_2" -> "compThreshold"
            auto id = paramId;
            const int underscore = id.lastIndexOfChar ('_');
            if (underscore > 0 && id.substring (underscore + 1).containsOnly ("0123456789"))
            {
                id = id.substring (0, underscore);
                f.extraInstance = true;
            }

            const auto& t = germanTable();
            if (const auto it = t.find (id.toStdString()); it != t.end())
            {
                f.entry = &it->second;
                return f;
            }

            // Numbered families: the first run of digits becomes "#"
            const int start = id.indexOfAnyOf ("0123456789");
            if (start > 0)
            {
                int end = start;
                while (end < id.length() && juce::CharacterFunctions::isDigit (id[end]))
                    ++end;
                f.number = id.substring (start, end);
                const auto pattern = id.substring (0, start) + "#" + id.substring (end);
                if (const auto it = t.find (pattern.toStdString()); it != t.end())
                    f.entry = &it->second;
            }
            return f;
        }

        juce::String pick (const char* german, const juce::String& english)
        {
            return german != nullptr && *german != 0 ? juce::String::fromUTF8 (german) : english;
        }
    }

    juce::String paramName (const juce::String& paramId, const juce::String& english)
    {
        if (! isGerman())
            return english;
        const auto f = find (paramId);
        if (f.entry == nullptr || f.entry->name == nullptr || *f.entry->name == 0)
            return english;

        auto name = format (juce::String::fromUTF8 (f.entry->name), { f.number });

        // "Compressor 2: Threshold": keep the instance name, drop the module prefix of the first instance
        if (f.extraInstance && english.contains (": "))
        {
            for (auto* prefix : { "EQ ", "Comp ", "De-Ess ", "Sat ", "Delay ", "Reverb ", "Doubler ", "Smash ", "Smooth ", "Ride " })
                if (name.startsWith (prefix))
                {
                    name = name.substring ((int) std::strlen (prefix));
                    break;
                }
            name = english.upToFirstOccurrenceOf (": ", true, false) + name;
        }
        return name;
    }

    juce::String paramDescription (const juce::String& paramId, const juce::String& english)
    {
        if (! isGerman())
            return english;
        const auto f = find (paramId);
        return f.entry != nullptr ? pick (f.entry->description, english) : english;
    }

    juce::String choiceName (const juce::String& paramId, int choiceIndex, const juce::String& english)
    {
        return isGerman() ? germanChoiceName (paramId, choiceIndex, english) : english;
    }

    juce::String germanChoiceName (const juce::String& paramId, int choiceIndex, const juce::String& english)
    {
        const auto f = find (paramId);
        if (f.entry == nullptr || choiceIndex < 0 || choiceIndex >= (int) f.entry->choices.size())
            return english;
        return pick (f.entry->choices[(size_t) choiceIndex], english);
    }
}
