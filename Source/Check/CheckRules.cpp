#include "CheckRules.h"
#include "../DSP/VocalAnalyzer.h"
#include "../Parameters/ParameterIDs.h"
#include "../Text/I18n.h"

// Source is compiled as UTF-8 (/utf-8): the texts are plain literals.
//
// The finding texts are built with i18n::both (German and English packed into one string):
// the analysis is kept by the processor and survives a language switch (the editor is rebuilt,
// the latest analysis is not), so the view picks the language at display with i18n::show.
namespace velvox::check
{
    namespace
    {
        using dsp::AnalysisResult;
        using dsp::Finding;
        using dsp::ModuleId;
        using K = ParamChange::Kind;
        using i18n::Language;

        const juce::String arrow = juce::String::fromUTF8 ("\xe2\x86\x92 ");

        juce::String num (float v, int decimals, Language language)
        {
            const float scale = std::pow (10.0f, (float) decimals);
            const float x = std::round (v * scale) / scale;
            const auto body = decimals == 0 ? juce::String (juce::roundToInt (std::abs (x)))
                                            : i18n::number (std::abs (x), decimals, language);
            return (x < 0.0f ? juce::String::fromUTF8 ("\xe2\x88\x92") : juce::String()) + body;
        }

        juce::String hzIn (float hz, Language language)
        {
            if (hz >= 1000.0f)
            {
                const float k = std::round (hz / 100.0f) / 10.0f;
                return (std::abs (k - std::round (k)) < 0.05f ? juce::String (juce::roundToInt (k)) : num (k, 1, language)) + " kHz";
            }
            return juce::String (juce::roundToInt (hz)) + " Hz";
        }

        juce::String dbIn (float db, bool withPlus, Language language)
        {
            const float v = std::round (db * 2.0f) / 2.0f;
            const bool whole = std::abs (v - std::round (v)) < 0.01f;
            const auto body = whole ? juce::String (juce::roundToInt (std::abs (v))) : num (std::abs (v), 1, language);
            const juce::String sign = v < 0.0f ? juce::String::fromUTF8 ("\xe2\x88\x92") : (withPlus && v > 0.0f ? juce::String ("+") : juce::String());
            return sign + body + (withPlus ? " dB" : "");
        }

        // A value of a finding text in both languages (numbers differ in the decimal separator)
        struct V
        {
            V (int i) : de (juce::String (i)), en (juce::String (i)) {}
            V (const char* german, const char* english) : de (juce::String::fromUTF8 (german)), en (juce::String::fromUTF8 (english)) {}
            V (juce::String german, juce::String english, bool) : de (std::move (german)), en (std::move (english)) {}
            juce::String de, en;
        };
        V numV (float v, int decimals)   { return { num (v, decimals, Language::german), num (v, decimals, Language::english), true }; }
        V hzV (float hz)                 { return { hzIn (hz, Language::german), hzIn (hz, Language::english), true }; }
        V dbV (float db)                 { return { dbIn (db, true, Language::german), dbIn (db, true, Language::english), true }; }
        V dbfsV (float dbfs)
        {
            // Peaks above full scale must read "+3", never "3" (which looks like a value below 0)
            return { dbIn (dbfs, true, Language::german).upToFirstOccurrenceOf (" dB", false, false),
                     dbIn (dbfs, true, Language::english).upToFirstOccurrenceOf (" dB", false, false), true };
        }

        // A finding text in both languages (packed, see above): "{0}", "{1}" ... replaced by the values
        juce::String txt (const char* german, const char* english, std::initializer_list<V> values = {})
        {
            auto g = juce::String::fromUTF8 (german), e = juce::String::fromUTF8 (english);
            int i = 0;
            for (const auto& v : values)
            {
                const auto key = "{" + juce::String (i++) + "}";
                g = g.replace (key, v.de);
                e = e.replace (key, v.en);
            }
            return i18n::both (g, e);
        }

        // Rounds an amount to a 0.5 dB step (the texts and the planner use the same value)
        float halfDb (float v) { return std::round (v * 2.0f) / 2.0f; }

        float conf (float value, float threshold, float span)
        {
            return juce::jlimit (0.05f, 0.95f, 0.5f + 0.45f * (value - threshold) / std::max (1.0e-3f, span));
        }

        Severity severity (float value, float threshold, float span)
        {
            const float x = (value - threshold) / std::max (1.0e-3f, span);
            return x < 0.5f ? Severity::low : (x < 1.5f ? Severity::medium : Severity::high);
        }

        float weight (Severity s) { return s == Severity::high ? 22.0f : (s == Severity::medium ? 12.0f : 5.0f); }

        struct Rules
        {
            AnalysisResult& r;
            const CheckTargets& t;
            float material = 1.0f;
            std::vector<Finding> all;

            Finding& add (const juce::String& id, Category category, float confidence, Severity level, std::optional<ModuleId> module,
                          juce::String problem, juce::String fix, juce::String detail, juce::String why, bool scaleByMaterial = true)
            {
                Finding f;
                f.id = id;
                f.category = category;
                f.confidence = juce::jlimit (0.0f, 0.99f, confidence * (scaleByMaterial ? material : 1.0f));
                f.level = level;
                f.module = module;
                f.problem = std::move (problem);
                f.fix = arrow + fix;
                f.title = f.problem;
                f.advice = f.fix;
                f.detail = std::move (detail);
                f.why = std::move (why);
                f.certainty = f.confidence >= 0.75f ? Finding::Certainty::sure : Finding::Certainty::possible;
                f.severity = weight (level) * f.confidence;
                all.push_back (std::move (f));
                return all.back();
            }

            // Deviation from the voice template: strongest local peak in [lo, hi)
            struct Bump { float hz = 0.0f, db = -100.0f, z = -100.0f; int band = -1; };
            // Strongest local peak in [lo, hi) of the deviation in units of the spread of the
            // template voices (bands where voices differ a lot need a larger deviation)
            Bump deviationPeak (float lo, float hi, float minDb) const
            {
                Bump best;
                const auto& spread = voiceTemplateSpread (r.highVoice);
                const auto& d = r.deviationDb;
                if (d.size() != (size_t) dsp::VocalAnalyzer::numBands)
                    return best;
                for (int b = 1; b + 1 < (int) d.size(); ++b)
                {
                    const float c = dsp::VocalAnalyzer::bandCentre (b);
                    if (c < lo || c >= hi || r.toneDb.size() != d.size() || r.toneDb[(size_t) b] < -190.0f)
                        continue;
                    const float z = d[(size_t) b] / std::max (1.5f, spread[(size_t) b]);
                    if (d[(size_t) b] >= minDb && z > best.z && d[(size_t) b] >= d[(size_t) b - 1] && d[(size_t) b] >= d[(size_t) b + 1])
                        best = { c, d[(size_t) b], z, b };
                }
                return best;
            }

            // Q of a bump: width where it is 3 dB below its top (in the deviation curve)
            float bumpQ (const Bump& p, const std::vector<float>& curve) const
            {
                if (p.band < 0)
                    return 1.4f;
                int lo = p.band, hi = p.band;
                while (lo > 0 && curve[(size_t) lo - 1] > p.db - 3.0f && p.band - lo < 18) --lo;
                while (hi + 1 < (int) curve.size() && curve[(size_t) hi + 1] > p.db - 3.0f && hi - p.band < 18) ++hi;
                const float octaves = std::max (1.0f / 6.0f, (float) (hi - lo + 1) / 12.0f);
                const float ratio = std::pow (2.0f, octaves);
                return juce::jlimit (0.7f, 3.0f, std::sqrt (ratio) / (ratio - 1.0f));
            }

            void level()
            {
                if (r.clipEvents > 0)
                    add ("clipping", Category::level, 0.99f, Severity::high, std::nullopt,
                         txt ("Übersteuert ({0}×)", "Clipping ({0}×)", { r.clipEvents }),
                         txt ("Aufnahme- oder Spurpegel senken", "Lower the recording or track level"),
                         txt ("{0} Stelle(n) mit Samples auf Vollaussteuerung an dieser Stelle der Kette.",
                              "{0} spot(s) with samples at full scale at this point of the chain.", { r.clipEvents }),
                         txt ("Übersteuerung ist Verzerrung, die kein Modul danach entfernen kann.",
                              "Clipping is distortion that no module after it can remove."), false);
                else if (r.peakDb > t.peakMax)
                {
                    const float amount = halfDb (std::min (-0.5f, -3.0f - r.peakDb));
                    auto& f = add ("headroom", Category::level, conf (r.peakDb, t.peakMax, 0.9f), severity (r.peakDb, t.peakMax, 0.6f), std::nullopt,
                                   txt ("Kaum Headroom ({0} dBFS)", "Little headroom ({0} dBFS)", { dbfsV (r.peakDb) }),
                                   txt ("Input {0}", "Input {0}", { dbV (amount) }),
                                   txt ("Höchster Pegel {0} dBFS.", "Highest level {0} dBFS.", { dbfsV (r.peakDb) }),
                                   txt ("Ohne Reserve übersteuert jede spätere Anhebung.", "Without headroom, any later boost clips."), false);
                    f.action.params.push_back ({ pid::inputGain, amount, K::amount, 0.0f });
                }

                if (r.enoughMaterial && r.loudnessLufs > -99.0f && r.loudnessLufs < t.loudnessMin)
                {
                    // Bring the voice to about -20 LUFS, but keep 3 dB headroom
                    const float amount = halfDb (juce::jlimit (0.0f, 18.0f, std::min (-20.0f - r.loudnessLufs, -3.0f - r.peakDb)));
                    auto& f = add ("quiet", Category::level, conf (-r.loudnessLufs, -t.loudnessMin, 8.0f), severity (-r.loudnessLufs, -t.loudnessMin, 6.0f),
                                   std::nullopt, txt ("Sehr leise ({0} LUFS)", "Very quiet ({0} LUFS)", { numV (r.loudnessLufs, 0) }),
                                   amount >= 1.0f ? txt ("Input {0}", "Input {0}", { dbV (amount) })
                                                  : txt ("Spurpegel anheben (Spitzen schon hoch)", "Raise the track level (peaks already high)"),
                                   txt ("Lautheit der Stimme {0} LUFS.", "Loudness of the voice {0} LUFS.", { numV (r.loudnessLufs, 1) }),
                                   txt ("Sehr leise Signale bringen Rauschen und Schwellen der Module aus dem Gleichgewicht.",
                                        "Very quiet signals throw noise and the thresholds of the modules off balance."));
                    if (amount >= 1.0f)
                        f.action.params.push_back ({ pid::inputGain, amount, K::amount, 0.0f });
                }
            }

            void lowEnd()
            {
                // Low cut below the voice: never above 70 % of its lowest notes
                const float lowest = r.pitchValid ? r.f0LowHz : 150.0f;
                if (r.rumbleDb > t.rumbleMax + 2.0f - 6.0f)
                {
                    const float threshold = t.rumbleMax + 2.0f;
                    const float fc = std::round (juce::jlimit (60.0f, 150.0f, std::min (r.highVoice ? 120.0f : 90.0f, 0.7f * lowest)));
                    const bool steep = r.rumbleDb > t.rumbleMax + 8.0f;
                    const float excess = r.rumbleDb - t.rumbleMax;
                    auto& f = add ("rumble", Category::lowEnd, conf (r.rumbleDb, threshold, 8.0f), severity (r.rumbleDb, threshold, 6.0f), ModuleId::eq,
                                   txt ("Rumpeln unter 90 Hz ({0})", "Rumble below 90 Hz ({0})", { dbV (excess) }),
                                   steep ? txt ("Low Cut {0}, 24 dB/Okt.", "Low Cut {0}, 24 dB/oct.", { hzV (fc) })
                                         : txt ("Low Cut {0}", "Low Cut {0}", { hzV (fc) }),
                                   txt ("40–90 Hz liegt {0} gegenüber dem Kern der Stimme (Ziel unter {1}).",
                                        "40–90 Hz sits at {0} relative to the core of the voice (target below {1}).", { dbV (r.rumbleDb), dbV (t.rumbleMax) }),
                                   txt ("Rumpeln und Trittschall füllen den Bassbereich von Kick und Bass und lassen Kompressoren pumpen.",
                                        "Rumble and footsteps fill the low end of kick and bass and make compressors pump."));
                    f.freqHz = fc;
                    f.action.modules.push_back (ModuleId::eq);
                    f.action.eq.push_back ({ EqRole::lowcut, EqShape::lowCut, fc, 0.0f, 0.7071f, steep ? 24 : 12 });
                }

                if (r.plosiveCount >= 2)
                {
                    const float fc = std::round (juce::jlimit (80.0f, 150.0f, std::min (120.0f, 0.8f * lowest)));
                    auto& f = add ("plosives", Category::lowEnd, conf (r.plosivesPerMinute, 5.0f, 10.0f), severity (r.plosivesPerMinute, 5.0f, 8.0f), ModuleId::eq,
                                   txt ("Plosive / Popps ({0}×)", "Plosives / pops ({0}×)", { r.plosiveCount }),
                                   txt ("Low Cut {0}, 24 dB/Okt.", "Low Cut {0}, 24 dB/oct.", { hzV (fc) }),
                                   txt ("{0} tieffrequente Stöße ({1} pro Minute Stimme).", "{0} low-frequency bursts ({1} per minute of voice).",
                                        { r.plosiveCount, numV (r.plosivesPerMinute, 1) }),
                                   txt ("P- und B-Laute treffen die Membran: kurze Bass-Stöße, die Kompressoren auslösen.",
                                        "P and B sounds hit the diaphragm: short bass bursts that trigger compressors."));
                    f.freqHz = fc;
                    f.action.modules.push_back (ModuleId::eq);
                    f.action.eq.push_back ({ EqRole::lowcut, EqShape::lowCut, fc, 0.0f, 0.7071f, 24 });
                }
            }

            void tonal()
            {
                // ---- Broad excesses against the voice template (where the template is reliable)
                const auto bumpFinding = [this] (const char* id, V word, EqRole role, float lo, float hi, float minDb)
                {
                    // Not below the lowest sung notes (the fundamentals are the voice itself)
                    const float from = std::max (lo, r.pitchValid ? r.f0LowHz * 1.12f : lo);
                    const auto p = deviationPeak (from, hi, minDb);
                    if (p.band < 0 || p.z < t.devZ - 1.0f)
                        return;
                    const float gain = -halfDb (juce::jlimit (1.5f, 6.0f, 0.75f * p.db));
                    const float q = bumpQ (p, r.deviationDb);
                    const float hz = p.hz;
                    auto& f = add (id, Category::tonal, conf (p.z, t.devZ, 1.5f), severity (p.db, minDb + 1.5f, 3.0f), ModuleId::eq,
                                   txt ("{0} um {1} ({2})", "{0} around {1} ({2})", { word, hzV (hz), dbV (halfDb (p.db)) }),
                                   txt ("EQ {0} bei {1}", "EQ {0} at {1}", { dbV (gain), hzV (hz) }),
                                   txt ("Um {0} {1} über einer typischen trockenen Stimme (1/3 Oktave).",
                                        "Around {0}, {1} above a typical dry voice (1/3 octave).", { hzV (hz), dbV (p.db) }),
                                   juce::String (id) == "mud"
                                       ? txt ("Zu viel 200–500 Hz klingt dumpf und verdeckt Bass und Kick.",
                                              "Too much 200–500 Hz sounds muddy and masks bass and kick.")
                                       : txt ("Zu viel 500 Hz–1 kHz klingt nach Pappkarton und nimmt der Stimme die Klarheit.",
                                              "Too much 500 Hz–1 kHz sounds like a cardboard box and takes the clarity out of the voice."));
                    f.freqHz = hz;
                    f.action.modules.push_back (ModuleId::eq);
                    f.action.eq.push_back ({ role, EqShape::bell, hz, gain, q, 12 });
                };
                bumpFinding ("mud", { "Mulm", "Mud" }, EqRole::mud, 200.0f, 500.0f, t.devMud);
                bumpFinding ("boxy", { "Boxig", "Boxy" }, EqRole::mud, 500.0f, 1000.0f, t.devBoxy);

                // ---- Thin / dull / bright (band windows of the objective)
                if (r.bodyDb < t.body.lo)
                {
                    const float deficit = t.body.lo - r.bodyDb;
                    const float gain = halfDb (juce::jlimit (1.0f, 3.0f, 0.5f * deficit));
                    auto& f = add ("thin", Category::tonal, conf (deficit, 1.5f, 6.0f), severity (deficit, 1.5f, 5.0f), ModuleId::eq,
                                   txt ("Dünn: wenig Körper ({0})", "Thin: little body ({0})", { dbV (-deficit) }),
                                   txt ("EQ {0} bei 180 Hz", "EQ {0} at 180 Hz", { dbV (gain) }),
                                   txt ("100–300 Hz liegt {0} gegenüber dem Kern der Stimme (Ziel ab {1}).",
                                        "100–300 Hz sits at {0} relative to the core of the voice (target from {1}).", { dbV (r.bodyDb), dbV (t.body.lo) }),
                                   txt ("100–300 Hz geben der Stimme Gewicht und Wärme.", "100–300 Hz give the voice weight and warmth."));
                    f.freqHz = 180.0f;
                    f.action.modules.push_back (ModuleId::eq);
                    f.action.eq.push_back ({ EqRole::warmth, EqShape::lowShelf, 180.0f, gain, 0.7071f, 12 });
                }

                if (r.topDb < t.top.lo)
                {
                    const float deficit = t.top.lo - r.topDb;
                    const float gain = halfDb (juce::jlimit (1.0f, 4.0f, 0.5f * deficit));
                    auto& f = add ("dull", Category::tonal, conf (deficit, 1.0f, 6.0f), severity (deficit, 1.0f, 5.0f), ModuleId::eq,
                                   txt ("Dumpf: wenig Höhen ({0})", "Dull: lacking highs ({0})", { dbV (-deficit) }),
                                   txt ("EQ {0} Höhen ab 9 kHz", "EQ {0} highs from 9 kHz", { dbV (gain) }),
                                   txt ("Alles über 2 kHz liegt {0} gegenüber dem Kern der Stimme (Ziel ab {1}).",
                                        "Everything above 2 kHz sits at {0} relative to the core of the voice (target from {1}).", { dbV (r.topDb), dbV (t.top.lo) }),
                                   txt ("Konsonanten und Air tragen die Verständlichkeit; eine dumpfe Stimme versinkt hinter Synths und Hats.",
                                        "Consonants and air carry intelligibility; a dull voice sinks behind synths and hats."));
                    f.freqHz = 9000.0f;
                    f.action.modules.push_back (ModuleId::eq);
                    f.action.eq.push_back ({ EqRole::air, EqShape::highShelf, 9000.0f, gain, 0.7071f, 12 });
                }
                else if (r.topDb > t.top.hi)
                {
                    const float excess = r.topDb - t.top.hi;
                    const float gain = -halfDb (juce::jlimit (1.0f, 3.0f, 0.5f * excess));
                    auto& f = add ("bright", Category::tonal, conf (excess, 1.0f, 5.0f), severity (excess, 1.0f, 4.0f), ModuleId::eq,
                                   txt ("Sehr hell ({0})", "Very bright ({0})", { dbV (excess) }),
                                   txt ("EQ {0} Höhen ab 9 kHz", "EQ {0} highs from 9 kHz", { dbV (gain) }),
                                   txt ("Alles über 2 kHz liegt {0} gegenüber dem Kern der Stimme (Ziel bis {1}).",
                                        "Everything above 2 kHz sits at {0} relative to the core of the voice (target up to {1}).", { dbV (r.topDb), dbV (t.top.hi) }),
                                   txt ("Zu viel Höhen klingen bei Club-Lautstärke spitz und ermüdend.",
                                        "Too much top end sounds piercing and tiring at club volume."));
                    f.freqHz = 9000.0f;
                    f.action.modules.push_back (ModuleId::eq);
                    f.action.eq.push_back ({ EqRole::air, EqShape::highShelf, 9000.0f, gain, 0.7071f, 12 });
                }
            }

            void harshness()
            {
                // ---- Narrow resonances (frequency specific)
                std::vector<float> resonanceHz;
                for (auto& res : r.resonances)
                {
                    if (res.excessDb < t.resonanceMin - 2.0f)
                        continue;
                    const bool harshRange = res.hz >= 2500.0f && res.hz <= 5000.0f;
                    const float gain = -halfDb (juce::jlimit (2.0f, 8.0f, 0.6f * res.excessDb));
                    // A peak with few pitch changes can also be a partial: less confidence
                    const float reliability = r.pitchValid && r.pitchSpreadSemitones < 3.0f ? 0.8f : 1.0f;
                    auto& f = add ("resonance." + juce::String (thirdOctaveCentre (res.hz)), harshRange ? Category::harshness : Category::resonance,
                                   conf (res.excessDb, t.resonanceMin, 6.0f) * reliability, severity (res.excessDb, t.resonanceMin, 5.0f),
                                   ModuleId::eq, txt ("Resonanz bei {0} ({1})", "Resonance at {0} ({1})", { hzV (res.hz), dbV (halfDb (res.excessDb)) }),
                                   txt ("EQ {0} bei {1} (schmal)", "EQ {0} at {1} (narrow)", { dbV (gain), hzV (res.hz) }),
                                   txt ("Schmale Spitze um {0}, {1} über ihrer Umgebung (±1/2 Oktave).",
                                        "Narrow peak around {0}, {1} above its surroundings (±1/2 octave).", { hzV (res.hz), dbV (res.excessDb) }),
                                   txt ("Eine schmale Spitze klingelt bei jedem Wort, das sie trifft (Raum, Mikrofon oder Stimme).",
                                        "A narrow peak rings on every word that hits it (room, mic or voice)."));
                    f.freqHz = res.hz;
                    f.action.modules.push_back (ModuleId::eq);
                    f.action.eq.push_back ({ res.hz < 1000.0f ? EqRole::mud : EqRole::presence, EqShape::bell, res.hz, gain, 6.0f, 12 });
                    resonanceHz.push_back (res.hz);
                }

                // ---- Broad harshness: 2-5 kHz far forward (objective window), placed at the
                // strongest point of 2.5-5 kHz over the trend of the spectrum around it
                if (r.presenceDb > t.presenceMax - 1.5f)
                {
                    const float excess = r.presenceDb - t.presenceMax;
                    float hz = 3150.0f;
                    if (r.smoothDb.size() == (size_t) dsp::VocalAnalyzer::numBands)
                    {
                        // Least-squares line of the smoothed spectrum over 1-10 kHz (log frequency)
                        double sx = 0, sy = 0, sxx = 0, sxy = 0;
                        int n = 0;
                        for (int b = 0; b < dsp::VocalAnalyzer::numBands; ++b)
                        {
                            const float c = dsp::VocalAnalyzer::bandCentre (b);
                            if (c < 1000.0f || c > 10000.0f || r.smoothDb[(size_t) b] < -190.0f)
                                continue;
                            const double x = std::log2 (c), y = r.smoothDb[(size_t) b];
                            sx += x; sy += y; sxx += x * x; sxy += x * y;
                            ++n;
                        }
                        if (n > 4)
                        {
                            const double slope = (n * sxy - sx * sy) / std::max (1.0e-9, n * sxx - sx * sx);
                            const double icpt = (sy - slope * sx) / n;
                            float best = -1.0e9f;
                            for (int b = 0; b < dsp::VocalAnalyzer::numBands; ++b)
                            {
                                const float c = dsp::VocalAnalyzer::bandCentre (b);
                                if (c < 2500.0f || c > 5000.0f || r.smoothDb[(size_t) b] < -190.0f)
                                    continue;
                                const float above = r.smoothDb[(size_t) b] - (float) (icpt + slope * std::log2 (c));
                                if (above > best)
                                {
                                    best = above;
                                    hz = c;
                                }
                            }
                        }
                    }
                    bool covered = false;
                    for (float rh : resonanceHz)
                        covered = covered || std::abs (std::log2 (rh / hz)) <= 1.0f / 3.0f;
                    if (! covered)
                    {
                        const float gain = -halfDb (juce::jlimit (1.5f, 5.0f, 0.75f * std::max (0.0f, excess) + 1.5f));
                        auto& f = add ("harsh", Category::harshness, conf (excess, 1.5f, 4.0f), severity (excess, 1.5f, 3.0f), ModuleId::eq,
                                       txt ("Scharf um {0} ({1})", "Harsh around {0} ({1})", { hzV (hz), dbV (halfDb (excess)) }),
                                       txt ("EQ {0} bei {1}", "EQ {0} at {1}", { dbV (gain), hzV (hz) }),
                                       txt ("2–5 kHz liegt {0} gegenüber dem Kern der Stimme (Ziel bis {1}).",
                                            "2–5 kHz sits at {0} relative to the core of the voice (target up to {1}).", { dbV (r.presenceDb), dbV (t.presenceMax) }),
                                       txt ("Bei 2–5 kHz ist das Ohr am empfindlichsten: zu viel klingt stechend und ermüdet bei Club-Lautstärke.",
                                            "The ear is most sensitive at 2–5 kHz: too much sounds piercing and gets tiring at club volume."));
                        f.freqHz = hz;
                        f.action.modules.push_back (ModuleId::eq);
                        f.action.eq.push_back ({ EqRole::presence, EqShape::bell, hz, gain, 1.2f, 12 });
                    }
                }
            }

            void sibilance()
            {
                if (! r.sibilancePeakValid)
                    return;
                const float threshold = t.sibilancePeakMax + 1.0f;
                if (r.sibilancePeakDb < threshold - 2.0f)
                    return;
                const float excess = r.sibilancePeakDb - t.sibilancePeakMax;
                const float sHz = r.sibilanceHz > 0.0f ? r.sibilanceHz : 7000.0f;
                const float detector = std::round (juce::jlimit (4000.0f, 8000.0f, 0.78f * sHz) / 100.0f) * 100.0f;
                const float range = halfDb (juce::jlimit (3.0f, 10.0f, 3.0f + excess));
                // Start reducing a little below the level of the S sounds
                const float thresholdDb = std::round (juce::jlimit (-60.0f, -6.0f, r.voiceLevelDb + (r.sibilanceValid ? r.sibilanceVsVowelDb : -8.0f) - 6.0f));
                auto& f = add ("sibilance", Category::sibilance, conf (r.sibilancePeakDb, threshold, 4.0f), severity (r.sibilancePeakDb, threshold, 3.0f),
                               ModuleId::deess, txt ("Scharfe S-Laute ({0})", "Harsh sibilance ({0})", { dbV (halfDb (excess)) }),
                               txt ("De-Esser bei {0}", "De-Esser at {0}", { hzV (detector) }),
                               txt ("Die lautesten S-Momente (5–10 kHz) liegen {0} gegenüber den Vokalen (Ziel bis {1}), Schwerpunkt um {2}.",
                                    "The loudest S moments (5–10 kHz) sit at {0} relative to the vowels (target up to {1}), centered around {2}.",
                                    { dbV (r.sibilancePeakDb), dbV (t.sibilancePeakMax), hzV (sHz) }),
                               txt ("Laute S-Laute stechen bei hoher Lautstärke und werden durch Kompression und Hall noch schärfer.",
                                    "Loud S sounds pierce at high volume and get even sharper with compression and reverb."));
                f.freqHz = sHz;
                f.action.modules.push_back (ModuleId::deess);
                f.action.params.push_back ({ pid::deessMode, 0.0f, K::set, 0.0f });
                f.action.params.push_back ({ pid::deessFreq, detector, K::set, 0.0f });
                f.action.params.push_back ({ pid::deessThreshold, thresholdDb, K::atMost, 0.0f });
                f.action.params.push_back ({ pid::deessRange, range, K::amount, 0.0f });
            }

            void dynamics()
            {
                bool dynamicsFound = false;
                if (r.phraseValid && r.loudnessRangeDb > t.phrase.hi)
                {
                    const float threshold = t.phrase.hi + 1.0f;
                    if (r.loudnessRangeDb >= threshold - 2.0f)
                    {
                        dynamicsFound = r.loudnessRangeDb >= threshold;
                        auto& f = add ("dynamics", Category::dynamics, conf (r.loudnessRangeDb, threshold, 6.0f), severity (r.loudnessRangeDb, threshold, 5.0f),
                                       ModuleId::ride, txt ("Pegel schwankt stark ({0} dB)", "Level varies a lot ({0} dB)", { numV (r.loudnessRangeDb, 0) }),
                                       txt ("Level Ride an", "Level Ride on"),
                                       txt ("Laute und leise Wörter liegen {0} dB auseinander (Ziel {1}–{2} dB).",
                                            "Loud and quiet words are {0} dB apart (target {1}–{2} dB).",
                                            { numV (r.loudnessRangeDb, 1), numV (t.phrase.lo, 0), numV (t.phrase.hi, 0) }),
                                       txt ("Leise Wörter verschwinden hinter dem Beat, laute springen heraus.",
                                            "Quiet words disappear behind the beat, loud ones jump out."));
                        f.action.modules.push_back (ModuleId::ride);
                        f.action.params.push_back ({ pid::rideTarget, std::round (juce::jlimit (-40.0f, -6.0f, r.voiceLevelDb + 1.0f)), K::set, 0.0f });
                        f.action.params.push_back ({ pid::rideAmount, std::round (juce::jlimit (50.0f, 90.0f, 50.0f + 5.0f * (r.loudnessRangeDb - t.phrase.hi))), K::atLeast, 0.0f });
                    }
                }

                if (r.lraValid && ! dynamicsFound && r.lraLu > t.lraMax - 1.0f)
                {
                    const float threshold = t.lraMax + 1.0f;
                    auto& f = add ("lra", Category::dynamics, conf (r.lraLu, threshold, 6.0f), severity (r.lraLu, threshold, 5.0f), ModuleId::ride,
                                   txt ("Große Lautstärkesprünge (LRA {0} LU)", "Big loudness jumps (LRA {0} LU)", { numV (r.lraLu, 0) }),
                                   txt ("Level Ride an (Phrasen)", "Level Ride on (phrases)"),
                                   txt ("Lautheitsbereich {0} LU (Ziel bis {1} LU).", "Loudness range {0} LU (target up to {1} LU).",
                                        { numV (r.lraLu, 1), numV (t.lraMax, 0) }),
                                   txt ("Ganze Passagen sind deutlich leiser oder lauter als der Rest.",
                                        "Whole passages are much quieter or louder than the rest."));
                    f.action.modules.push_back (ModuleId::ride);
                    f.action.params.push_back ({ pid::rideTarget, std::round (juce::jlimit (-40.0f, -6.0f, r.voiceLevelDb + 1.0f)), K::set, 0.0f });
                    f.action.params.push_back ({ pid::ridePhrase, 70.0f, K::atLeast, 0.0f });
                    f.action.params.push_back ({ pid::rideAmount, 60.0f, K::atLeast, 0.0f });
                }

                if (r.crestDb > t.crestMax - 1.0f)
                {
                    const float threshold = t.crestMax + 1.0f;
                    auto& f = add ("crest", Category::dynamics, conf (r.crestDb, threshold, 5.0f), severity (r.crestDb, threshold, 4.0f), ModuleId::comp,
                                   txt ("Starke Pegelspitzen (Crest {0} dB)", "Strong peaks (crest {0} dB)", { numV (r.crestDb, 0) }),
                                   txt ("Kompressor 3:1", "Compressor 3:1"),
                                   txt ("Spitze zu RMS {0} dB im Median (Ziel bis {1} dB).", "Peak to RMS {0} dB median (target up to {1} dB).",
                                        { numV (r.crestDb, 1), numV (t.crestMax, 0) }),
                                   txt ("Einzelne Silben springen weit über den Rest und bestimmen den Pegel.",
                                        "Single syllables jump far above the rest and set the level."));
                    f.action.modules.push_back (ModuleId::comp);
                    f.action.params.push_back ({ pid::compThreshold, std::round (juce::jlimit (-50.0f, -6.0f, r.voiceLevelDb + 4.0f)), K::atMost, 0.0f });
                    f.action.params.push_back ({ pid::compRatio, 3.0f, K::atLeast, 1.0f });
                    f.action.params.push_back ({ pid::compAttack, 5.0f, K::set, 0.0f });
                    f.action.params.push_back ({ pid::compRelease, 120.0f, K::set, 0.0f });
                }
                else if (r.phraseValid && r.crestDb < t.crestMin + 1.0f && r.loudnessRangeDb < t.phrase.lo + 1.0f)
                {
                    const float deficit = t.crestMin - r.crestDb;
                    auto& f = add ("overcompressed", Category::dynamics, conf (deficit, 0.5f, 2.0f), severity (deficit, 0.5f, 2.0f), ModuleId::comp,
                                   txt ("Überkomprimiert (Crest {0} dB)", "Overcompressed (crest {0} dB)", { numV (r.crestDb, 0) }),
                                   txt ("Kompression zurücknehmen", "Back off the compression"),
                                   txt ("Nur {0} dB Pegelunterschied und {1} dB Crest-Faktor.", "Only {0} dB level difference and {1} dB crest factor.",
                                        { numV (r.loudnessRangeDb, 1), numV (r.crestDb, 1) }),
                                   txt ("Mehr Kompression bringt hier nur Pumpen und Atemgeräusche, keine Lautheit.",
                                        "More compression only brings pumping and breath noise here, no loudness."));
                    // Only modules that are on (no module is switched on for this)
                    f.action.params.push_back ({ pid::compRatio, -1.5f, K::amount, 1.0f });
                    f.action.params.push_back ({ pid::smashBlend, -15.0f, K::amount, 0.0f });
                }
            }

            void stereo()
            {
                if (! r.stereo)
                    return;
                const float corrDeficit = t.correlationMin - r.correlation;
                if (corrDeficit > -0.1f || r.monoLossDb > 3.0f)
                {
                    const float c = std::max (conf (corrDeficit, 0.0f, 0.4f), conf (r.monoLossDb, 3.0f, 6.0f));
                    auto& f = add ("mono", Category::stereo, c, r.correlation < 0.0f ? Severity::high : Severity::medium, ModuleId::choir,
                                   txt ("Mono-Problem (Korrelation {0})", "Mono problem (correlation {0})", { numV (r.correlation, 2) }),
                                   txt ("Breite 70 %", "Width 70 %"),
                                   txt ("Korrelation links/rechts {0}, Mono-Verlust {1} dB.", "Left/right correlation {0}, mono loss {1} dB.",
                                        { numV (r.correlation, 2), numV (r.monoLossDb, 1) }),
                                   txt ("Club-Anlagen und Handys spielen oft mono: Teile der Stimme löschen sich dort aus.",
                                        "Club systems and phones often play mono: parts of the voice cancel out there."));
                    f.action.params.push_back ({ pid::stereoWidth, 70.0f, K::atMost, 100.0f });
                    f.action.params.push_back ({ pid::choirWidth, -30.0f, K::amount, 0.0f });
                    f.action.params.push_back ({ pid::doublerMonoSafe, 80.0f, K::atLeast, 0.0f });
                }
            }

            void noiseAndSpace()
            {
                if (r.noiseFloorDb > -199.0f && r.snrDb < t.snrMin + 2.0f)
                {
                    const float deficit = t.snrMin - r.snrDb;
                    auto& f = add ("noise", Category::noise, conf (deficit, 0.0f, 8.0f), severity (deficit, 0.0f, 8.0f), ModuleId::ride,
                                   txt ("Rauschen in Pausen ({0} dB Abstand)", "Noise in the gaps ({0} dB margin)", { numV (r.snrDb, 0) }),
                                   txt ("Rauschen nicht mit anheben (Ride-Schwelle)", "Don't lift the noise (Ride threshold)"),
                                   txt ("Die Pausen liegen bei {0} dBFS, nur {1} dB unter der Stimme.", "The gaps sit at {0} dBFS, only {1} dB below the voice.",
                                        { numV (r.noiseFloorDb, 1), numV (r.snrDb, 1) }),
                                   txt ("Jedes Dynamikmodul hebt das Rauschen zwischen den Wörtern an.",
                                        "Every dynamics module lifts the noise between the words."));
                    // Only when LEVEL RIDE is on (it is not switched on for this)
                    f.action.params.push_back ({ pid::rideSilence, std::round (juce::jlimit (-80.0f, -30.0f, r.noiseFloorDb + 6.0f)), K::atLeast, 0.0f });
                }

                if (r.tailSeconds > t.tailMax - 0.2f)
                {
                    auto& f = add ("space", Category::space, conf (r.tailSeconds, t.tailMax, 1.0f), severity (r.tailSeconds, t.tailMax, 0.8f), ModuleId::reverb,
                                   txt ("Verhallt (Ausklang {0} s)", "Too much reverb (tail {0} s)", { numV (r.tailSeconds, 1) }),
                                   txt ("Hall-/Delay-Mix senken", "Lower the reverb/delay mix"),
                                   txt ("Nach Wörtern dauert es etwa {0} s bis −30 dB (Ziel bis {1} s).",
                                        "After words it takes about {0} s to fall to −30 dB (target up to {1} s).",
                                        { numV (r.tailSeconds, 1), numV (t.tailMax, 1) }),
                                   txt ("Lange Fahnen zwischen den Wörtern verwischen die nächsten Silben.",
                                        "Long tails between the words smear the next syllables."));
                    // Only reverb / delay that are on before VOCAL CHECK (a room on the recording cannot be removed)
                    f.action.params.push_back ({ pid::reverbMix, -8.0f, K::amount, 0.0f });
                    f.action.params.push_back ({ pid::delayMix, -8.0f, K::amount, 0.0f });
                }
            }
        };
    }

    int thirdOctaveCentre (float hz)
    {
        static const int centres[] { 25, 31, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
                                     2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000 };
        int best = centres[0];
        for (int c : centres)
            if (std::abs (std::log2 (hz / (float) c)) < std::abs (std::log2 (hz / (float) best)))
                best = c;
        return best;
    }

    juce::String hzText (float hz)                  { return hzIn (hz, i18n::current()); }
    juce::String dbText (float db, bool withPlus)   { return dbIn (db, withPlus, i18n::current()); }

    juce::String dbfsText (float dbfs)
    {
        const auto v = dbfsV (dbfs);
        return i18n::isGerman() ? v.de : v.en;
    }

    CheckTargets targetsFor (const dsp::AnalysisResult& r, Genre genre)
    {
        return CheckTargets::make (genre, r.highVoice, r.pitchValid && r.pitchedShare < 0.45f);
    }

    void evaluate (dsp::AnalysisResult& r, Genre genre)
    {
        const auto t = targetsFor (r, genre);
        Rules rules { r, t };
        rules.material = juce::jlimit (0.9f, 1.0f, 0.9f + 0.1f * (r.voicedSeconds - dsp::VocalAnalyzer::minVoicedSeconds) / 4.0f);

        rules.level();
        if (r.enoughMaterial)
        {
            rules.lowEnd();
            rules.tonal();
            rules.harshness();
            rules.sibilance();
            rules.dynamics();
            rules.stereo();
            rules.noiseAndSpace();
        }

        r.findings.clear();
        r.hiddenFindings = 0;
        for (auto& f : rules.all)
        {
            if (f.confidence >= showThreshold)
                r.findings.push_back (std::move (f));
            else
                ++r.hiddenFindings;
        }
        std::stable_sort (r.findings.begin(), r.findings.end(), [] (const Finding& a, const Finding& b)
        {
            if (a.level != b.level)
                return a.level > b.level;
            return a.confidence > b.confidence;
        });
        r.score = scoreOf (r);
    }

    int scoreOf (const dsp::AnalysisResult& r, const std::function<bool (const dsp::Finding&)>& include)
    {
        if (! r.enoughMaterial)
            return -1;
        float penalty = 0.0f;
        for (auto& f : r.findings)
            if (! include || include (f))
                penalty += weight (f.level) * f.confidence;
        return juce::jlimit (0, 100, juce::roundToInt (100.0f - penalty));
    }
}
