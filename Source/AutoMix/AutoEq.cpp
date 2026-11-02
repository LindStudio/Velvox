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
        const bool high = t == EqBandType::highShelf || b.freq > 5000.0f;
        return b.gain >= -maxBroadCutDb - eps && b.gain <= (high ? maxHighLiftDb : maxBroadBoostDb) + eps;
    }

    juce::String regionName (float f, bool cut)
    {
        if (cut)
        {
            if (f >= 150.0f && f < 450.0f)   return both ("Dröhnen", "Boomy");
            if (f >= 450.0f && f < 900.0f)   return both ("Pappig", "Boxy");
            if (f >= 900.0f && f < 1600.0f)  return both ("Nasal", "Nasal");
            if (f >= 2500.0f && f < 5000.0f) return both ("Härte", "Harshness");
        }
        else
        {
            if (f < 300.0f)                   return both ("Körper", "Body");
            if (f >= 1600.0f && f < 5000.0f)  return both ("Verständlichkeit", "Clarity");
            if (f >= 7000.0f)                 return both ("Brillanz", "Brilliance");
        }
        return f < 300.0f ? both ("Tiefen", "Lows") : (f < 5000.0f ? both ("Mitten", "Mids") : both ("Höhen", "Highs"));
    }

    juce::String titleOf (EqRole role, const EqBand& b)
    {
        const float g = b.gain;
        const bool notchRole = role == EqRole::autoNotch1 || role == EqRole::autoNotch2 || role == EqRole::autoNotch3;
        if (notchRole && b.q >= minNarrowQ - 1.0e-3f)
            return both ("Resonanz {0} {1} (schmal, Q {2})", "Resonance {0} {1} (narrow, Q {2})", { hz (b.freq), db (g), juce::String (juce::roundToInt (b.q)) });
        const auto t = b.bandType();
        // a shelf acts beyond its corner: named after the range it moves
        const auto region = regionName (t == EqBandType::lowShelf ? 0.5f * b.freq : (t == EqBandType::highShelf ? 1.5f * b.freq : b.freq), g < 0.0f);
        if (t == EqBandType::lowShelf || t == EqBandType::highShelf)
            return both ("{0} {1} (Shelf {2})", "{0} {1} (shelf {2})", { region, db (g), hz (b.freq) });
        return both ("{0} {1} bei {2}", "{0} {1} at {2}", { region, db (g), hz (b.freq) });
    }


    std::pair<int, int> fitRange (const VocalProfile& p)
    {
        // From 160 Hz (below lives the low cut) - and above the lowest sung notes - to 12.9 kHz
        const float lowest = std::max (160.0f, p.pitchValid ? 1.3f * p.f0LowHz : 160.0f);
        int lo = 0, hi = numThirds - 1;
        while (lo < numThirds - 1 && thirdCentre (lo) < lowest * 0.99f)
            ++lo;
        while (hi > 0 && thirdCentre (hi) > 13000.0f)
            --hi;
        return { lo, hi };
    }

    std::array<float, numThirds> unmaskOf (const VocalProfile& p, bool conservative)
    {
        // Where the beat covers the voice (sidechain, at the output level), 1 - 5 kHz: the target rises there
        std::array<float, numThirds> u {};
        if (! maskingOf (p).valid)
            return u;
        const float shift = leadOverBeatLu - p.vocalOverBeatLu;
        for (int k = 0; k < numThirds; ++k)
        {
            const float c = thirdCentre (k);
            if (c < 1000.0f * 0.99f || c > 5000.0f * 1.01f)
                continue;
            float sum = 0.0f;
            int n = 0;
            for (int b = 12 + 4 * k - 2; b <= 12 + 4 * k + 1; ++b)
                if (b >= 0 && b < (int) p.beatOverVocalDb.size())
                {
                    sum += p.beatOverVocalDb[(size_t) b];
                    ++n;
                }
            u[(size_t) k] = n > 0 ? unmaskGainDb (sum / (float) n - shift, conservative) : 0.0f;
        }
        return u;
    }

    namespace
    {
        const MixTarget& targetOf (const Context& ctx)
        {
            static const MixTarget party = targetFor (Genre::party, Vibe::punchy);
            return ctx.target != nullptr ? *ctx.target : party;
        }

        // `voice` + `offset` against the target; > 0 = too much, 0 = on target (also outside [lo, hi])
        std::vector<float> residualOf (const std::vector<float>& voice, const std::vector<float>& offset, const Context& ctx, int lo, int hi)
        {
            const auto& t = targetOf (ctx);
            std::vector<float> r ((size_t) numThirds, 0.0f);
            for (int k = lo; k <= hi; ++k)
                if (voice[(size_t) k] > -190.0f)
                    // the beat's lift applies in full, like the vibe (inside the spread it would vanish) - not on a harsh voice
                    r[(size_t) k] = residualDb (t, k, voice[(size_t) k] + offset[(size_t) k]) - (ctx.harsh ? 0.0f : ctx.unmaskDb[(size_t) k]);
            return r;
        }

        float costOf (const std::vector<float>& r)
        {
            float c = 0.0f;
            for (auto v : r)
                c += v * v;
            return c;
        }

        constexpr float costPerDb = 0.3f;   // a move has to pay for its size

        // Where a band acts: a shelf beyond its corner (like regionName in titleOf)
        float actingHz (const EqBand& b)
        {
            const auto t = b.bandType();
            return t == EqBandType::lowShelf ? 0.5f * b.freq : (t == EqBandType::highShelf ? 1.5f * b.freq : b.freq);
        }

        // How far a broad move at `centre` may go (spec: cuts down to -6 dB, -3 dB below 300 Hz; lifts up to
        // +4 dB, 2 dB above 5 kHz on a noisy / sibilant take, none on a resonance, none on the low end of a
        // high voice, none of 2-5 kHz on a harsh take)
        std::pair<float, float> limitsOf (const Context& ctx, float centre, EqBandType type, const std::vector<Move>& moves)
        {
            const auto& p = *ctx.profile;
            const float scale = ctx.conservative ? 0.6f : 1.0f;
            const bool highVoice = p.pitchValid && p.f0LowHz > 250.0f;
            const float cut = (centre < 300.0f ? 3.0f : maxBroadCutDb) * scale;
            float lift = maxBroadBoostDb;
            if (type == EqBandType::highShelf || centre > 5000.0f)
                lift = ctx.noisy ? maxHighLiftNoisyDb : (ctx.sibilant ? maxHighLiftSibilantDb : maxHighLiftDb);
            bool resonance = false;
            for (auto& r : p.resonances)
            {
                // a resonance the plan already notches does not stand in the way of a broad lift around it
                const bool notched = std::any_of (moves.begin(), moves.end(), [&r] (const Move& m)
                                                  { return m.narrow && std::abs (std::log2 (m.band.freq / r.hz)) < 1.0f / 12.0f; });
                resonance = resonance || (! notched && r.excessDb > 7.0f && std::abs (std::log2 (centre / r.hz)) < 1.0f / 3.0f);
            }
            if ((highVoice && centre < 300.0f) || resonance || (ctx.harsh && centre >= 2000.0f && centre <= 5000.0f))
                lift = 0.0f;
            return { -cut, lift * scale };
        }

        // A lift above 5 kHz on a noisy / sibilant take: never more than 2 / 4 dB at 8 or 12 kHz in all
        bool liftTooHigh (const Context& ctx, const std::vector<EqBand>& before, const std::vector<EqBand>& after)
        {
            if (! (ctx.noisy || ctx.sibilant))
                return false;
            const float cap = (ctx.noisy ? maxHighLiftNoisyDb : maxHighLiftSibilantDb) + 1.0e-3f;
            return responseDb (after, 8000.0f) - responseDb (before, 8000.0f) > cap
                || responseDb (after, 12000.0f) - responseDb (before, 12000.0f) > cap;
        }

        // Greedy fit of the broad moves (spec: "EQ: Kurvenabgleich"): the largest connected stretch of one
        // sign outside the target gets a bell (Q from its width) or a shelf at an end of the fit range, its
        // gain on the model (~75 %, the closed loop finishes the job), until `budget` moves exist.
        // `offset`: what the chain does on top of the EQ (closed loop; 0 when planning).
        void fitBroad (const Context& ctx, std::vector<Move>& moves, const std::vector<float>& offset, int budget, bool afterRender)
        {
            const auto& p = *ctx.profile;
            const auto& spec = p.spectrumDb;
            const auto& target = targetOf (ctx);
            const auto [lo, hi] = fitRange (p);
            std::vector<bool> blocked ((size_t) numThirds, false);
            // stretches a move already covers are not treated twice
            for (auto& m : moves)
                if (! m.narrow)
                    for (int k = lo; k <= hi; ++k)
                        if (std::abs (std::log2 (thirdCentre (k) / m.band.freq)) <= 0.5f)
                            blocked[(size_t) k] = true;
            const auto bandsOf = [&ctx, &moves]
            {
                auto bands = ctx.fixed;
                for (auto& m : moves)
                    bands.push_back (m.band);
                return bands;
            };
            const auto roleFree = [&moves] (EqRole role)
            {
                return std::none_of (moves.begin(), moves.end(), [role] (const Move& m) { return m.role == role; });
            };
            const auto roleFor = [&] (float centre) -> EqRole
            {
                // The notch roles stay with the resonances; WARMTH (below 700 Hz) and the PRESENCE slider's air
                // band (above 4 kHz) only take moves of their own range, so the SIMPLE sliders keep their meaning
                std::vector<EqRole> order;
                if (centre < 300.0f)
                    order.push_back (EqRole::warmth);
                else if (centre > 7000.0f)
                    order.push_back (EqRole::air);
                for (auto r : { EqRole::autoMix, EqRole::autoTone })
                    order.push_back (r);
                if (centre < 700.0f)
                    order.push_back (EqRole::warmth);
                if (centre > 4000.0f)
                    order.push_back (EqRole::air);
                for (auto r : order)
                    if (roleFree (r))
                        return r;
                return EqRole::none;
            };

            while ((int) moves.size() < budget)
            {
                const auto applied = bandsOf();
                const auto r = residualOf (thirdsOf (spec, applied), offset, ctx, lo, hi);

                int bestA = -1, bestB = -1;
                float bestScore = 0.0f;
                for (int k = lo; k <= hi;)
                {
                    if (r[(size_t) k] == 0.0f || blocked[(size_t) k])
                    {
                        ++k;
                        continue;
                    }
                    const bool up = r[(size_t) k] > 0.0f;
                    int e = k;
                    float score = 0.0f, peak = 0.0f;
                    while (e <= hi && r[(size_t) e] != 0.0f && (r[(size_t) e] > 0.0f) == up && ! blocked[(size_t) e])
                    {
                        score += std::abs (r[(size_t) e]);
                        peak = std::max (peak, std::abs (r[(size_t) e]));
                        ++e;
                    }
                    if (peak >= 1.0f && score > bestScore)
                    {
                        bestScore = score;
                        bestA = k;
                        bestB = e - 1;
                    }
                    k = e;
                }
                if (bestA < 0)
                    break;

                const auto blockRun = [&] { for (int k = bestA; k <= bestB; ++k) blocked[(size_t) k] = true; };
                const bool cut = r[(size_t) bestA] > 0.0f;
                double wsum = 0.0, lsum = 0.0;
                for (int k = bestA; k <= bestB; ++k)
                {
                    wsum += std::abs (r[(size_t) k]);
                    lsum += std::abs (r[(size_t) k]) * std::log2 (thirdCentre (k));
                }
                const float centre = (float) std::pow (2.0, lsum / wsum);
                const float width = std::max (1.0f / 3.0f, (float) (bestB - bestA + 1) / 3.0f);

                auto type = EqBandType::bell;
                float freq = centre, q = juce::jlimit (0.7f, 4.0f, 1.4f / width);
                if (bestA == lo && width >= 1.0f && centre < 250.0f)
                {
                    type = EqBandType::lowShelf;
                    freq = thirdCentre (bestB);
                    q = 0.7071f;
                }
                else if (bestB == hi && width >= 1.0f && thirdCentre (bestA) >= 5000.0f * 0.99f)
                {
                    type = EqBandType::highShelf;
                    freq = thirdCentre (bestA);
                    q = 0.7071f;
                }

                // A lift needs a stretch of at least two thirds and stays broad (Q <= 2): a lift of one
                // third chases a dip between formants and sounds random
                if (! cut)
                    q = std::min (q, 2.0f);
                const auto [gLo, gHi] = limitsOf (ctx, centre, type, moves);
                const auto role = roleFor (centre);
                if ((cut ? gLo : gHi) == 0.0f || role == EqRole::none || (! cut && bestB == bestA))
                {
                    blockRun();
                    continue;
                }

                const float c0 = costOf (r);
                float best = 0.0f, bestCost = c0;
                for (float g = cut ? -0.5f : 0.5f; cut ? g >= gLo - 1.0e-4f : g <= gHi + 1.0e-4f; g += cut ? -0.5f : 0.5f)
                {
                    auto bands = applied;
                    bands.push_back (bandOf (type, freq, g, q));
                    if (! cut && liftTooHigh (ctx, applied, bands))
                        break;
                    const float c = costOf (residualOf (thirdsOf (spec, bands), offset, ctx, lo, hi)) + costPerDb * std::abs (g);
                    if (c < bestCost - 1.0e-4f)
                    {
                        best = g;
                        bestCost = c;
                    }
                }
                best = roundHalf (0.75f * best);
                if (std::abs (best) < minMoveDb || bestCost > c0 - 0.5f)
                {
                    blockRun();
                    continue;
                }

                const auto band = bandOf (type, freq, best, q);
                const float excess = (float) (wsum / (bestB - bestA + 1)) * (cut ? 1.0f : -1.0f);
                const auto why = both ("{0}: {1} gegenüber dem Ziel für {2} · {3} (fertige Profi-Vocals) - hier und nur hier.",
                                       "{0}: {1} against the target for {2} · {3} (finished professional vocals) - here and only here.",
                                       { regionName (centre, cut), db (excess), genreName (target.genre), vibeName (target.vibe) })
                               + (! cut && (ctx.noisy || ctx.sibilant) && (type == EqBandType::highShelf || centre > 5000.0f)
                                      ? both (" Anhebung begrenzt (Rauschen/S-Laute).", " Lift limited (noise/sibilance).") : juce::String());
                moves.push_back ({ role, band, titleOf (role, band),
                                   afterRender ? both ("Nach der Messung der ganzen Kette: {0}", "After measuring the whole chain: {0}", { why }) : why,
                                   false });
                blockRun();
            }
        }
    }

    std::vector<Move> planCurveMoves (const Context& ctx)
    {
        std::vector<Move> moves;
        if (ctx.profile == nullptr || ctx.budget <= 0)
            return moves;
        const auto& p = *ctx.profile;
        if (! p.enoughMaterial || p.spectrumDb.size() != (size_t) VocalProfiler::numBands)
            return moves;

        const float scale = ctx.conservative ? 0.6f : 1.0f;
        const auto bandsWithout = [&] (EqRole skip)
        {
            auto bands = ctx.fixed;
            for (auto& m : moves)
                if (m.role != skip)
                    bands.push_back (m.band);
            return bands;
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

        fitBroad (ctx, moves, std::vector<float> ((size_t) numThirds, 0.0f), ctx.budget, false);
        return moves;
    }

    std::vector<Move> correctCurve (const std::vector<float>& rendered, const Context& ctx, const std::vector<Move>& owned)
    {
        std::vector<Move> out;
        if (ctx.profile == nullptr || ! ctx.profile->enoughMaterial || ctx.profile->spectrumDb.size() != (size_t) VocalProfiler::numBands
            || rendered.size() != (size_t) numThirds)
            return out;
        const auto& p = *ctx.profile;
        const auto& spec = p.spectrumDb;
        const auto [lo, hi] = fitRange (p);

        // What the chain does on top of the EQ: the render against the model of the EQ as it is now
        auto bands = ctx.fixed;
        for (auto& m : owned)
            bands.push_back (m.band);
        const auto model = thirdsOf (spec, bands);
        std::vector<float> offset ((size_t) numThirds, 0.0f);
        for (int k = 0; k < numThirds; ++k)
            if (rendered[(size_t) k] > -190.0f && model[(size_t) k] > -190.0f)
                offset[(size_t) k] = rendered[(size_t) k] - model[(size_t) k];

        const auto r0 = residualOf (model, offset, ctx, lo, hi);
        if (std::all_of (r0.begin(), r0.end(), [] (float v) { return std::abs (v) <= 0.5f; }))
            return out;

        // 1) The broad moves of the plan: gains re-tuned on the render (frequency and shape stay)
        auto state = owned;
        for (size_t i = 0; i < state.size(); ++i)
        {
            if (state[i].narrow)
                continue;
            auto others = ctx.fixed;
            for (size_t j = 0; j < state.size(); ++j)
                if (j != i)
                    others.push_back (state[j].band);
            const auto& b0 = state[i].band;
            const auto [gLo, gHi] = limitsOf (ctx, actingHz (b0), b0.bandType(), state);
            const auto costAt = [&] (float g)
            {
                auto b = b0;
                b.gain = g;
                auto all = others;
                all.push_back (b);
                return costOf (residualOf (thirdsOf (spec, all), offset, ctx, lo, hi)) + costPerDb * std::abs (g);
            };
            float best = b0.gain, bestCost = costAt (b0.gain);
            for (float g = std::ceil (std::min (gLo, b0.gain) * 2.0f) / 2.0f; g <= std::max (gHi, b0.gain) + 1.0e-4f; g += 0.5f)
            {
                if (g < gLo - 1.0e-4f || g > gHi + 1.0e-4f)
                    continue;
                auto b = b0;
                b.gain = g;
                auto all = others;
                all.push_back (b);
                if (g > b0.gain && liftTooHigh (ctx, others, all))
                    continue;
                if (const float c = costAt (g); c < bestCost - 0.25f)
                {
                    best = g;
                    bestCost = c;
                }
            }
            if (std::abs (best - b0.gain) < 0.5f)
                continue;
            state[i].band.gain = std::abs (best) < 0.25f ? 0.0f : best;
            state[i].title = titleOf (state[i].role, state[i].band);
            state[i].reason = both ("Nach der Messung der ganzen Kette auf das Ziel nachgeregelt.", "Readjusted to the target after measuring the whole chain.");
            out.push_back (state[i]);
        }

        // 2) What is still off: new moves within the budget
        const size_t before = state.size();
        fitBroad (ctx, state, offset, (int) owned.size() + ctx.budget, true);
        for (size_t i = before; i < state.size(); ++i)
            out.push_back (state[i]);
        return out;
    }

    Masking maskingOf (const VocalProfile& p)
    {
        Masking m;
        const auto& d = p.beatOverVocalDb;
        if (! p.beatPresent || d.size() != (size_t) VocalProfiler::numBands)
            return m;
        m.valid = true;
        // At the output the voice moves by (lead - measured): the beat's lead over it shrinks by that
        const float shift = leadOverBeatLu - p.vocalOverBeatLu;
        const auto mean = [&d] (int from, int to)
        {
            double s = 0.0;
            for (int b = from; b <= to; ++b)
                s += d[(size_t) b];
            return (float) (s / (to - from + 1));
        };
        for (int b = 1; b + 1 < (int) d.size(); ++b)
        {
            const float c = VocalProfiler::bandCentre (b);
            if (c >= 1500.0f && c <= 6000.0f)
                if (const float v = mean (b - 1, b + 1) - shift; v > m.presenceDb)
                {
                    m.presenceDb = v;
                    m.hz = c;
                }
        }
        double s = 0.0;
        int n = 0;
        for (int b = 0; b < (int) d.size(); ++b)
            if (const float c = VocalProfiler::bandCentre (b); c >= 250.0f && c < 500.0f)
            {
                s += d[(size_t) b];
                ++n;
            }
        m.lowMidDb = n > 0 ? (float) (s / n) - shift : -100.0f;
        return m;
    }

    float unmaskGainDb (float excessDb, bool conservative) noexcept
    {
        if (excessDb <= unmaskFromDb)
            return 0.0f;
        const float g = juce::jlimit (0.5f, 2.5f, 0.3f * (excessDb - unmaskFromDb) * (conservative ? 0.6f : 1.0f));
        return std::max (0.5f, roundHalf (g));
    }

}
