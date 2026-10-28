// The auto mix hears the beat (plan 2026-10-28): synthetic beats, the band-by-band comparison
// of the voice against the beat, its density and loudness.

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "SyntheticBeat.h"
#include "AutoMix/VocalProfile.h"
#include "AutoMix/AutoMixEngine.h"
#include "Parameters/ParameterLayout.h"
#include "Text/I18n.h"

using namespace velvox;
using namespace velvox::test;
using namespace velvox::automix;

namespace
{
    // The vocal with `beat` on the sidechain (cut or padded with silence to the vocal's length)
    CapturedAudio withBeat (CapturedAudio vocal, const CapturedAudio& beat, int beatChannels = 2)
    {
        vocal.beat.clear();
        for (int ch = 0; ch < beatChannels; ++ch)
        {
            auto b = beat.vocal[(size_t) std::min (ch, (int) beat.vocal.size() - 1)];
            b.resize ((size_t) vocal.numSamples(), 0.0f);
            vocal.beat.push_back (std::move (b));
        }
        return vocal;
    }

    float lufsOf (const CapturedAudio& a)
    {
        std::vector<const float*> ch;
        for (auto& c : a.vocal)
            ch.push_back (c.data());
        return VocalProfiler::integratedLufs (ch, a.numSamples(), a.sampleRate);
    }

    // Mean of beatOverVocalDb over the bands with centres in [lo, hi)
    float region (const VocalProfile& p, float lo, float hi)
    {
        double s = 0.0;
        int c = 0;
        for (int b = 0; b < (int) p.beatOverVocalDb.size(); ++b)
            if (VocalProfiler::bandCentre (b) >= lo && VocalProfiler::bandCentre (b) < hi)
            {
                s += p.beatOverVocalDb[(size_t) b];
                ++c;
            }
        return c > 0 ? (float) (s / c) : 0.0f;
    }

    std::map<juce::String, float> defaults()
    {
        std::map<juce::String, float> v;
        for (auto& s : getParameterSpecs())
            v[s.id] = s.defaultValue;
        return v;
    }

    // The output decision of a mix, in English
    juce::String outputReason (const AutoMixResult& r)
    {
        for (auto& d : r.plan.decisions)
            if (d.outputStage)
                return i18n::show (d.title, i18n::Language::english) + " / " + i18n::show (d.reason, i18n::Language::english);
        return {};
    }

    VocalSpec vocalSpec (float levelDb = -18.0f)
    {
        VocalSpec s;
        s.seconds = 12.0;
        s.channels = 2;
        s.levelDb = levelDb;
        return s;
    }
}

class BeatTests final : public juce::UnitTest
{
public:
    BeatTests() : juce::UnitTest ("Velvox auto mix with a beat", "Velvox") {}

    void runTest() override
    {
        beginTest ("Beat: synthetic beats have the requested loudness");
        {
            for (auto kind : { BeatKind::dense, BeatKind::sparse, BeatKind::presenceHeavy, BeatKind::lowMidHeavy })
                expectWithinAbsoluteError (lufsOf (makeBeat (kind, 48000.0, 12.0, -16.0f)), -16.0f, 0.5f);
        }

        beginTest ("Beat: the profile compares voice and beat band by band");
        {
            const auto vocal = makeVocal (vocalSpec());
            const auto dense = VocalProfiler::analyse (withBeat (vocal, makeBeat (BeatKind::dense, 48000.0, 12.0, -16.0f)));
            const auto bright = VocalProfiler::analyse (withBeat (vocal, makeBeat (BeatKind::presenceHeavy, 48000.0, 12.0, -16.0f)));
            expect (dense.beatPresent && bright.beatPresent);
            expectEquals ((int) bright.beatOverVocalDb.size(), VocalProfiler::numBands);
            // The presence-heavy beat covers 2-5 kHz clearly more than the neutral one, relative to the core
            const float lift = (region (bright, 2000, 5000) - region (bright, 300, 1000))
                             - (region (dense, 2000, 5000) - region (dense, 300, 1000));
            logMessage ("presence lift of the bright beat against the neutral one: " + juce::String (lift, 1) + " dB");
            expectGreaterThan (lift, 6.0f);
            expect (bright.maskingHint, "the old hint still follows the 2-5 kHz comparison");
        }

        beginTest ("Beat: density (gap share)");
        {
            const auto vocal = makeVocal (vocalSpec());
            const auto dense = VocalProfiler::analyse (withBeat (vocal, makeBeat (BeatKind::dense, 48000.0, 12.0, -16.0f)));
            const auto sparse = VocalProfiler::analyse (withBeat (vocal, makeBeat (BeatKind::sparse, 48000.0, 12.0, -16.0f)));
            logMessage ("gap share dense " + juce::String (dense.beatGapShare, 2) + ", sparse " + juce::String (sparse.beatGapShare, 2));
            expectLessThan (dense.beatGapShare, 0.15f);
            expectGreaterThan (sparse.beatGapShare, 0.35f);
        }

        beginTest ("Beat: loudness of the voice against the beat");
        {
            const auto vocal = makeVocal (vocalSpec());
            const float vocalLufs = lufsOf (vocal);
            const auto p = VocalProfiler::analyse (withBeat (vocal, makeBeat (BeatKind::dense, 48000.0, 12.0, vocalLufs + 4.0f)));
            expectWithinAbsoluteError (p.vocalOverBeatLu, -4.0f, 0.5f);
        }

        beginTest ("Beat: only where it plays; a short beat counts as none");
        {
            const auto vocal = makeVocal (vocalSpec());
            const auto late = VocalProfiler::analyse (withBeat (vocal, makeBeat (BeatKind::dense, 48000.0, 12.0, -16.0f, 4.0)));
            expect (late.beatPresent);
            expectWithinAbsoluteError (late.beatPlayingSeconds, 8.0f, 0.3f);
            const auto shortBeat = VocalProfiler::analyse (withBeat (vocal, makeBeat (BeatKind::dense, 48000.0, 2.0, -16.0f)));
            expect (! shortBeat.beatPresent, "a 2 s beat is not enough");
            const auto none = VocalProfiler::analyse (vocal);
            expect (! none.beatPresent && none.beatOverVocalDb.empty());
        }

        beginTest ("Beat: a mono sidechain measures like the same beat in stereo");
        {
            const auto vocal = makeVocal (vocalSpec());
            const auto beat = makeBeat (BeatKind::presenceHeavy, 48000.0, 12.0, -16.0f);
            const auto stereo = VocalProfiler::analyse (withBeat (vocal, beat, 2));
            const auto mono = VocalProfiler::analyse (withBeat (vocal, beat, 1));
            expectWithinAbsoluteError (mono.beatGapShare, stereo.beatGapShare, 0.01f);
            expectWithinAbsoluteError (region (mono, 2000, 5000), region (stereo, 2000, 5000), 0.1f);
        }

        levelTests();
        eqTests();
    }

    void eqTests()
    {
        const auto vocal = makeVocal (vocalSpec());
        const float vocalLufs = lufsOf (vocal);
        const auto profileWith = [&] (BeatKind kind)
        {
            return VocalProfiler::analyse (withBeat (vocal, makeBeat (kind, 48000.0, 12.0, vocalLufs + 3.0f)));
        };
        const auto toneBell = [] (const MixPlan& plan) { return roleBand (plan, {}, dsp::EqRole::autoTone); };
        const auto hasNote = [] (const MixPlan& plan, const char* text)
        {
            for (auto& n : plan.notes)
                if (i18n::show (n, i18n::Language::english).contains (text))
                    return true;
            return false;
        };

        beginTest ("Beat EQ: a lift only where the beat covers the voice");
        {
            const auto none = AutoMixPlanner::plan (VocalProfiler::analyse (vocal), defaults());
            for (auto kind : { BeatKind::presenceHeavy, BeatKind::dense, BeatKind::dark })
            {
                const auto p = profileWith (kind);
                const auto m = autoeq::maskingOf (p);
                const auto plan = AutoMixPlanner::plan (p, defaults());
                const auto b = toneBell (plan);
                logMessage (juce::String ((int) kind) + ": beat over voice " + juce::String (m.presenceDb, 1) + " dB at "
                            + juce::String (m.hz, 0) + " Hz -> tone bell " + (b && b->on ? juce::String (b->gain, 1) + " dB at " + juce::String (b->freq, 0) : juce::String ("off")));
                if (kind == BeatKind::presenceHeavy)
                {
                    expect (b.has_value() && b->on, "presence-heavy beat: a lift");
                    if (b && b->on)
                    {
                        expect (b->freq >= 1500.0f && b->freq <= 6000.0f, juce::String (b->freq));
                        expect (b->gain >= 0.5f && b->gain <= 2.5f, juce::String (b->gain));
                    }
                }
                if (kind == BeatKind::dark)
                    expect (! (b && b->on && b->gain > 0.0f), "a beat that leaves the presence free: no lift");
            }
            const auto b0 = toneBell (none);
            expect (! (b0 && b0->on && b0->gain > 0.0f), "no beat: no lift from it");
            expect (autoeq::unmaskGainDb (3.0f, false) == 0.0f && autoeq::unmaskGainDb (30.0f, false) == 2.5f
                    && autoeq::unmaskGainDb (4.0f, false) == 0.5f);
        }

        beginTest ("Beat EQ: a sharp voice is not lifted, the report says why");
        {
            auto p = profileWith (BeatKind::presenceHeavy);
            p.issues.push_back ({ "harsh", "Betonte obere Mitten", "", Confidence::likely, 10.0f });
            const auto plan = AutoMixPlanner::plan (p, defaults());
            const auto b = toneBell (plan);
            expect (! (b && b->on && b->gain > 0.0f), "no lift on a sharp voice");
            expect (hasNote (plan, "already sharp there"));
        }

        beginTest ("Beat EQ: a crowded low mid deepens a mud cut, never makes one");
        {
            // The rule on set measurements: synthetic beats normalised to a loudness hardly crowd the low
            // mids of a voice set 1 LU above them, but bass-heavy productions can
            const auto crowd = [] (VocalProfile p)
            {
                const float shift = autoeq::leadOverBeatLu - p.vocalOverBeatLu;
                for (int b = 0; b < VocalProfiler::numBands; ++b)
                    if (const float c = VocalProfiler::bandCentre (b); c >= 250.0f && c < 500.0f)
                        p.beatOverVocalDb[(size_t) b] = 9.0f + shift;
                return p;
            };
            const auto cutOf = [] (const MixPlan& plan) { return roleGain (plan, {}, dsp::EqRole::autoMix); };
            const auto withBeatProfile = profileWith (BeatKind::dense);
            expectLessThan (autoeq::maskingOf (withBeatProfile).lowMidDb, 6.0f, "the dense test beat does not crowd them");
            expectWithinAbsoluteError (autoeq::maskingOf (crowd (withBeatProfile)).lowMidDb, 9.0f, 0.01f);

            for (float mud : { 0.0f, 3.0f, 4.5f, 6.0f })
            {
                auto p = withBeatProfile;
                for (int b = 0; b < VocalProfiler::numBands; ++b)
                    if (const float c = VocalProfiler::bandCentre (b); c >= 250.0f && c < 500.0f)
                        p.spectrumDb[(size_t) b] += mud;
                const float without = cutOf (AutoMixPlanner::plan (p, defaults()));
                const float with = cutOf (AutoMixPlanner::plan (crowd (p), defaults()));
                logMessage ("mud +" + juce::String (mud, 1) + " dB: cut " + juce::String (without, 1) + " -> " + juce::String (with, 1) + " with a crowded low mid");
                if (without < 0.0f)
                    expectWithinAbsoluteError (with, std::max (-autoeq::maxBroadCutDb, without - 1.0f), 0.01f);
                else
                    expectEquals (with, without, "no mud cut from the beat alone");
            }
        }
    }

    void levelTests()
    {
        const auto vocal = makeVocal (vocalSpec (-26.0f));
        const float vocalLufs = lufsOf (vocal);

        beginTest ("Beat level: the voice sits 1 LU above the beat");
        {
            for (float beatOver : { 2.0f, -14.0f })
            {
                const float beatLufs = vocalLufs + beatOver;
                const auto r = AutoMixEngine::run (withBeat (vocal, makeBeat (BeatKind::dense, 48000.0, 12.0, beatLufs)), defaults());
                expect (r.ok, r.error);
                logMessage ("beat " + juce::String (beatLufs, 1) + " LUFS -> voice " + juce::String (r.processed.lufs, 1) + ": " + outputReason (r));
                expectWithinAbsoluteError (r.processed.lufs, beatLufs + 1.0f, 0.5f);
                expect (r.processed.peakDb <= -0.9f);
                expect (outputReason (r).contains ("above the beat"), outputReason (r));
            }
        }

        beginTest ("Beat level: a much louder beat - the peaks decide, the report says so");
        {
            const auto r = AutoMixEngine::run (withBeat (vocal, makeBeat (BeatKind::dense, 48000.0, 12.0, vocalLufs + 20.0f)), defaults());
            expect (r.ok, r.error);
            logMessage (outputReason (r));
            expect (r.processed.peakDb <= -0.9f, "peak " + juce::String (r.processed.peakDb));
            expect (r.plan.get (pid::outputGain) <= 24.0f);
            expect (outputReason (r).contains ("LU short of that"), outputReason (r));
        }

        beginTest ("Beat level: without a beat the output level is unchanged");
        {
            const auto a = AutoMixEngine::run (vocal, defaults());
            const auto b = AutoMixEngine::run (withBeat (vocal, makeBeat (BeatKind::dense, 48000.0, 2.0, vocalLufs)), defaults());   // too short = none
            expect (a.ok && b.ok);
            expectEquals (b.plan.get (pid::outputGain), a.plan.get (pid::outputGain));
            expect (! outputReason (a).contains ("beat"), outputReason (a));
        }
    }
};

static BeatTests beatTests;
