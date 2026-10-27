#include "ModulePresets.h"
#include "../DSP/Instances.h"
#include "../Parameters/ParameterIDs.h"
#include "../Text/I18n.h"

namespace velvox
{
    namespace
    {
        using V = std::vector<ModulePreset::Value>;
    }

    const std::vector<ModulePreset>& getModulePresets (dsp::ModuleId module)
    {
        using M = dsp::ModuleId;

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
            case M::smash:  return smash;
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
