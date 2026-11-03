#include "MixTarget.h"
#include "AutoEq.h"
#include "VocalProfile.h"
#include "ReportText.h"
#include "PartyTargetCurve.h"

namespace velvox::automix
{
    float thirdCentre (int k) noexcept
    {
        return 80.0f * std::pow (2.0f, (float) k / 3.0f);
    }

    std::vector<float> thirdsOf (const std::vector<float>& spectrumDb, const std::vector<dsp::EqBand>& bands)
    {
        // Power per 1/12-octave band = density x bandwidth (proportional to the centre), like autoeq::levelsOf
        const auto power = [&] (int b)
        {
            if (b < 0 || b >= (int) spectrumDb.size() || spectrumDb[(size_t) b] <= -190.0f)
                return 0.0;
            const float c = VocalProfiler::bandCentre (b);
            const double eq = bands.empty() ? 1.0 : std::pow (10.0, autoeq::responseDb (bands, c) / 10.0);
            return std::pow (10.0, spectrumDb[(size_t) b] / 10.0) * c * eq;
        };
        double core = 0.0;
        for (int b = 0; b < (int) spectrumDb.size(); ++b)
            if (const float c = VocalProfiler::bandCentre (b); c >= 300.0f && c < 2000.0f)
                core += power (b);

        std::vector<float> out ((size_t) numThirds, -200.0f);
        for (int k = 0; k < numThirds; ++k)
        {
            double e = 0.0;
            const int centre = 12 + 4 * k;
            for (int b = centre - 2; b <= centre + 1; ++b)
                e += power (b);
            if (e > 0.0 && core > 0.0)
                out[(size_t) k] = (float) (10.0 * std::log10 (e / core));
        }
        return out;
    }

    namespace
    {
        // Adds `db` to the target curve in the thirds whose centre lies within [lo, hi]
        void lift (MixTarget& t, float lo, float hi, float db, bool style = false)
        {
            for (int k = 0; k < numThirds; ++k)
                if (const float c = thirdCentre (k); c >= lo * 0.99f && c <= hi * 1.01f)
                {
                    t.curveDb[(size_t) k] += db;
                    if (style)
                        t.styleDb[(size_t) k] += db;
                }
        }
        void scaleSpace (MixTarget& t, float s)
        {
            t.space.reverbScale *= s;
            t.space.delayScale *= s;
        }
    }

    MixTarget targetFor (Genre genre, Vibe vibe)
    {
        MixTarget t;
        t.genre = genre;
        t.vibe = vibe;
        for (int k = 0; k < numThirds; ++k)
        {
            t.curveDb[(size_t) k] = generated::partyCurve[k];
            t.spreadDb[(size_t) k] = std::max (1.5f, generated::partySpread[k]);
        }

        // ---- Genres: estimated from party until there is data for them
        switch (genre)
        {
            case Genre::party:
                break;
            case Genre::rap:      // drier, denser, words in front
                lift (t, 2000.0f, 4000.0f, 1.0f);
                t.space = { 0.45f, 0.5f, 8.0f, 0.6f };
                t.dynamics = { 2.0f, 4.5f, 6.0f, 9.0f };
                break;
            case Genre::pop:      // a little more air, less density
                lift (t, 8000.0f, 16000.0f, 1.0f);
                t.densityBlend = 12.0f;
                break;
            case Genre::edm:      // brighter, more room
                lift (t, 5000.0f, 16000.0f, 1.5f);
                t.space = { 1.4f, 1.3f, 28.0f, 1.3f };
                break;
            case Genre::ballad:   // more dynamics and body, more reverb, less colour
                lift (t, 125.0f, 250.0f, 1.0f);
                t.dynamics = { 4.0f, 8.0f, 8.0f, 13.0f };
                t.densityBlend = 8.0f;
                t.satMix = 15.0f;
                t.space = { 1.6f, 1.0f, 15.0f, 1.5f };
                break;
        }
        t.estimated = genre != Genre::party;

        // ---- Vibes: estimated offsets until there is data for them
        switch (vibe)
        {
            case Vibe::punchy:       // the genre's standard
                break;
            case Vibe::aggressive:   // bite in the presence, denser, harder colour, drier
                lift (t, 2500.0f, 4000.0f, 3.0f, true);   // clearly beyond the spread of the finished vocals (1.5 - 2 dB)
                // 2026-11-03: the styles measured only 1-2.5 dB apart - every vibe now moves dynamics, colour and
                // space far enough to be heard (style_compare.py on the SONOVOX dry leads)
                t.dynamics.phraseLo -= 1.0f;
                t.dynamics.phraseHi -= 1.5f;
                t.dynamics.crestLo -= 1.5f;
                t.dynamics.crestHi -= 2.5f;
                t.densityBlend += 14.0f;
                t.satCharacter = 2;
                t.satDrive += 4.0f;
                t.satMix += 15.0f;
                scaleSpace (t, 0.55f);
                t.space.decayBars *= 0.75f;
                break;
            case Vibe::floaty:       // airy, softer, more room and width
                lift (t, 2000.0f, 4000.0f, -1.0f, true);
                lift (t, 10000.0f, 16000.0f, 3.0f, true);
                t.dynamics.phraseLo += 1.0f;
                t.dynamics.phraseHi += 1.0f;
                t.dynamics.crestLo += 1.0f;
                t.dynamics.crestHi += 1.5f;
                t.space.reverbScale *= 1.9f;
                t.space.delayScale *= 1.8f;
                t.space.decayBars *= 1.4f;
                t.space.doublerMix += 12.0f;
                break;
            case Vibe::warm:         // more body, less air, tape
                lift (t, 160.0f, 250.0f, 2.0f, true);
                lift (t, 6400.0f, 16000.0f, -3.0f, true);
                t.satCharacter = 1;
                t.satDrive += 2.0f;
                t.satMix += 15.0f;
                t.space.decayBars *= 1.15f;
                break;
            case Vibe::natural:      // close to the recording: little density, little colour, little effect
                t.dynamics.phraseLo += 2.0f;
                t.dynamics.phraseHi += 3.0f;
                t.dynamics.crestLo += 2.0f;
                t.dynamics.crestHi += 3.0f;
                t.densityBlend = 0.0f;
                t.satMix = 10.0f;
                scaleSpace (t, 0.65f);
                break;
        }
        if (vibe != Vibe::punchy)
            t.estimated = true;
        return t;
    }

    float residualDb (const MixTarget& t, int k, float voiceDb) noexcept
    {
        const size_t i = (size_t) k;
        const float d = voiceDb - (t.curveDb[i] - t.styleDb[i]);   // against the genre's curve
        const float s = t.spreadDb[i];
        const float outside = d > s ? d - s : (d < -s ? d + s : 0.0f);
        return outside - t.styleDb[i];
    }

    juce::String genreName (Genre genre)
    {
        switch (genre)
        {
            case Genre::party:  return txt::both ("Party", "Party");
            case Genre::rap:    return txt::both ("Deutschrap / Hip-Hop", "Rap / Hip-Hop");
            case Genre::pop:    return txt::both ("Pop", "Pop");
            case Genre::edm:    return txt::both ("EDM / Techno", "EDM / Techno");
            case Genre::ballad: return txt::both ("Ballade / Akustik", "Ballad / Acoustic");
        }
        return {};
    }

    juce::String vibeName (Vibe vibe)
    {
        switch (vibe)
        {
            case Vibe::punchy:     return txt::both ("Druckvoll", "Punchy");
            case Vibe::aggressive: return txt::both ("Aggressiv", "Aggressive");
            case Vibe::floaty:     return txt::both ("Floaty", "Floaty");
            case Vibe::warm:       return txt::both ("Warm", "Warm");
            case Vibe::natural:    return txt::both ("Klar & natürlich", "Clear & natural");
        }
        return {};
    }
}
