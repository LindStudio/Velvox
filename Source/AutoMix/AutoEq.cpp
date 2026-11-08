#include "AutoEq.h"
#include "ReportText.h"
#include "Soft.h"

namespace vocalypse::automix::autoeq
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

    float noisyWeight (const VocalProfile& p) noexcept
    {
        return p.snrValid ? soft (p.snrDb, 28.0f, 24.0f) : 0.0f;
    }

    float sibilantWeight (const VocalProfile& p) noexcept
    {
        return p.sibilanceValid ? soft (p.sibilanceVsVowelDb, -5.0f, -2.0f) : 0.0f;
    }

    float harshWeight (const VocalProfile& p) noexcept
    {
        // the profiler flags "harsh" only where the take is not dull (above 2 kHz at -16 dB or more)
        return soft (p.presenceDb, -2.0f, 1.0f) * soft (p.topDb, -16.0f, -14.0f);
    }

    float dullWeight (const VocalProfile& p) noexcept
    {
        return soft (p.topDb, -16.0f, -20.0f);
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

    std::array<float, numThirds> unmaskOf (const VocalProfile& p, float caution)
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
            u[(size_t) k] = n > 0 ? unmaskGainDb (sum / (float) n - shift, caution) : 0.0f;
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
                    r[(size_t) k] = residualDb (t, k, voice[(size_t) k] + offset[(size_t) k]) - (1.0f - ctx.harsh) * ctx.unmaskDb[(size_t) k];
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
            const float scale = 1.0f - 0.4f * cautionOf (ctx);
            // Every restriction is a weight 0 .. 1 of the measurement behind it (Task 7): the limit shrinks
            // continuously instead of dropping to 0 at one number
            const float highVoiceW = p.pitchValid ? soft (p.f0LowHz, 250.0f, 280.0f) * soft (centre, 300.0f, 260.0f) : 0.0f;
            const float cut = blend (soft (centre, 250.0f, 350.0f), 3.0f, maxBroadCutDb) * scale;
            float lift = maxBroadBoostDb;
            if (type == EqBandType::highShelf || centre > 5000.0f)
                lift = blend (ctx.noisy, blend (ctx.sibilant, maxHighLiftDb, maxHighLiftSibilantDb), maxHighLiftNoisyDb);
            float resonanceW = 0.0f;
            for (auto& r : p.resonances)
            {
                // a resonance the plan already notches does not stand in the way of a broad lift around it
                const bool notched = std::any_of (moves.begin(), moves.end(), [&r] (const Move& m)
                                                  { return m.narrow && std::abs (std::log2 (m.band.freq / r.hz)) < 1.0f / 12.0f; });
                if (! notched)
                    resonanceW = juce::jmax (resonanceW, soft (r.excessDb, 7.0f, 10.0f) * soft (std::abs (std::log2 (centre / r.hz)), 1.0f / 3.0f, 0.25f));
            }
            const float harshW = ctx.harsh * soft (centre, 1800.0f, 2000.0f) * soft (centre, 5400.0f, 5000.0f);
            const float noLift = juce::jmax (highVoiceW, resonanceW, harshW);
            // The vibe's deliberate shift comes on top of the clean-up limit (2026-11-03: with one shared 4 dB
            // the clean-up used it up and AGGRESSIV / FLOATY reached only a third of their shift)
            if (lift > 0.0f)
            {
                // the largest shift within half an octave: a broad band reaches its neighbours (a presence bell at
                // 4.2 kHz carries Aggressive's 2.5 - 4 kHz lift)
                float style = 0.0f;
                for (int k = 0; k < numThirds; ++k)
                    if (std::abs (std::log2 (thirdCentre (k) / centre)) <= 0.5f)
                        style = juce::jmax (style, targetOf (ctx).styleDb[(size_t) k]);
                lift += style * (1.0f - ctx.noisy);
            }
            return { -cut, lift * (1.0f - noLift) * scale };
        }

        // A lift above 5 kHz on a noisy / sibilant take: never more than 2 / 4 dB at 8 or 12 kHz in all
        bool liftTooHigh (const Context& ctx, const std::vector<EqBand>& before, const std::vector<EqBand>& after)
        {
            // the cap closes in continuously with the weights (12 dB = out of reach of every limit)
            const float cap = blend (ctx.noisy, blend (ctx.sibilant, 12.0f, maxHighLiftSibilantDb), maxHighLiftNoisyDb) + 1.0e-3f;
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
            const auto roleFor = [&] (float centre, EqBandType shape) -> EqRole
            {
                // The notch roles stay with the resonances; WARMTH (below 700 Hz) and the PRESENCE slider's air
                // band (above 4 kHz) only take moves of their own range, so the SIMPLE sliders keep their meaning
                std::vector<EqRole> order;
                if (shape == EqBandType::lowShelf || (shape != EqBandType::highShelf && centre < 300.0f))   // a shelf keeps to its role (Task 7: no flip at a centre frequency)
                    order.push_back (EqRole::warmth);
                else if (shape == EqBandType::highShelf || centre > 7000.0f)
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
                float bestPeak = 0.0f;
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
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestPeak = peak;
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
                    freq = std::max (thirdCentre (lo), 1.4f * centre);   // the corner follows the weight of the stretch (continuous), not its first third
                    q = 0.7071f;
                }
                else if (bestB == hi && width >= 1.0f && thirdCentre (bestA) >= 5000.0f * 0.99f)
                {
                    type = EqBandType::highShelf;
                    freq = std::min (thirdCentre (hi), std::max (5000.0f, centre / 1.4f));
                    q = 0.7071f;
                }

                // A lift needs a stretch of at least two thirds and stays broad (Q <= 2): a lift of one
                // third chases a dip between formants and sounds random
                if (! cut)
                    q = std::min (q, 2.0f);
                const auto [gLo, gHi] = limitsOf (ctx, centre, type, moves);
                const auto role = roleFor (centre, type);
                if ((cut ? -gLo : gHi) < 0.5f || role == EqRole::none)
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
                // The gain is the deviation's own gain times the weight of every edge a hard "is it worth a band"
                // test used to be (Task 7): the size of the move (nothing below 0.5 dB, full from 1.5 dB), what the
                // move gains on the cost (0.25 .. 1), and, for a lift of a single third (a dip between formants),
                // how deep that dip is. So a move grows from 0 instead of jumping to +-1 dB at the edge.
                const float weight = soft (std::abs (best), minMoveDb - 0.5f, minMoveDb + 0.5f)
                                   * soft (c0 - bestCost, 0.25f, 1.0f)
                                   * (! cut && bestB == bestA ? soft (bestPeak, 2.0f, 4.0f) : 1.0f);
                best = roundHalf (0.75f * best * weight);
                if (std::abs (best) < 0.5f)
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

        const float scale = 1.0f - 0.4f * cautionOf (ctx);
        const auto bandsWithout = [&] (EqRole skip)
        {
            auto bands = ctx.fixed;
            for (auto& m : moves)
                if (m.role != skip)
                    bands.push_back (m.band);
            return bands;
        };

        // ---- Narrow notches: persistent resonances, unless a kept band already takes them out (Task 7: continuous)
        // A notch's depth is the resonance's own excess times three weights: how clearly it stands out
        // (`soft (excess, 5, 9)`: nothing below 5 dB, full from 9 dB), how persistent it is (the plan: >= 0.5 of
        // the take; 0.4 .. 0.6) and the range 250 Hz .. 8 kHz (soft edges). The strongest three (excess x
        // persistence) get a notch; a later resonance replaces an earlier one in the ranking only when it beats
        // it by more than 1 dB, so the slots (notch 1 .. 3) do not swap on a hair.
        {
            struct Candidate { const Resonance* r; float score; };
            std::vector<Candidate> ranked;
            for (auto& r : p.resonances)
                if (r.persistence > 0.0f && r.excessDb > 0.0f)
                    ranked.push_back ({ &r, r.excessDb * r.persistence });
            for (size_t i = 1; i < ranked.size(); ++i)   // insertion sort with a margin (the profile's order is the favourite)
                for (size_t j = i; j > 0 && ranked[j].score > ranked[j - 1].score + 1.0f; --j)
                    std::swap (ranked[j], ranked[j - 1]);

            int n = 0;
            for (auto& c : ranked)
            {
                const auto& r = *c.r;
                if (n >= maxNotches || (int) moves.size() >= ctx.budget)
                    break;
                // What is left of the peak over its surroundings after the kept bands
                const auto applied = bandsWithout (EqRole::none);
                const float third = std::pow (2.0f, 1.0f / 3.0f);
                const float remaining = r.excessDb + responseDb (applied, r.hz)
                                      - 0.5f * (responseDb (applied, r.hz * third) + responseDb (applied, r.hz / third));
                const float weight = soft (remaining, 5.0f, 9.0f) * soft (r.persistence, 0.4f, 0.6f)
                                   * soft (r.hz, 220.0f, 280.0f) * soft (r.hz, 8500.0f, 7500.0f);
                const float depth = roundHalf (juce::jmin (maxNarrowCutDb, 0.6f * remaining * weight) * scale);
                if (depth < 0.5f)
                    continue;
                const float q = juce::jlimit (minNarrowQ, maxNarrowQ, r.q);
                const auto role = notchRoles[n++];
                const auto b = bandOf (EqBandType::bell, r.hz, -depth, q);
                moves.push_back ({ role, b, titleOf (role, b),
                                   both ("Schmale Spitze {0} über ihrer Umgebung, in der ganzen Aufnahme vorhanden (Raum oder Mikrofon) - ein schmaler Schnitt nimmt nur sie heraus.",
                                         "Narrow peak {0} above its surroundings, present in the whole recording (room or microphone) - a narrow cut takes out only that.",
                                         { db (remaining) }), true });
            }
            // Roles (and so slots 5-7) go by frequency, not by rank: a notch keeps its role while a neighbour appears or goes
            std::sort (moves.begin(), moves.end(), [] (const Move& a, const Move& b) { return a.band.freq < b.band.freq; });
            for (size_t i = 0; i < moves.size(); ++i)
            {
                moves[i].role = notchRoles[i];
                moves[i].title = titleOf (moves[i].role, moves[i].band);
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
            const float cost0 = costAt (b0.gain);
            float best = b0.gain, bestCost = cost0;
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
                if (const float c = costAt (g); c < bestCost - 1.0e-4f)
                {
                    best = g;
                    bestCost = c;
                }
            }
            // The re-tune goes only as far as it pays (Task 7): the step towards the best gain is weighted with
            // what the render's cost gains (0.25 .. 1.0), so it grows from 0 instead of jumping at one edge
            const float step = (best - b0.gain) * soft (cost0 - bestCost, 0.25f, 1.0f);
            if (std::abs (step) < 0.25f)
                continue;
            const float gain = roundHalf (b0.gain + step);
            if (gain == b0.gain)
                continue;
            state[i].band.gain = std::abs (gain) < 0.25f ? 0.0f : gain;
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

    float cautionOf (const Context& ctx) noexcept
    {
        return ctx.caution >= 0.0f ? juce::jlimit (0.0f, 1.0f, ctx.caution) : (ctx.conservative ? 1.0f : 0.0f);
    }

    float unmaskGainDb (float excessDb, float caution) noexcept
    {
        if (excessDb <= unmaskFromDb)
            return 0.0f;
        const float g = juce::jlimit (0.5f, 2.5f, 0.3f * (excessDb - unmaskFromDb) * (1.0f - 0.4f * juce::jlimit (0.0f, 1.0f, caution)));
        return std::max (0.5f, roundHalf (g));
    }

}
