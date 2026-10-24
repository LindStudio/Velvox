// VOCAL CHECK 0.6: finding schema (id, category, severity, confidence, German texts,
// fix action), measurements, rules, score, fix planning and the apply model.

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "DSP/VocalAnalyzer.h"
#include "Check/CheckTypes.h"
#include "Check/CheckTargets.h"
#include "Check/CheckFixPlanner.h"
#include "Parameters/ParameterLayout.h"
#include "DSP/Instances.h"
#include "StateKeys.h"
#include "Parameters/EqBands.h"

using namespace velvox;
using namespace velvox::test;
using namespace velvox::dsp;

namespace
{
    AnalysisResult analyseVocal (const automix::CapturedAudio& v, double fs)
    {
        CheckModule check;
        check.setEnabled (true);
        VocalAnalyzer analyzer (check);
        analyzer.setSampleRate (fs);
        const int n = v.numSamples();
        for (int start = 0; start < n; start += 512)
        {
            const int len = std::min (512, n - start);
            const float* ch[2] { v.vocal[0].data() + start, v.vocal[v.vocal.size() > 1 ? 1 : 0].data() + start };
            check.process (ch, 2, len);
            analyzer.pump();
        }
        return analyzer.analyse();
    }

    juce::String describe (const Finding& f)
    {
        return f.id + " [" + juce::String (juce::roundToInt (f.confidence * 100.0f)) + " %, f " + juce::String (juce::roundToInt (f.freqHz))
             + "] " + f.problem + " | " + f.fix;
    }

    // First finding whose id starts with `prefix` ("mud", "resonance." ...)
    const Finding* findId (const AnalysisResult& r, const juce::String& prefix)
    {
        for (auto& f : r.findings)
            if (f.id.startsWith (prefix))
                return &f;
        return nullptr;
    }

    bool withinThirdOctave (float a, float b) { return a > 0.0f && b > 0.0f && std::abs (std::log2 (a / b)) <= 1.0f / 3.0f + 1.0e-3f; }

    void applyPeak (automix::CapturedAudio& v, double freq, double q, double gainDb)
    {
        for (auto& ch : v.vocal)
        {
            dsp::Biquad f;
            f.setCoeffs (dsp::design::peak (v.sampleRate, freq, q, gainDb));
            for (auto& x : ch)
                x = f.process (x, 0);
        }
    }

    // The synthetic voice (saw + vowel formants) has a different long-term spectrum than a real
    // voice. A 1/3-octave graphic EQ moves it onto the voice template, so "clean" means
    // "typical voice" for the tonal rules too.
    automix::CapturedAudio matchTemplate (automix::CapturedAudio v)
    {
        for (int pass = 0; pass < 4; ++pass)
        {
            const auto r = analyseVocal (v, v.sampleRate);
            if (r.deviationDb.empty())
                break;
            for (float c : { 160.0f, 200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f, 1600.0f, 2000.0f })
            {
                const int b = (int) std::lround (12.0 * std::log2 (c / 40.0));
                const float g = juce::jlimit (-12.0f, 12.0f, -0.7f * r.deviationDb[(size_t) b]);
                if (std::abs (g) > 0.5f)
                    applyPeak (v, c, 4.3, g);
            }
        }
        return v;
    }

    void logFindings (juce::UnitTest& t, const juce::String& label, const AnalysisResult& r)
    {
        t.logMessage ("  " + label + ": score " + juce::String (r.score) + ", " + juce::String ((int) r.findings.size()) + " shown, "
                      + juce::String (r.hiddenFindings) + " hidden");
        for (auto& f : r.findings)
            t.logMessage ("    " + describe (f));
    }
}

class VocalCheckFixTests final : public juce::UnitTest
{
public:
    VocalCheckFixTests() : juce::UnitTest ("Headliner Vocal Check 0.6", "Headliner") {}

    void runTest() override
    {
        constexpr double fs = 48000.0;
        targetsAndTemplate();
        rules();
        fixModel();
        controller();

        beginTest ("Finding schema: id, category, German problem / fix text, confidence above the threshold");
        {
            VocalSpec s;
            s.seconds = 16.0;
            s.sampleRate = fs;
            s.rumbleDb = -26.0f;
            s.resonanceHz = 3150.0f;
            s.resonanceDb = 16.0f;
            const auto r = analyseVocal (makeVocal (s), fs);
            expect (! r.findings.empty());
            for (auto& f : r.findings)
            {
                logMessage ("  " + describe (f));
                expect (f.id.isNotEmpty(), "id");
                expect (f.problem.isNotEmpty() && f.fix.isNotEmpty(), "texts: " + f.id);
                expect (f.fix.startsWith (juce::String::fromUTF8 ("\xe2\x86\x92")), "fix text starts with an arrow: " + f.id);
                expect (f.confidence >= check::showThreshold && f.confidence <= 1.0f, "confidence: " + f.id);
            }
        }

        beginTest ("Measurements: pitch of the voice");
        {
            VocalSpec s;
            s.seconds = 14.0;
            s.sampleRate = fs;
            s.f0 = 220.0f;
            const auto r = analyseVocal (makeVocal (s), fs);
            logMessage ("  f0 median " + juce::String (r.f0MedianHz, 1) + " Hz, pitched " + juce::String (r.pitchedShare, 2)
                        + ", notes " + juce::String ((int) r.sungNotesHz.size()));
            expect (r.pitchValid);
            // The syllables use semitones around 220 Hz: the median and every sung note on that grid
            expectWithinAbsoluteError (r.f0MedianHz, 220.0f, 220.0f * 0.06f);
            for (float note : r.sungNotesHz)
            {
                const float semis = 12.0f * std::log2 (note / 220.0f);
                expectWithinAbsoluteError (semis, std::round (semis), 0.4f, "note on the semitone grid");
            }
            expect (! r.highVoice, "220 Hz median, lowest notes below 250 Hz");
            expect (r.sungNotesHz.size() >= 3);

            s.f0 = 440.0f;
            const auto high = analyseVocal (makeVocal (s), fs);
            expectWithinAbsoluteError (high.f0MedianHz, 440.0f, 440.0f * 0.06f);
            expect (high.highVoice);
        }

        beginTest ("Measurements: loudness (K-weighted, gated) and loudness range");
        {
            const int n = (int) (10.0 * fs);
            automix::CapturedAudio a;
            a.sampleRate = fs;
            a.channels = 2;
            a.vocal.assign (2, std::vector<float> ((size_t) n));
            for (int i = 0; i < n; ++i)
                a.vocal[0][(size_t) i] = a.vocal[1][(size_t) i] = 0.1414f * (float) std::sin (2.0 * pi * 1000.0 * i / fs);
            const auto r = analyseVocal (a, fs);
            logMessage ("  1 kHz at -20 dB RMS: " + juce::String (r.loudnessLufs, 2) + " LUFS");
            expectWithinAbsoluteError (r.loudnessLufs, -20.0f, 0.7f);

            // 4 s at -14 dB, 4 s at -30 dB, three times: about 16 LU
            const int m = (int) (24.0 * fs);
            automix::CapturedAudio b;
            b.sampleRate = fs;
            b.channels = 1;
            b.vocal.assign (1, std::vector<float> ((size_t) m));
            for (int i = 0; i < m; ++i)
            {
                const float level = ((i / (int) (4.0 * fs)) % 2 == 0) ? -14.0f : -30.0f;
                b.vocal[0][(size_t) i] = 1.414f * juce::Decibels::decibelsToGain (level) * (float) std::sin (2.0 * pi * 500.0 * i / fs);
            }
            const auto rb = analyseVocal (b, fs);
            logMessage ("  LRA " + juce::String (rb.lraLu, 1) + " LU");
            expect (rb.lraValid);
            expectWithinAbsoluteError (rb.lraLu, 16.0f, 2.0f);
        }

        beginTest ("Measurements: plosives (low-frequency bursts) are counted");
        {
            VocalSpec s;
            s.seconds = 16.0;
            s.sampleRate = fs;
            auto v = makeVocal (s);
            const auto clean = analyseVocal (v, fs);
            expectEquals (clean.plosiveCount, 0, "no plosives in the clean take");
            addPlosives (v, 6, 0.5f);
            const auto r = analyseVocal (v, fs);
            logMessage ("  plosives " + juce::String (r.plosiveCount) + " (" + juce::String (r.plosivesPerMinute, 1) + " / min)");
            expectWithinAbsoluteError (r.plosiveCount, 6, 1);
            expectGreaterThan (r.plosivesPerMinute, 10.0f);
        }

        beginTest ("Measurements: S centre, mono loss, generation");
        {
            VocalSpec s;
            s.seconds = 14.0;
            s.sampleRate = fs;
            s.sibilance = 1.5f;
            s.channels = 2;
            const auto v = makeVocal (s);
            const auto r = analyseVocal (v, fs);
            logMessage ("  S centre " + juce::String (r.sibilanceHz, 0) + " Hz, mono loss " + juce::String (r.monoLossDb, 2) + " dB");
            expect (r.sibilanceHz > 4500.0f && r.sibilanceHz < 11000.0f);
            expectWithinAbsoluteError (r.monoLossDb, 0.0f, 0.1f);

            auto inverted = v;
            for (auto& x : inverted.vocal[1])
                x = -x;
            const auto ri = analyseVocal (inverted, fs);
            expectGreaterThan (ri.monoLossDb, 20.0f, "anti-phase channels cancel in mono");

            CheckModule check;
            VocalAnalyzer analyzer (check);
            const auto g = analyzer.getGeneration();
            analyzer.clear();
            expectEquals ((int) (analyzer.getGeneration() - g), 1);
        }
    }

    void targetsAndTemplate()
    {
        beginTest ("Targets: party = the auto-mix objective windows; genres and voices differ");
        {
            using check::CheckTargets;
            using check::Genre;
            const auto t = CheckTargets::make (Genre::party, false, false);
            expectEquals (t.body.lo, -9.0f);
            expectEquals (t.body.hi, -1.0f);
            expectEquals (t.lowMid.lo, -7.0f);
            expectEquals (t.lowMid.hi, 0.0f);
            expectEquals (t.top.lo, -16.0f);
            expectEquals (t.top.hi, -7.0f);
            expectEquals (t.air.lo, -32.0f);
            expectEquals (t.air.hi, -16.0f);
            expectEquals (t.presenceMax, -4.0f);
            expectEquals (t.phrase.lo, 3.0f);
            expectEquals (t.phrase.hi, 6.0f);
            expectEquals (t.crestMin, 5.5f);
            expectEquals (t.sibilanceMax, -6.0f);
            const auto high = CheckTargets::make (Genre::party, true, false);
            expectLessThan (high.body.lo, -100.0f, "a high voice has no fundamentals in the body band");
            const auto acoustic = CheckTargets::make (Genre::acoustic, false, false);
            expectGreaterThan (acoustic.phrase.hi, t.phrase.hi, "acoustic allows more dynamics");
        }

        beginTest ("Voice template: finite, smooth, voice shaped (both voice classes)");
        {
            for (bool high : { false, true })
            {
                const auto& tpl = check::voiceTemplate (high);
                expectEquals ((int) tpl.size(), VocalAnalyzer::numBands);
                const auto at = [&] (float hz) { return tpl[(size_t) juce::jlimit (0, VocalAnalyzer::numBands - 1, (int) std::lround (12.0 * std::log2 (hz / 40.0)))]; };
                for (size_t b = 1; b < tpl.size(); ++b)
                {
                    expect (std::isfinite (tpl[b]));
                    // Between the fundamentals and 3 kHz (below F0 the spectrum falls steeply; VocalSet also
                    // falls ~20 dB between 3.5 and 4.5 kHz - its recording chain, so the template is only used below 2.5 kHz)
                    if (VocalAnalyzer::bandCentre ((int) b) > 300.0f && VocalAnalyzer::bandCentre ((int) b) < 3000.0f)
                        expectLessThan (std::abs (tpl[b] - tpl[b - 1]), 3.0f, "smooth at band " + juce::String ((int) b));
                }
                expectGreaterThan (at (500.0f) - at (6000.0f), 10.0f, "the voice falls towards the highs");
            }
        }
    }

    void rules()
    {
        constexpr double fs = 48000.0;
        VocalSpec spec;
        spec.seconds = 16.0;
        spec.sampleRate = fs;
        spec.levelDb = -24.0f;
        spec.sibilance = 0.15f;
        const auto cleanVocal = matchTemplate (makeVocal (spec));
        const auto clean = analyseVocal (cleanVocal, fs);
        {
            juce::String dev;
            for (float c : { 200.0f, 315.0f, 500.0f, 800.0f, 1250.0f, 2000.0f })
                dev << juce::String (c, 0) << ": " << juce::String (clean.deviationDb[(size_t) std::lround (12.0 * std::log2 (c / 40.0))], 1) << "  ";
            logMessage ("  template-matched synthetic voice, deviation " + dev);
        }

        beginTest ("Rules: a clean take raises none of the problems below and scores high");
        {
            logFindings (*this, "clean", clean);
            for (auto* id : { "rumble", "plosives", "mud", "boxy", "resonance.", "harsh", "sibilance" })
                expect (findId (clean, id) == nullptr, juce::String ("no ") + id);
            expectGreaterOrEqual (clean.score, 70);
        }

        beginTest ("Rules: mud (+12 dB at 300 Hz; +6 dB lies within the spread of natural voices, see the ledger) is found at its frequency, with an EQ cut there");
        {
            auto v = cleanVocal;
            applyPeak (v, 300.0, 1.4, 12.0);
            const auto r = analyseVocal (v, fs);
            logFindings (*this, "mud 300", r);
            const auto* f = findId (r, "mud");
            expect (f != nullptr, "mud found");
            if (f != nullptr)
            {
                expect (withinThirdOctave (f->freqHz, 300.0f), "at 300 Hz +-1/3 octave: " + juce::String (f->freqHz));
                expect (f->category == check::Category::tonal);
                expectEquals ((int) f->action.eq.size(), 1);
                if (! f->action.eq.empty())
                {
                    const auto& e = f->action.eq.front();
                    expect (e.role == check::EqRole::mud && e.shape == check::EqShape::bell);
                    expect (withinThirdOctave (e.freqHz, 300.0f));
                    expect (e.gainDb <= -1.5f && e.gainDb >= -6.0f, "a moderate cut: " + juce::String (e.gainDb));
                }
                expect (f->problem.startsWith ("Mulm um "), f->problem);
                expect (f->fix.contains ("EQ"), f->fix);
            }
            expectLessThan (r.score, clean.score, "a problem lowers the score");
        }

        beginTest ("Rules: a narrow 3.5 kHz resonance is a harsh resonance at its frequency");
        {
            auto v = cleanVocal;
            applyPeak (v, 3500.0, 12.0, 20.0);   // a clearly ringing resonance (the synthetic formants slope steeply there)
            const auto r = analyseVocal (v, fs);
            logFindings (*this, "resonance 3.5k", r);
            for (auto& res : r.resonances)
                logMessage ("    peak " + juce::String (res.hz, 0) + " Hz +" + juce::String (res.excessDb, 1) + " dB");
            const auto* f = findId (r, "resonance.");
            expect (f != nullptr, "resonance found");
            if (f != nullptr)
            {
                expect (withinThirdOctave (f->freqHz, 3500.0f), "at 3.5 kHz: " + juce::String (f->freqHz));
                expect (f->category == check::Category::harshness);
                expect (! f->action.eq.empty() && f->action.eq.front().q >= 3.0f, "a narrow cut");
            }
        }

        beginTest ("Rules: rumble, plosives and sibilance");
        {
            auto spec2 = spec;
            spec2.rumbleDb = -36.0f;
            const auto rumble = analyseVocal (makeVocal (spec2), fs);
            logFindings (*this, "rumble", rumble);
            const auto* fr = findId (rumble, "rumble");
            expect (fr != nullptr, "rumble found");
            if (fr != nullptr)
            {
                expect (fr->category == check::Category::lowEnd);
                expect (! fr->action.eq.empty() && fr->action.eq.front().shape == check::EqShape::lowCut
                        && fr->action.eq.front().role == check::EqRole::lowcut);
                if (! fr->action.eq.empty())
                    expect (fr->action.eq.front().freqHz >= 60.0f && fr->action.eq.front().freqHz <= 150.0f);
            }

            auto pv = cleanVocal;
            addPlosives (pv, 6, 0.5f);
            const auto plosives = analyseVocal (pv, fs);
            logFindings (*this, "plosives", plosives);
            expect (findId (plosives, "plosives") != nullptr, "plosives found");

            auto sv = cleanVocal;
            applyPeak (sv, 7000.0, 1.0, 8.0);
            const auto sib = analyseVocal (sv, fs);
            logMessage ("  S vs vowels: clean " + juce::String (clean.sibilanceVsVowelDb, 1) + " dB, boosted " + juce::String (sib.sibilanceVsVowelDb, 1) + " dB");
            logFindings (*this, "sibilance", sib);
            const auto* fs1 = findId (sib, "sibilance");
            expect (fs1 != nullptr, "sibilance found");
            if (fs1 != nullptr)
            {
                expect (fs1->category == check::Category::sibilance);
                bool deess = false;
                for (auto m : fs1->action.modules)
                    deess = deess || m == ModuleId::deess;
                expect (deess, "the fix switches the de-esser on");
            }
        }

        beginTest ("Rules: confidence grows with the size of the problem");
        {
            auto mild = spec, strong = spec;
            mild.rumbleDb = -40.0f;
            strong.rumbleDb = -30.0f;
            const auto rm = analyseVocal (makeVocal (mild), fs), rs = analyseVocal (makeVocal (strong), fs);
            const auto* fm = findId (rm, "rumble");
            const auto* fs2 = findId (rs, "rumble");
            expect (fs2 != nullptr);
            if (fm != nullptr && fs2 != nullptr)
                expectGreaterThan (fs2->confidence, fm->confidence);
            if (fs2 != nullptr)
                expect (fs2->level != check::Severity::low, "a strong problem is not low severity");
        }

        beginTest ("Rules: too little material gives no findings and no score");
        {
            auto shortSpec = spec;
            shortSpec.seconds = 2.5;
            const auto r = analyseVocal (makeVocal (shortSpec), fs);
            expect (! r.enoughMaterial);
            expectEquals (r.score, -1);
            for (auto& f : r.findings)
                expect (f.category == check::Category::level, "only level facts: " + f.id);
        }
    }

    // A parameter state as plain values (defaults), with a routing and the audible rule of the processor
    struct State
    {
        std::map<juce::String, float> values;
        Routing routing;

        State (const juce::String& routingText)
        {
            for (auto& s : getParameterSpecs())
                values[s.id] = s.defaultValue;
            routing = Routing::fromString (routingText);
        }

        float get (const juce::String& id) const
        {
            const auto it = values.find (id);
            return it != values.end() ? it->second : 0.0f;
        }

        void apply (const check::ResolvedFix& f)
        {
            for (auto& p : f.params)
                values[p.paramId] = p.value;
            if (f.routing.has_value())
                routing = *f.routing;
        }

        check::FixContext context (const check::EqBandResolver& eq) const
        {
            check::FixContext c;
            c.value = [this] (const juce::String& id) { return get (id); };
            c.routing = routing;
            c.audible = [this] (ModuleId m, int k)
            {
                return routing.contains (m, k) && get (instanceParamId (check::moduleOnParam (m), k)) > 0.5f;
            };
            c.eq = &eq;
            return c;
        }
    };

    // A dummy processor whose state has an UndoManager (the shell track adds the global one)
    struct UndoHost final : juce::AudioProcessor
    {
        juce::UndoManager undo;
        juce::AudioProcessorValueTreeState apvts { *this, &undo, "PARAMETERS", createParameterLayout() };
        UndoHost() { apvts.state.setProperty (statekeys::routing, Routing::defaults().toString(), nullptr); }
        const juce::String getName() const override { return "undo"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}
        float value (const char* id) { return apvts.getParameter (id)->convertFrom0to1 (apvts.getParameter (id)->getValue()); }
    };

    // The EQ band of a role in a value map (nullopt = none)
    static std::optional<dsp::EqBand> roleBand (std::map<juce::String, float>& values, dsp::EqRole role, int instance = 0)
    {
        const auto a = eqbands::accessFor (values);
        const int b = eqbands::findBand (a, instance, role);
        if (b < 0)
            return std::nullopt;
        return eqbands::readBand (a, instance, b);
    }

    void fixModel()
    {
        using namespace check;
        const EqBandsResolver bands;
        FixAction mud;
        mud.modules.push_back (ModuleId::eq);
        mud.eq.push_back ({ check::EqRole::mud, EqShape::bell, 300.0f, -3.0f, 1.2f, 12 });

        beginTest ("Fix model: a missing EQ is switched on and inserted, the cut goes into the mud band");
        {
            State s ("v2,comp,check");
            s.values[pid::eqOn] = 0.0f;
            const auto f = planFix (mud, "Mulm", s.context (bands));
            expect (f.routing.has_value() && f.routing->contains (ModuleId::eq), "EQ inserted");
            s.apply (f);
            expect (s.routing.indexOf (ModuleId::eq) < s.routing.indexOf (ModuleId::check), "where an EQ usually sits (before the check)");
            expectEquals (s.get (pid::eqOn), 1.0f);
            const auto b = roleBand (s.values, dsp::EqRole::mud);
            expect (b.has_value(), "mud band created");
            if (b.has_value())
            {
                expect (b->on && b->bandType() == dsp::EqBandType::bell);
                expectWithinAbsoluteError (b->freq, 300.0f, 0.5f);
                expectWithinAbsoluteError (b->gain, -3.0f, 0.01f);
                expectWithinAbsoluteError (b->q, 1.2f, 0.01f);
            }
        }

        beginTest ("Fix model: user bands (role none) and other roles are never touched");
        {
            State s ("v2,check,eq");
            s.values[pid::eqOn] = 1.0f;
            const auto a = eqbands::accessFor (s.values);
            dsp::EqBand user;
            user.on = true;
            user.freq = 300.0f;
            user.gain = -2.0f;
            eqbands::writeBand (a, 0, 0, user);
            eqbands::findOrCreateBand (a, 0, dsp::EqRole::air, dsp::EqBandType::highShelf, 10000.0f, 2.0f, 0.7071f);
            s.apply (planFix (mud, "Mulm", s.context (bands)));
            const auto u = eqbands::readBand (a, 0, 0);
            expect (u == user, "the user band is unchanged");
            expectWithinAbsoluteError (roleBand (s.values, dsp::EqRole::air)->gain, 2.0f, 0.01f);
            expectWithinAbsoluteError (roleBand (s.values, dsp::EqRole::mud)->gain, -3.0f, 0.01f);
        }

        beginTest ("Fix model: EQ after VOCAL CHECK - applying twice does not double the cut, a deeper cut stays");
        {
            State s ("v2,check,eq");
            s.values[pid::eqOn] = 1.0f;
            s.apply (planFix (mud, "Mulm", s.context (bands)));
            expectWithinAbsoluteError (roleBand (s.values, dsp::EqRole::mud)->gain, -3.0f, 0.01f);
            s.apply (planFix (mud, "Mulm", s.context (bands)));
            expectWithinAbsoluteError (roleBand (s.values, dsp::EqRole::mud)->gain, -3.0f, 0.01f, "idempotent");

            const auto a = eqbands::accessFor (s.values);
            eqbands::findOrCreateBand (a, 0, dsp::EqRole::mud, dsp::EqBandType::bell, 300.0f, -5.0f, 1.2f);
            s.apply (planFix (mud, "Mulm", s.context (bands)));
            expectWithinAbsoluteError (roleBand (s.values, dsp::EqRole::mud)->gain, -5.0f, 0.01f, "never weakens an existing cut");
        }

        beginTest ("Fix model: EQ before VOCAL CHECK - the measurement contains its cut, the fix adds to it");
        {
            State s ("v2,eq,check");
            s.values[pid::eqOn] = 1.0f;
            eqbands::findOrCreateBand (eqbands::accessFor (s.values), 0, dsp::EqRole::mud, dsp::EqBandType::bell, 310.0f, -2.0f, 1.2f);
            s.apply (planFix (mud, "Mulm", s.context (bands)));
            expectWithinAbsoluteError (roleBand (s.values, dsp::EqRole::mud)->gain, -5.0f, 0.01f);
        }

        beginTest ("Fix model: parameter kinds; modules that are off are not touched unless the fix switches them on");
        {
            State s ("v2,eq,check");
            FixAction sib;
            sib.modules.push_back (ModuleId::deess);
            sib.params.push_back ({ pid::deessFreq, 5500.0f, ParamChange::Kind::set, 0.0f });
            sib.params.push_back ({ pid::deessThreshold, -36.0f, ParamChange::Kind::atMost, 0.0f });
            sib.params.push_back ({ pid::deessRange, 9.0f, ParamChange::Kind::amount, 0.0f });
            s.values[pid::deessOn] = 0.0f;
            s.apply (planFix (sib, "S", s.context (bands)));
            expectEquals (s.get (pid::deessOn), 1.0f);
            expect (s.routing.contains (ModuleId::deess));
            expectWithinAbsoluteError (s.get (pid::deessFreq), 5500.0f, 0.5f);
            expectWithinAbsoluteError (s.get (pid::deessThreshold), -36.0f, 0.01f);
            expectWithinAbsoluteError (s.get (pid::deessRange), 9.0f, 0.01f);

            FixAction over;   // reduce compression: only where a compressor is on
            over.params.push_back ({ pid::compRatio, -1.5f, ParamChange::Kind::amount, 1.0f });
            State off ("v2,eq,check");
            off.values[pid::compOn] = 0.0f;
            expect (planFix (over, "x", off.context (bands)).empty(), "nothing to reduce");
            State on ("v2,comp,check");
            on.values[pid::compOn] = 1.0f;
            on.values[pid::compRatio] = 4.0f;
            on.apply (planFix (over, "x", on.context (bands)));
            expectWithinAbsoluteError (on.get (pid::compRatio), 2.5f, 0.01f);
        }

        beginTest ("Fix model: several fixes merge (the stronger cut on the same band wins; low cut takes the higher frequency)");
        {
            State s ("v2,check,eq");
            s.values[pid::eqOn] = 1.0f;
            FixAction mud2 = mud;
            mud2.eq.front().freqHz = 320.0f;
            mud2.eq.front().gainDb = -4.5f;
            FixAction rumble, plosive;
            rumble.modules.push_back (ModuleId::eq);
            rumble.eq.push_back ({ check::EqRole::lowcut, EqShape::lowCut, 90.0f, 0.0f, 0.7071f, 12 });
            plosive.modules.push_back (ModuleId::eq);
            plosive.eq.push_back ({ check::EqRole::lowcut, EqShape::lowCut, 120.0f, 0.0f, 0.7071f, 24 });
            const auto f = planFixes ({ { mud, "a" }, { rumble, "b" }, { mud2, "c" }, { plosive, "d" } }, s.context (bands), "Alles");
            s.apply (f);
            const auto m = roleBand (s.values, dsp::EqRole::mud);
            expectWithinAbsoluteError (m->gain, -4.5f, 0.01f);
            expectWithinAbsoluteError (m->freq, 320.0f, 0.5f);
            const auto lc = roleBand (s.values, dsp::EqRole::lowCut);
            expect (lc.has_value() && lc->on && lc->bandType() == dsp::EqBandType::lowCut);
            expectWithinAbsoluteError (lc->freq, 120.0f, 0.5f);
            expectEquals (lc->slope, 3, "24 dB/oct");
            expectEquals (f.name, juce::String ("Alles"));
        }

        beginTest ("Fix model: applying is one undo step (parameters and routing)");
        {
            UndoHost host;
            State s ("v2,comp,check");
            for (auto& [id, v] : s.values)
                if (auto* p = host.apvts.getParameter (id))
                    v = p->convertFrom0to1 (p->getValue());
            host.apvts.state.setProperty (statekeys::routing, s.routing.toString(), nullptr);
            host.apvts.getParameter (pid::eqOn)->setValueNotifyingHost (0.0f);
            s.values[pid::eqOn] = 0.0f;
            host.apvts.copyState();
            host.undo.clearUndoHistory();
            const auto routingBefore = host.apvts.state.getProperty (statekeys::routing).toString();

            applyResolvedFix (host.apvts, planFix (mud, "Vocal Check: Mulm", s.context (bands)));
            const auto access = eqbands::accessFor (host.apvts);
            const int band = eqbands::findBand (access, 0, dsp::EqRole::mud);
            expect (band >= 0, "mud band");
            const auto gainId = eqbands::bandParamId (0, std::max (0, band), pid::eqband::gain);
            expectWithinAbsoluteError (host.value (gainId.toRawUTF8()), -3.0f, 0.01f);
            expectEquals (host.value (pid::eqOn), 1.0f);
            expect (host.apvts.state.getProperty (statekeys::routing).toString() != routingBefore);
            expect (host.undo.canUndo(), "undoable");
            expectEquals (host.undo.getUndoDescription(), juce::String ("Vocal Check: Mulm"));

            host.undo.undo();
            expectWithinAbsoluteError (host.value (gainId.toRawUTF8()), 0.0f, 0.01f, "one undo restores the band");
            expect (eqbands::findBand (access, 0, dsp::EqRole::mud) < 0, "... removes its role");
            expectEquals (host.value (pid::eqOn), 0.0f, "... the switch");
            expectEquals (host.apvts.state.getProperty (statekeys::routing).toString(), routingBefore, "... and the routing");
            expect (! host.undo.canUndo(), "it was a single step");
        }
    }

    static void runThroughPlugin (Harness& h, const automix::CapturedAudio& v)
    {
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;
        for (int start = 0; start + 512 <= v.numSamples(); start += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom (ch, 0, v.vocal[(size_t) std::min (ch, (int) v.vocal.size() - 1)].data() + start, 512);
            h.proc.processBlock (block, midi);
            if ((start / 512) % 8 == 0)
                h.proc.pumpAnalyzer();
        }
        h.proc.pumpAnalyzer();
        h.proc.refreshAnalysis();
    }

    void controller()
    {
        constexpr double fs = 48000.0;
        VocalSpec spec;
        spec.seconds = 14.0;
        spec.sampleRate = fs;
        spec.levelDb = -24.0f;
        spec.rumbleDb = -40.0f;
        spec.channels = 2;
        auto v = matchTemplate (makeVocal (spec));
        addPlosives (v, 6, 0.3f);

        const auto prepare = [] (Harness& h, bool capture)
        {
            h.neutral();
            h.set (pid::checkOn, 1.0f);
            h.set (pid::checkMode, capture ? 1.0f : 0.0f);
            h.set (pid::eqOn, 0.0f);
            h.proc.setRouting (Routing::fromString ("v2,comp,check"));
            h.proc.pumpAnalyzer();
            if (capture)
                h.proc.getAnalyzer().startCapture();
        };

        beginTest ("Controller: Anwenden inserts the EQ and sets the low cut; a second Anwenden on the same measurement does nothing");
        {
            Harness h;
            prepare (h, true);
            runThroughPlugin (h, v);
            h.proc.getAnalyzer().stopCapture();
            h.proc.refreshAnalysis();
            auto& vc = h.proc.getVocalCheck();
            for (auto* f : vc.visibleFindings())
                logMessage ("  " + describe (*f));
            const auto* rumble = vc.finding ("rumble");
            expect (rumble != nullptr, "rumble found");
            expect (vc.canApply ("rumble"));
            expect (vc.apply ("rumble"));
            expect (h.proc.getRouting().contains (ModuleId::eq), "EQ inserted");
            expectEquals (h.get (pid::eqOn), 1.0f);
            const auto lowCut = [&h]
            {
                const auto a = eqbands::accessFor (h.proc.getState());
                const int b = eqbands::findBand (a, 0, dsp::EqRole::lowCut);
                return b < 0 ? dsp::EqBand {} : eqbands::readBand (a, 0, b);
            };
            const auto lc = lowCut();
            expect (lc.on && lc.bandType() == dsp::EqBandType::lowCut, "low cut band");
            expect (lc.freq >= 60.0f && lc.freq <= 150.0f);
            expect (! vc.canApply ("rumble"), "applied on this measurement");
            expect (! vc.apply ("rumble"));
            expect (lowCut() == lc, "unchanged");
            expect (! vc.canApply ("nothing-like-this"));
        }

        beginTest ("Controller: continuous mode measures afresh after Anwenden");
        {
            Harness h;
            prepare (h, false);
            runThroughPlugin (h, v);
            auto& vc = h.proc.getVocalCheck();
            expect (vc.finding ("rumble") != nullptr);
            const auto generation = h.proc.getLatestAnalysis().generation;
            expect (vc.apply ("rumble"));
            expectGreaterThan ((int) (h.proc.getLatestAnalysis().generation - generation), 0, "new measurement");
            expect (! h.proc.getLatestAnalysis().enoughMaterial, "collecting again");
            expectEquals (vc.score(), -1);
        }

        beginTest ("Controller: Alles anwenden applies every applicable finding at once");
        {
            Harness h;
            prepare (h, true);
            runThroughPlugin (h, v);
            h.proc.getAnalyzer().stopCapture();
            h.proc.refreshAnalysis();
            auto& vc = h.proc.getVocalCheck();
            int applicable = 0;
            for (auto* f : vc.visibleFindings())
                applicable += vc.canApply (f->id) ? 1 : 0;
            expectGreaterThan (applicable, 1);
            expectEquals (vc.applyAll(), applicable);
            {
                const auto a = eqbands::accessFor (h.proc.getState());
                const int b = eqbands::findBand (a, 0, dsp::EqRole::lowCut);
                expect (b >= 0 && eqbands::readBand (a, 0, b).slope == 3, "plosives: the steeper cut (24 dB/oct) won");
            }
            expectEquals (vc.applyAll(), 0, "nothing left on this measurement");
        }

        beginTest ("Controller: Ignorieren hides a finding, leaves the score, persists in the project, restore brings it back");
        {
            Harness h;
            prepare (h, true);
            runThroughPlugin (h, v);
            h.proc.getAnalyzer().stopCapture();
            h.proc.refreshAnalysis();
            auto& vc = h.proc.getVocalCheck();
            const int before = vc.score();
            const auto visible = vc.visibleFindings().size();
            vc.ignore ("rumble");
            expect (vc.isIgnored ("rumble"));
            expectEquals (vc.visibleFindings().size(), visible - 1);
            expectEquals ((int) vc.ignoredFindings().size(), 1);
            expectGreaterThan (vc.score(), before, "an ignored finding does not cost points");
            expect (! vc.canApply ("rumble"));
            vc.ignore ("resonance.3150");   // ids that are not present now are kept as well

            juce::MemoryBlock state;
            h.proc.getStateInformation (state);
            Harness reloaded;
            reloaded.proc.setStateInformation (state.getData(), (int) state.getSize());
            auto& rc = reloaded.proc.getVocalCheck();
            expect (rc.isIgnored ("rumble") && rc.isIgnored ("resonance.3150"), "ignored after reload");

            vc.restore ("rumble");
            expect (! vc.isIgnored ("rumble"));
            expectEquals (vc.visibleFindings().size(), visible);
            vc.restoreAll();
            expect (vc.getIgnoredIds().isEmpty());
        }

        beginTest ("Controller: genre is stored and changes the targets");
        {
            Harness h;
            auto& vc = h.proc.getVocalCheck();
            expect (vc.getGenre() == check::Genre::party, "default");
            vc.setGenre (check::Genre::acoustic);
            juce::MemoryBlock state;
            h.proc.getStateInformation (state);
            Harness reloaded;
            reloaded.proc.setStateInformation (state.getData(), (int) state.getSize());
            expect (reloaded.proc.getVocalCheck().getGenre() == check::Genre::acoustic);
        }
    }

    // Low-frequency thumps (80 Hz, 40 ms) at syllable-like positions, `amp` peak
    static void addPlosives (automix::CapturedAudio& v, int count, float amp)
    {
        const double fs = v.sampleRate;
        const int len = (int) (0.04 * fs);
        for (int k = 0; k < count; ++k)
        {
            const int start = (int) ((1.0 + 2.2 * k) * fs);
            for (auto& ch : v.vocal)
                for (int i = 0; i < len && start + i < (int) ch.size(); ++i)
                {
                    const float env = std::sin (juce::MathConstants<float>::pi * (float) i / (float) len);
                    ch[(size_t) (start + i)] += amp * env * (float) std::sin (2.0 * pi * 80.0 * i / fs);
                }
        }
    }
};

static VocalCheckFixTests vocalCheckFixTests;
