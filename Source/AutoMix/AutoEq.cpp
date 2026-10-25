#include "AutoEq.h"

namespace velvox::automix::autoeq
{
    using dsp::EqBand;
    using dsp::EqBandType;
    using dsp::EqRole;

    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }
        juce::String db (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }
        juce::String hz (float f)
        {
            return f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
        }

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

        using Measure = float (*) (const Levels&);
        float bodyOf (const Levels& l) { return l.body; }
        float lowMidOf (const Levels& l) { return l.lowMid; }
        float presenceOf (const Levels& l) { return l.presence; }
        float topOf (const Levels& l) { return l.top; }
        float airOf (const Levels& l) { return l.air; }

        // Gain in [lo, hi] for which `measure` of the spectrum with `make (gain)` added reaches
        // `target`; the nearest end when it cannot be reached. Bisection: every measure here rises
        // monotonically with the gain of the band that serves it.
        float solve (const std::vector<float>& spec, const std::vector<EqBand>& applied,
                     const std::function<EqBand (float)>& make, Measure measure, float target, float lo, float hi)
        {
            if (hi <= lo)
                return lo;
            auto bands = applied;
            bands.push_back (make (lo));
            const auto at = [&] (float g)
            {
                bands.back() = make (g);
                return measure (levelsOf (spec, bands)) - target;
            };
            float a = lo, b = hi;
            float fa = at (a);
            const float fb = at (b);
            if (fa * fb > 0.0f)
                return std::abs (fa) < std::abs (fb) ? a : b;
            for (int i = 0; i < 24; ++i)
            {
                const float m = 0.5f * (a + b);
                const float fm = at (m);
                if (fm * fa > 0.0f) { a = m; fa = fm; }
                else                b = m;
            }
            return 0.5f * (a + b);
        }

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

        Measure measureOf (EqRole role)
        {
            switch (role)
            {
                case EqRole::autoMix:  return lowMidOf;
                case EqRole::warmth:   return bodyOf;
                case EqRole::autoTone: return presenceOf;
                case EqRole::air:      return topOf;
                default:               return topOf;
            }
        }

        Window windowOf (EqRole role)
        {
            switch (role)
            {
                case EqRole::autoMix:  return lowMidWindow;
                case EqRole::warmth:   return bodyWindow;
                case EqRole::autoTone: return presenceWindow;
                default:               return topWindow;
            }
        }

        const char* nameOf (EqRole role)
        {
            switch (role)
            {
                case EqRole::autoMix:  return "250-500 Hz";
                case EqRole::warmth:   return "100-300 Hz";
                case EqRole::autoTone: return "2-5 kHz";
                default:               return "über 2 kHz";
            }
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
                return de ("Resonanz ") + hz (b.freq) + " " + db (g) + de (" (schmal, Q ") + juce::String (juce::roundToInt (b.q)) + ")";
            case EqRole::warmth:
                return (g < 0.0f ? de ("Tiefen ") : de ("Wärme ")) + db (g) + de (" (Shelf ") + hz (b.freq) + ")";
            case EqRole::autoMix:
                return de ("Tiefmitten ") + db (g) + de (" bei ") + hz (b.freq);
            case EqRole::autoTone:
                return de ("Präsenz ") + db (g) + de (" bei ") + hz (b.freq) + de (" (breit)");
            case EqRole::air:
                return (b.freq >= 10500.0f && g > 0.0f ? de ("Luft ") : de ("Höhen ")) + db (g) + de (" (Shelf ") + hz (b.freq) + ")";
            default:
                return de ("EQ ") + db (g) + de (" bei ") + hz (b.freq);
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
        auto applied = ctx.fixed;
        const auto add = [&] (EqRole role, const EqBand& band, juce::String reason, bool narrow)
        {
            moves.push_back ({ role, band, titleOf (role, band), std::move (reason), narrow });
            applied.push_back (band);
        };
        const auto used = [&moves] (EqRole role)
        {
            return std::any_of (moves.begin(), moves.end(), [role] (const Move& m) { return m.role == role; });
        };
        // A broad move on the model, towards `target`, rounded to 0.5 dB, skipped when too small
        const auto broad = [&] (EqRole role, float target, bool lift, float maxLift, const juce::String& reason)
        {
            const auto [cut, liftRange] = rangeOf (role, ctx);
            const float lo = lift ? 0.0f : cut, hi = lift ? std::min (liftRange, maxLift) : 0.0f;
            if (hi - lo < minMoveDb)
                return false;
            const auto make = [&] (float g) { return shapeOf (role, g, ctx, applied, nullptr); };
            const float g = roundHalf (solve (spec, applied, make, measureOf (role), target, lo, hi));
            if (std::abs (g) < minMoveDb)
                return false;
            add (role, make (g), reason, false);
            return true;
        };

        // ---- Narrow notches: sustained resonances, unless a kept band already takes them out
        {
            int n = 0;
            for (auto& r : p.resonances)
            {
                if (n >= maxNotches)
                    break;
                if (! r.sustained || r.hz < 250.0f || r.hz > 8000.0f)
                    continue;
                // What is left of the peak over its surroundings after the kept bands
                const float third = std::pow (2.0f, 1.0f / 3.0f);
                const float remaining = r.excessDb + responseDb (applied, r.hz)
                                      - 0.5f * (responseDb (applied, r.hz * third) + responseDb (applied, r.hz / third));
                if (remaining < 8.0f)
                    continue;
                const float gain = -roundHalf (juce::jlimit (3.0f, maxNarrowCutDb, 0.6f * remaining) * scale);
                const float q = juce::jlimit (minNarrowQ, maxNarrowQ, r.q);
                add (notchRoles[n++], bandOf (EqBandType::bell, r.hz, gain, q),
                     de ("Schmale Spitze ") + db (remaining) + de (" über ihrer Umgebung, in der ganzen Aufnahme vorhanden "
                                                                    "(Raum oder Mikrofon) - ein schmaler Schnitt nimmt nur sie heraus."),
                     true);
            }
        }

        // ---- Open loop, from the analysis: cut what is clearly too much, lift a dull top end.
        // Lifts of body and low mids are left to the engine's correction on the real render: the
        // chain (compression, saturation, effects) raises them by itself on many takes.
        {
            const auto l = levelsOf (spec, applied);
            if (l.lowMid > lowMidWindow.hi - 1.0f)
                broad (EqRole::autoMix, lowMidWindow.hi - 2.0f, false, 0.0f,
                       de ("250-500 Hz ist ") + db (l.lowMid) + de (" gegenüber dem Kern - leicht mulmig. Ein breiter Schnitt macht Platz für Kick und Bass."));
        }
        {
            const auto l = levelsOf (spec, applied);
            if (l.body > -3.0f)
                broad (EqRole::warmth, -4.5f, false, 0.0f,
                       de ("Viel Grundton-Energie (100-300 Hz: ") + db (l.body) + de (") - wirkt im Club-Mix schnell dröhnend."));
            else if (l.body < -16.0f && p.pitchValid && p.f0LowHz < 200.0f)
                broad (EqRole::warmth, -14.0f, true, 2.0f,
                       de ("Wenig Körper (100-300 Hz: ") + db (l.body) + de (") - eine kleine Anhebung gibt Wärme."));
        }
        {
            const auto l = levelsOf (spec, applied);
            if (l.presence > presenceWindow.hi + 0.5f)
                // SMOOTH works on the same harshness: the static cut only takes the bulk
                broad (EqRole::autoTone, presenceWindow.hi - 0.5f, false, 0.0f,
                       de ("2-5 kHz liegt bei ") + db (l.presence) + de (" gegenüber dem Kern - kann scharf klingen."));
        }
        {
            const auto l = levelsOf (spec, applied);
            if (l.top > topWindow.hi + 0.5f)
                broad (EqRole::air, topWindow.hi - 1.0f, false, 0.0f,
                       de ("Die Aufnahme ist sehr hell (über 2 kHz: ") + db (l.top) + de (") - ein breiter Shelf nimmt die Schärfe."));
            else if (l.top < topWindow.lo - 0.5f)
            {
                broad (EqRole::air, topWindow.lo + 1.5f, true, maxBroadBoostDb,
                       de ("Die Aufnahme ist dunkel (über 2 kHz: ") + db (l.top) + ")"
                           + (ctx.noisy || ctx.sibilant ? de (" - begrenzt wegen Rauschen/S-Lauten.") : juce::String (".")));
                const auto after = levelsOf (spec, applied);
                if (after.top < topWindow.lo - 0.5f && ! used (EqRole::autoTone))
                    broad (EqRole::autoTone, topWindow.lo + 1.5f, true, 3.0f,
                           de ("Auch mit der Höhenanhebung noch dunkel (über 2 kHz: ") + db (after.top)
                               + de (") - eine breite Anhebung bringt die Stimme nach vorne."));
            }
            else if (l.air < airWindow.lo + 2.0f && ! ctx.harsh && ! ctx.sibilant && ! ctx.noisy)
            {
                const auto make = [&] (float g) { return bandOf (EqBandType::highShelf, 11000.0f, g, 0.7071f); };
                const float g = roundHalf (solve (spec, applied, make, airOf, airWindow.lo + 4.0f, 0.0f, 3.0f * scale));
                if (g >= minMoveDb)
                    add (EqRole::air, make (g), de ("Wenig Energie über 10 kHz (") + db (l.air) + de (") - etwas Luft hilft der Stimme durch dichte Dance-Mixe."), false);
            }
        }

        // ---- The beat masks the presence range (sidechain, only a hint): a small lift
        if (ctx.masking && ! ctx.harsh && ! used (EqRole::autoTone))
            add (EqRole::autoTone, bandOf (EqBandType::bell, 3000.0f, 1.0f, 0.9f),
                 de ("Der Beat ist im Präsenzbereich lauter als die Stimme (Sidechain-Messung, nur ein Hinweis) - daher nur eine kleine Anhebung."), false);

        // ---- Budget: the largest moves stay
        if ((int) moves.size() > ctx.budget)
        {
            std::stable_sort (moves.begin(), moves.end(), [] (const Move& a, const Move& b) { return std::abs (a.band.gain) > std::abs (b.band.gain); });
            moves.resize ((size_t) ctx.budget);
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
        int budget = ctx.budget;

        // Model state: the kept bands and the auto-mix bands as planned (updated as we go)
        std::vector<Move> state = owned;
        const auto modelBands = [&] (EqRole skip)
        {
            auto bands = ctx.fixed;
            for (auto& m : state)
                if (m.role != skip)
                    bands.push_back (m.band);
            return bands;
        };
        const auto find = [&state] (EqRole role) -> Move*
        {
            for (auto& m : state)
                if (m.role == role)
                    return &m;
            return nullptr;
        };

        const auto value = [&] (EqRole role)
        {
            switch (role)
            {
                case EqRole::autoMix:  return rendered.lowMid;
                case EqRole::warmth:   return rendered.body;
                case EqRole::autoTone: return rendered.presence;
                default:               return rendered.top;
            }
        };

        for (auto role : broadRoles)
        {
            auto w = windowOf (role);
            if (role == EqRole::warmth && highVoice)
                w.lo = -200.0f;   // no fundamentals in 100-300 Hz: only too much body counts (objective)
            const float v = value (role);
            float want = 0.0f;
            if (v > w.hi + 0.5f)      want = (w.hi - 1.0f) - v;
            else if (v < w.lo - 0.5f) want = (w.lo + 1.0f) - v;
            if (want == 0.0f)
                continue;

            auto* existing = find (role);
            const EqBand* keep = existing != nullptr && existing->band.on ? &existing->band : nullptr;
            const float g0 = keep != nullptr ? keep->gain : 0.0f;
            if (existing == nullptr && budget <= 0)
                continue;

            const auto [cut, lift] = rangeOf (role, ctx);
            const float lo = want < 0.0f ? cut : g0, hi = want < 0.0f ? g0 : lift;
            if (hi - lo < 0.5f)
                continue;   // at the limit in that direction

            const auto applied = modelBands (role);
            const auto make = [&] (float g) { return shapeOf (role, g, ctx, applied, keep); };
            const float now = measureOf (role) (levelsOf (p.spectrumDb, [&] { auto b = applied; if (keep != nullptr) b.push_back (*keep); return b; }()));
            // Damped: the chain does not follow the model 1:1
            const float g = roundHalf (solve (p.spectrumDb, applied, make, measureOf (role), now + 0.8f * want, lo, hi));
            if (std::abs (g - g0) < 0.5f)
                continue;

            Move m;
            m.role = role;
            m.band = std::abs (g) < 0.5f ? [&] { auto b = keep != nullptr ? *keep : make (0.0f); b.gain = 0.0f; return b; }() : make (g);
            m.title = titleOf (role, m.band);
            m.reason = de ("Nach der Messung der ganzen Kette: ") + nameOf (role) + de (" lag bei ") + db (v)
                     + de (" gegenüber dem Kern (Ziel ") + (w.lo > -150.0f ? db (w.lo) + de (" bis ") : de ("bis ")) + db (w.hi) + ").";
            if (existing != nullptr)
                existing->band = m.band;
            else
            {
                state.push_back (m);
                --budget;
            }
            out.push_back (m);
        }

        // Still dull with the top shelf at its limit: the presence lift (as in the plan)
        if (rendered.top < topWindow.lo - 0.5f && std::none_of (out.begin(), out.end(), [] (const Move& m) { return m.role == EqRole::autoTone; }))
        {
            auto* air = find (EqRole::air);
            auto* tone = find (EqRole::autoTone);
            const auto [cutT, liftT] = rangeOf (EqRole::autoTone, ctx);
            const auto [cutA, liftA] = rangeOf (EqRole::air, ctx);
            juce::ignoreUnused (cutT, cutA);
            const bool airAtLimit = air != nullptr && air->band.gain >= liftA - 0.25f;
            const float g0 = tone != nullptr && tone->band.on ? tone->band.gain : 0.0f;
            if (airAtLimit && g0 >= 0.0f && g0 < liftT - 0.25f && (tone != nullptr || budget > 0))
            {
                const auto applied = modelBands (EqRole::autoTone);
                const EqBand* keep = tone != nullptr && tone->band.on ? &tone->band : nullptr;
                const auto make = [&] (float g) { return shapeOf (EqRole::autoTone, g, ctx, applied, keep); };
                const float now = topOf (levelsOf (p.spectrumDb, [&] { auto b = applied; if (keep != nullptr) b.push_back (*keep); return b; }()));
                const float want = (topWindow.lo + 1.0f) - rendered.top;
                const float g = roundHalf (solve (p.spectrumDb, applied, make, topOf, now + 0.8f * want, g0, liftT));
                if (g - g0 >= 0.5f)
                {
                    Move m { EqRole::autoTone, make (g), {}, {}, false };
                    m.title = titleOf (EqRole::autoTone, m.band);
                    m.reason = de ("Nach der Messung der ganzen Kette noch dunkel (über 2 kHz: ") + db (rendered.top)
                             + de (") - eine breite Anhebung bringt die Stimme nach vorne.");
                    out.push_back (m);
                }
            }
        }
        return out;
    }
}
