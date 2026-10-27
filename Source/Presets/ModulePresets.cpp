#include "ModulePresets.h"
#include "../DSP/Instances.h"
#include "../Parameters/ParameterIDs.h"
#include "../Text/I18n.h"

namespace velvox
{
    namespace
    {
        using V = std::vector<ModulePreset::Value>;

        // Six curve points: times then levels
        V curve (std::array<float, 6> x, std::array<float, 6> y)
        {
            V v;
            for (size_t i = 0; i < 6; ++i)
            {
                v.push_back ({ pid::pulseX[i], x[i] });
                v.push_back ({ pid::pulseY[i], y[i] });
            }
            return v;
        }

        V join (V a, const V& b)
        {
            a.insert (a.end(), b.begin(), b.end());
            return a;
        }

        // Pulse rate indices (PulseModule::rateNames)
        constexpr float rate1_2 = 1, rate1_4 = 2, rate1_8 = 3, rate1_16 = 4;
        constexpr float linear = 0, smooth = 1, step = 2;
    }

    const std::vector<ModulePreset>& getModulePresets (dsp::ModuleId module)
    {
        using M = dsp::ModuleId;

        static const std::vector<ModulePreset> pulse {
            { "Subtle Pump", i18n::both ("Sanftes Atmen in Vierteln unter einer durchgehenden Kick.", "Gentle quarter-note breathing under a four-on-the-floor kick."),
              join ({ { pid::pulseOn, 1 }, { pid::pulseRate, rate1_4 }, { pid::pulseDepth, 35 }, { pid::pulseInterp, smooth },
                      { pid::pulseTension, 10 }, { pid::pulseAttack, 2 }, { pid::pulseRelease, 20 }, { pid::pulseSmooth, 3 }, { pid::pulseMix, 100 } },
                    curve ({ 0.0f, 0.08f, 0.3f, 0.55f, 0.8f, 0.95f }, { 0.0f, 0.15f, 0.75f, 0.95f, 1.0f, 1.0f })) },
            { "Hard Pump", i18n::both ("Tiefes, druckvolles Sidechain-Pumpen - die Vocal duckt auf jedem Schlag kräftig.", "Deep, punchy sidechain pump - the vocal ducks hard on every beat."),
              join ({ { pid::pulseOn, 1 }, { pid::pulseRate, rate1_4 }, { pid::pulseDepth, 90 }, { pid::pulseInterp, smooth },
                      { pid::pulseTension, 45 }, { pid::pulseAttack, 0.5f }, { pid::pulseRelease, 8 }, { pid::pulseSmooth, 1.5f }, { pid::pulseMix, 100 } },
                    curve ({ 0.0f, 0.04f, 0.35f, 0.6f, 0.8f, 0.97f }, { 0.0f, 0.0f, 0.8f, 1.0f, 1.0f, 1.0f })) },
            { "Offbeat", i18n::both ("Laut auf dem Offbeat, leiser auf dem Schlag - der klassische Hard-Dance-Bounce.", "Loud on the offbeat, dipped on the beat - classic hard dance bounce."),
              join ({ { pid::pulseOn, 1 }, { pid::pulseRate, rate1_4 }, { pid::pulseDepth, 75 }, { pid::pulseInterp, smooth },
                      { pid::pulseTension, 0 }, { pid::pulseAttack, 1 }, { pid::pulseRelease, 6 }, { pid::pulseSmooth, 2 }, { pid::pulseMix, 100 } },
                    curve ({ 0.0f, 0.2f, 0.42f, 0.58f, 0.8f, 0.95f }, { 0.1f, 0.15f, 1.0f, 1.0f, 0.4f, 0.12f })) },
            { "Vocal Stutter", i18n::both ("Hartes 1/16-Gate für gestotterte Hooks.", "Hard 1/16 gating for stuttered hooks."),
              join ({ { pid::pulseOn, 1 }, { pid::pulseRate, rate1_8 }, { pid::pulseDepth, 100 }, { pid::pulseInterp, step },
                      { pid::pulseTension, 0 }, { pid::pulseAttack, 0.5f }, { pid::pulseRelease, 1.5f }, { pid::pulseSmooth, 1.5f }, { pid::pulseMix, 100 } },
                    curve ({ 0.0f, 0.3f, 0.5f, 0.8f, 0.9f, 0.95f }, { 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f })) },
            { "Trance Gate 1/16", i18n::both ("Schnelles rhythmisches Gate in Sechzehnteln mit weichen Kanten.", "Fast rhythmic gate in sixteenths with soft edges."),
              join ({ { pid::pulseOn, 1 }, { pid::pulseRate, rate1_16 }, { pid::pulseDepth, 85 }, { pid::pulseInterp, linear },
                      { pid::pulseTension, 0 }, { pid::pulseAttack, 0.8f }, { pid::pulseRelease, 2 }, { pid::pulseSmooth, 2 }, { pid::pulseMix, 100 } },
                    curve ({ 0.0f, 0.1f, 0.55f, 0.65f, 0.8f, 0.95f }, { 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f })) },
            { "Half-Time Swell", i18n::both ("Langsames Anschwellen über zwei Schläge - für Breakdowns.", "Slow swell over two beats - for breakdowns."),
              join ({ { pid::pulseOn, 1 }, { pid::pulseRate, rate1_2 }, { pid::pulseDepth, 60 }, { pid::pulseInterp, smooth },
                      { pid::pulseTension, -20 }, { pid::pulseAttack, 5 }, { pid::pulseRelease, 40 }, { pid::pulseSmooth, 6 }, { pid::pulseMix, 100 } },
                    curve ({ 0.0f, 0.1f, 0.5f, 0.7f, 0.85f, 0.95f }, { 0.0f, 0.1f, 0.7f, 0.95f, 1.0f, 1.0f })) },
        };

        static const std::vector<ModulePreset> smash {
            { "Gentle Glue", i18n::both ("Etwas Dichte unter einer natürlichen Vocal.", "A little density under a natural vocal."),
              { { pid::smashOn, 1 }, { pid::smashAmount, 30 }, { pid::smashBlend, 25 }, { pid::smashAttack, 5 }, { pid::smashRelease, 120 }, { pid::smashTone, 0 }, { pid::smashOutput, 0 } } },
            { "Party Density", i18n::both ("Dick und präsent - behauptet sich gegen einen lauten Beat.", "Thick and present - holds its own against a loud beat."),
              { { pid::smashOn, 1 }, { pid::smashAmount, 60 }, { pid::smashBlend, 45 }, { pid::smashAttack, 2 }, { pid::smashRelease, 80 }, { pid::smashTone, 30 }, { pid::smashOutput, 0 } } },
            { "Hard Smash", i18n::both ("Aggressive, vorne stehende Hypertechno-Vocal.", "Aggressive, upfront hypertechno vocal."),
              { { pid::smashOn, 1 }, { pid::smashAmount, 90 }, { pid::smashBlend, 55 }, { pid::smashAttack, 0.5f }, { pid::smashRelease, 50 }, { pid::smashTone, 45 }, { pid::smashOutput, -1 } } },
            { "Warm Thickener", i18n::both ("Runderer Körper ohne zusätzliche Schärfe.", "Rounder body without extra edge."),
              { { pid::smashOn, 1 }, { pid::smashAmount, 55 }, { pid::smashBlend, 40 }, { pid::smashAttack, 8 }, { pid::smashRelease, 150 }, { pid::smashTone, -45 }, { pid::smashOutput, 0 } } },
        };

        static const std::vector<ModulePreset> choir {
            { "Subtle Double", i18n::both ("Wie ein zweiter Take, kaum breiter.", "Feels like a second take, barely wider."),
              { { pid::choirOn, 1 }, { pid::choirVoices, 0 }, { pid::choirDetune, 6 }, { pid::choirTiming, 12 }, { pid::choirHumanize, 40 },
                { pid::choirWidth, 70 }, { pid::choirDensity, 40 }, { pid::choirMix, 25 } } },
            { "Wide Double", i18n::both ("Vier Stimmen um den Lead - breit, aber straff.", "Four voices around the lead - wide but tight."),
              { { pid::choirOn, 1 }, { pid::choirVoices, 1 }, { pid::choirDetune, 10 }, { pid::choirTiming, 18 }, { pid::choirHumanize, 50 },
                { pid::choirWidth, 90 }, { pid::choirDensity, 50 }, { pid::choirMix, 35 } } },
            { "Gang Vocal", i18n::both ("Eine Menge, die die Hook gemeinsam ruft.", "A crowd shouting the hook together."),
              { { pid::choirOn, 1 }, { pid::choirVoices, 2 }, { pid::choirDetune, 14 }, { pid::choirTiming, 30 }, { pid::choirHumanize, 70 },
                { pid::choirWidth, 95 }, { pid::choirDensity, 70 }, { pid::choirMix, 50 } } },
            { "Wide Choir", i18n::both ("Großer, satter Chor mit acht Stimmen für Drops und Hymnen.", "Big, lush eight-voice choir for drops and anthems."),
              { { pid::choirOn, 1 }, { pid::choirVoices, 3 }, { pid::choirDetune, 18 }, { pid::choirTiming, 35 }, { pid::choirHumanize, 75 },
                { pid::choirWidth, 100 }, { pid::choirDensity, 75 }, { pid::choirMix, 45 } } },
        };

        static const std::vector<ModulePreset> smoothPresets {
            { "Harsh Highs", i18n::both ("Zähmt stechende Spitzen bei 2-10 kHz, ohne die Luft zu dämpfen.", "Tames piercing 2-10 kHz peaks without dulling the air."),
              { { pid::smoothOn, 1 }, { pid::smoothAmount, 60 }, { pid::smoothSensitivity, 55 }, { pid::smoothLow, 2000 }, { pid::smoothHigh, 10000 },
                { pid::smoothAttack, 4 }, { pid::smoothRelease, 70 }, { pid::smoothMode, 0 }, { pid::smoothOutput, 0 }, { pid::smoothDelta, 0 } } },
            { "Nasal Vocal", i18n::both ("Beruhigt näselnde Resonanzen zwischen 700 Hz und 2,5 kHz.", "Calms honky 700 Hz - 2.5 kHz resonances."),
              { { pid::smoothOn, 1 }, { pid::smoothAmount, 55 }, { pid::smoothSensitivity, 60 }, { pid::smoothLow, 700 }, { pid::smoothHigh, 2500 },
                { pid::smoothAttack, 8 }, { pid::smoothRelease, 120 }, { pid::smoothMode, 0 }, { pid::smoothOutput, 0 }, { pid::smoothDelta, 0 } } },
            { "Aggressive Resonances", i18n::both ("Starke, schnelle Kontrolle für sehr klingelnde oder verzerrte Vocals.", "Strong, fast control for very ringing or distorted vocals."),
              { { pid::smoothOn, 1 }, { pid::smoothAmount, 80 }, { pid::smoothSensitivity, 70 }, { pid::smoothLow, 800 }, { pid::smoothHigh, 12000 },
                { pid::smoothAttack, 2 }, { pid::smoothRelease, 50 }, { pid::smoothMode, 1 }, { pid::smoothOutput, 0 }, { pid::smoothDelta, 0 } } },
            { "Gentle Polish", i18n::both ("Leichtes, breites Glätten für eine schon gute Vocal.", "Light, broad smoothing for an already good vocal."),
              { { pid::smoothOn, 1 }, { pid::smoothAmount, 35 }, { pid::smoothSensitivity, 40 }, { pid::smoothLow, 1000 }, { pid::smoothHigh, 14000 },
                { pid::smoothAttack, 6 }, { pid::smoothRelease, 100 }, { pid::smoothMode, 0 }, { pid::smoothOutput, 0 }, { pid::smoothDelta, 0 } } },
        };

        static const std::vector<ModulePreset> ride {
            { "Natural", i18n::both ("Sanftes Ausgleichen über Phrasen - die Performance bleibt erhalten.", "Gentle riding over phrases - keeps the performance."),
              { { pid::rideOn, 1 }, { pid::rideTarget, -18 }, { pid::rideAmount, 60 }, { pid::rideMaxBoost, 6 }, { pid::rideMaxCut, 6 },
                { pid::rideResponse, 40 }, { pid::ridePhrase, 60 }, { pid::rideTransient, 60 }, { pid::rideSilence, -50 } } },
            { "Tight", i18n::both ("Wort für Wort - jede Silbe auf ähnlichem Pegel.", "Word by word - every syllable at a similar level."),
              { { pid::rideOn, 1 }, { pid::rideTarget, -18 }, { pid::rideAmount, 85 }, { pid::rideMaxBoost, 10 }, { pid::rideMaxCut, 10 },
                { pid::rideResponse, 70 }, { pid::ridePhrase, 20 }, { pid::rideTransient, 70 }, { pid::rideSilence, -48 } } },
            { "Phrase Only", i18n::both ("Gleicht nur ganze Phrasen aus, nie einzelne Wörter.", "Only evens out whole phrases, never single words."),
              { { pid::rideOn, 1 }, { pid::rideTarget, -18 }, { pid::rideAmount, 70 }, { pid::rideMaxBoost, 8 }, { pid::rideMaxCut, 8 },
                { pid::rideResponse, 25 }, { pid::ridePhrase, 90 }, { pid::rideTransient, 50 }, { pid::rideSilence, -50 } } },
            { "Noisy Room", i18n::both ("Ausgleich, der Atmer und Raumrauschen in Ruhe lässt.", "Leveling that stays away from breaths and room noise."),
              { { pid::rideOn, 1 }, { pid::rideTarget, -18 }, { pid::rideAmount, 65 }, { pid::rideMaxBoost, 5 }, { pid::rideMaxCut, 8 },
                { pid::rideResponse, 45 }, { pid::ridePhrase, 50 }, { pid::rideTransient, 80 }, { pid::rideSilence, -42 } } },
        };

        static const std::vector<ModulePreset> none;

        switch (module)
        {
            case M::pulse:  return pulse;
            case M::smash:  return smash;
            case M::choir:  return choir;
            case M::smooth: return smoothPresets;
            case M::ride:   return ride;
            default:        return none;
        }
    }

    void applyModulePreset (const ModulePreset& preset, juce::AudioProcessorValueTreeState& apvts, int instance)
    {
        for (const auto& [id, value] : preset.values)
        {
            auto* p = apvts.getParameter (dsp::instanceParamId (id, instance));
            if (p == nullptr)
                p = apvts.getParameter (id);   // a parameter without a bank (global)
            if (p != nullptr)
            {
                const float normalised = p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (value));
                p->beginChangeGesture();
                p->setValueNotifyingHost (normalised);
                p->endChangeGesture();
            }
        }
    }
}
