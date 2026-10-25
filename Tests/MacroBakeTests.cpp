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
#include "MacroTestHelpers.h"
#include "Parameters/MacroBake.h"
#include "Parameters/MacroMapper.h"

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

    // Loads an old case into `r` (a project state, or a preset applied to a fresh instance)
    inline void loadOld (const OldCase& c, Harness& r, const Values& macros)
    {
        if (c.asPreset)
        {
            juce::ValueTree tree;
            {
                Harness h;
                c.configure (h);
                tree = oldPresetTree (h, macros, c.name.startsWith ("0.5"));
            }
            ParameterSnapshot::fromValueTree (tree).applyTo (r.proc.getState());
            settle();
            return;
        }
        juce::MemoryBlock blob;
        {
            Harness h;
            c.configure (h);
            blob = withOldMacros (h, macros);
        }
        load (r, blob);
    }

    inline juce::AudioBuffer<float> renderOld (const OldCase& c, const juce::AudioBuffer<float>& in)
    {
        Harness r;
        loadOld (c, r, c.macros);
        return r.run (in);
    }

    // The old semantics, rendered without the processor: the same case loaded with its macros at 0 (the
    // module state the user had), then dsp::applyMacros on top - what 0.2 - 0.6.0 processed - through a
    // bare chain with the routing the loaded project has
    inline juce::AudioBuffer<float> renderReference (const OldCase& c, const juce::AudioBuffer<float>& in, const dsp::Routing& routing)
    {
        Values zero;
        for (const auto& [id, v] : c.macros)
            zero[id] = 0.0f;
        Harness base;
        loadOld (c, base, zero);
        dsp::MacroValues m;
        for (int i = 0; i < dsp::numMacros; ++i)
            if (const auto it = c.macros.find (MacroMapper::paramIds()[(size_t) i]); it != c.macros.end())
                m.set ((dsp::MacroId) i, it->second * 0.01f);
        auto settings = dsp::applyMacros (base.proc.readSettings(), m);
        settings.routing = routing;
        auto chain = std::make_unique<dsp::VocalChain>();
        chain->prepare (base.fs, base.blockSize);
        juce::AudioBuffer<float> out (in);
        for (int start = 0; start < out.getNumSamples(); start += base.blockSize)
        {
            const int n = std::min (base.blockSize, out.getNumSamples() - start);
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), start, n);
            chain->process (view, out.getNumChannels(), settings, nullptr);
        }
        return out;
    }
}

using namespace velvox::test::macrobaketest;

class MacroBakeTests final : public juce::UnitTest
{
public:
    MacroBakeTests() : juce::UnitTest ("Headliner macro bake", "Velvox") {}

    void runTest() override
    {
        valueMapTests();
        nullTests();
        baselineAgainstOtherBuild();
    }

    void nullTests()
    {
        beginTest ("Old projects / presets with macros: baked once on load, they sound exactly as 0.6.0 played them");
        const auto in = signal();
        for (const auto& c : oldCases())
        {
            Harness r;
            loadOld (c, r, c.macros);
            const auto out = r.run (in);
            const auto ref = renderReference (c, in, r.proc.getRouting());
            const float d = maxAbsDiff (out, ref);
            logMessage (c.name + ": max difference to the old macro layer " + juce::String (juce::Decibels::gainToDecibels (d, -400.0f), 1) + " dBFS"
                        + " (law " + r.proc.getMixLaws().toString() + ")");
            if (c.exact)
                expectLessThan (d, 1.0e-5f, c.name);

            // No layer is left: the loaded parameters ARE what is processed, the positions are kept
            expect (macroFieldDifference (r.proc.readSettings(), r.proc.readEffectiveSettings()).isEmpty());
            for (const auto& [id, v] : c.macros)
                expectWithinAbsoluteError (r.get (id.toRawUTF8()), v, 1.0e-3f, c.name + ": slider position " + id);

            // Saved again and reloaded: nothing is baked twice
            if (! c.asPreset)
            {
                juce::MemoryBlock blob;
                r.proc.getStateInformation (blob);
                Harness again;
                load (again, blob);
                expectLessThan (maxAbsDiff (again.run (in), out), 1.0e-6f, c.name + ": reloaded");
                expect (macroFieldDifference (again.proc.readSettings(), r.proc.readSettings(), 1.0e-5f).isEmpty(), c.name + ": reload bakes nothing");
            }
        }

        beginTest ("A project saved by this version keeps its slider positions as positions (no bake)");
        {
            Harness h;
            h.neutral();
            h.set (pid::macroSpace, 60.0f);
            const float mix = h.get (pid::reverbMix);
            juce::MemoryBlock blob;
            h.proc.getStateInformation (blob);
            Harness r;
            load (r, blob);
            expectWithinAbsoluteError (r.get (pid::reverbMix), mix, 1.0e-4f);
            expectWithinAbsoluteError (r.get (pid::macroSpace), 60.0f, 1.0e-4f);
        }
    }

    static Values allValues (Harness& h)
    {
        Values v;
        for (auto* p : h.proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                v[rp->getParameterID()] = rp->convertFrom0to1 (rp->getValue());
        return v;
    }

    void valueMapTests()
    {
        beginTest ("Bake on a value map: the parameters alone are what applyMacros produced; positions stay");
        for (const auto& c : oldCases())
        {
            Harness h;
            c.configure (h);
            settle();
            auto values = allValues (h);
            for (const auto& [id, v] : c.macros)
                values[id] = v;
            const auto routing = h.proc.getRouting();
            auto laws = h.proc.getMixLaws();
            const auto access = eqbands::accessFor (values);

            const auto base = macrobake::readSettings (access, routing);
            auto expected = dsp::applyMacros (base, macrobake::readMacros (access));
            auto expectedLaws = laws;
            macrobake::keepDry (base, expected, routing, expectedLaws);

            expect (macrobake::bake (access, routing, &laws), c.name);
            const auto diff = macroFieldDifference (macrobake::readSettings (access, routing), expected, 1.0e-4f);
            expect (diff.isEmpty(), c.name + ": " + diff);
            expect (laws == expectedLaws, c.name);
            for (const auto& [id, v] : c.macros)
                expectEquals (values[id], v, c.name + ": position " + id);
            auto noMacros = allValues (h);
            const auto before = noMacros;
            expect (! macrobake::bake (eqbands::accessFor (noMacros), routing, &laws), "all macros at 0: nothing to bake");
            expect (noMacros == before);
        }

        beginTest ("keepDry: the 0.6.0 macros never took the dry of an additive insert away");
        {
            using dsp::ModuleId;
            const auto inserts = dsp::Routing::fromString ("v2,doubler,delay:insert,reverb:insert");
            const auto sends = dsp::Routing::fromString ("v2,doubler,delay:send,reverb:send");
            dsp::ChainSettings before;
            before.doubler.enabled = true;  before.doubler.mix = 0.3f;
            before.reverbOn = true;         before.reverbMix = 0.4f;
            before.delayOn = false;         before.delayMix = 0.2f;

            // Doubler 30 % -> 90 %, reverb insert 40 % -> 70 %, delay off -> 15 %: the first two need the additive law
            auto after = before;
            after.doubler.mix = 0.9f;
            after.reverbMix = 0.7f;
            after.delayOn = true; after.delayMix = 0.15f;
            auto laws = dsp::MixLaws::allTrue();
            expect (macrobake::keepDry (before, after, inserts, laws));
            expect (laws.isLegacy (ModuleId::doubler, 0) && laws.isLegacy (ModuleId::reverb, 0) && ! laws.isLegacy (ModuleId::delay, 0));
            expectEquals (after.doubler.dryDb, 0.0f, "the dry was at unity");

            // As sends the Delay / Reverb have no dry of their own
            auto afterSends = before;
            afterSends.reverbMix = 0.7f;
            auto lawsSends = dsp::MixLaws::allTrue();
            expect (! macrobake::keepDry (before, afterSends, sends, lawsSends));
            expect (lawsSends == dsp::MixLaws::allTrue());

            // A Doubler already above 50 % (dry 0.6): the additive law plus its Dry level keep that dry
            auto b70 = before;
            b70.doubler.mix = 0.7f;
            auto a70 = b70;
            a70.doubler.mix = 1.0f;
            auto laws70 = dsp::MixLaws::allTrue();
            expect (macrobake::keepDry (b70, a70, inserts, laws70));
            expect (laws70.isLegacy (ModuleId::doubler, 0));
            expectWithinAbsoluteError (a70.doubler.dryDb, juce::Decibels::gainToDecibels (0.6f), 1.0e-4f);

            // Nothing to keep: up to 50 %, lowered Mix, legacy instances
            auto small = before;
            small.doubler.mix = 0.5f;
            auto lawsSmall = dsp::MixLaws::allTrue();
            expect (! macrobake::keepDry (before, small, inserts, lawsSmall));
            auto legacy = dsp::MixLaws::allLegacy();
            expect (! macrobake::keepDry (before, after, inserts, legacy));
        }
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
