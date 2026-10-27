#include "FactoryPresets.h"
#include "../Parameters/ParameterIDs.h"
#include "../Parameters/EqBands.h"
#include "../Text/I18n.h"

namespace velvox
{
    namespace
    {
        // Delay division indices (see delaydiv::names())
        constexpr float div1_16 = 3.0f, div1_8 = 6.0f, div1_8D = 7.0f, div1_4 = 9.0f;
        constexpr float slope24 = 1.0f;
        constexpr float room = 0.0f, plate = 1.0f;
        constexpr float warm = 0.0f, tape = 1.0f, tube = 2.0f, hard = 3.0f;   // saturation characters
        constexpr float rms = 1.0f;                                         // compressor detector

        struct Value
        {
            const char* id;
            float value;
        };

        // Names and categories stay English (data, shown translated); the description holds both languages
        ParameterSnapshot make (const char* name, const char* category, const juce::String& description,
                                std::initializer_list<Value> values)
        {
            ParameterSnapshot s;
            s.name = name;
            s.category = category;
            s.description = description;
            for (const auto& v : values)
                s.values[v.id] = v.value;
            // Written for the 0.5 fixed bands: listed as bands 1-5 with their roles, matched response
            eqbands::migrateState (eqbands::accessFor (s.values), false);
            return s;
        }
    }

    const std::vector<ParameterSnapshot>& getFactoryPresets()
    {
        using namespace pid;

        static const std::vector<ParameterSnapshot> presets
        {
            make ("Init", "Utility", i18n::both ("Neutrale Grundeinstellung.", "Neutral default settings."), {}),

            // =================== v0.2 presets ===================
            make ("Clean Lead", "Clean",
                  i18n::both ("Natürlicher, kontrollierter Lead: Low Cut, weniger Pappigkeit, leichte Kompression, eine kurze, geduckte Plate.", "Natural, controlled lead: low cut, de-boxing, light compression, a short ducked plate."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 90 }, { hpfSlope, slope24 },
                      { lowFreq, 180 }, { lowGain, -1.0f },
                      { midFreq, 380 }, { midGain, -2.0f }, { midQ, 1.2f },
                      { highFreq, 10000 }, { highGain, 2.5f },
                      { compOn, 1 }, { compThreshold, -20 }, { compRatio, 2.5f }, { compAttack, 8 }, { compRelease, 120 },
                      { compKnee, 6 }, { compMakeup, 3 }, { compDetector, rms },
                      { deessOn, 1 }, { deessFreq, 6500 }, { deessThreshold, -32 }, { deessRange, 5 },
                      { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 12 }, { reverbDecay, 1.2f }, { reverbPreDelay, 25 },
                      { reverbSize, 45 }, { reverbDamping, 7000 }, { reverbLowCut, 300 }, { reverbWidth, 85 },
                      { reverbDuckAmount, 3 }, { reverbDuckThreshold, -32 },
                      { outputGain, 0.0f },
                  }),

            make ("Hypertechno Lead", "Punchy",
                  i18n::both ("Heller, dichter Lead, der sich gegen harte Kicks durchsetzt: Röhren-Drive, gedoppelte Kanten, Ping-Pong-Achtel und eine große, geduckte Plate.", "Bright, dense lead that cuts through hard kicks: tube drive, doubled edges, ping-pong eighths and a big ducked plate."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 160 }, { hpfSlope, slope24 },
                      { lowFreq, 300 }, { lowGain, -3.0f },
                      { midFreq, 2500 }, { midGain, 3.0f }, { midQ, 1.0f },
                      { highFreq, 9000 }, { highGain, 5.0f },
                      { compOn, 1 }, { compThreshold, -28 }, { compRatio, 6 }, { compAttack, 2 }, { compRelease, 60 },
                      { compKnee, 3 }, { compMakeup, 7 }, { compScHpf, 120 },
                      { deessOn, 1 }, { deessFreq, 6000 }, { deessThreshold, -36 }, { deessRange, 9 },
                      { satOn, 1 }, { satCharacter, tube }, { satDrive, 12 }, { satOutput, -1 }, { satMix, 65 },
                      { doublerOn, 1 }, { doublerMix, 35 }, { doublerWidth, 90 }, { doublerDelay, 12 }, { doublerDetune, 7 },
                      { doublerMonoSafe, 60 }, { doublerTone, 9000 },
                      { delayOn, 1 }, { delaySync, 1 }, { delayDivision, div1_8 }, { delayFeedback, 35 }, { delayMix, 18 },
                      { delayLowCut, 500 }, { delayHighCut, 7000 }, { delayPingPong, 1 },
                      { delayDuckAmount, 6 }, { delayDuckThreshold, -30 }, { delayDuckRelease, 250 },
                      { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 24 }, { reverbDecay, 2.4f }, { reverbPreDelay, 35 },
                      { reverbSize, 70 }, { reverbDamping, 9000 }, { reverbLowCut, 400 }, { reverbWidth, 100 },
                      { reverbDuckAmount, 8 }, { reverbDuckThreshold, -30 }, { reverbDuckRelease, 400 },
                      { delayToReverb, 30 }, { stereoWidth, 115 }, { monoBelow, 150 }, { outputGain, 6.5f },
                  }),

            make ("Aggressive Party Vocal", "Aggressive",
                  i18n::both ("Vocal direkt ins Gesicht: Biss in den oberen Mitten, schnelle starke Kompression, harter Drive, kurzes Slap-Echo und ein enger Raum.", "In-your-face hype vocal: upper-mid bite, fast heavy compression, hard drive, short slap and tight room."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 130 }, { hpfSlope, slope24 },
                      { lowFreq, 250 }, { lowGain, -2.0f },
                      { midFreq, 1800 }, { midGain, 4.0f }, { midQ, 1.2f },
                      { highFreq, 8000 }, { highGain, 3.0f },
                      { compOn, 1 }, { compThreshold, -32 }, { compRatio, 8 }, { compAttack, 1 }, { compRelease, 45 },
                      { compKnee, 2 }, { compMakeup, 9 }, { compMix, 85 }, { compScHpf, 150 },
                      { deessOn, 1 }, { deessFreq, 6500 }, { deessThreshold, -34 }, { deessRange, 8 },
                      { satOn, 1 }, { satCharacter, hard }, { satDrive, 14 }, { satOutput, -2 }, { satMix, 70 },
                      { doublerOn, 1 }, { doublerMix, 20 }, { doublerWidth, 70 }, { doublerMonoSafe, 70 },
                      { delayOn, 1 }, { delaySync, 1 }, { delayDivision, div1_16 }, { delayFeedback, 20 }, { delayMix, 10 },
                      { delayDuckAmount, 9 }, { delayDuckThreshold, -32 },
                      { reverbOn, 1 }, { reverbType, room }, { reverbMix, 10 }, { reverbDecay, 0.9f }, { reverbPreDelay, 10 },
                      { reverbSize, 35 }, { reverbDamping, 7000 }, { reverbLowCut, 350 }, { reverbWidth, 80 },
                      { reverbDuckAmount, 6 }, { reverbDuckThreshold, -32 },
                      { stereoWidth, 110 }, { outputGain, 5.5f },
                  }),

            make ("Wide Festival Vocal", "Wide",
                  i18n::both ("Riesiger, offener Festival-Lead: gedoppelte Takes, punktiertes Ping-Pong, lange Plate aus dem Delay gespeist, breit, aber mono-sicher im Bass.", "Huge, open festival lead: doubled takes, dotted ping-pong, long plate fed by the delay, wide but mono-safe low end."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 100 }, { hpfSlope, slope24 },
                      { lowFreq, 200 }, { lowGain, -1.0f },
                      { midFreq, 3200 }, { midGain, 2.0f }, { midQ, 0.8f },
                      { highFreq, 12000 }, { highGain, 4.0f },
                      { compOn, 1 }, { compThreshold, -22 }, { compRatio, 3.5f }, { compAttack, 8 }, { compRelease, 130 },
                      { compKnee, 6 }, { compMakeup, 4 }, { compDetector, rms },
                      { deessOn, 1 }, { deessFreq, 7000 }, { deessThreshold, -32 }, { deessRange, 6 },
                      { satOn, 1 }, { satCharacter, warm }, { satDrive, 5 }, { satMix, 30 },
                      { doublerOn, 1 }, { doublerMix, 55 }, { doublerWidth, 100 }, { doublerDelay, 16 }, { doublerDetune, 9 },
                      { doublerMonoSafe, 50 }, { doublerTone, 8000 },
                      { delayOn, 1 }, { delaySync, 1 }, { delayDivision, div1_8D }, { delayFeedback, 35 }, { delayMix, 20 },
                      { delayLowCut, 400 }, { delayHighCut, 6000 }, { delayPingPong, 1 },
                      { delayDuckAmount, 6 }, { delayDuckThreshold, -30 },
                      { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 28 }, { reverbDecay, 3.0f }, { reverbPreDelay, 40 },
                      { reverbSize, 85 }, { reverbDamping, 8000 }, { reverbLowCut, 300 }, { reverbWidth, 100 },
                      { reverbDuckAmount, 7 }, { reverbDuckThreshold, -30 }, { reverbDuckRelease, 600 },
                      { delayToReverb, 40 }, { stereoWidth, 130 }, { monoBelow, 180 },
                      { outputGain, -0.5f },
                  }),

            // =================== 0.3 presets (use the new modules) ===================
            make ("Hard Dance Lead", "Aggressive",
                  i18n::both ("Im Pegel ausgeglichener Hard-Dance-Lead ohne Resonanzen, mit paralleler Dichte und Viertel-Pumpen gegen die Kick. VOCAL CHECK hört am Ende mit.", "Level-ridden, resonance-free hard dance lead with parallel density and a quarter-note pump against the kick. VOCAL CHECK listens at the end."),
                  {
                      { rideOn, 1 }, { rideAmount, 70 }, { rideResponse, 50 }, { ridePhrase, 40 },
                      { hpfOn, 1 }, { hpfFreq, 140 }, { hpfSlope, slope24 },
                      { lowFreq, 280 }, { lowGain, -2.0f },
                      { midFreq, 2600 }, { midGain, 2.5f }, { midQ, 1.0f },
                      { highFreq, 10000 }, { highGain, 4.0f },
                      { compOn, 1 }, { compThreshold, -24 }, { compRatio, 4 }, { compAttack, 3 }, { compRelease, 70 },
                      { compKnee, 4 }, { compMakeup, 4 }, { compScHpf, 120 },
                      { smashOn, 1 }, { smashAmount, 65 }, { smashBlend, 40 }, { smashAttack, 1.5f }, { smashRelease, 70 }, { smashTone, 30 },
                      { deessOn, 1 }, { deessFreq, 6500 }, { deessThreshold, -34 }, { deessRange, 8 },
                      { smoothOn, 1 }, { smoothAmount, 55 }, { smoothSensitivity, 55 }, { smoothLow, 1500 }, { smoothHigh, 10000 },
                      { satOn, 1 }, { satCharacter, tube }, { satDrive, 8 }, { satMix, 45 },
                      { delayOn, 1 }, { delaySync, 1 }, { delayDivision, div1_8 }, { delayFeedback, 30 }, { delayMix, 14 },
                      { delayLowCut, 500 }, { delayHighCut, 7000 }, { delayPingPong, 1 }, { delayDuckAmount, 8 },
                      { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 18 }, { reverbDecay, 1.8f }, { reverbPreDelay, 30 },
                      { reverbSize, 65 }, { reverbLowCut, 400 }, { reverbDuckAmount, 8 },
                      { checkOn, 1 },
                      { stereoWidth, 115 }, { monoBelow, 150 }, { outputGain, 3.0f },
                  }),

            make ("Festival Choir Hook", "Wide",
                  i18n::both ("Eine einzelne Stimme wird zum breiten, satten Chor für Drops und Mitsing-Hooks.", "A single vocal turned into a wide, lush choir for drops and sing-along hooks."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 110 }, { hpfSlope, slope24 },
                      { highFreq, 11000 }, { highGain, 3.0f },
                      { compOn, 1 }, { compThreshold, -22 }, { compRatio, 3 }, { compAttack, 8 }, { compRelease, 120 }, { compMakeup, 3 },
                      { smashOn, 1 }, { smashAmount, 40 }, { smashBlend, 25 },
                      { deessOn, 1 }, { deessThreshold, -32 }, { deessRange, 6 },
                      
                      { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 30 }, { reverbDecay, 3.2f }, { reverbPreDelay, 40 },
                      { reverbSize, 85 }, { reverbLowCut, 300 }, { reverbDuckAmount, 6 },
                      { delayOn, 1 }, { delaySync, 1 }, { delayDivision, div1_8D }, { delayFeedback, 30 }, { delayMix, 14 }, { delayPingPong, 1 },
                      { stereoWidth, 120 }, { monoBelow, 180 },
                      { outputGain, 5.0f },
                  }),

            make ("Pumping Party Vocal", "Creative",
                  i18n::both ("Hartes Pumpen wie mit Sidechain und eine zerquetschte Parallel-Ebene - die Vocal atmet mit der Kick.", "Hard sidechain-style pumping and a smashed parallel layer - the vocal breathes with the kick."),
                  {
                      { rideOn, 1 }, { rideAmount, 60 },
                      { hpfOn, 1 }, { hpfFreq, 150 },
                      { midFreq, 2000 }, { midGain, 3.0f }, { highGain, 3.0f },
                      { compOn, 1 }, { compThreshold, -26 }, { compRatio, 5 }, { compAttack, 2 }, { compRelease, 60 }, { compMakeup, 5 },
                      { smashOn, 1 }, { smashAmount, 85 }, { smashBlend, 50 }, { smashAttack, 0.8f }, { smashRelease, 50 }, { smashTone, 40 },
                      { deessOn, 1 }, { deessThreshold, -34 }, { deessRange, 8 },
                      
                      
                      { reverbOn, 1 }, { reverbType, room }, { reverbMix, 12 }, { reverbDecay, 1.0f },
                      { outputGain, 4.0f },
                  }),

            make ("Bright Pop Vocal", "Clean",
                  i18n::both ("Polierte Radio-Pop-Vocal: Präsenz und Luft, feste, weiche Kompression, Tape-Färbung, synchrones Viertel-Delay, geduckte Plate.", "Polished radio-pop vocal: presence and air, firm smooth compression, tape colour, synced quarter delay, ducked plate."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 100 }, { hpfSlope, slope24 },
                      { lowFreq, 220 }, { lowGain, -2.0f },
                      { midFreq, 3000 }, { midGain, 2.0f }, { midQ, 0.9f },
                      { highFreq, 11000 }, { highGain, 4.5f }, { highQ, 0.8f },
                      { compOn, 1 }, { compThreshold, -24 }, { compRatio, 4 }, { compAttack, 5 }, { compRelease, 90 },
                      { compKnee, 6 }, { compMakeup, 5 },
                      { deessOn, 1 }, { deessFreq, 7000 }, { deessThreshold, -34 }, { deessRange, 7 },
                      { satOn, 1 }, { satCharacter, tape }, { satDrive, 4 }, { satMix, 40 },
                      { delayOn, 1 }, { delaySync, 1 }, { delayDivision, div1_4 }, { delayFeedback, 22 }, { delayMix, 12 },
                      { delayLowCut, 400 }, { delayHighCut, 5000 }, { delayDuckAmount, 4 },
                      { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 15 }, { reverbDecay, 1.6f }, { reverbPreDelay, 30 },
                      { reverbSize, 55 }, { reverbDamping, 8000 }, { reverbLowCut, 300 }, { reverbDuckAmount, 4 },
                      { stereoWidth, 105 },
                      { outputGain, 4.5f },
                  }),

            make ("Telephone Vocal", "Creative",
                  i18n::both ("Lo-Fi-Telefon- / Radio-Effekt: enger Band Pass, nasale Mitten, hartes Clipping, kleiner trockener Raum, mono.", "Lo-fi phone / radio effect: narrow band-pass, nasal mid push, hard clipping, small dry room, mono."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 400 }, { hpfSlope, slope24 },
                      { lpfOn, 1 }, { lpfFreq, 3400 }, { lpfSlope, slope24 },
                      { midFreq, 1500 }, { midGain, 6.0f }, { midQ, 1.5f },
                      { compOn, 1 }, { compThreshold, -26 }, { compRatio, 6 }, { compAttack, 3 }, { compRelease, 80 },
                      { compMakeup, 6 },
                      { deessOn, 0 },
                      { satOn, 1 }, { satCharacter, hard }, { satDrive, 10 }, { satMix, 60 },
                      { reverbOn, 1 }, { reverbType, room }, { reverbMix, 8 }, { reverbDecay, 0.6f }, { reverbSize, 25 },
                      { reverbLowCut, 500 }, { reverbWidth, 0 },
                      { stereoWidth, 0 }, { outputGain, 5.0f },
                  }),

            make ("Vocal Chop", "Creative",
                  i18n::both ("Rhythmischer Hypertechno-Chop: Gate in Sechzehnteln mit Stutters, Ping-Pong-Sechzehntel und eine geduckte Plate.", "Rhythmic hypertechno chop: 16th gate with stutters, ping-pong sixteenths and a ducked plate."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 120 }, { hpfSlope, slope24 },
                      { highFreq, 10000 }, { highGain, 3.0f },
                      { compOn, 1 }, { compThreshold, -24 }, { compRatio, 4 }, { compAttack, 3 }, { compRelease, 70 },
                      { compMakeup, 4 },
                      { deessOn, 1 }, { deessFreq, 6500 }, { deessThreshold, -32 }, { deessRange, 6 },
                      { delayOn, 1 }, { delaySync, 1 }, { delayDivision, div1_16 }, { delayFeedback, 30 }, { delayMix, 15 },
                      { delayPingPong, 1 }, { delayLowCut, 500 }, { delayHighCut, 7000 },
                      { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 18 }, { reverbDecay, 1.8f }, { reverbSize, 60 },
                      { reverbLowCut, 400 }, { reverbDuckAmount, 6 },
                      { outputGain, 7.5f },
                  }),

            make ("Octave Down Layer", "Creative",
                  i18n::both ("Dunkle Hypertechno-Ebene: eine Kopie eine Oktave tiefer unter dem Lead (etwa 15 ms zusätzliche Latenz).", "Dark hypertechno layer: an octave-down copy blended under the lead (adds about 15 ms latency)."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 90 }, { hpfSlope, slope24 },
                      { compOn, 1 }, { compThreshold, -24 }, { compRatio, 4 }, { compAttack, 5 }, { compRelease, 90 },
                      { compMakeup, 4 },
                      { deessOn, 1 }, { deessFreq, 6500 }, { deessThreshold, -32 }, { deessRange, 6 },
                      { satOn, 1 }, { satCharacter, tube }, { satDrive, 8 }, { satMix, 45 },
                      { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 16 }, { reverbDecay, 2.0f }, { reverbDuckAmount, 5 },
                      { outputGain, 6.0f },
                  }),

            make ("Chipmunk Hook", "Creative",
                  i18n::both ("Hochgepitchte Hook (+12, Formanten wandern mit) für Drops und Vocal Chops (etwa 15 ms zusätzliche Latenz).", "Pitched-up hook (+12, formants moving with the pitch) for drops and vocal chops (adds about 15 ms latency)."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 200 }, { hpfSlope, slope24 },
                      { compOn, 1 }, { compThreshold, -22 }, { compRatio, 4 }, { compMakeup, 3 },
                      { deessOn, 1 }, { deessFreq, 8000 }, { deessThreshold, -30 }, { deessRange, 8 },
                      { delayOn, 1 }, { delaySync, 1 }, { delayDivision, div1_8D }, { delayFeedback, 30 }, { delayMix, 16 },
                      { delayPingPong, 1 },
                      { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 20 }, { reverbDecay, 1.8f }, { reverbDuckAmount, 6 },
                      { outputGain, 4.0f },
                  }),

            make ("Wide Adlibs", "Wide",
                  i18n::both ("Adlibs und Backings, die den Lead einrahmen: schlankere Tiefen, kräftige Dopplungen, sehr breit, leiser im Hintergrund.", "Adlibs and backings that frame the lead: thinner lows, strong doubles, very wide, set back in level."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 200 }, { hpfSlope, slope24 },
                      { lowFreq, 300 }, { lowGain, -3.0f },
                      { highFreq, 10000 }, { highGain, 3.0f },
                      { compOn, 1 }, { compThreshold, -24 }, { compRatio, 4 }, { compAttack, 6 }, { compRelease, 100 },
                      { compMakeup, 4 },
                      { deessOn, 1 }, { deessFreq, 6500 }, { deessThreshold, -32 }, { deessRange, 6 },
                      { doublerOn, 1 }, { doublerMix, 70 }, { doublerWidth, 100 }, { doublerDelay, 22 }, { doublerDetune, 14 },
                      { doublerMonoSafe, 80 }, { doublerTone, 7000 },
                      { delayOn, 1 }, { delaySync, 1 }, { delayDivision, div1_8D }, { delayFeedback, 30 }, { delayMix, 20 },
                      { delayPingPong, 1 }, { delayDuckAmount, 6 },
                      { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 30 }, { reverbDecay, 2.4f }, { reverbSize, 75 },
                      { reverbWidth, 100 }, { reverbDuckAmount, 6 },
                      { stereoWidth, 150 }, { monoBelow, 200 }, { outputGain, 4.0f },
                  }),

            make ("Heavy Parallel Compression", "Punchy",
                  i18n::both ("Stark gequetschter Compressor unter der natürlichen Stimme: dick und laut, die Transienten bleiben lebendig.", "Crushed compressor blended under the natural voice: thick and loud, transients stay alive."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 90 }, { hpfSlope, slope24 },
                      { compOn, 1 }, { compThreshold, -40 }, { compRatio, 12 }, { compAttack, 3 }, { compRelease, 80 },
                      { compKnee, 3 }, { compMakeup, 14 }, { compMix, 40 },
                      { deessOn, 1 }, { deessFreq, 6500 }, { deessThreshold, -32 }, { deessRange, 6 },
                      { satOn, 1 }, { satCharacter, warm }, { satDrive, 6 }, { satMix, 30 },
                      { reverbOn, 1 }, { reverbType, room }, { reverbMix, 8 }, { reverbDecay, 1.0f },
                      { outputGain, 3.0f },
                  }),

            // =================== v0.1 presets (kept unchanged) ===================
            make ("Clean Vocal", "Classic (v0.1)",
                  i18n::both ("Natürliche, kontrollierte Vocal: sanfter Low Cut, weniger Pappigkeit, leichte Kompression, kurzer Raum.", "Natural, controlled vocal: gentle low cut, de-boxing, light compression, short room."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 90 }, { hpfSlope, slope24 },
                      { lowFreq, 180 }, { lowGain, -1.0f },
                      { midFreq, 380 }, { midGain, -2.0f }, { midQ, 1.2f },
                      { highFreq, 10000 }, { highGain, 2.0f },
                      { compOn, 1 }, { compThreshold, -20 }, { compRatio, 2.5f }, { compAttack, 8 }, { compRelease, 120 },
                      { compKnee, 6 }, { compMakeup, 3 }, { compMix, 100 },
                      { deessOn, 1 }, { deessFreq, 6500 }, { deessThreshold, -32 }, { deessRange, 5 },
                      { satOn, 0 },
                      { delayOn, 0 },
                      { reverbOn, 1 }, { reverbType, room }, { reverbMix, 10 }, { reverbDecay, 1.0f }, { reverbPreDelay, 15 },
                      { reverbSize, 40 }, { reverbDamping, 6500 }, { reverbLowCut, 250 }, { reverbWidth, 80 },
                      { outputGain, 3.0f },
                  }),

            make ("Modern Pop", "Classic (v0.1)",
                  i18n::both ("Polierte, präsente Pop-Vocal: Präsenz und Luft, feste Kompression, synchrones Viertel-Delay, Plate.", "Polished, forward pop vocal: presence and air, firm compression, synced quarter delay, plate."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 100 }, { hpfSlope, slope24 },
                      { lowFreq, 220 }, { lowGain, -2.0f },
                      { midFreq, 3000 }, { midGain, 2.0f }, { midQ, 0.9f },
                      { highFreq, 11000 }, { highGain, 4.0f },
                      { compOn, 1 }, { compThreshold, -24 }, { compRatio, 4 }, { compAttack, 5 }, { compRelease, 90 },
                      { compKnee, 6 }, { compMakeup, 5 }, { compMix, 100 },
                      { deessOn, 1 }, { deessFreq, 7000 }, { deessThreshold, -34 }, { deessRange, 7 },
                      { satOn, 1 }, { satDrive, 4 }, { satOutput, 0 }, { satMix, 40 },
                      { delayOn, 1 }, { delaySync, 1 }, { delayDivision, div1_4 }, { delayTime, 400 }, { delayFeedback, 25 },
                      { delayMix, 12 }, { delayLowCut, 400 }, { delayHighCut, 5000 }, { delayPingPong, 0 },
                      { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 15 }, { reverbDecay, 1.6f }, { reverbPreDelay, 30 },
                      { reverbSize, 55 }, { reverbDamping, 8000 }, { reverbLowCut, 300 }, { reverbWidth, 100 },
                      { outputGain, 4.0f },
                  }),

            make ("Hypertechno Lead v0.1", "Classic (v0.1)",
                  i18n::both ("Helle, dichte Lead-Vocal, die sich gegen laute Kicks durchsetzt: starker Low Cut, harte Kompression, Drive, Ping-Pong 1/8, große Plate.", "Bright, dense lead vocal that cuts through loud kicks: heavy low cut, hard compression, drive, ping-pong 1/8, big plate."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 150 }, { hpfSlope, slope24 },
                      { lowFreq, 300 }, { lowGain, -3.0f },
                      { midFreq, 2500 }, { midGain, 3.0f }, { midQ, 1.0f },
                      { highFreq, 9000 }, { highGain, 5.0f },
                      { compOn, 1 }, { compThreshold, -28 }, { compRatio, 6 }, { compAttack, 2 }, { compRelease, 60 },
                      { compKnee, 3 }, { compMakeup, 7 }, { compMix, 100 },
                      { deessOn, 1 }, { deessFreq, 6000 }, { deessThreshold, -36 }, { deessRange, 9 },
                      { satOn, 1 }, { satDrive, 12 }, { satOutput, -1 }, { satMix, 70 },
                      { delayOn, 1 }, { delaySync, 1 }, { delayDivision, div1_8 }, { delayTime, 250 }, { delayFeedback, 35 },
                      { delayMix, 18 }, { delayLowCut, 500 }, { delayHighCut, 7000 }, { delayPingPong, 1 },
                      { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 22 }, { reverbDecay, 2.4f }, { reverbPreDelay, 40 },
                      { reverbSize, 70 }, { reverbDamping, 9000 }, { reverbLowCut, 400 }, { reverbWidth, 100 },
                      { outputGain, 6.5f },
                  }),

            make ("Aggressive Vocal", "Classic (v0.1)",
                  i18n::both ("Vocal direkt ins Gesicht: Biss in den oberen Mitten, schnelle starke Kompression, kräftige Sättigung, enger Raum.", "In-your-face vocal: upper-mid bite, fast heavy compression, strong saturation, tight room."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 120 }, { hpfSlope, slope24 },
                      { lowFreq, 250 }, { lowGain, -2.0f },
                      { midFreq, 1800 }, { midGain, 4.0f }, { midQ, 1.2f },
                      { highFreq, 8000 }, { highGain, 3.0f },
                      { compOn, 1 }, { compThreshold, -30 }, { compRatio, 8 }, { compAttack, 1 }, { compRelease, 50 },
                      { compKnee, 2 }, { compMakeup, 9 }, { compMix, 85 },
                      { deessOn, 1 }, { deessFreq, 6500 }, { deessThreshold, -34 }, { deessRange, 8 },
                      { satOn, 1 }, { satDrive, 16 }, { satOutput, -2 }, { satMix, 80 },
                      { delayOn, 0 },
                      { reverbOn, 1 }, { reverbType, room }, { reverbMix, 8 }, { reverbDecay, 0.8f }, { reverbPreDelay, 10 },
                      { reverbSize, 35 }, { reverbDamping, 7000 }, { reverbLowCut, 300 }, { reverbWidth, 70 },
                      { outputGain, 5.0f },
                  }),

            make ("Wide Vocal", "Classic (v0.1)",
                  i18n::both ("Offene, breite Vocal: luftige Höhen, punktiertes Achtel-Delay im Ping-Pong und eine große Plate in voller Breite.", "Open, wide vocal: airy top end, ping-pong dotted-eighth delay and a large, full-width plate."),
                  {
                      { hpfOn, 1 }, { hpfFreq, 90 }, { hpfSlope, slope24 },
                      { lowFreq, 200 }, { lowGain, -1.0f },
                      { midFreq, 3500 }, { midGain, 1.5f }, { midQ, 0.8f },
                      { highFreq, 12000 }, { highGain, 4.0f },
                      { compOn, 1 }, { compThreshold, -22 }, { compRatio, 3 }, { compAttack, 10 }, { compRelease, 150 },
                      { compKnee, 6 }, { compMakeup, 4 }, { compMix, 100 },
                      { deessOn, 1 }, { deessFreq, 7000 }, { deessThreshold, -32 }, { deessRange, 6 },
                      { satOn, 1 }, { satDrive, 4 }, { satOutput, 0 }, { satMix, 30 },
                      { delayOn, 1 }, { delaySync, 1 }, { delayDivision, div1_8D }, { delayTime, 375 }, { delayFeedback, 30 },
                      { delayMix, 20 }, { delayLowCut, 400 }, { delayHighCut, 6000 }, { delayPingPong, 1 },
                      { reverbOn, 1 }, { reverbType, plate }, { reverbMix, 25 }, { reverbDecay, 2.2f }, { reverbPreDelay, 25 },
                      { reverbSize, 80 }, { reverbDamping, 8000 }, { reverbLowCut, 300 }, { reverbWidth, 100 },
                      { outputGain, 2.5f },
                  }),
        };

        return presets;
    }
}
