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

        measureInto (m, out);
        m.compGrMedianDb = percentile (compGr, 0.5f);
        m.compGrMaxDb = compGr.empty() ? 0.0f : *std::max_element (compGr.begin(), compGr.end());
        m.deessGrMaxDb = deessMax;

        if (output != nullptr)
            *output = std::move (out);
        return m;
    }

    //==========================================================================
    AutoMixResult AutoMixEngine::run (const CapturedAudio& audio, const std::map<juce::String, float>& current,
                                      const std::function<bool()>& shouldStop, const Progress& progress)
    {
        AutoMixResult r;
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
            if (m.topDb > r.original.topDb + 5.0f && plan.get (pid::highGain) > 0.0f)
            {
                plan.set (pid::highGain, std::max (0.0f, plan.get (pid::highGain) - 1.5f));
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
            if (m.bodyDb < r.original.bodyDb - 6.0f && plan.get (pid::hpfFreq) > 65.0f)
            {
                plan.set (pid::hpfFreq, plan.get (pid::hpfFreq) * 0.85f);
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

        // ---- Output level: match the target loudness, then keep peaks below -1 dBFS
        report (0.85f, "Pegel wird angepasst");
        const float target = targetLufs (r.original.lufs);
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
        r.ok = true;
        return r;
    }
}
