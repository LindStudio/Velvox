#include "CheckTargets.h"
#include "VoiceTemplate.h"

namespace velvox::check
{
    CheckTargets CheckTargets::make (Genre genre, bool highVoice, bool spoken)
    {
        CheckTargets t;   // party: the auto-mix objective windows
        switch (genre)
        {
            case Genre::party:
                break;
            case Genre::pop:
                t.top = { -17.0f, -7.0f };
                t.phrase = { 3.0f, 8.0f };
                t.crestMin = 6.0f;
                t.lraMax = 12.0f;
                t.tailMax = 1.2f;
                break;
            case Genre::acoustic:
                t.top = { -19.0f, -8.0f };
                t.air = { -34.0f, -16.0f };
                t.phrase = { 4.0f, 10.0f };
                t.crestMin = 7.0f;
                t.crestMax = 22.0f;
                t.lraMax = 14.0f;
                t.tailMax = 1.6f;
                t.sibilancePeakMax = -10.0f;
                break;
        }
        // A high voice (lowest notes above 250 Hz) has no fundamentals in the body band:
        // there only too much body counts (as in the objective)
        if (highVoice)
            t.body.lo = -200.0f;
        // Speech / rap: drier, and words follow each other faster
        if (spoken)
            t.tailMax = std::min (t.tailMax, 0.6f);
        return t;
    }

    namespace
    {
        std::vector<float> table (const float* values)
        {
            return std::vector<float> (values, values + voicetemplate::numBands);
        }
    }

    const std::vector<float>& voiceTemplate (bool highVoice)
    {
        static const auto low = table (voicetemplate::lowVoice), high = table (voicetemplate::highVoice);
        return highVoice ? high : low;
    }

    const std::vector<float>& voiceTemplateSpread (bool highVoice)
    {
        static const auto low = table (voicetemplate::lowVoiceSpread), high = table (voicetemplate::highVoiceSpread);
        return highVoice ? high : low;
    }
}
