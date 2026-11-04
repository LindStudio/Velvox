#include "AnalysisStore.h"
#include <cstring>

namespace vocalypse::automix
{
    namespace
    {
        juce::CriticalSection directoryLock;
        juce::File directoryOverride;

        // A number that is null when it is not finite (JSON has no NaN / inf)
        juce::var number (double v) { return std::isfinite (v) ? juce::var (v) : juce::var(); }

        // Replaces every non-finite number in the tree by null
        void sanitise (juce::var& v)
        {
            if (v.isDouble())
            {
                if (! std::isfinite ((double) v))
                    v = juce::var();
            }
            else if (auto* array = v.getArray())
            {
                for (auto& e : *array)
                    sanitise (e);
            }
            else if (auto* object = v.getDynamicObject())
            {
                juce::Array<juce::Identifier> names;
                for (auto& property : object->getProperties())
                    names.add (property.name);
                for (auto& name : names)
                {
                    auto copy = object->getProperty (name);
                    sanitise (copy);
                    object->setProperty (name, copy);
                }
            }
        }

        juce::var floats (const std::vector<float>& v)
        {
            juce::Array<juce::var> a;
            for (float x : v)
                a.add (number (x));
            return a;
        }

        struct Fields
        {
            juce::DynamicObject* o = new juce::DynamicObject();
            Fields& add (const char* key, double v) { o->setProperty (key, number (v)); return *this; }
            Fields& add (const char* key, float v) { return add (key, (double) v); }
            Fields& add (const char* key, int v) { o->setProperty (key, v); return *this; }
            Fields& add (const char* key, bool v) { o->setProperty (key, v); return *this; }
            Fields& add (const char* key, const juce::var& v) { o->setProperty (key, v); return *this; }
            juce::var done() { return juce::var (o); }
        };

        juce::var profileToVar (const VocalProfile& p)
        {
            Fields f;
            f.add ("sampleRate", p.sampleRate).add ("channels", p.channels).add ("capturedSeconds", p.capturedSeconds)
             .add ("voicedSeconds", p.voicedSeconds).add ("enoughMaterial", p.enoughMaterial).add ("stoppedEarly", p.stoppedEarly)
             .add ("hostBpm", p.hostBpm)
             .add ("integratedLufs", p.integratedLufs).add ("peakDb", p.peakDb).add ("clipRuns", p.clipRuns).add ("crestDb", p.crestDb)
             .add ("loudnessRangeDb", p.loudnessRangeDb).add ("loudnessRangeValid", p.loudnessRangeValid)
             .add ("dynamicsDb", p.dynamicsDb).add ("dynamicsValid", p.dynamicsValid)
             .add ("voiceRmsDb", p.voiceRmsDb).add ("voiceRmsLowDb", p.voiceRmsLowDb).add ("voiceRmsHighDb", p.voiceRmsHighDb)
             .add ("noiseFloorDb", p.noiseFloorDb).add ("snrDb", p.snrDb).add ("snrValid", p.snrValid).add ("breathShare", p.breathShare)
             .add ("f0MedianHz", p.f0MedianHz).add ("f0LowHz", p.f0LowHz).add ("f0HighHz", p.f0HighHz).add ("pitchedShare", p.pitchedShare)
             .add ("pitchSpreadSemitones", p.pitchSpreadSemitones).add ("pitchValid", p.pitchValid).add ("sungNotesHz", floats (p.sungNotesHz))
             .add ("spectrumDb", floats (p.spectrumDb)).add ("thirdsDb", floats (p.thirdsDb))
             .add ("rumbleDb", p.rumbleDb).add ("bodyDb", p.bodyDb).add ("lowMidDb", p.lowMidDb).add ("presenceDb", p.presenceDb)
             .add ("sibilanceBandDb", p.sibilanceBandDb).add ("airDb", p.airDb).add ("topDb", p.topDb)
             .add ("tiltDbPerOctave", p.tiltDbPerOctave).add ("lowMidPeakHz", p.lowMidPeakHz)
             .add ("onsetRate", p.onsetRate).add ("onsetSharpnessDb", p.onsetSharpnessDb).add ("onsetsValid", p.onsetsValid)
             .add ("sibilanceValid", p.sibilanceValid).add ("sibilanceVsVowelDb", p.sibilanceVsVowelDb).add ("sibilanceHz", p.sibilanceHz)
             .add ("sibilantShare", p.sibilantShare).add ("deessDetectorHz", p.deessDetectorHz)
             .add ("sibBandVowelP90Db", p.sibBandVowelP90Db).add ("sibBandSibilantP50Db", p.sibBandSibilantP50Db)
             .add ("tailSeconds", p.tailSeconds).add ("tailCount", p.tailCount).add ("reverberant", p.reverberant)
             .add ("stereoContent", p.stereoContent).add ("correlation", p.correlation).add ("balanceDb", p.balanceDb)
             .add ("sideToMidDb", p.sideToMidDb).add ("heavilyCompressed", p.heavilyCompressed).add ("distortionLikely", p.distortionLikely)
             .add ("beatPresent", p.beatPresent).add ("beatPlayingSeconds", p.beatPlayingSeconds).add ("beatLufs", p.beatLufs)
             .add ("vocalOverBeatLu", p.vocalOverBeatLu).add ("beatGapShare", p.beatGapShare)
             .add ("beatPresenceVsVocalDb", p.beatPresenceVsVocalDb).add ("beatLowMidVsVocalDb", p.beatLowMidVsVocalDb)
             .add ("maskingHint", p.maskingHint)
             .add ("sibilantFrames", (int) p.sibilantFrames.size()).add ("vowelFrames", (int) p.vowelFrames.size());

            juce::Array<juce::var> resonances;
            for (auto& r : p.resonances)
                resonances.add (Fields().add ("hz", r.hz).add ("excessDb", r.excessDb).add ("strong", r.strong)
                                        .add ("persistence", r.persistence).add ("sustained", r.sustained).add ("q", r.q).done());
            f.add ("resonances", resonances);

            juce::Array<juce::var> issues;
            for (auto& i : p.issues)
                issues.add (Fields().add ("key", juce::var (i.key)).add ("title", juce::var (i.title)).add ("detail", juce::var (i.detail))
                                    .add ("confidence", (int) i.confidence).add ("severity", i.severity).done());
            f.add ("issues", issues);
            return f.done();
        }
    }

    juce::File AnalysisStore::defaultDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Lind Audio").getChildFile ("VOCALYPSE").getChildFile ("AutoMix");
    }

    void AnalysisStore::setDirectory (const juce::File& d)
    {
        const juce::ScopedLock lock (directoryLock);
        directoryOverride = d;
    }

    juce::File AnalysisStore::directory()
    {
        const juce::ScopedLock lock (directoryLock);
        return directoryOverride != juce::File() ? directoryOverride : defaultDirectory();
    }

    juce::String AnalysisStore::hashOf (const CapturedAudio& a)
    {
        juce::uint64 h = 14695981039346656037ull;
        const auto feed = [&h] (const std::vector<std::vector<float>>& channels)
        {
            for (auto& c : channels)
                for (float x : c)
                {
                    juce::uint32 bits;
                    std::memcpy (&bits, &x, sizeof (bits));
                    for (int b = 0; b < 4; ++b)
                    {
                        h ^= (bits >> (8 * b)) & 0xffu;
                        h *= 1099511628211ull;
                    }
                }
        };
        feed (a.vocal);
        feed (a.beat);
        return juce::String::toHexString ((juce::int64) h).paddedLeft ('0', 16);
    }

    juce::var AnalysisStore::toVar (const AutoMixResult& r, const CapturedAudio& captured, const juce::String& hash)
    {
        double ppqStart = -1.0;
        for (double q : captured.blockPpq)
            if (q >= 0.0)
            {
                ppqStart = q;
                break;
            }

        Fields root;
        root.add ("version", version);
        root.add ("intent", Fields().add ("genre", (int) r.intent.genre).add ("vibe", (int) r.intent.vibe).done());
        root.add ("capture", Fields().add ("seconds", captured.seconds()).add ("voicedSeconds", r.profile.voicedSeconds)
                                     .add ("hash", juce::var (hash.isNotEmpty() ? hash : hashOf (captured)))
                                     .add ("ppqStart", ppqStart >= 0.0 ? number (ppqStart) : juce::var()).done());

        juce::Array<juce::var> segments;
        for (size_t i = 0; i < r.segments.size(); ++i)
        {
            const auto& s = r.segments[i];
            auto profile = i < r.segmentProfiles.size() ? profileToVar (r.segmentProfiles[i]) : juce::var();
            segments.add (Fields().add ("startSample", s.startSample).add ("numSamples", s.numSamples)
                                  .add ("startPpq", s.startPpq >= 0.0 ? number (s.startPpq) : juce::var())
                                  .add ("voicedSeconds", s.voicedSeconds)
                                  .add ("inExcerpt", std::find (r.excerptSegments.begin(), r.excerptSegments.end(), (int) i) != r.excerptSegments.end())
                                  .add ("profile", profile).done());
        }
        root.add ("segments", segments);
        root.add ("profile", profileToVar (r.profile));

        juce::Array<juce::var> log;
        for (auto& l : r.plan.log)
            log.add (automix::toVar (l));
        root.add ("log", log);

        Fields values;
        for (auto& [id, v] : r.plan.values)
            values.add (id.toRawUTF8(), (double) v);
        root.add ("values", values.done());
        root.add ("routing", juce::var (r.plan.routing.toString()));

        auto tree = root.done();
        sanitise (tree);
        return tree;
    }

    juce::String AnalysisStore::toJson (const AutoMixResult& r, const CapturedAudio& captured, const juce::String& hash)
    {
        return juce::JSON::toString (toVar (r, captured, hash), false);
    }

    void AnalysisStore::prune (const juce::File& dir)
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false, "????????_??????_*.json");
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
        for (int i = keepFiles; i < files.size(); ++i)
            files.getReference (i).deleteFile();
    }

    juce::File AnalysisStore::writeTo (const juce::File& dir, const AutoMixResult& r, const CapturedAudio& captured)
    {
        if (! dir.createDirectory().wasOk())
            return {};
        const auto hash = hashOf (captured);
        const auto name = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + "_" + hash + ".json";
        const auto file = dir.getChildFile (name);
        if (! file.replaceWithText (toJson (r, captured, hash), false, false, "\n"))
            return {};
        prune (dir);
        return file;
    }

    juce::File AnalysisStore::write (const AutoMixResult& r, const CapturedAudio& captured)
    {
        return writeTo (directory(), r, captured);
    }
}
