#include "AutoEq.h"
#include "ReportText.h"

namespace velvox::automix::autoeq
{
    using dsp::EqBand;
    using dsp::EqBandType;
    using dsp::EqRole;

    namespace
    {
        // Titles and reasons end up in the saved report: both languages
        using txt::both;
        using txt::db;
        using txt::hz;

        EqBand bandOf (EqBandType type, float freq, float gain, float q)
        {
            EqBand b;
            b.on = true;
            b.type = (int) type;
            b.freq = freq;
            b.gain = gain;
            b.q = q;
            return b;
        }

        double magnitudeSquared (const std::vector<EqBand>& bands, double freq)
        {
            double m = 1.0;
            for (auto& b : bands)
                if (b.on)
                {
                    const double a = dsp::eqdesign::analogMagnitude (b, freq);
                    m *= a * a;
                }
            return m;
        }

        float roundHalf (float v) { return std::round (v * 2.0f) / 2.0f; }

        // Strongest point of 2.5-5 kHz (1/3-octave smoothed)
        float presencePeakHz (const std::vector<float>& spec)
        {
            float f = 3150.0f, best = -1.0e9f;
            for (int b = 2; b + 2 < (int) spec.size(); ++b)
            {
                const float c = VocalProfiler::bandCentre (b);
                if (c < 2500.0f || c > 5000.0f)
                    continue;
                float s = 0.0f;
                for (int j = b - 2; j <= b + 2; ++j)
                    s += std::max (-120.0f, spec[(size_t) j]);
                if (s > best) { best = s; f = c; }
            }
            return f;
        }

        // Where a high shelf can lift the top end: 9 kHz when there is content up there, else lower
        float topShelfHz (const std::vector<float>& spec, const std::vector<EqBand>& applied)
        {
            auto probe = applied;
            const float before = levelsOf (spec, probe).top;
            probe.push_back (bandOf (EqBandType::highShelf, 9000.0f, 3.0f, 0.7071f));
            return levelsOf (spec, probe).top - before > 0.35f * 3.0f ? 9000.0f : 4500.0f;
        }

    }

    bool ownedRole (EqRole role) noexcept
    {
        switch (role)
        {
            case EqRole::lowCut: case EqRole::warmth: case EqRole::autoMix: case EqRole::air: case EqRole::highCut:
            case EqRole::autoTone: case EqRole::autoNotch1: case EqRole::autoNotch2: case EqRole::autoNotch3:
                return true;
            case EqRole::none: case EqRole::presence: case EqRole::mud:
                break;
        }
        return false;
    }

    float responseDb (const std::vector<EqBand>& bands, float freq)
    {
        return (float) (10.0 * std::log10 (std::max (1.0e-30, magnitudeSquared (bands, freq))));
    }

    Levels levelsOf (const std::vector<float>& spectrumDb, const std::vector<EqBand>& bands)
    {
        // Power per band = density x bandwidth (1/12 octave: proportional to the centre)
        const auto energy = [&] (float lo, float hi)
        {
            double e = 0.0;
            for (size_t b = 0; b < spectrumDb.size(); ++b)
            {
                const float c = VocalProfiler::bandCentre ((int) b);
                if (c >= lo && c < hi && spectrumDb[b] > -190.0f)
                    e += std::pow (10.0, spectrumDb[b] / 10.0) * c * magnitudeSquared (bands, c);
            }
            return e;
        };
        const auto rel = [] (double e, double core)
        {
            return (float) (10.0 * std::log10 (std::max (1.0e-30, e) / std::max (1.0e-30, core)));
        };
        const double core = energy (300.0f, 2000.0f);
        Levels l;
        l.body = rel (energy (100.0f, 300.0f), core);
        l.lowMid = rel (energy (250.0f, 500.0f), core);
        l.presence = rel (energy (2000.0f, 5000.0f), core);
        l.top = rel (energy (2000.0f, 16000.0f), core);
        l.air = rel (energy (10000.0f, 16000.0f), core);
        return l;
    }

    bool withinLimits (const EqBand& b) noexcept
    {
        const auto t = b.bandType();
        if (t == EqBandType::lowCut || t == EqBandType::highCut)
            return true;
        constexpr float eps = 1.0e-3f;
        if (b.q >= minNarrowQ - eps)
            return t == EqBandType::bell && b.gain <= eps && b.gain >= -maxNarrowCutDb - eps && b.q <= maxNarrowQ + eps;
        return b.gain >= -maxBroadCutDb - eps && b.gain <= maxBroadBoostDb + eps;
    }

    juce::String titleOf (EqRole role, const EqBand& b)
    {
        const float g = b.gain;
        switch (role)
        {
            case EqRole::autoNotch1: case EqRole::autoNotch2: case EqRole::autoNotch3:
                return both ("Resonanz {0} {1} (schmal, Q {2})", "Resonance {0} {1} (narrow, Q {2})", { hz (b.freq), db (g), juce::String (juce::roundToInt (b.q)) });
            case EqRole::warmth:
                return g < 0.0f ? both ("Tiefen {0} (Shelf {1})", "Lows {0} (shelf {1})", { db (g), hz (b.freq) })
                                : both ("Wärme {0} (Shelf {1})", "Warmth {0} (shelf {1})", { db (g), hz (b.freq) });
            case EqRole::autoMix:
                return both ("Tiefmitten {0} bei {1}", "Low mids {0} at {1}", { db (g), hz (b.freq) });
            case EqRole::autoTone:
                return both ("Präsenz {0} bei {1} (breit)", "Presence {0} at {1} (broad)", { db (g), hz (b.freq) });
            case EqRole::air:
                return b.freq >= 10500.0f && g > 0.0f ? both ("Luft {0} (Shelf {1})", "Air {0} (shelf {1})", { db (g), hz (b.freq) })
                                                      : both ("Höhen {0} (Shelf {1})", "Highs {0} (shelf {1})", { db (g), hz (b.freq) });
            default:
                return both ("EQ {0} bei {1}", "EQ {0} at {1}", { db (g), hz (b.freq) });
        }
    }

    namespace
    {
        // The broad band of `role` for gain `g`: the shape depends on the direction (a cut of the
        // low mids sits at their strongest point, a lift is wide at 350 Hz ...). `keep`: an existing
        // band of the role whose frequency stays as long as the direction does not change.
        EqBand shapeOf (EqRole role, float g, const Context& ctx, const std::vector<EqBand>& applied, const EqBand* keep)
        {
            if (keep != nullptr && keep->on && keep->gain != 0.0f && (keep->gain > 0.0f) == (g > 0.0f))
            {
                auto b = *keep;
                b.gain = g;
                return b;
            }
            const auto& p = *ctx.profile;
            switch (role)
            {
                case EqRole::autoMix:
                    return g < 0.0f ? bandOf (EqBandType::bell, juce::jlimit (200.0f, 500.0f, p.lowMidPeakHz), g, 1.2f)
                                    : bandOf (EqBandType::bell, 350.0f, g, 1.0f);
                case EqRole::warmth:
                    return bandOf (EqBandType::lowShelf, g < 0.0f ? 200.0f : 170.0f, g, 0.7071f);
                case EqRole::autoTone:
                    return g < 0.0f ? bandOf (EqBandType::bell, presencePeakHz (p.spectrumDb), g, 1.0f)
                                    : bandOf (EqBandType::bell, 3000.0f, g, 0.8f);
                default:
                    return bandOf (EqBandType::highShelf, g < 0.0f ? 5000.0f : topShelfHz (p.spectrumDb, applied), g, 0.7071f);
            }
        }

        // How far a broad role may go from 0 in each direction for this take
        std::pair<float, float> rangeOf (EqRole role, const Context& ctx)
        {
            const auto& p = *ctx.profile;
            const float scale = ctx.conservative ? 0.6f : 1.0f;
            const bool highVoice = p.pitchValid && p.f0LowHz > 250.0f;
            const float cut = -maxBroadCutDb * scale;
            switch (role)
            {
                case EqRole::autoMix:  return { cut, highVoice || ctx.noisy ? 0.0f : 3.0f * scale };
                case EqRole::warmth:   return { cut, highVoice || ctx.noisy || ! p.pitchValid ? 0.0f : maxBroadBoostDb * scale };
                case EqRole::autoTone: return { cut, ctx.harsh || ctx.noisy || ctx.sibilant ? 0.0f : 3.0f * scale };
                default:               return { cut, ctx.harsh ? 0.0f : (ctx.noisy || ctx.sibilant ? 2.0f : maxBroadBoostDb) * scale };
            }
        }
    }

    float tonalPenalty (const Levels& l, bool highVoice, float margin)
    {
        const float m = margin;
        const auto w = [] (float v, float lo, float hi, float s)
        {
            const float d = v < lo ? lo - v : (v > hi ? v - hi : 0.0f);
            return (d / s) * (d / s);
        };
        return 0.6f * w (l.body, highVoice ? -200.0f : bodyWindow.lo + m, bodyWindow.hi - m, 2.0f)
             + 0.8f * w (l.lowMid, lowMidWindow.lo + m, lowMidWindow.hi - m, 2.0f)
             + 1.0f * w (l.top, topWindow.lo + m, topWindow.hi - m, 2.0f)
             + 0.4f * w (l.air, airWindow.lo + m, airWindow.hi - m, 3.0f)
             + 1.0f * w (l.presence, presenceWindow.lo, presenceWindow.hi - m, 2.0f);
    }

    namespace
    {
        constexpr float costPerDb = 0.03f;     // a move has to buy more than its size
        constexpr float minGain = 0.05f;       // ... and at least this much penalty
        constexpr float planningMargin = 1.0f; // aim 1 dB inside the windows: the chain does not follow the model 1:1

        // Best gain of one broad role on a 0.5 dB grid in [lo, hi]: the smallest total of the
        // predicted tonal penalty plus the size of the move. Returns g0 when nothing is clearly better.
        float optimise (const std::function<Levels (float)>& predict, bool highVoice, float lo, float hi, float g0)
        {
            const auto cost = [&] (float g) { return tonalPenalty (predict (g), highVoice, planningMargin) + costPerDb * std::abs (g); };
            const float c0 = cost (g0);
            float best = g0, bestCost = c0;
            for (float g = std::ceil (lo * 2.0f) / 2.0f; g <= hi + 1.0e-4f; g += 0.5f)
                if (const float c = cost (g); c < bestCost - 1.0e-6f)
                {
                    best = g;
                    bestCost = c;
                }
            return bestCost < c0 - minGain ? best : g0;
        }

        juce::String reasonFor (EqRole role, float g, const Levels& l, const Context& ctx)
        {
            switch (role)
            {
                case EqRole::autoMix:
                    return g < 0.0f ? both ("250-500 Hz ist {0} gegenüber dem Kern - leicht mulmig. Ein breiter Schnitt macht Platz für Kick und Bass.",
                                            "250-500 Hz is {0} against the core - slightly muddy. A broad cut makes room for kick and bass.", { db (l.lowMid) })
                                    : both ("250-500 Hz liegt nur bei {0} gegenüber dem Kern - eine breite Anhebung gibt der Stimme Fülle.",
                                            "250-500 Hz is only {0} against the core - a broad boost gives the voice fullness.", { db (l.lowMid) });
                case EqRole::warmth:
                    return g < 0.0f ? both ("Viel Grundton-Energie (100-300 Hz: {0}) - wirkt im Club-Mix schnell dröhnend.",
                                            "A lot of fundamental energy (100-300 Hz: {0}) - quickly sounds boomy in a club mix.", { db (l.body) })
                                    : both ("Wenig Körper (100-300 Hz: {0}) - eine Anhebung gibt Wärme.",
                                            "Little body (100-300 Hz: {0}) - a boost adds warmth.", { db (l.body) });
                case EqRole::autoTone:
                    return g < 0.0f ? both ("2-5 kHz liegt bei {0} gegenüber dem Kern (über 2 kHz: {1}) - der breite Schnitt sitzt dort, wo die Schärfe liegt, und lässt die Luft darüber stehen.",
                                            "2-5 kHz is at {0} against the core (above 2 kHz: {1}) - the broad cut sits where the harshness is and leaves the air above it alone.",
                                            { db (l.presence), db (l.top) })
                                    : both ("Dunkel (über 2 kHz: {0}) - eine breite Anhebung bringt die Stimme nach vorne.",
                                            "Dark (above 2 kHz: {0}) - a broad boost brings the voice forward.", { db (l.top) });
                default:
                    return g < 0.0f ? both ("Die Aufnahme ist sehr hell (über 2 kHz: {0}) - ein breiter Shelf nimmt die Schärfe.",
                                            "The recording is very bright (above 2 kHz: {0}) - a broad shelf takes off the harshness.", { db (l.top) })
                                    : both ("Wenig Höhen (über 2 kHz: {0}, über 10 kHz: {1})", "Few highs (above 2 kHz: {0}, above 10 kHz: {1})", { db (l.top), db (l.air) })
                                          + (ctx.noisy || ctx.sibilant ? both (" - begrenzt wegen Rauschen/S-Lauten.", " - limited because of noise/sibilance.")
                                                                       : both (" - etwas Glanz hilft der Stimme durch dichte Dance-Mixe.", " - a little sheen helps the voice through dense dance mixes."));
            }
        }
    }

    std::vector<Move> planMoves (const Context& ctx)
    {
        std::vector<Move> moves;
        if (ctx.profile == nullptr || ctx.budget <= 0)
            return moves;
        const auto& p = *ctx.profile;
        const auto& spec = p.spectrumDb;
        if (! p.enoughMaterial || spec.size() != (size_t) VocalProfiler::numBands)
            return moves;

        const float scale = ctx.conservative ? 0.6f : 1.0f;
        const bool highVoice = p.pitchValid && p.f0LowHz > 250.0f;
        const auto bandsWithout = [&] (EqRole skip)
        {
            auto bands = ctx.fixed;
            for (auto& m : moves)
                if (m.role != skip)
                    bands.push_back (m.band);
            return bands;
        };
        const auto find = [&moves] (EqRole role)
        {
            return std::find_if (moves.begin(), moves.end(), [role] (const Move& m) { return m.role == role; });
        };

        // ---- Narrow notches: sustained resonances, unless a kept band already takes them out
        {
            int n = 0;
            for (auto& r : p.resonances)
            {
                if (n >= maxNotches || (int) moves.size() >= ctx.budget)
                    break;
                if (! r.sustained || r.hz < 250.0f || r.hz > 8000.0f)
                    continue;
                // What is left of the peak over its surroundings after the kept bands
                const auto applied = bandsWithout (EqRole::none);
                const float third = std::pow (2.0f, 1.0f / 3.0f);
                const float remaining = r.excessDb + responseDb (applied, r.hz)
                                      - 0.5f * (responseDb (applied, r.hz * third) + responseDb (applied, r.hz / third));
                if (remaining < 8.0f)
                    continue;
                const float gain = -roundHalf (juce::jlimit (3.0f, maxNarrowCutDb, 0.6f * remaining) * scale);
                const float q = juce::jlimit (minNarrowQ, maxNarrowQ, r.q);
                const auto role = notchRoles[n++];
                const auto b = bandOf (EqBandType::bell, r.hz, gain, q);
                moves.push_back ({ role, b, titleOf (role, b),
                                   both ("Schmale Spitze {0} über ihrer Umgebung, in der ganzen Aufnahme vorhanden (Raum oder Mikrofon) - ein schmaler Schnitt nimmt nur sie heraus.",
                                         "Narrow peak {0} above its surroundings, present in the whole recording (room or microphone) - a narrow cut takes out only that.",
                                         { db (remaining) }), true });
            }
        }

        // ---- Broad moves on the model, towards the target curve (two passes: the roles interact).
        // Open loop, from the analysis: cuts everywhere, lifts only of the highs (and the gentle
        // 0.6.0 body lift). Lifts of body and low mids are left to the engine's correction on the
        // real render - on many takes the chain (compression, saturation, effects) raises them itself.
        const auto before = levelsOf (spec, bandsWithout (EqRole::none));
        for (int pass = 0; pass < 2; ++pass)
            for (auto role : broadRoles)
            {
                auto [lo, hi] = rangeOf (role, ctx);
                if (role == EqRole::autoMix)
                    hi = 0.0f;
                else if (role == EqRole::warmth)
                    hi = before.body < -16.0f && p.pitchValid && p.f0LowHz < 200.0f ? std::min (hi, 2.0f) : 0.0f;
                auto it = find (role);
                if (it == moves.end() && (int) moves.size() >= ctx.budget)
                    continue;
                const bool has = it != moves.end();
                const EqBand keep = has ? it->band : EqBand {};
                const float g0 = has ? it->band.gain : 0.0f;
                const auto applied = bandsWithout (role);
                const auto make = [&] (float g) { return shapeOf (role, g, ctx, applied, has ? &keep : nullptr); };
                const auto predict = [&] (float g) { auto b = applied; b.push_back (make (g)); return levelsOf (spec, b); };
                const float g = optimise (predict, highVoice, lo, hi, g0);
                if (g == g0)
                    continue;
                if (has && std::abs (g) < 0.25f)
                {
                    moves.erase (it);
                    continue;
                }
                if (! has && std::abs (g) < minMoveDb)
                    continue;
                Move m { role, make (g), {}, reasonFor (role, g, before, ctx), false };
                m.title = titleOf (role, m.band);
                if (has)
                    *it = m;
                else
                    moves.push_back (m);
            }

        // ---- The beat masks the presence range (sidechain, only a hint): a small lift
        if (ctx.masking && ! ctx.harsh && find (EqRole::autoTone) == moves.end() && (int) moves.size() < ctx.budget)
        {
            const auto b = bandOf (EqBandType::bell, 3000.0f, 1.0f, 0.9f);
            moves.push_back ({ EqRole::autoTone, b, titleOf (EqRole::autoTone, b),
                               both ("Der Beat ist im Präsenzbereich lauter als die Stimme (Sidechain-Messung, nur ein Hinweis) - daher nur eine kleine Anhebung.",
                                     "The beat is louder than the voice in the presence range (sidechain measurement, only a hint) - so only a small boost."), false });
        }
        return moves;
    }

    std::vector<Move> correct (const Levels& rendered, const Context& ctx, const std::vector<Move>& owned)
    {
        std::vector<Move> out;
        if (ctx.profile == nullptr || ctx.profile->spectrumDb.size() != (size_t) VocalProfiler::numBands || ! ctx.profile->enoughMaterial)
            return out;
        const auto& p = *ctx.profile;
        const bool highVoice = p.pitchValid && p.f0LowHz > 250.0f;
        if (tonalPenalty (rendered, highVoice) <= 0.0f)
            return out;
        int budget = ctx.budget;

        // The render is the starting point; the model predicts what a move changes
        std::vector<Move> state = owned;
        const auto bandsWithout = [&] (EqRole skip)
        {
            auto bands = ctx.fixed;
            for (auto& m : state)
                if (m.role != skip)
                    bands.push_back (m.band);
            return bands;
        };
        const auto modelAtRender = levelsOf (p.spectrumDb, bandsWithout (EqRole::none));

        for (auto role : broadRoles)
        {
            auto it = std::find_if (state.begin(), state.end(), [role] (const Move& m) { return m.role == role; });
            const bool has = it != state.end();
            if (! has && budget <= 0)
                continue;
            const EqBand keep = has ? it->band : EqBand {};
            const float g0 = has ? it->band.gain : 0.0f;
            const auto [lo, hi] = rangeOf (role, ctx);
            const auto applied = bandsWithout (role);
            const auto make = [&] (float g) { return shapeOf (role, g, ctx, applied, has ? &keep : nullptr); };
            const auto predict = [&] (float g)
            {
                auto b = applied;
                b.push_back (make (g));
                const auto m = levelsOf (p.spectrumDb, b);
                Levels r = rendered;   // rendered + the change on the model since the render
                r.body += m.body - modelAtRender.body;
                r.lowMid += m.lowMid - modelAtRender.lowMid;
                r.presence += m.presence - modelAtRender.presence;
                r.top += m.top - modelAtRender.top;
                r.air += m.air - modelAtRender.air;
                return r;
            };
            const float g = std::clamp (optimise (predict, highVoice, std::min (lo, g0), std::max (hi, g0), g0), lo, hi);
            if (std::abs (g - g0) < 0.5f || (! has && std::abs (g) < minMoveDb))
                continue;

            const auto v = predict (g0);
            Move m { role, make (g), {}, {}, false };
            if (std::abs (g) < 0.25f)
                m.band.gain = 0.0f;
            m.title = titleOf (role, m.band);
            m.reason = both ("Nach der Messung der ganzen Kette: {0}", "After measuring the whole chain: {0}", { reasonFor (role, g, v, ctx) });
            if (has)
                it->band = m.band;
            else
            {
                state.push_back (m);
                --budget;
            }
            out.push_back (m);
        }
        return out;
    }
}
