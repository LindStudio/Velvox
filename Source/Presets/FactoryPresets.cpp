#include "FactoryPresets.h"
#include "../Parameters/ParameterIDs.h"

namespace velvox
{
    namespace
    {
        // Delay division indices (see delaydiv::names())
        constexpr float div1_8 = 6.0f, div1_8D = 7.0f, div1_4 = 9.0f;
        constexpr float slope24 = 1.0f;
        constexpr float room = 0.0f, plate = 1.0f;

        struct Value
        {
            const char* id;
            float value;
        };

        ParameterSnapshot make (const char* name, const char* category, const char* description,
                                std::initializer_list<Value> values)
        {
            ParameterSnapshot s;
            s.name = name;
            s.category = category;
            s.description = description;
            for (const auto& v : values)
                s.values[v.id] = v.value;
            return s;
        }
    }

    const std::vector<ParameterSnapshot>& getFactoryPresets()
    {
        using namespace pid;

        static const std::vector<ParameterSnapshot> presets
        {
            make ("Init", "Utility", "Neutral default settings.", {}),

            make ("Clean Vocal", "Vocal",
                  "Natural, controlled vocal: gentle low cut, de-boxing, light compression, short room.",
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
                  }),

            make ("Modern Pop", "Vocal",
                  "Polished, forward pop vocal: presence and air, firm compression, synced quarter delay, plate.",
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
                  }),

            make ("Hypertechno Lead", "EDM",
                  "Bright, dense lead vocal that cuts through loud kicks: heavy low cut, hard compression, drive, ping-pong 1/8, big plate.",
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
                      { outputGain, -1.0f },
                  }),

            make ("Aggressive Vocal", "Vocal",
                  "In-your-face vocal: upper-mid bite, fast heavy compression, strong saturation, tight room.",
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
                      { outputGain, -2.0f },
                  }),

            make ("Wide Vocal", "Vocal",
                  "Open, wide vocal: airy top end, ping-pong dotted-eighth delay and a large, full-width plate.",
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
                  }),
        };

        return presets;
    }
}
