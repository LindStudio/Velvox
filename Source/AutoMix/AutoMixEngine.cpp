#include "AutoMixEngine.h"
#include "../Parameters/SettingsReader.h"
#include "../Parameters/ParameterLayout.h"
#include "ReportText.h"

namespace vocalypse::automix
{
    namespace
    {
        // Candidate names, titles, notes and adjustments end up in the saved report: both languages
        using txt::both;
        using txt::db;
        using txt::num;

        // Keeps the overview true to an EQ role band the verification changed: the decision that
        // shows `before` gets the new title, a new band gets a decision, a flattened one loses it
        void updateEqDecision (MixPlan& plan, dsp::EqRole role, const dsp::EqBand& before, const dsp::EqBand& after, const juce::String& reason)
        {
            const bool wasAudible = before.on && std::abs (before.gain) > 1.0e-3f;
            const auto oldTitle = autoeq::titleOf (role, before);
            auto it = std::find_if (plan.decisions.begin(), plan.decisions.end(), [&] (const Decision& d)
                                    { return ! d.outputStage && d.module == dsp::ModuleId::eq && wasAudible && d.title == oldTitle; });
            const bool audible = after.on && std::abs (after.gain) > 1.0e-3f;
            if (! audible)
            {
                if (it != plan.decisions.end())
                    plan.decisions.erase (it);
                return;
            }
            if (it != plan.decisions.end())
            {
                it->title = autoeq::titleOf (role, after);
                it->reason = reason;
                it->measured = true;
                return;
            }
            // New EQ decisions go after the other EQ steps (the overview groups by module order)
            auto pos = plan.decisions.begin();
            for (auto i = plan.decisions.begin(); i != plan.decisions.end(); ++i)
                if (! i->outputStage && i->module == dsp::ModuleId::eq)
                    pos = i + 1;
            plan.decisions.insert (pos, { dsp::ModuleId::eq, false, autoeq::titleOf (role, after), reason, true });
        }

        float percentile (std::vector<float> v, float p)
        {
            if (v.empty())
                return 0.0f;
            const auto k = (size_t) juce::jlimit (0, (int) v.size() - 1, (int) std::round (p * (float) (v.size() - 1)));
            std::nth_element (v.begin(), v.begin() + (long) k, v.end());
            return v[k];
        }

        std::vector<const float*> pointers (const CapturedAudio& a)
        {
            std::vector<const float*> p;
            for (auto& c : a.vocal)
                p.push_back (c.data());
            return p;
        }

        // Fills spectral / level metrics from the audio itself
        // `scaleDb`: `a` is the capture scaled by this gain - loudness, peak and clipping are reported as captured
        void measureInto (RenderMetrics& m, const CapturedAudio& a, float scaleDb = 0.0f)
        {
            const int n = a.numSamples();
            if (n <= 0)
                return;
            m.valid = true;
            const float lufsOfA = VocalProfiler::integratedLufs (pointers (a), n, a.sampleRate);
            m.lufs = lufsOfA - scaleDb;

            float peak = 0.0f;
            for (auto& c : a.vocal)
                for (float x : c)
                    peak = std::max (peak, std::abs (x));
            m.peakDb = juce::Decibels::gainToDecibels (peak, -100.0f) - scaleDb;

            const auto prof = VocalProfiler::analyse (a, 0.0f, {}, scaleDb);
            m.topDb = prof.topDb;
            m.presenceDb = prof.presenceDb;
            m.bodyDb = prof.bodyDb;
            m.lowMidDb = prof.lowMidDb;
            m.airDb = prof.airDb;
            m.thirdsDb = prof.thirdsDb;
            m.dynamicsDb = prof.dynamicsDb;
            m.dynamicsValid = prof.dynamicsValid;
            m.crestDb = prof.crestDb;
            m.clipRuns = prof.clipRuns;

            // Effects in the gaps: 20 ms frames of the mid signal; words = loud frames, gaps =
            // quiet frames within 400 ms after a word. Median gap level relative to the words.
            {
                const int frame = std::max (1, (int) (0.02 * a.sampleRate));
                std::vector<float> levels;
                for (int start = 0; start + frame <= n; start += frame)
                {
                    double sum = 0.0;
                    for (int i = start; i < start + frame; ++i)
                    {
                        float x = 0.0f;
                        for (auto& c : a.vocal)
                            x += c[(size_t) i];
                        x /= (float) a.vocal.size();
                        sum += (double) x * x;
                    }
                    levels.push_back ((float) (10.0 * std::log10 (std::max (1.0e-12, sum / frame))));   // floor -120 dB
                }
                if (levels.size() > 50)
                {
                    const float loud = percentile (levels, 0.95f);
                    std::vector<float> words, gaps;
                    int sinceWord = 1000;
                    for (float l : levels)
                    {
                        if (l > loud - 20.0f)
                        {
                            words.push_back (l);
                            sinceWord = 0;
                        }
                        else if (++sinceWord <= 20 && l < loud - 30.0f)
                            gaps.push_back (l);
                    }
                    if (words.size() > 20 && gaps.size() > 20)
                    {
                        m.gapDb = percentile (gaps, 0.5f) - percentile (words, 0.5f);
                        m.gapValid = true;
                    }
                }
            }

            if (a.channels > 1)
            {
                double ll = 0, rr = 0, lr = 0;
                const float* L = a.vocal[0].data();
                const float* R = a.vocal[1].data();
                for (int i = 0; i < n; ++i) { ll += (double) L[i] * L[i]; rr += (double) R[i] * R[i]; lr += (double) L[i] * R[i]; }
                m.correlation = ll > 0.0 && rr > 0.0 ? (float) (lr / std::sqrt (ll * rr)) : 1.0f;

                std::vector<float> monoSum ((size_t) n);
                for (int i = 0; i < n; ++i)
                    monoSum[(size_t) i] = 0.5f * (L[i] + R[i]);
                const std::vector<const float*> mono { monoSum.data(), monoSum.data() };
                m.monoLossDb = VocalProfiler::integratedLufs (mono, n, a.sampleRate) - lufsOfA;
            }
        }
    }

    RenderMetrics AutoMixEngine::measure (const CapturedAudio& audio, float scaleDb)
    {
        RenderMetrics m;
        measureInto (m, audio, scaleDb);
        return m;
    }

    float AutoMixEngine::workingGainDb (const CapturedAudio& audio, float* wantedDb)
    {
        if (wantedDb != nullptr)
            *wantedDb = 0.0f;
        const int n = audio.numSamples();
        if (n <= 0 || audio.channels < 1 || audio.sampleRate <= 0.0)
            return 0.0f;

        // 50 ms blocks of the mid signal (like the profiler's level distribution)
        const int block = std::max (64, (int) std::lround (0.05 * audio.sampleRate));
        const auto& L = audio.vocal[0];
        const auto& R = audio.channels > 1 ? audio.vocal[1] : audio.vocal[0];
        std::vector<float> blockDb;
        for (int start = 0; start + block <= n; start += block)
        {
            double s = 0.0;
            for (int i = start; i < start + block; ++i)
            {
                const double mid = 0.5 * ((double) L[(size_t) i] + (double) R[(size_t) i]);
                s += mid * mid;
            }
            blockDb.push_back ((float) (10.0 * std::log10 (std::max (1.0e-20, s / block))));
        }
        if (blockDb.empty())
            return 0.0f;

        // Voiced: within 35 dB of the loud blocks (relative only - an absolute floor would make the set of voiced
        // blocks, and so the median, depend on the recording level), and not digital silence
        const float gate = std::max (-90.0f, percentile (blockDb, 0.95f) - 35.0f);
        std::vector<float> voiced;
        for (float b : blockDb)
            if (b > gate)
                voiced.push_back (b);
        if (voiced.empty())
            return 0.0f;

        const float wanted = workingLevelDb - percentile (voiced, 0.5f);
        if (wantedDb != nullptr)
            *wantedDb = wanted;
        return juce::jlimit (-24.0f, 24.0f, wanted);
    }

    RenderMetrics AutoMixEngine::renderChecked (const CapturedAudio& audio, const std::map<juce::String, float>& current,
                                                const MixPlan& plan, const VocalProfile& profile)
    {
        CapturedAudio out;
        auto m = render (audio, current, plan.values, plan.routing, &out);
        if (auto s = VocalProfiler::sibilanceAt (out, profile))
        {
            m.sibilanceVsVowelDb = *s;
            m.sibilanceValid = true;
        }
        return m;
    }

    RenderMetrics AutoMixEngine::render (const CapturedAudio& audio, const std::map<juce::String, float>& current,
                                         const std::map<juce::String, float>& values, const dsp::Routing& routing,
                                         CapturedAudio* output)
    {
        RenderMetrics m;
        const int n = audio.numSamples();
        const int channels = juce::jlimit (1, 2, audio.channels);
        if (n <= 0)
            return m;

        // Defaults, the session parameters from `current`, then everything the plan sets
        OfflineParameters params;
        params.setAll (startValues (current));
        params.setAll (values);
        auto s = params.read (routing);
        s.safetyClip = false;   // measure real overs; the plugin keeps its clip guard
        s.bypass = false;
        s.levelMatch = dsp::ChainSettings::LevelMatch::off;
        s.monoMonitor = false;
        s.hostBpm = 0.0;        // the offline render knows no tempo: synced delay uses its fallback time
        s.hostPlaying = false;
        s.duckExternal = false;

        auto chain = std::make_unique<dsp::VocalChain>();
        constexpr int block = 512;
        chain->prepare (audio.sampleRate, block);

        CapturedAudio out;
        out.sampleRate = audio.sampleRate;
        out.channels = channels;
        out.vocal = audio.vocal;
        out.vocal.resize ((size_t) channels);

        // Voiced blocks (input level) for the gain-reduction statistics
        std::vector<float> blockDb;
        for (int start = 0; start < n; start += block)
        {
            const int len = std::min (block, n - start);
            double sum = 0.0;
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < len; ++i)
                    sum += (double) audio.vocal[(size_t) ch][(size_t) (start + i)] * audio.vocal[(size_t) ch][(size_t) (start + i)];
            // at the level the chain sees (after the input gain), so the -50 dB floor below does not depend on the recording level
            blockDb.push_back ((float) (10.0 * std::log10 (std::max (1.0e-20, sum / ((double) len * channels)))) + s.inputGainDb);
        }
        const float gate = std::max (-50.0f, percentile (blockDb, 0.95f) - 30.0f);

        std::vector<float> compGr;
        float deessMax = 0.0f;
        std::array<float*, 2> ptrs {};
        for (int start = 0, b = 0; start < n; start += block, ++b)
        {
            const int len = std::min (block, n - start);
            for (int ch = 0; ch < channels; ++ch)
                ptrs[(size_t) ch] = out.vocal[(size_t) ch].data() + start;
            juce::AudioBuffer<float> view (ptrs.data(), channels, len);
            chain->process (view, channels, s, nullptr);

            const float gr = chain->getCompressor().gainReductionDb.pull();
            deessMax = std::max (deessMax, chain->getDeEsser().gainReductionDb.pull());
            if (blockDb[(size_t) b] > gate)
                compGr.push_back (gr);
        }

        // Latency compensation (e.g. LEVEL RIDE's lookahead, SMOOTH's STFT): like a host, align the
        // output with the input - the analysis (S frames, levels) refers to input positions
        if (const int latency = chain->getLatencySamples(); latency > 0)
        {
            std::vector<std::vector<float>> tail ((size_t) channels, std::vector<float> ((size_t) latency, 0.0f));
            for (int start = 0; start < latency; start += block)
            {
                const int len = std::min (block, latency - start);
                for (int ch = 0; ch < channels; ++ch)
                    ptrs[(size_t) ch] = tail[(size_t) ch].data() + start;
                juce::AudioBuffer<float> view (ptrs.data(), channels, len);
                chain->process (view, channels, s, nullptr);
            }
            for (int ch = 0; ch < channels; ++ch)
            {
                auto& v = out.vocal[(size_t) ch];
                v.erase (v.begin(), v.begin() + std::min (latency, n));
                v.insert (v.end(), tail[(size_t) ch].begin(), tail[(size_t) ch].begin() + std::min (latency, n));
            }
        }

        measureInto (m, out);
        m.compGrMedianDb = percentile (compGr, 0.5f);
        m.compGrMaxDb = compGr.empty() ? 0.0f : *std::max_element (compGr.begin(), compGr.end());
        m.deessGrMaxDb = deessMax;

        if (output != nullptr)
            *output = std::move (out);
        return m;
    }

    //==========================================================================
    namespace
    {
        float outside (float v, float lo, float hi) { return v < lo ? lo - v : (v > hi ? v - hi : 0.0f); }

        // The voice without its reverb / delay sends, for the dynamics search and the tone loop (2026-11-03):
        // measured with the tails, more reverb read as a brighter, denser voice - Floaty got darker highs and
        // every style the same compression. The sends run in parallel, so the voice itself is what is left.
        MixPlan withoutSpace (const MixPlan& plan)
        {
            auto dry = plan;
            dry.set (pid::reverbOn, 0.0f);
            dry.set (pid::delayOn, 0.0f);
            return dry;
        }

        // How far a render's dynamics are from the target (phrases, crest, and no squashing). 2026-11-03: aimed at
        // the middle of the style's window (+-0.75 dB phrases, +-1 dB crest) - the windows of the styles overlap,
        // and "anywhere inside" left every style at the same dynamics (SONOVOX bench: 6.8-7.8 dB for all five)
        float dynamicsError (const RenderMetrics& m, const MixTarget& t)
        {
            const float crestMid = 0.5f * (t.dynamics.crestLo + t.dynamics.crestHi);
            const float phraseMid = 0.5f * (t.dynamics.phraseLo + t.dynamics.phraseHi);
            float e = 0.5f * outside (m.crestDb, crestMid - 1.0f, crestMid + 1.0f) + std::max (0.0f, m.compGrMedianDb - 6.5f);
            if (m.dynamicsValid)
                e += outside (m.dynamicsDb, phraseMid - 0.75f, phraseMid + 0.75f);
            return e;
        }
    }

    int AutoMixEngine::tuneDynamics (const CapturedAudio& audio, const std::map<juce::String, float>& current, MixPlan& plan,
                                     const VocalProfile& profile, const MixTarget& target, int maxRenders)
    {
        if (! plan.on (pid::compOn))
            return 0;
        // Task 6: the search starts from the planner's continuous ratio / threshold and moves in symmetric steps
        // around the current value (threshold +-3 dB, ratio x / : 1.4) - no grid that a gain shift could move. A
        // candidate replaces the current setting only when it lowers the dynamics-window cost by at least 15 %
        constexpr float thresholdStepDb = 3.0f, ratioStep = 1.4f, marginShare = 0.15f;
        const float t0 = plan.get (pid::compThreshold);
        float thr = t0;
        float ratio = plan.get (pid::compRatio);
        const auto apply = [&plan] (float threshold, float r)
        {
            plan.set (pid::compThreshold, threshold);
            plan.set (pid::compRatio, r);
        };
        apply (thr, ratio);
        int renders = 0;
        auto m = renderChecked (audio, current, withoutSpace (plan), profile);
        ++renders;
        float err = dynamicsError (m, target);
        const float inDyn = profile.dynamicsDb;

        while (renders < maxRenders && err > 0.05f)
        {
            const bool loose = (m.dynamicsValid && m.dynamicsDb > 0.5f * (target.dynamics.phraseLo + target.dynamics.phraseHi) + 0.75f)
                            || m.crestDb > 0.5f * (target.dynamics.crestLo + target.dynamics.crestHi) + 1.0f;
            std::vector<std::pair<float, float>> moves;   // threshold, ratio
            if (loose)
                moves = { { thr - thresholdStepDb, ratio }, { thr, ratio * ratioStep } };
            else
                moves = { { thr + thresholdStepDb, ratio }, { thr, ratio / ratioStep } };
            // Every move is rendered, the best one that earns the margin is taken (not the first)
            std::vector<float> scores { -err };
            std::vector<std::pair<float, float>> tried { { thr, ratio } };
            std::vector<RenderMetrics> rendered { m };
            for (auto [t, r] : moves)
            {
                r = juce::jlimit (1.5f, 5.0f, r);   // the planner's range
                if (renders >= maxRenders || (t == thr && r == ratio) || t < t0 - 12.0f || t > t0 + 12.0f || t < -60.0f || t > -6.0f)
                    continue;
                apply (t, r);
                const auto mt = renderChecked (audio, current, withoutSpace (plan), profile);
                ++renders;
                scores.push_back (-dynamicsError (mt, target));
                tried.push_back ({ t, r });
                rendered.push_back (mt);
            }
            const int pick = chooseWithMargin (scores, 0, marginShare * err);
            apply (thr, ratio);
            if (pick <= 0)
                break;
            thr = tried[(size_t) pick].first;
            ratio = tried[(size_t) pick].second;
            m = rendered[(size_t) pick];
            err = -scores[(size_t) pick];
            apply (thr, ratio);
        }
        apply (thr, ratio);

        // Still too far apart between phrases (2026-11-03: Rap / Aggressive ended at 4-8 dB for a 1-3 dB window: the
        // compressor hardly moves the distance between phrases, and the Level Ride, already at 85 %, stopped at its
        // +6 / -8 dB range): the ride gets a wider range (and full amount), kept only when it measurably helps
        {
            const float phraseMid = 0.5f * (target.dynamics.phraseLo + target.dynamics.phraseHi);
            if (m.dynamicsValid && m.dynamicsDb > phraseMid + 0.75f && plan.on (pid::rideOn) && renders < maxRenders + 1)
            {
                const float amount0 = plan.get (pid::rideAmount), boost0 = plan.get (pid::rideMaxBoost), cut0 = plan.get (pid::rideMaxCut);
                const float boost = boost0 >= 6.0f ? 10.0f : boost0 + 2.0f;   // a noisy take (3 dB) gets only a little more
                plan.set (pid::rideAmount, std::max (amount0, 85.0f));
                plan.set (pid::rideMaxBoost, boost);
                plan.set (pid::rideMaxCut, std::max (cut0, 12.0f));
                const auto mr = renderChecked (audio, current, withoutSpace (plan), profile);
                ++renders;
                if (const float e = dynamicsError (mr, target); e < err - 0.05f)
                {
                    m = mr;
                    err = e;
                    for (auto& d : plan.decisions)
                        if (! d.outputStage && d.module == dsp::ModuleId::ride)
                            d.title = "Level Ride " + juce::String (juce::roundToInt (plan.get (pid::rideAmount))) + " %, max. +"
                                      + juce::String (juce::roundToInt (boost)) + " dB";
                }
                else
                {
                    plan.set (pid::rideAmount, amount0);
                    plan.set (pid::rideMaxBoost, boost0);
                    plan.set (pid::rideMaxCut, cut0);
                }
            }
        }

        // Vocal Smash: the blend that brings the crest into the window (the vibe's density as the preference)
        const bool smashAllowed = ! profile.heavilyCompressed && ! plan.conservative && ! profile.distortionLikely;
        if (smashAllowed)
        {
            // the density target, scaled by how peaky the voice still is after the compressor (crest at the
            // bottom of the window x 0.6, at the top x 1.4)
            const float lo = target.dynamics.crestLo, hi = target.dynamics.crestHi;
            const float d = std::round (target.densityBlend * juce::jmap (juce::jlimit (lo, hi, m.crestDb), lo, hi, 0.6f, 1.4f));
            if (plan.on (pid::smashOn) && d > 0.0f)
                plan.set (pid::smashBlend, d);
            const float b0 = plan.on (pid::smashOn) ? plan.get (pid::smashBlend) : 0.0f;
            std::vector<float> blends;
            if (m.crestDb > target.dynamics.crestHi)
                blends = { std::max (6.0f, 1.5f * std::max (d, b0)) };
            else if (m.crestDb < target.dynamics.crestLo && b0 > 0.0f)
                blends = { 0.5f * b0, 0.0f };
            const auto setBlend = [&plan] (float b)
            {
                if (b <= 0.0f)
                {
                    plan.set (pid::smashOn, 0.0f);
                    return;
                }
                if (! plan.on (pid::smashOn))
                {
                    plan.set (pid::smashAmount, 40.0f);
                    plan.set (pid::smashAttack, 2.0f);
                    plan.set (pid::smashRelease, 90.0f);
                    plan.set (pid::smashTone, 10.0f);
                    plan.set (pid::smashOutput, 0.0f);
                }
                plan.set (pid::smashOn, 1.0f);
                plan.set (pid::smashBlend, std::round (b));
            };
            const auto cost = [&] (const RenderMetrics& x, float b) { return dynamicsError (x, target) + 0.02f * std::abs (b - d); };
            float best = b0, bestCost = cost (m, b0);
            for (float b : blends)
            {
                if (renders >= maxRenders + 2)
                    break;
                setBlend (b);
                const auto mb = renderChecked (audio, current, withoutSpace (plan), profile);
                ++renders;
                if (const float c = cost (mb, b); c < bestCost - 0.05f)
                {
                    best = b;
                    bestCost = c;
                    m = mb;
                }
            }
            setBlend (best);
            plan.decisions.erase (std::remove_if (plan.decisions.begin(), plan.decisions.end(),
                                                  [] (const Decision& x) { return ! x.outputStage && x.module == dsp::ModuleId::smash; }), plan.decisions.end());
            if (best > 0.0f)
                plan.decisions.push_back ({ dsp::ModuleId::smash, false,
                                            both ("Parallelkompression {0} % Blend", "Parallel compression {0} % blend", { num (best, 0) }),
                                            both ("Dichte für {0} · {1}: Crest nach der Kette {2} dB (Ziel {3}-{4} dB).",
                                                  "Density for {0} · {1}: crest after the chain {2} dB (target {3}-{4} dB).",
                                                  { genreName (target.genre), vibeName (target.vibe), num (m.crestDb, 1),
                                                    num (target.dynamics.crestLo, 1), num (target.dynamics.crestHi, 1) }), true });
        }

        if (auto it = std::find_if (plan.decisions.begin(), plan.decisions.end(), [] (const Decision& x) { return ! x.outputStage && x.module == dsp::ModuleId::comp; });
            it != plan.decisions.end())
        {
            it->title = both ("Kompressor {0}:1, Schwelle {1}", "Compressor {0}:1, threshold {1}", { num (ratio, 1), db (thr) });
            if (m.dynamicsValid && profile.dynamicsValid)
                it->reason += both (" Gesucht auf das Dynamik-Ziel ({0}-{1} dB): Phrasen {2} -> {3} dB.", " Searched for the dynamics target ({0}-{1} dB): phrases {2} -> {3} dB.",
                                    { num (target.dynamics.phraseLo, 0), num (target.dynamics.phraseHi, 0), num (inDyn, 1), num (m.dynamicsDb, 1) });
            it->measured = true;
        }
        return renders;
    }

    std::vector<std::pair<juce::String, MixPlan>> AutoMixEngine::candidatesFor (const MixPlan& base, const VocalProfile& profile,
                                                                               const std::map<juce::String, float>& current, const MixTarget& target)
    {
        using dsp::EqRole;
        using dsp::EqBandType;
        using M = dsp::ModuleId;
        std::vector<std::pair<juce::String, MixPlan>> out;
        out.push_back ({ both ("Basisplan", "Base plan"), base });
        const auto add = [&out, &base] (const juce::String& name, const std::function<bool (MixPlan&)>& modify)
        {
            MixPlan c = base;
            if (modify (c))
                out.push_back ({ name, std::move (c) });
        };
        const auto k2 = [] (const char* id) { return dsp::instanceParamId (id, 1); };
        const auto style = styleOf (profile);

        // 0.8: no "lighter / denser dynamics" variants - tuneDynamics searched the compressor and Smash on the render

        // Saturation: the other way round
        add (base.on (pid::satOn) ? both ("Ohne Sättigung", "Without saturation") : both ("Mit Sättigung", "With saturation"), [&] (MixPlan& c)
        {
            if (c.on (pid::satOn))
            {
                c.set (pid::satOn, 0.0f);
                return true;
            }
            if (profile.distortionLikely)
                return false;
            // 0.8: the character of the genre / vibe, a little gentler than the target
            c.set (pid::satOn, 1.0f);
            c.set (pid::satCharacter, (float) target.satCharacter);
            c.set (pid::satDrive, std::max (2.0f, target.satDrive - 1.0f));
            c.set (pid::satMix, std::max (10.0f, 0.8f * target.satMix));
            c.set (pid::satOutput, 0.0f);
            return true;
        });

        // Softer highs: the planner only ever lifts the top end. On a take that is already
        // brighter than the window (above its top, 2026-10-29: -3 dB above 2 kHz against the core) a broad shelf from
        // 5 kHz takes the edge off; the render decides whether that is worth it.
        add (both ("Höhen sanfter", "Softer highs"), [&] (MixPlan& c)
        {
            // 0.6.1: not when the base plan already cuts the highs (AutoEq)
            const float air = roleGain (c, current, EqRole::air);
            if (profile.topDb <= autoeq::topWindow.hi || air > 0.2f || air < -0.9f || ! c.routing.contains (M::eq))
                return false;
            c.set (pid::eqOn, 1.0f);
            return setRoleBand (c, current, 0, EqRole::air,
                                eqBand (EqBandType::highShelf, 5000.0f, -juce::jlimit (1.5f, 4.0f, 1.0f + 0.4f * (profile.topDb - autoeq::topWindow.hi)), 0.7071f), true) >= 0;
        });

        // Tone after the dynamics: the high shelf (and a presence boost) moves into a second
        // EQ instance behind compressor, de-esser and resonance control. A boost before the
        // compressor makes it react to the highs and lifts the S sounds into the de-esser.
        add (both ("Höhen nach der Dynamik (EQ 2)", "Highs after the dynamics (EQ 2)"), [&] (MixPlan& c)
        {
            const auto air = roleBand (c, current, EqRole::air);
            const float high = air.has_value() && air->on ? air->gain : 0.0f;
            // A presence lift: the 0.6.0 mid bell or the 0.6.1 tone bell (AutoEq)
            std::vector<std::pair<EqRole, dsp::EqBand>> presence;
            for (auto role : { EqRole::autoMix, EqRole::autoTone })
                if (auto b = roleBand (c, current, role); b.has_value() && b->on && b->gain > 0.2f && b->freq >= 1800.0f)
                    presence.push_back ({ role, *b });
            const bool presenceBoost = ! presence.empty();
            if (high <= 0.2f && ! presenceBoost)
                return false;
            c.set (k2 (pid::eqOn), 1.0f);
            c.set (k2 (pid::eqMix), 100.0f);
            // EQ 2 carries only what moves there: its own role bands are switched off / flat
            for (auto role : { EqRole::lowCut, EqRole::highCut })
                if (auto b = roleBand (c, current, role, 1); b.has_value() && b->on)
                {
                    b->on = false;
                    setRoleBand (c, current, 1, role, *b, false);
                }
            for (auto role : { EqRole::warmth, EqRole::autoMix, EqRole::air, EqRole::autoTone })
                if (auto b = roleBand (c, current, role, 1); b.has_value() && b->gain != 0.0f)
                {
                    b->gain = 0.0f;
                    setRoleBand (c, current, 1, role, *b, false);
                }
            if (high > 0.2f)
            {
                setRoleBand (c, current, 1, EqRole::air, *air, true);
                auto flat = *air;
                flat.gain = 0.0f;
                setRoleBand (c, current, 0, EqRole::air, flat, false);
            }
            for (auto& [role, b] : presence)
            {
                setRoleBand (c, current, 1, role, b, true);
                auto flat = b;
                flat.gain = 0.0f;
                setRoleBand (c, current, 0, role, flat, false);
            }
            int after = -1;
            for (auto m : { M::comp, M::smash, M::deess, M::smooth })
                after = std::max (after, c.routing.indexOf (m));
            c.routing.insertAt (after + 1, M::eq, 1);
            return c.routing.contains (M::eq, 1);
        });

        // Effects drier / wetter
        add (both ("Effekte trockener", "Drier effects"), [&] (MixPlan& c)
        {
            if (! c.on (pid::reverbOn) && ! c.on (pid::delayOn))
                return false;
            c.set (pid::reverbMix, c.get (pid::reverbMix) * 0.65f);
            c.set (pid::delayMix, c.get (pid::delayMix) * 0.65f);
            return true;
        });
        add (style == VocalStyle::sustained ? both ("Mehr Raum (Large Hall)", "More space (Large Hall)") : both ("Mehr Raum", "More space"), [&] (MixPlan& c)
        {
            if (! c.on (pid::reverbOn) || style == VocalStyle::spoken || profile.reverberant)
                return false;
            c.set (pid::reverbMix, std::min (28.0f, c.get (pid::reverbMix) * 1.35f));
            if (style == VocalStyle::sustained)
            {
                c.set (pid::reverbType, 3.0f);      // Large Hall
                c.set (pid::reverbDecay, 2.6f);
                c.set (pid::reverbPreDelay, 40.0f);
                c.set (pid::reverbBass, 0.8f);
            }
            return true;
        });

        // De-esser stronger (when S sounds were an issue)
        add (both ("De-Esser stärker", "Stronger De-Esser"), [&] (MixPlan& c)
        {
            if (! c.on (pid::deessOn) || ! profile.sibilanceValid || profile.sibilanceVsVowelDb < -8.0f)
                return false;
            c.set (pid::deessThreshold, c.get (pid::deessThreshold) - 3.0f);
            c.set (pid::deessRange, std::min (12.0f, c.get (pid::deessRange) + 2.0f));
            return true;
        });

        return out;
    }

    namespace
    {
        // Keeps the explanations true to the chosen candidate: titles that carry values are
        // rebuilt, decisions of modules the candidate switched off are removed
        void syncDecisions (MixPlan& plan, const juce::String& candidateName, const ObjectiveBreakdown& before,
                            const ObjectiveBreakdown& after, const std::map<juce::String, float>& current)
        {
            using M = dsp::ModuleId;
            plan.decisions.erase (std::remove_if (plan.decisions.begin(), plan.decisions.end(), [&plan] (const Decision& d)
            {
                if (d.outputStage)
                    return false;
                switch (d.module)
                {
                    case M::smash: return ! plan.on (pid::smashOn);
                    case M::sat:   return ! plan.on (pid::satOn);
                    default:       return false;
                }
            }), plan.decisions.end());

            if (auto* d = plan.find (M::comp))
                d->title = both ("Kompressor {0}:1, Schwelle {1}", "Compressor {0}:1, threshold {1}",
                                 { num (plan.get (pid::compRatio), 1), db (plan.get (pid::compThreshold)) });
            if (auto* d = plan.find (M::reverb))
            {
                d->title = both ("{0} {1} %, {2} s, geduckt", "{0} {1} %, {2} s, ducked",
                                 { txt::reverbType (juce::jlimit (0, 7, (int) plan.get (pid::reverbType))),
                                   juce::String (juce::roundToInt (plan.get (pid::reverbMix))), num (plan.get (pid::reverbDecay), 1) });
            }
            if (plan.on (pid::smashOn) && plan.find (M::smash) == nullptr)
                plan.decisions.push_back ({ M::smash, false,
                                            both ("Parallelkompression {0} % Blend", "Parallel compression {0} % blend", { juce::String (juce::roundToInt (plan.get (pid::smashBlend))) }),
                                            both ("Die Variante mit mehr Dichte schnitt in der Messung besser ab.", "The denser variant scored better in the measurement."), true });
            if (plan.on (pid::satOn) && plan.find (M::sat) == nullptr)
                plan.decisions.push_back ({ M::sat, false,
                                            both ("Sättigung {0}, {1} %", "Saturation {0}, {1} %", { juce::StringArray { "Warm", "Tape", "Tube", "Hard" }[juce::jlimit (0, 3, (int) plan.get (pid::satCharacter))],
                                                                                        juce::String (juce::roundToInt (plan.get (pid::satMix))) }),
                                            both ("Leichte Obertöne - die Variante mit Sättigung schnitt in der Messung besser ab.",
                                                  "Light harmonics - the variant with saturation scored better in the measurement."), true });
            // The air shelf of the "Softer highs" candidate (and any other candidate's) is an audible EQ band: it gets its entry
            if (const auto air = roleBand (plan, current, dsp::EqRole::air); air.has_value() && air->on && std::abs (air->gain) > 1.0e-3f
                && std::none_of (plan.decisions.begin(), plan.decisions.end(), [&] (const Decision& d)
                                 { return ! d.outputStage && d.module == M::eq && d.title == autoeq::titleOf (dsp::EqRole::air, *air); }))
            {
                auto before = *air;
                before.on = false;
                updateEqDecision (plan, dsp::EqRole::air, before, *air,
                                  both ("Die Variante mit sanfteren Höhen schnitt in der Messung besser ab.", "The variant with softer highs scored better in the measurement."));
            }
            if (plan.routing.contains (M::eq, 1))
            {
                const float g = roleGain (plan, current, dsp::EqRole::air, 1);
                const juce::String title = g > 0.0f ? both ("EQ 2 nach der Dynamik: Höhen {0}", "EQ 2 after the dynamics: highs {0}", { db (g) })
                                                    : both ("EQ 2 nach der Dynamik", "EQ 2 after the dynamics");
                plan.decisions.push_back ({ M::eq, false, title,
                                            both ("Die Höhenanhebung sitzt hinter Kompressor und De-Esser: Der Kompressor reagiert nicht mehr auf die Höhen, "
                                                  "und die S-Laute werden vor der Anhebung kontrolliert.",
                                                  "The high boost sits after the compressor and De-Esser: the compressor no longer reacts to the highs, "
                                                  "and the S sounds are controlled before the boost."), true });
            }
            plan.notes.push_back (both ("Variante \"{0}\" gewählt: Bewertung {1} -> {2} ({3}).", "Variant \"{0}\" chosen: score {1} -> {2} ({3}).",
                                        { candidateName, num (before.total(), 2), num (after.total(), 2), after.describe() }));
        }
    }

    //==========================================================================
    AutoMixResult AutoMixEngine::run (const CapturedAudio& audio, const std::map<juce::String, float>& live,
                                      const std::function<bool()>& shouldStop, const Progress& progress, const MixIntent& intent)
    {
        // The live state is read for the session parameters only; everything else starts at its default
        const auto current = startValues (live);
        const auto mixTarget = targetFor (intent.genre, intent.vibe);
        AutoMixResult r;
        r.intent = intent;
        const auto startMs = juce::Time::getMillisecondCounterHiRes();
        const auto stop = [&shouldStop] { return shouldStop && shouldStop(); };
        // The stage text is shown later on the message thread: both languages
        const auto report = [&progress] (float f, const char* german, const char* english) { if (progress) progress (f, both (german, english)); };

        // Level independence: measure the capture, scale a copy to the working level, analyse and plan on the copy.
        // The plan's input gain is that scaling, so the renders below take the ORIGINAL capture and the chain applies it.
        float wantedGainDb = 0.0f;
        const float gainDb = workingGainDb (audio, &wantedGainDb);
        CapturedAudio scaled;
        {
            const float g = juce::Decibels::decibelsToGain (gainDb);
            scaled = audio;
            for (auto* part : { &scaled.vocal, &scaled.beat })   // the beat scales with the voice: their relation stays as captured
                for (auto& ch : *part)
                    for (auto& x : ch)
                        x *= g;
        }

        report (0.05f, "Aufnahme wird analysiert", "Analyzing the recording");
        r.profile = VocalProfiler::analyse (scaled, 5.0f, shouldStop, gainDb);
        if (stop())
        {
            r.error = both ("Abgebrochen.", "Canceled.");
            return r;
        }
        if (r.profile.voicedSeconds < 2.0f)
        {
            r.error = both ("Zu wenig Gesang erfasst - bitte die Vocal-Spur abspielen und erneut analysieren.",
                            "Too little singing captured - play the vocal track and analyze again.");
            return r;
        }

        report (0.3f, "Mix wird geplant", "Planning the mix");
        r.plan = AutoMixPlanner::plan (r.profile, current, intent);
        r.plan.set (pid::inputGain, gainDb);
        {
            // The input stage in the decision log (entry 1 of the global stages; the output stage is entry 0)
            ModuleLog in;
            in.globalStage = true;
            in.instance = 1;
            in.active = true;
            in.problem = "level";
            in.need = 1.0f;
            in.intensity = std::abs (gainDb) / 24.0f;
            in.measurements.push_back ({ "voiceRms", workingLevelDb - wantedGainDb, "dBFS" });
            in.measurements.push_back ({ "workingLevel", workingLevelDb, "dBFS" });
            in.measurements.push_back ({ "wantedGain", wantedGainDb, "dB" });
            r.plan.log.insert (r.plan.log.begin(), std::move (in));
            fillParameters (r.plan.log, r.plan.values);
            if (std::abs (wantedGainDb - gainDb) > 1.0e-4f)
            {
                setMeasured (r.plan.log.front().result, "clamped", 1.0f, "");
                r.plan.notes.push_back (both ("Die Aufnahme ist sehr {0}: Der Eingangspegel reicht mit {1} nicht bis zum Arbeitspegel ({2}).",
                                              "The recording is very {0}: the input gain of {1} does not reach the working level ({2}).",
                                              { wantedGainDb > 0.0f ? both ("leise", "quiet") : both ("laut", "loud"), db (gainDb), num (workingLevelDb, 0) + " dBFS" }));
            }
        }
        r.original = measure (scaled, gainDb);
        r.original.sibilanceVsVowelDb = r.profile.sibilanceVsVowelDb;
        r.original.sibilanceValid = r.profile.sibilanceValid;

        // ---- Dynamics (0.8): compressor and Smash searched on the render into the target window
        report (0.33f, "Dynamik wird gesucht", "Searching the dynamics");
        r.iterations += tuneDynamics (audio, current, r.plan, r.profile, mixTarget);

        // ---- Verify the whole chain and adjust where the measurement is reliable
        auto& plan = r.plan;
        RenderMetrics m;
        for (int round = 0; round < 4; ++round)
        {
            if (stop())
            {
                r.error = both ("Abgebrochen.", "Canceled.");
                return r;
            }
            report (0.35f + 0.12f * (float) round, "Mix wird geprüft", "Checking the mix");
            m = renderChecked (audio, current, withoutSpace (plan), r.profile);
            ++r.iterations;
            bool changed = false;

            // De-esser: S sounds controlled, but no lisp
            if (plan.on (pid::deessOn) && m.sibilanceValid && r.profile.sibilanceValid)
            {
                const float thr = plan.get (pid::deessThreshold);
                // Too strong in absolute terms, or lifted by the chain clearly above the original
                const bool tooStrong = m.sibilanceVsVowelDb > -3.0f
                                    || m.sibilanceVsVowelDb > std::max (-8.0f, r.original.sibilanceVsVowelDb + 2.0f);
                // Task 6: the correction grows with the excess (half of the 3 dB / 2 dB step for a small one, all of it from 3 dB over)
                const float over = std::max (m.sibilanceVsVowelDb + 3.0f, m.sibilanceVsVowelDb - std::max (-8.0f, r.original.sibilanceVsVowelDb + 2.0f));
                const float stepShare = 0.5f + 0.5f * soft (over, 0.0f, 3.0f);
                if (tooStrong && thr > -55.0f && plan.get (pid::deessRange) < 8.0f)
                {
                    plan.set (pid::deessThreshold, thr - 3.0f * stepShare);
                    plan.set (pid::deessRange, std::min (8.0f, plan.get (pid::deessRange) + 2.0f * stepShare));
                    r.adjustments.push_back (both ("De-Esser verstärkt (S-Laute nach der Kette noch {0}).", "De-Esser strengthened (sibilance still {0} after the chain).", { db (m.sibilanceVsVowelDb) }));
                    changed = true;
                }
                else if (m.sibilanceVsVowelDb < r.profile.sibilanceVsVowelDb - 10.0f)
                {
                    const float under = (r.profile.sibilanceVsVowelDb - 10.0f) - m.sibilanceVsVowelDb;
                    plan.set (pid::deessThreshold, thr + 3.0f * (0.5f + 0.5f * soft (under, 0.0f, 3.0f)));
                    r.adjustments.push_back (both ("De-Esser zurückgenommen (S-Laute wurden zu stark gedämpft).", "De-Esser backed off (S sounds were reduced too much)."));
                    changed = true;
                }
            }

            // The chain itself (compression, presence, saturation) can lift S sounds:
            // then a gentle de-esser is set up even if the recording did not need one
            if (! plan.on (pid::deessOn) && m.sibilanceValid
                && m.sibilanceVsVowelDb > std::max (-8.0f, r.original.sibilanceVsVowelDb + 2.0f))
            {
                AutoMixPlanner::configureDeEsser (plan, r.profile, 0.0f, 4.0f);   // the profile is at the working level already
                plan.decisions.push_back ({ dsp::ModuleId::deess, false,
                                            both ("De-Esser ab {0} Hz, max. 4 dB", "De-Esser from {0} Hz, max. 4 dB", { juce::String (juce::roundToInt (plan.get (pid::deessFreq))) }),
                                            both ("Nach der Kette standen die S-Laute {0} stärker hervor als im Original - dezent ausgeglichen.",
                                                  "After the chain the S sounds stood out {0} more than in the original - gently evened out.",
                                                  { db (m.sibilanceVsVowelDb - r.original.sibilanceVsVowelDb) }), true });
                r.adjustments.push_back (both ("De-Esser ergänzt (die Kette hob die S-Laute an).", "De-Esser added (the chain lifted the S sounds)."));
                changed = true;
            }

            // Brightness: the chain must not make an already bright voice brighter (a vibe that asks for more
            // highs, e.g. Floaty, moves the limit by its own lift - otherwise this undid the style, 2026-11-03)
            bool airAdjusted = false;
            const float styleTopDb = [&mixTarget]
            {
                float sum = 0.0f;
                int n = 0;
                for (int k = 0; k < numThirds; ++k)
                    if (thirdCentre (k) >= 5000.0f * 0.99f)
                    {
                        sum += mixTarget.styleDb[(size_t) k];
                        ++n;
                    }
                return n > 0 ? std::max (0.0f, sum / (float) n) : 0.0f;
            }();
            if (auto air = roleBand (plan, current, dsp::EqRole::air); m.topDb > r.original.topDb + 5.0f + styleTopDb && air.has_value() && air->on && air->gain > 0.0f)
            {
                const auto before = *air;
                air->gain = std::max (0.0f, air->gain - 1.5f);
                setRoleBand (plan, current, 0, dsp::EqRole::air, *air, false);
                updateEqDecision (plan, dsp::EqRole::air, before, *air, both ("Höhenanhebung verringert: die Kette hob die Höhen schon an.",
                                                                              "High boost reduced: the chain already lifted the highs."));
                r.adjustments.push_back (both ("Höhenanhebung verringert (Kette wurde zu hell).", "High boost reduced (the chain got too bright)."));
                changed = true;
                airAdjusted = true;
            }

            // Tone (0.8): the auto-mix bands follow the third octaves of the real render onto the target
            // curve - compression, saturation, the de-esser and effects move the balance as well (every round:
            // a de-esser strengthened in a later round darkens the top again)
            if (plan.on (pid::eqOn) && plan.routing.contains (dsp::ModuleId::eq))
            {
                auto state = autoEqState (plan, current, r.profile);
                state.context.target = &mixTarget;
                state.context.unmaskDb = autoeq::unmaskOf (r.profile, plan.conservative);
                // S sounds still too strong after the chain: no more lift up there than on a sibilant take
                state.context.sibilant = state.context.sibilant || (m.sibilanceValid && m.sibilanceVsVowelDb > mixTarget.sibilanceMax);
                for (const auto& c : autoeq::correctCurve (m.thirdsDb, state.context, state.owned))
                {
                    if (c.role == dsp::EqRole::air && airAdjusted)
                        continue;
                    const auto before = roleBand (plan, current, c.role);
                    if (setRoleBand (plan, current, 0, c.role, c.band, true) < 0)
                        continue;
                    updateEqDecision (plan, c.role, before.has_value() ? *before : dsp::EqBand {}, c.band, c.reason);
                    r.adjustments.push_back (both ("Klang nachgeregelt: {0}.", "Tone readjusted: {0}.", { c.title }));
                    changed = true;
                }
            }
            // ... and must not dull it either
            if (m.topDb < r.original.topDb - 5.0f && plan.on (pid::smoothOn) && plan.get (pid::smoothAmount) > 20.0f)
            {
                plan.set (pid::smoothAmount, plan.get (pid::smoothAmount) - 10.0f);
                r.adjustments.push_back (both ("Resonanzkontrolle verringert (Höhen gingen verloren).", "Resonance control reduced (highs were lost)."));
                changed = true;
            }

            // Low end: keep the body of the voice
            if (auto lowCut = roleBand (plan, current, dsp::EqRole::lowCut); m.bodyDb < r.original.bodyDb - 6.0f && lowCut.has_value() && lowCut->freq > 65.0f)
            {
                lowCut->freq *= 0.85f;
                setRoleBand (plan, current, 0, dsp::EqRole::lowCut, *lowCut, false);
                r.adjustments.push_back (both ("Low Cut gesenkt (zu viel Körper ging verloren).", "Low Cut lowered (too much body was lost)."));
                changed = true;
            }

            // Mono compatibility of the result
            if (m.monoLossDb < -1.5f && plan.on (pid::doublerOn))
            {
                plan.set (pid::doublerMix, plan.get (pid::doublerMix) * 0.6f);
                r.adjustments.push_back (both ("Doubler reduziert (Pegelverlust in Mono).", "Doubler reduced (level loss in mono)."));
                changed = true;
            }

            if (! changed)
                break;
        }

        // Gain staging between compressor and the rest of the chain
        if (plan.on (pid::compOn))
            plan.set (pid::compMakeup, juce::jlimit (0.0f, 8.0f, std::round (m.compGrMedianDb * 0.8f * 2.0f) / 2.0f));

        // ---- Candidate search: interacting decisions, each rendered and scored at the same
        //      target loudness (AutoMixObjective). A variant must beat the base plan.
        // The variants are rendered with the plan's input gain, i.e. at the working level: they are scored against the
        // target of the take at that level. (The target's range limit, -24 .. -14 LUFS, applied to the loudness as
        // captured would make the score depend on the recording level.) The output stage below aims at the target of
        // the take as captured.
        const float target = targetLufs (r.original.lufs + gainDb);
        {
            auto variants = candidatesFor (plan, r.profile, current, mixTarget);
            const auto score = [&] (const juce::String& name, const MixPlan& cand, bool isBase)
            {
                CandidateResult c;
                c.name = name;
                c.metrics = renderChecked (audio, current, cand, r.profile);
                c.routing = cand.routing;
                c.score = evaluate (c.metrics, r.original, r.profile, target, isBase, cand.on (pid::reverbOn) || cand.on (pid::delayOn), mixTarget);
                ++r.iterations;
                return c;
            };
            for (size_t i = 0; i < variants.size(); ++i)
            {
                if (stop())
                {
                    r.error = both ("Abgebrochen.", "Canceled.");
                    return r;
                }
                report (0.55f + 0.25f * (float) i / (float) variants.size(), "Varianten werden verglichen", "Comparing variants");
                r.candidates.push_back (score (variants[i].first, variants[i].second, i == 0));
            }

            // The two best single changes together (when both beat the base plan and touch
            // different decisions)
            std::vector<size_t> order;
            for (size_t i = 1; i < r.candidates.size(); ++i)
                if (r.candidates[i].score.total() < r.candidates[0].score.total())
                    order.push_back (i);
            std::sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return r.candidates[a].score.total() < r.candidates[b].score.total(); });
            if (order.size() >= 2 && ! stop())
            {
                MixPlan combined = variants[order[0]].second;
                const auto& second = variants[order[1]].second;
                for (const auto& [id, v] : second.values)
                    if (std::abs (v - plan.get (id, v)) > 1.0e-4f)
                        combined.set (id, v);
                for (const auto& slot : second.routing)
                    if (! combined.routing.contains (slot.id, slot.instance))
                        combined.routing.insertAt (std::min (combined.routing.size(), second.routing.indexOf (slot.id, slot.instance)), slot.id, slot.instance, slot.insert);
                const auto name = variants[order[0]].first + " + " + variants[order[1]].first;
                variants.push_back ({ name, combined });
                r.candidates.push_back (score (name, combined, false));
            }

            size_t best = 0;
            for (size_t i = 1; i < r.candidates.size(); ++i)
                if (r.candidates[i].score.total() < r.candidates[best].score.total())
                    best = i;
            r.chosenCandidate = (int) best;
            if (best != 0)
            {
                const auto baseValues = plan.values;
                plan = variants[best].second;
                // decision log: the chosen candidate's name on every module it changed
                for (const auto& [id, v] : plan.values)
                {
                    const auto it = baseValues.find (id);
                    if (it != baseValues.end() && std::abs (it->second - v) < 1.0e-4f)
                        continue;
                    const auto* spec = findParameterSpec (id);
                    const auto module = spec != nullptr ? moduleOfParameter (*spec) : std::nullopt;
                    if (module.has_value())
                        for (auto& l : plan.log)
                            if (! l.globalStage && l.module == *module && l.instance == 0)
                                setMeasured (l.result, "candidate: " + variants[best].first, (float) best, "");
                }
                syncDecisions (plan, variants[best].first, r.candidates[0].score, r.candidates[best].score, current);
                r.adjustments.push_back (both ("Variante gewählt: ", "Variant chosen: ") + variants[best].first);
            }
        }

        // ---- Output level: match the target loudness, then keep peaks below -1 dBFS. With a beat on the
        //      sidechain (Jakob 2026-10-28) the voice sits leadOverBeatLu above the beat instead: the
        //      original raised by what it lacks against the beat (measured where the beat plays)
        const bool toBeat = r.profile.beatPresent;
        const float outputTarget = toBeat ? r.original.lufs + (autoeq::leadOverBeatLu - r.profile.vocalOverBeatLu) : targetLufs (r.original.lufs);
        report (0.85f, "Pegel wird angepasst", "Adjusting the level");
        m = renderChecked (audio, current, plan, r.profile);
        ++r.iterations;
        float out = plan.get (pid::outputGain) + (outputTarget - m.lufs);
        plan.set (pid::outputGain, juce::jlimit (-24.0f, 24.0f, out));

        m = renderChecked (audio, current, plan, r.profile);
        ++r.iterations;
        bool peakLimited = false;
        if (m.peakDb > -1.0f)
        {
            plan.set (pid::outputGain, plan.get (pid::outputGain) - (m.peakDb + 1.0f));
            m = renderChecked (audio, current, plan, r.profile);
            ++r.iterations;
            peakLimited = true;
        }
        r.processed = m;

        Decision outDecision;
        outDecision.outputStage = true;
        outDecision.measured = true;
        outDecision.title = both ("Ausgang {0} LUFS, Spitze {1}", "Output {0} LUFS, peak {1}", { num (m.lufs, 1), db (m.peakDb) });
        if (toBeat)
        {
            const float missing = outputTarget - m.lufs;   // what the peaks / the gain range left out
            outDecision.reason = both ("Stimme 1 LU über dem Beat eingepegelt (Beat am Sidechain {0} LUFS).",
                                       "Voice set 1 LU above the beat (beat on the sidechain {0} LUFS).", { num (r.profile.beatLufs, 1) })
                               + (missing > 0.5f ? both (" Wegen der Spitzen (1 dB Reserve) bleibt sie {0} LU darunter - den Kanal-Fader hochziehen.",
                                                         " The peaks (1 dB of headroom) keep it {0} LU short of that - raise the channel fader.",
                                                         { num (missing, 1) })
                                                 : juce::String())
                               + both (" Annahme: Der Sidechain bekommt den Beat so laut, wie er spielt (Send nach dem Fader, 0 dB).",
                                       " Assumes the sidechain gets the beat as loud as it plays (post-fader send at 0 dB).");
        }
        else
        {
            outDecision.reason = both ("Lautheit an das Original angeglichen ({0} LUFS), damit der Vergleich fair bleibt.",
                                       "Loudness matched to the original ({0} LUFS) so the comparison stays fair.", { num (r.original.lufs, 1) })
                               + (peakLimited ? both (" Etwas leiser, um 1 dB Reserve zu halten.", " A little quieter to keep 1 dB of headroom.") : juce::String());
        }
        outDecision.reason += both (" Clip Guard bleibt als Schutz aktiv.", " Clip Guard stays on as a safeguard.");
        plan.decisions.push_back (outDecision);

        // ---- Decision log: the final state of every module, and what the render showed
        for (auto& l : plan.log)
        {
            if (l.globalStage)
                continue;
            const bool on = l.module == dsp::ModuleId::denoise ? plan.on (pid::denoiseOn) : l.module == dsp::ModuleId::breath ? plan.on (pid::breathOn)
                                                                                        : plan.activeModules().count (l.module) > 0;
            if (on != l.active)
            {
                l.active = on;
                l.need = on ? 1.0f : 0.0f;
                if (on)
                    l.skipReason = {};
            }
            if (l.module == dsp::ModuleId::comp && on)
            {
                setMeasured (l.result, "compGrMedian", m.compGrMedianDb, "dB");
                setMeasured (l.result, "compGrMax", m.compGrMaxDb, "dB");
            }
            if (l.module == dsp::ModuleId::deess && on)
            {
                if (m.sibilanceValid)
                    setMeasured (l.result, "sibilanceVsVowelAfter", m.sibilanceVsVowelDb, "dB");
                setMeasured (l.result, "deessGrMax", m.deessGrMaxDb, "dB");
            }
        }
        for (auto& l : plan.log)
            if (l.globalStage && l.instance == 0)   // the output stage
            {
                setMeasured (l.result, "lufs", m.lufs, "LUFS");
                setMeasured (l.result, "peak", m.peakDb, "dBFS");
            }
        fillParameters (plan.log, plan.values);

        report (1.0f, "Fertig", "Done");
        r.seconds = (juce::Time::getMillisecondCounterHiRes() - startMs) * 0.001;
        r.ok = true;
        return r;
    }
}
