#include "AutoMixEngine.h"
#include "../Parameters/SettingsReader.h"

namespace velvox::automix
{
    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }
        juce::String db (float v) { return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }

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
        void measureInto (RenderMetrics& m, const CapturedAudio& a)
        {
            const int n = a.numSamples();
            if (n <= 0)
                return;
            m.valid = true;
            m.lufs = VocalProfiler::integratedLufs (pointers (a), n, a.sampleRate);

            float peak = 0.0f;
            for (auto& c : a.vocal)
                for (float x : c)
                    peak = std::max (peak, std::abs (x));
            m.peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);

            const auto prof = VocalProfiler::analyse (a, 0.0f);
            m.topDb = prof.topDb;
            m.presenceDb = prof.presenceDb;
            m.bodyDb = prof.bodyDb;
            m.lowMidDb = prof.lowMidDb;
            m.airDb = prof.airDb;
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
                m.monoLossDb = VocalProfiler::integratedLufs (mono, n, a.sampleRate) - m.lufs;
            }
        }
    }

    RenderMetrics AutoMixEngine::measure (const CapturedAudio& audio)
    {
        RenderMetrics m;
        measureInto (m, audio);
        return m;
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

        OfflineParameters params;
        params.setAll (current);
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
            blockDb.push_back ((float) (10.0 * std::log10 (std::max (1.0e-20, sum / ((double) len * channels)))));
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
    std::vector<std::pair<juce::String, MixPlan>> AutoMixEngine::candidatesFor (const MixPlan& base, const VocalProfile& profile,
                                                                               const std::map<juce::String, float>& current)
    {
        using dsp::EqRole;
        using dsp::EqBandType;
        using M = dsp::ModuleId;
        std::vector<std::pair<juce::String, MixPlan>> out;
        out.push_back ({ de ("Basisplan"), base });
        const auto add = [&out, &base] (const juce::String& name, const std::function<bool (MixPlan&)>& modify)
        {
            MixPlan c = base;
            if (modify (c))
                out.push_back ({ name, std::move (c) });
        };
        const auto k2 = [] (const char* id) { return dsp::instanceParamId (id, 1); };
        const auto style = styleOf (profile);

        // Dynamics: lighter
        add (de ("Dynamik leichter"), [&] (MixPlan& c)
        {
            if (! c.on (pid::compOn))
                return false;
            c.set (pid::compRatio, std::max (1.5f, c.get (pid::compRatio) * 0.7f));
            c.set (pid::compThreshold, c.get (pid::compThreshold) + 2.0f);
            c.set (pid::compMakeup, std::max (0.0f, c.get (pid::compMakeup) - 1.0f));
            if (c.on (pid::smashOn))
                c.set (pid::smashOn, 0.0f);
            return true;
        });

        // Dynamics: denser (never on an already squashed take)
        add (de ("Dynamik dichter"), [&] (MixPlan& c)
        {
            if (! c.on (pid::compOn) || profile.heavilyCompressed)
                return false;
            c.set (pid::compRatio, std::min (6.0f, c.get (pid::compRatio) * 1.35f));
            c.set (pid::compThreshold, c.get (pid::compThreshold) - 2.0f);
            c.set (pid::compMakeup, std::min (8.0f, c.get (pid::compMakeup) + 1.0f));
            if (! c.on (pid::smashOn) && ! profile.distortionLikely)
            {
                c.set (pid::smashOn, 1.0f);
                c.set (pid::smashAmount, 40.0f);
                c.set (pid::smashBlend, 18.0f);
                c.set (pid::smashAttack, 2.0f);
                c.set (pid::smashRelease, 90.0f);
                c.set (pid::smashTone, 10.0f);
                c.set (pid::smashOutput, 0.0f);
            }
            return true;
        });

        // Saturation: the other way round
        add (base.on (pid::satOn) ? de ("Ohne Sättigung") : de ("Mit Sättigung"), [&] (MixPlan& c)
        {
            if (c.on (pid::satOn))
            {
                c.set (pid::satOn, 0.0f);
                return true;
            }
            if (profile.distortionLikely)
                return false;
            c.set (pid::satOn, 1.0f);
            c.set (pid::satCharacter, 1.0f);
            c.set (pid::satDrive, 4.0f);
            c.set (pid::satMix, 25.0f);
            c.set (pid::satOutput, 0.0f);
            return true;
        });

        // Softer highs: the planner only ever lifts the top end. On a take that is already
        // brighter than the window (> -7 dB above 2 kHz against the core) a broad shelf from
        // 5 kHz takes the edge off; the render decides whether that is worth it.
        add (de ("Höhen sanfter"), [&] (MixPlan& c)
        {
            if (profile.topDb <= -7.0f || roleGain (c, current, EqRole::air) > 0.2f || ! c.routing.contains (M::eq))
                return false;
            c.set (pid::eqOn, 1.0f);
            return setRoleBand (c, current, 0, EqRole::air,
                                eqBand (EqBandType::highShelf, 5000.0f, -juce::jlimit (1.5f, 4.0f, 1.0f + 0.4f * (profile.topDb + 7.0f)), 0.7071f), true) >= 0;
        });

        // Tone after the dynamics: the high shelf (and a presence boost) moves into a second
        // EQ instance behind compressor, de-esser and resonance control. A boost before the
        // compressor makes it react to the highs and lifts the S sounds into the de-esser.
        add (de ("Höhen nach der Dynamik (EQ 2)"), [&] (MixPlan& c)
        {
            const auto air = roleBand (c, current, EqRole::air);
            const auto mid = roleBand (c, current, EqRole::autoMix);
            const float high = air.has_value() && air->on ? air->gain : 0.0f;
            const bool presenceBoost = mid.has_value() && mid->on && mid->gain > 0.2f && mid->freq >= 1800.0f;
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
            for (auto role : { EqRole::warmth, EqRole::autoMix, EqRole::air })
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
            if (presenceBoost)
            {
                setRoleBand (c, current, 1, EqRole::autoMix, *mid, true);
                auto flat = *mid;
                flat.gain = 0.0f;
                setRoleBand (c, current, 0, EqRole::autoMix, flat, false);
            }
            int after = -1;
            for (auto m : { M::comp, M::smash, M::deess, M::smooth })
                after = std::max (after, c.routing.indexOf (m));
            c.routing.insertAt (after + 1, M::eq, 1);
            return c.routing.contains (M::eq, 1);
        });

        // Effects drier / wetter
        add (de ("Effekte trockener"), [&] (MixPlan& c)
        {
            if (! c.on (pid::reverbOn) && ! c.on (pid::delayOn))
                return false;
            c.set (pid::reverbMix, c.get (pid::reverbMix) * 0.65f);
            c.set (pid::delayMix, c.get (pid::delayMix) * 0.65f);
            return true;
        });
        add (style == VocalStyle::sustained ? de ("Mehr Raum (Large Hall)") : de ("Mehr Raum"), [&] (MixPlan& c)
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
        add (de ("De-Esser stärker"), [&] (MixPlan& c)
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
                d->title = de ("Kompressor ") + juce::String (plan.get (pid::compRatio), 1) + de (":1, Schwelle ") + db (plan.get (pid::compThreshold));
            if (auto* d = plan.find (M::reverb))
            {
                static const char* types[] { "Room-Hall", "Plate-Hall", "Hall", "Large Hall", "Ambience", "Atmosphere", "Cinematic", "Ethereal" };
                d->title = juce::String (types[juce::jlimit (0, 7, (int) plan.get (pid::reverbType))]) + " "
                         + juce::String (juce::roundToInt (plan.get (pid::reverbMix))) + " %, "
                         + juce::String (plan.get (pid::reverbDecay), 1).replaceCharacter ('.', ',') + de (" s, geduckt");
            }
            if (plan.on (pid::smashOn) && plan.find (M::smash) == nullptr)
                plan.decisions.push_back ({ M::smash, false, de ("Parallelkompression ") + juce::String (juce::roundToInt (plan.get (pid::smashBlend))) + " % Blend",
                                            de ("Die Variante mit mehr Dichte schnitt in der Messung besser ab."), true });
            if (plan.on (pid::satOn) && plan.find (M::sat) == nullptr)
                plan.decisions.push_back ({ M::sat, false, de ("Sättigung Tape, ") + juce::String (juce::roundToInt (plan.get (pid::satMix))) + " %",
                                            de ("Leichte Obertöne - die Variante mit Sättigung schnitt in der Messung besser ab."), true });
            if (plan.routing.contains (M::eq, 1))
            {
                const float g = roleGain (plan, current, dsp::EqRole::air, 1);
                juce::String title = de ("EQ 2 nach der Dynamik");
                if (g > 0.0f)
                    title << de (": Höhen ") << db (g);
                plan.decisions.push_back ({ M::eq, false, title,
                                            de ("Die Höhenanhebung sitzt hinter Kompressor und De-Esser: Der Kompressor reagiert nicht mehr auf die Höhen, "
                                                "und die S-Laute werden vor der Anhebung kontrolliert."), true });
            }
            plan.notes.push_back (de ("Variante \"") + candidateName + de ("\" gewählt: Bewertung ") + juce::String (before.total(), 2)
                                  + " -> " + juce::String (after.total(), 2) + " (" + after.describe() + ").");
        }
    }

    //==========================================================================
    AutoMixResult AutoMixEngine::run (const CapturedAudio& audio, const std::map<juce::String, float>& current,
                                      const std::function<bool()>& shouldStop, const Progress& progress)
    {
        AutoMixResult r;
        const auto startMs = juce::Time::getMillisecondCounterHiRes();
        const auto stop = [&shouldStop] { return shouldStop && shouldStop(); };
        const auto report = [&progress] (float f, const char* stage) { if (progress) progress (f, juce::String::fromUTF8 (stage)); };

        report (0.05f, "Aufnahme wird analysiert");
        r.profile = VocalProfiler::analyse (audio, 5.0f, shouldStop);
        if (stop())
        {
            r.error = de ("Abgebrochen.");
            return r;
        }
        if (r.profile.voicedSeconds < 2.0f)
        {
            r.error = de ("Zu wenig Gesang erfasst - bitte die Vocal-Spur abspielen und erneut analysieren.");
            return r;
        }

        report (0.3f, "Mix wird geplant");
        r.plan = AutoMixPlanner::plan (r.profile, current);
        r.original = measure (audio);
        r.original.sibilanceVsVowelDb = r.profile.sibilanceVsVowelDb;
        r.original.sibilanceValid = r.profile.sibilanceValid;

        // ---- Verify the whole chain and adjust where the measurement is reliable
        auto& plan = r.plan;
        RenderMetrics m;
        for (int round = 0; round < 4; ++round)
        {
            if (stop())
            {
                r.error = de ("Abgebrochen.");
                return r;
            }
            report (0.35f + 0.12f * (float) round, "Mix wird geprüft");
            m = renderChecked (audio, current, plan, r.profile);
            ++r.iterations;
            bool changed = false;

            // Compressor: neither squashed nor idle
            if (plan.on (pid::compOn))
            {
                const float thr = plan.get (pid::compThreshold);
                if (m.compGrMedianDb > 6.5f)
                {
                    plan.set (pid::compThreshold, thr + std::min (4.0f, m.compGrMedianDb - 4.5f));
                    r.adjustments.push_back (de ("Kompressor entschärft (") + juce::String (m.compGrMedianDb, 1) + de (" dB Reduktion gemessen)."));
                    changed = true;
                }
                else if (m.compGrMedianDb < 0.8f && ! r.profile.heavilyCompressed && thr > -50.0f)
                {
                    plan.set (pid::compThreshold, thr - 2.0f);
                    r.adjustments.push_back (de ("Kompressor-Schwelle gesenkt (kaum Reduktion gemessen)."));
                    changed = true;
                }
            }

            // De-esser: S sounds controlled, but no lisp
            if (plan.on (pid::deessOn) && m.sibilanceValid && r.profile.sibilanceValid)
            {
                const float thr = plan.get (pid::deessThreshold);
                // Too strong in absolute terms, or lifted by the chain clearly above the original
                const bool tooStrong = m.sibilanceVsVowelDb > -3.0f
                                    || m.sibilanceVsVowelDb > std::max (-8.0f, r.original.sibilanceVsVowelDb + 2.0f);
                if (tooStrong && thr > -55.0f && plan.get (pid::deessRange) < 8.0f)
                {
                    plan.set (pid::deessThreshold, thr - 3.0f);
                    plan.set (pid::deessRange, std::min (10.0f, plan.get (pid::deessRange) + 2.0f));
                    r.adjustments.push_back (de ("De-Esser verstärkt (S-Laute nach der Kette noch ") + db (m.sibilanceVsVowelDb) + ").");
                    changed = true;
                }
                else if (m.sibilanceVsVowelDb < r.profile.sibilanceVsVowelDb - 10.0f)
                {
                    plan.set (pid::deessThreshold, thr + 3.0f);
                    r.adjustments.push_back (de ("De-Esser zurückgenommen (S-Laute wurden zu stark gedämpft)."));
                    changed = true;
                }
            }

            // The chain itself (compression, presence, saturation) can lift S sounds:
            // then a gentle de-esser is set up even if the recording did not need one
            if (! plan.on (pid::deessOn) && m.sibilanceValid
                && m.sibilanceVsVowelDb > std::max (-8.0f, r.original.sibilanceVsVowelDb + 2.0f))
            {
                AutoMixPlanner::configureDeEsser (plan, r.profile, current.count (pid::inputGain) ? current.at (pid::inputGain) : 0.0f, 4.0f);
                plan.decisions.push_back ({ dsp::ModuleId::deess, false, de ("De-Esser ab ") + juce::String (juce::roundToInt (plan.get (pid::deessFreq))) + " Hz, max. 4 dB",
                                            de ("Nach der Kette standen die S-Laute ") + db (m.sibilanceVsVowelDb - r.original.sibilanceVsVowelDb)
                                                + de (" stärker hervor als im Original - dezent ausgeglichen."), true });
                r.adjustments.push_back (de ("De-Esser ergänzt (die Kette hob die S-Laute an)."));
                changed = true;
            }

            // Brightness: the chain must not make an already bright voice brighter
            if (auto air = roleBand (plan, current, dsp::EqRole::air); m.topDb > r.original.topDb + 5.0f && air.has_value() && air->on && air->gain > 0.0f)
            {
                air->gain = std::max (0.0f, air->gain - 1.5f);
                setRoleBand (plan, current, 0, dsp::EqRole::air, *air, false);
                r.adjustments.push_back (de ("Höhenanhebung verringert (Kette wurde zu hell)."));
                changed = true;
            }
            // ... and must not dull it either
            if (m.topDb < r.original.topDb - 5.0f && plan.on (pid::smoothOn) && plan.get (pid::smoothAmount) > 20.0f)
            {
                plan.set (pid::smoothAmount, plan.get (pid::smoothAmount) - 10.0f);
                r.adjustments.push_back (de ("Resonanzkontrolle verringert (Höhen gingen verloren)."));
                changed = true;
            }

            // Low end: keep the body of the voice
            if (auto lowCut = roleBand (plan, current, dsp::EqRole::lowCut); m.bodyDb < r.original.bodyDb - 6.0f && lowCut.has_value() && lowCut->freq > 65.0f)
            {
                lowCut->freq *= 0.85f;
                setRoleBand (plan, current, 0, dsp::EqRole::lowCut, *lowCut, false);
                r.adjustments.push_back (de ("Low Cut gesenkt (zu viel Körper ging verloren)."));
                changed = true;
            }

            // Mono compatibility of the result
            if (m.monoLossDb < -1.5f && plan.on (pid::doublerOn))
            {
                plan.set (pid::doublerMix, plan.get (pid::doublerMix) * 0.6f);
                r.adjustments.push_back (de ("Doubler reduziert (Pegelverlust in Mono)."));
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
        const float target = targetLufs (r.original.lufs);
        {
            auto variants = candidatesFor (plan, r.profile, current);
            const auto score = [&] (const juce::String& name, const MixPlan& cand, bool isBase)
            {
                CandidateResult c;
                c.name = name;
                c.metrics = renderChecked (audio, current, cand, r.profile);
                c.routing = cand.routing;
                c.score = evaluate (c.metrics, r.original, r.profile, target, isBase, cand.on (pid::reverbOn) || cand.on (pid::delayOn));
                ++r.iterations;
                return c;
            };
            for (size_t i = 0; i < variants.size(); ++i)
            {
                if (stop())
                {
                    r.error = de ("Abgebrochen.");
                    return r;
                }
                report (0.55f + 0.25f * (float) i / (float) variants.size(), "Varianten werden verglichen");
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
                plan = variants[best].second;
                syncDecisions (plan, variants[best].first, r.candidates[0].score, r.candidates[best].score, current);
                r.adjustments.push_back (de ("Variante gewählt: ") + variants[best].first);
            }
        }

        // ---- Output level: match the target loudness, then keep peaks below -1 dBFS
        report (0.85f, "Pegel wird angepasst");
        m = renderChecked (audio, current, plan, r.profile);
        ++r.iterations;
        float out = plan.get (pid::outputGain) + (target - m.lufs);
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
        outDecision.title = de ("Ausgang ") + juce::String (m.lufs, 1) + " LUFS, Spitze " + db (m.peakDb);
        outDecision.reason = de ("Lautheit an das Original angeglichen (") + juce::String (r.original.lufs, 1)
                           + de (" LUFS), damit der Vergleich fair bleibt.")
                           + (peakLimited ? de (" Etwas leiser, um 1 dB Reserve zu halten.") : juce::String())
                           + de (" Clip Guard bleibt als Schutz aktiv.");
        plan.decisions.push_back (outDecision);

        report (1.0f, "Fertig");
        r.seconds = (juce::Time::getMillisecondCounterHiRes() - startMs) * 0.001;
        r.ok = true;
        return r;
    }
}
