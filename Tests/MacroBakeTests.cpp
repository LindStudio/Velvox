// 0.6.1 item 2: the Simple sliders set the modules. Old projects and presets (made while the macros
// were an offset layer in the audio path) are baked once on load and must sound exactly as before.
//
// The old states are built the way 0.6.0 saved them: the module parameters as the user left them,
// the macro values on top, stateVersion 3 (snapshots: format 2). The same code runs on the 0.6.0
// build (where the macros are applied at run time) and on this one (baked on load), so the env-gated
// part compares the two builds directly:
//   VELVOX_MACRO_BASELINE_WRITE=<dir>  writes the renders (run on the 0.6.0 build)
//   VELVOX_MACRO_BASELINE=<dir>        compares with them

#include "TestHelpers.h"
#include "SyntheticVocal.h"
#include "MixLawFixtures.h"
#include "StateKeys.h"

using namespace velvox;
using namespace velvox::test;

namespace velvox::test::macrobaketest
{
    using Values = std::map<juce::String, float>;

    inline juce::MemoryBlock state05()
    {
        juce::MemoryOutputStream z;
        juce::Base64::convertFromBase64 (z, mixlaw::state05Base64);
        juce::MemoryInputStream zin (z.getData(), z.getDataSize(), false);
        juce::GZIPDecompressorInputStream gz (zin);
        juce::MemoryBlock blob;
        gz.readIntoMemoryBlock (blob);
        return blob;
    }

    inline void settle()
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }

    // The state of `h` with `macros` written into it as a 0.6.0 project would hold them
    inline juce::MemoryBlock withOldMacros (Harness& h, const Values& macros, int stateVersion = 3)
    {
        settle();
        juce::MemoryBlock blob;
        h.proc.getStateInformation (blob);
        auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
        jassert (xml != nullptr);
        for (auto* child : xml->getChildIterator())
            if (const auto it = macros.find (child->getStringAttribute ("id")); it != macros.end())
                child->setAttribute ("value", (double) it->second);
        xml->setAttribute ("stateVersion", stateVersion);
        juce::MemoryBlock out;
        juce::AudioProcessor::copyXmlToBinary (*xml, out);
        return out;
    }

    inline void load (Harness& h, const juce::MemoryBlock& blob)
    {
        h.proc.setStateInformation (blob.getData(), (int) blob.getSize());
        settle();
    }

    // 1.5 s of a synthetic vocal, then 1 s of silence (tails, ducking release)
    inline juce::AudioBuffer<float> signal (double fs = 48000.0)
    {
        VocalSpec s;
        s.seconds = 1.5;
        s.sampleRate = fs;
        s.sibilance = 0.6f;
        const auto v = makeVocal (s);
        juce::AudioBuffer<float> b (2, v.numSamples() + (int) fs);
        b.clear();
        for (int ch = 0; ch < 2; ++ch)
            b.copyFrom (ch, 0, v.vocal[0].data(), v.numSamples(), ch == 0 ? 1.0f : 0.9f);
        return b;
    }

    inline float maxAbsDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        float d = 0.0f;
        for (int ch = 0; ch < std::min (a.getNumChannels(), b.getNumChannels()); ++ch)
            for (int i = 0; i < std::min (a.getNumSamples(), b.getNumSamples()); ++i)
                d = std::max (d, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
        return d;
    }

    struct OldCase
    {
        juce::String name;
        std::function<void (Harness&)> configure;   // the module state the user left (no macros)
        Values macros;
        bool exact = true;                          // false: the one documented residual (see the ledger)
        bool asPreset = false;                      // a 0.6.0 user preset (snapshot format 2) applied to a fresh instance
    };

    inline Values allMacros (float presence, float warmth, float aggression, float width, float space, float density)
    {
        return { { pid::macroPresence, presence }, { pid::macroWarmth, warmth }, { pid::macroAggression, aggression },
                 { pid::macroWidth, width }, { pid::macroSpace, space }, { pid::macroDensity, density } };
    }

    inline void applyFactory (Harness& h, const char* name)
    {
        for (auto& p : getFactoryPresets())
            if (p.name == name)
                p.applyTo (h.proc.getState());
    }

    inline std::vector<OldCase> oldCases()
    {
        std::vector<OldCase> c;
        c.push_back ({ "0.5 project, every macro", [] (Harness& h) { load (h, state05()); }, allMacros (60, 40, 70, 80, 50, 30) });
        c.push_back ({ "0.5 project, macros at 100 %", [] (Harness& h) { load (h, state05()); }, allMacros (100, 100, 100, 100, 100, 100) });
        c.push_back ({ "fresh 0.6.0 instance, every macro", [] (Harness&) {}, allMacros (60, 40, 70, 100, 70, 30) });
        c.push_back ({ "fresh 0.6.0, SPACE had made sends", [] (Harness& h) {
                           h.proc.setRouting (dsp::Routing::fromString ("v2,eq,comp,deess,delay:send,reverb:send")); },
                       allMacros (0, 0, 0, 50, 100, 0) });
        c.push_back ({ "true-law inserts the user switched on (Mix pushed over 50 %)", [] (Harness& h) {
                           h.set (pid::reverbOn, 1.0f); h.set (pid::reverbMix, 40.0f);
                           h.set (pid::delayOn, 1.0f); h.set (pid::delayMix, 45.0f);
                           h.set (pid::doublerOn, 1.0f); h.set (pid::doublerMix, 30.0f); },
                       allMacros (0, 0, 0, 100, 100, 0) });
        c.push_back ({ "true-law Doubler at 70 % + WIDTH", [] (Harness& h) {
                           h.set (pid::doublerOn, 1.0f); h.set (pid::doublerMix, 70.0f); },
                       allMacros (20, 0, 0, 60, 0, 0) });
        c.push_back ({ "factory preset Aggressive Vocal + macros", [] (Harness& h) { applyFactory (h, "Aggressive Vocal"); },
                       allMacros (35, 25, 45, 40, 30, 55) });
        c.push_back ({ "factory preset Modern Pop + PRESENCE / WARMTH", [] (Harness& h) { applyFactory (h, "Modern Pop"); },
                       allMacros (80, 70, 0, 0, 0, 0) });
        c.push_back ({ "user EQ bands only (no role bands)", [] (Harness& h) {
                           for (int b = 1; b <= 16; ++b)
                               h.set (dsp::instanceParamId (pid::eqBandId (b, pid::eqband::role), 0).toRawUTF8(), 0.0f); },
                       allMacros (50, 50, 0, 0, 0, 0) });
        // The one case 0.6.0 played that parameters cannot express: a true-law Reverb insert whose own Mix is above
        // 50 % (its dry already fading) plus SPACE - the old macro raised the wet but froze the dry (see the ledger)
        c.push_back ({ "true-law Reverb insert at 60 % + SPACE (residual)", [] (Harness& h) {
                           h.set (pid::reverbOn, 1.0f); h.set (pid::reverbMix, 60.0f); },
                       allMacros (0, 0, 0, 0, 100, 0), false });
        c.push_back ({ "0.6.0 user preset (Modern Pop + every macro)", [] (Harness& h) { applyFactory (h, "Modern Pop"); },
                       allMacros (45, 35, 55, 65, 75, 25), true, true });
        c.push_back ({ "0.5 user preset (0.5 project + macros)", [] (Harness& h) { load (h, state05()); },
                       allMacros (30, 60, 20, 90, 40, 70), true, true });
        return c;
    }

    // Renders an old case through a processor that loaded it
    inline juce::ValueTree oldPresetTree (Harness& h, const Values& macros, bool format05)
    {
        settle();
        auto tree = ParameterSnapshot::capture (h.proc.getState(), "old preset").toValueTree();
        tree.setProperty ("version", 2, nullptr);
        if (format05)   // a 0.5 preset file: no EQ bands, no mix law
        {
            tree.removeProperty (statekeys::mixLaw, nullptr);
            for (int i = tree.getNumChildren(); --i >= 0;)
            {
                const auto id = tree.getChild (i).getProperty ("id").toString();
                if (id.startsWith ("eqBand") || id.startsWith ("eqLegacyResponse"))
                    tree.removeChild (i, nullptr);
            }
        }
        for (auto child : tree)
            if (const auto it = macros.find (child.getProperty ("id").toString()); it != macros.end())
                child.setProperty ("value", it->second, nullptr);
        return tree;
    }

    inline juce::AudioBuffer<float> renderOld (const OldCase& c, const juce::AudioBuffer<float>& in)
    {
        if (c.asPreset)
        {
            juce::ValueTree tree;
            {
                Harness h;
                c.configure (h);
                tree = oldPresetTree (h, c.macros, c.name.startsWith ("0.5"));
            }
            Harness r;
            ParameterSnapshot::fromValueTree (tree).applyTo (r.proc.getState());
            settle();
            return r.run (in);
        }
        juce::MemoryBlock blob;
        {
            Harness h;
            c.configure (h);
            blob = withOldMacros (h, c.macros);
        }
        Harness r;
        load (r, blob);
        return r.run (in);
    }
}

using namespace velvox::test::macrobaketest;

class MacroBakeTests final : public juce::UnitTest
{
public:
    MacroBakeTests() : juce::UnitTest ("Headliner macro bake", "Velvox") {}

    void runTest() override
    {
        baselineAgainstOtherBuild();
    }

    void baselineAgainstOtherBuild()
    {
        const auto writeDir = juce::SystemStats::getEnvironmentVariable ("VELVOX_MACRO_BASELINE_WRITE", {});
        const auto readDir = juce::SystemStats::getEnvironmentVariable ("VELVOX_MACRO_BASELINE", {});
        if (writeDir.isEmpty() && readDir.isEmpty())
            return;

        beginTest ("Old projects with macros: renders of this build vs the 0.6.0 build (env-gated)");
        const auto in = signal();
        int index = 0;
        for (const auto& c : oldCases())
        {
            const auto out = renderOld (c, in);
            const auto file = juce::File (writeDir.isNotEmpty() ? writeDir : readDir).getChildFile ("old" + juce::String (index++) + ".raw");
            if (writeDir.isNotEmpty())
            {
                file.getParentDirectory().createDirectory();
                juce::MemoryBlock raw;
                for (int ch = 0; ch < out.getNumChannels(); ++ch)
                    raw.append (out.getReadPointer (ch), sizeof (float) * (size_t) out.getNumSamples());
                expect (file.replaceWithData (raw.getData(), raw.getSize()), file.getFullPathName());
                continue;
            }
            juce::MemoryBlock raw;
            expect (file.loadFileAsData (raw), file.getFullPathName());
            juce::AudioBuffer<float> ref (out.getNumChannels(), out.getNumSamples());
            if (raw.getSize() != sizeof (float) * (size_t) (out.getNumChannels() * out.getNumSamples()))
            {
                expect (false, c.name + ": baseline size");
                continue;
            }
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                ref.copyFrom (ch, 0, static_cast<const float*> (raw.getData()) + (size_t) ch * (size_t) out.getNumSamples(), out.getNumSamples());
            const float d = maxAbsDiff (out, ref);
            logMessage (c.name + ": max difference to 0.6.0 " + juce::String (juce::Decibels::gainToDecibels (d, -400.0f), 1) + " dBFS");
            if (c.exact)
                expectLessThan (d, 1.0e-5f, c.name);
        }
    }
};

static MacroBakeTests macroBakeTests;
