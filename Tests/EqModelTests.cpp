// EQ 0.6 model: parameters of the bands, migration of 0.5 states / presets, role bands
// (findOrCreateEqBand, macros, automatic mix)

#include "EqTestHelpers.h"
#include "Parameters/EqBands.h"

using namespace velvox;
using namespace velvox::test;
using namespace velvox::test::eq;

class EqParameterTests final : public juce::UnitTest
{
public:
    EqParameterTests() : juce::UnitTest ("Velvox EQ parameters", "Velvox") {}

    void runTest() override
    {
        beginTest ("Every EQ instance has 16 bands of parameters; bands default to off");
        {
            Harness h;
            auto& apvts = h.proc.getState();
            for (int k = 0; k < dsp::maxInstances (dsp::ModuleId::eq); ++k)
            {
                for (int n = 1; n <= dsp::maxEqBands; ++n)
                    for (auto* field : { pid::eqband::on, pid::eqband::type, pid::eqband::freq, pid::eqband::gain, pid::eqband::q, pid::eqband::slope, pid::eqband::role })
                        expect (apvts.getParameter (dsp::instanceParamId (pid::eqBandId (n, field), k)) != nullptr,
                                dsp::instanceParamId (pid::eqBandId (n, field), k));
                expect (apvts.getParameter (dsp::instanceParamId (pid::eqLegacyResponse, k)) != nullptr);
            }
            expect (apvts.getParameter ("eqBand16Role_4") != nullptr, "spelling: eqBand16Role_4");
            expect (apvts.getParameter ("eqBand17On") == nullptr, "no 17th band");

            expectEquals (h.get ("eqBand1On"), 0.0f);
            expectEquals (h.get ("eqBand1Type"), 0.0f);
            expectWithinAbsoluteError (h.get ("eqBand1Freq"), 1000.0f, 1.0e-3f);
            expectEquals (h.get ("eqBand1Gain"), 0.0f);
            expectWithinAbsoluteError (h.get ("eqBand1Q"), 1.0f, 1.0e-6f);
            expectEquals (h.get ("eqBand1Slope"), 3.0f);
            expectEquals (h.get ("eqBand1Role"), 0.0f);
            expectEquals (h.get (pid::eqLegacyResponse), 0.0f);

            const auto settings = h.proc.readSettings();
            for (const auto& b : settings.eq.bands)
                expect (b.isFree());
            expect (! settings.eq.legacyResponse);
        }

        beginTest ("Ranges: 10 Hz - 30 kHz, +-30 dB, Q 0.025 - 40, 8 types, 6 slopes, 8 roles");
        {
            const auto* f = findParameterSpec ("eqBand5Freq");
            const auto* g = findParameterSpec ("eqBand5Gain");
            const auto* q = findParameterSpec ("eqBand5Q");
            expect (f != nullptr && g != nullptr && q != nullptr);
            expectEquals (f->minValue, 10.0f);  expectEquals (f->maxValue, 30000.0f);
            expectEquals (g->minValue, -30.0f); expectEquals (g->maxValue, 30.0f);
            expectEquals (q->minValue, 0.025f); expectEquals (q->maxValue, 40.0f);
            expectEquals (findParameterSpec ("eqBand5Type")->choices.size(), dsp::numEqBandTypes);
            expectEquals (findParameterSpec ("eqBand5Slope")->choices.size(), dsp::numEqSlopes);
            expectEquals (findParameterSpec ("eqBand5Role")->choices.size(), dsp::numEqRoles);
            expectEquals (findParameterSpec ("eqBand5Type")->choices[7], juce::String ("Tilt"));
            expectEquals (findParameterSpec ("eqBand5Slope")->choices[4], juce::String ("36 dB/oct"));
        }

        beginTest ("Roles, the legacy flag and the 0.5 fixed bands are hidden and not automatable");
        {
            Harness h;
            auto& apvts = h.proc.getState();
            for (auto* id : { "eqBand3Role", "eqBand3Role_2", "eqLegacyResponse", "eqLegacyResponse_3",
                              "hpfOn", "hpfFreq", "hpfSlope", "lowFreq", "lowGain", "midFreq", "midGain", "midQ", "highFreq", "highGain",
                              "lowType", "lowQ", "highType", "highQ", "lpfOn", "lpfFreq", "lpfSlope", "lowGain_2", "lpfSlope_4" })
            {
                auto* p = apvts.getParameter (id);
                expect (p != nullptr, id);
                if (p != nullptr)
                    expect (! p->isAutomatable(), juce::String (id) + " not automatable");
                const auto* spec = findParameterSpec (id);
                expect (spec != nullptr && spec->hidden && ! spec->automatable, juce::String (id) + " hidden");
            }
            for (auto* id : { "eqBand3On", "eqBand3Type", "eqBand3Freq", "eqBand3Gain", "eqBand3Q", "eqBand3Slope", "eqOn", "eqMix", "eqBand9Gain_2" })
            {
                expect (apvts.getParameter (id)->isAutomatable(), juce::String (id) + " automatable");
                expect (! findParameterSpec (id)->hidden, juce::String (id) + " visible");
            }
        }

        beginTest ("Band parameters of every instance reach the DSP settings");
        {
            Harness h;
            h.set ("eqBand2On", 1.0f);
            h.set ("eqBand2Type", (float) dsp::EqBandType::highShelf);
            h.set ("eqBand2Freq", 8000.0f);
            h.set ("eqBand2Gain", 6.0f);
            h.set ("eqBand2Q", 0.5f);
            h.set ("eqBand2Slope", 5.0f);
            h.set ("eqBand2Role", (float) dsp::EqRole::air);
            h.set ("eqBand16On_3", 1.0f);
            h.set ("eqBand16Gain_3", -7.5f);
            h.set ("eqLegacyResponse_4", 1.0f);
            const auto s = h.proc.readSettings();
            const auto& b = s.eq.bands[1];
            expect (b.on && b.bandType() == dsp::EqBandType::highShelf && b.bandRole() == dsp::EqRole::air && b.slope == 5);
            expectWithinAbsoluteError (b.freq, 8000.0f, 0.05f);
            expectWithinAbsoluteError (b.gain, 6.0f, 1.0e-4f);
            expectWithinAbsoluteError (b.q, 0.5f, 1.0e-4f);
            expect (s.extra[1].eq.bands[15].on);
            expectWithinAbsoluteError (s.extra[1].eq.bands[15].gain, -7.5f, 1.0e-4f);
            expect (! s.extra[1].eq.legacyResponse && s.extra[2].eq.legacyResponse);
            expect (! s.extra[0].eq.bands[1].on, "instances stay separate");
        }

        beginTest ("A band set through its parameters is heard (fresh instance, EQ in the default chain)");
        {
            Harness h;
            h.neutral();
            h.set (pid::eqOn, 1.0f);
            h.set ("eqBand4On", 1.0f);
            h.set ("eqBand4Freq", 1000.0f);
            h.set ("eqBand4Gain", 12.0f);
            h.set ("eqBand4Q", 1.0f);
            const auto out = h.run (h.sine (1000.0, 0.05f, 1.0));
            const double gainDb = juce::Decibels::gainToDecibels (toneLevel (out, 24000, 24000, 1000.0, h.fs) / 0.05);
            expectWithinAbsoluteError (gainDb, 12.0, 0.1);
        }
    }
};

static EqParameterTests eqParameterTests;

//==============================================================================
namespace
{
    // A 0.5 project: the current state without any 0.6 EQ band values, with the given fixed-band values
    juce::MemoryBlock oldProjectState (const std::map<juce::String, float>& legacyValues, const juce::String& routing = "v2,eq")
    {
        Harness h;
        h.proc.setRouting (dsp::Routing::fromString (routing));
        juce::MemoryBlock block;
        h.proc.getStateInformation (block);
        auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
        auto tree = juce::ValueTree::fromXml (*xml);
        for (int i = tree.getNumChildren(); --i >= 0;)
        {
            const auto id = tree.getChild (i).getProperty ("id").toString();
            if (id.startsWith ("eqBand") || id.startsWith (pid::eqLegacyResponse))
                tree.removeChild (i, nullptr);
            else if (legacyValues.count (id) > 0)
                tree.getChild (i).setProperty ("value", legacyValues.at (id), nullptr);
        }
        juce::MemoryBlock out;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), out);
        return out;
    }

    std::map<juce::String, float> legacyValuesOf (const legacy::v05::EqSettings& o, int instance = 0)
    {
        const auto id = [instance] (const char* base) { return dsp::instanceParamId (base, instance); };
        return { { id (pid::hpfOn), o.hpfOn ? 1.0f : 0.0f }, { id (pid::hpfFreq), o.hpfFreq }, { id (pid::hpfSlope), (float) o.hpfSlope },
                 { id (pid::lowType), (float) o.lowType }, { id (pid::lowFreq), o.lowFreq }, { id (pid::lowGain), o.lowGain }, { id (pid::lowQ), o.lowQ },
                 { id (pid::midFreq), o.midFreq }, { id (pid::midGain), o.midGain }, { id (pid::midQ), o.midQ },
                 { id (pid::highType), (float) o.highType }, { id (pid::highFreq), o.highFreq }, { id (pid::highGain), o.highGain }, { id (pid::highQ), o.highQ },
                 { id (pid::lpfOn), o.lpfOn ? 1.0f : 0.0f }, { id (pid::lpfFreq), o.lpfFreq }, { id (pid::lpfSlope), (float) o.lpfSlope } };
    }

    // The fixed-band values the processor holds (after loading / clamping to the 0.5 ranges)
    legacy::v05::EqSettings asLoaded (Harness& h, int instance = 0)
    {
        const auto v = [&h, instance] (const char* base) { return h.get (dsp::instanceParamId (base, instance).toRawUTF8()); };
        legacy::v05::EqSettings o;
        o.hpfOn = v (pid::hpfOn) > 0.5f; o.hpfFreq = v (pid::hpfFreq); o.hpfSlope = (int) v (pid::hpfSlope);
        o.lowType = (int) v (pid::lowType); o.lowFreq = v (pid::lowFreq); o.lowGain = v (pid::lowGain); o.lowQ = v (pid::lowQ);
        o.midFreq = v (pid::midFreq); o.midGain = v (pid::midGain); o.midQ = v (pid::midQ);
        o.highType = (int) v (pid::highType); o.highFreq = v (pid::highFreq); o.highGain = v (pid::highGain); o.highQ = v (pid::highQ);
        o.lpfOn = v (pid::lpfOn) > 0.5f; o.lpfFreq = v (pid::lpfFreq); o.lpfSlope = (int) v (pid::lpfSlope);
        return o;
    }

    dsp::EqBand bandOf (Harness& h, int band0, int instance = 0)
    {
        return h.proc.readSettings().instance (instance).eq.bands[(size_t) band0];
    }

    legacy::v05::EqSettings typicalOldEq()
    {
        legacy::v05::EqSettings o;
        o.hpfFreq = 120.0f; o.hpfSlope = 0; o.lowFreq = 200.0f; o.lowGain = -3.0f; o.midFreq = 2500.0f; o.midGain = 4.0f; o.midQ = 2.0f;
        o.highFreq = 9000.0f; o.highGain = 5.0f; o.lpfOn = true; o.lpfFreq = 16000.0f; o.lpfSlope = 1;
        return o;
    }

    // Plays `in` through the processor with every module except the EQ inaudible
    juce::AudioBuffer<float> renderEqOnly (Harness& h, const juce::AudioBuffer<float>& in)
    {
        for (auto* id : { pid::compOn, pid::deessOn, pid::satOn, pid::delayOn, pid::reverbOn, pid::safetyClip, pid::bypass })
            h.set (id, 0.0f);
        h.set (pid::inputGain, 0.0f);
        h.set (pid::outputGain, 0.0f);
        h.set (pid::eqOn, 1.0f);
        h.set (pid::eqMix, 100.0f);
        h.proc.setRouting (dsp::Routing::fromString ("v2,eq"));
        return h.run (in);
    }
}

class EqMigrationTests final : public juce::UnitTest
{
public:
    EqMigrationTests() : juce::UnitTest ("Velvox EQ migration", "Velvox") {}

    void runTest() override
    {
        beginTest ("A 0.5 project loads into bands 1-5 with their roles and the legacy response");
        {
            const auto state = oldProjectState (legacyValuesOf (typicalOldEq()));
            Harness h;
            h.proc.setStateInformation (state.getData(), (int) state.getSize());
            expectEquals (h.get (pid::eqLegacyResponse), 1.0f, "legacy flag set by the migration");
            const auto expected = dsp::EqualizerModule::legacyBands (legacyFieldsOf (asLoaded (h)));
            for (int i = 0; i < 5; ++i)
            {
                const auto b = bandOf (h, i);
                const auto& e = expected[(size_t) i];
                expect (b.on == e.on && b.type == e.type && b.slope == e.slope && b.role == e.role, "band " + juce::String (i + 1));
                expectWithinAbsoluteError (b.freq, e.freq, 1.0e-3f * e.freq);
                expectWithinAbsoluteError (b.gain, e.gain, 1.0e-4f);
                expectWithinAbsoluteError (b.q, e.q, 1.0e-4f);
            }
            expect (bandOf (h, 0).bandRole() == dsp::EqRole::lowCut && bandOf (h, 0).bandType() == dsp::EqBandType::lowCut && bandOf (h, 0).slope == 1);
            expect (bandOf (h, 1).bandRole() == dsp::EqRole::warmth && bandOf (h, 2).bandRole() == dsp::EqRole::autoMix);
            expect (bandOf (h, 3).bandRole() == dsp::EqRole::air && bandOf (h, 4).bandRole() == dsp::EqRole::highCut && bandOf (h, 4).slope == 3);
            for (int i = 5; i < 16; ++i)
                expect (bandOf (h, i).isFree(), "band " + juce::String (i + 1) + " stays free");
        }

        beginTest ("A migrated 0.5 project renders null against the 0.5 EQ (<= -120 dB), at 44.1 and 48 kHz");
        for (double fs : { 44100.0, 48000.0 })
            for (const auto& o : { legacy::v05::EqSettings {}, typicalOldEq(), legacyCases()[2], legacyCases()[3] })
            {
                auto values = legacyValuesOf (o);
                values[pid::hpfSlope] = (float) std::min (o.hpfSlope, 1);   // the 0.5 parameter allows 12 / 24 dB/oct
                values[pid::lpfSlope] = (float) std::min (o.lpfSlope, 1);
                const auto state = oldProjectState (values);
                Harness h (fs, 512, 2);
                h.proc.setStateInformation (state.getData(), (int) state.getSize());
                const auto in = h.noise (0.2f, 1.0, 21);
                const auto out = renderEqOnly (h, in);

                legacy::v05::EqualizerModule ref;
                ref.setSettings (asLoaded (h));
                ref.prepare (fs, 256);
                auto expected = in;
                runModule (ref, expected);
                // From 100 ms on: the chain fades a module in after a state change (ModuleGate, as in 0.5).
                // Not bit-exact: band values pass through their own normalised parameter range (one ulp).
                const double d = diffDb (out, expected, (int) (0.1 * fs));
                expect (d <= -120.0, "null at " + juce::String (fs) + ": " + juce::String (d, 1) + " dB");
            }

        beginTest ("A 0.6 project round-trips without migration");
        {
            Harness a;
            a.set ("eqBand7On", 1.0f);
            a.set ("eqBand7Type", (float) dsp::EqBandType::notch);
            a.set ("eqBand7Freq", 3150.0f);
            {
                eqbands::ScopedBridgeSuspend noBridge (a.proc.getState());
                a.set (pid::hpfFreq, 300.0f);   // a stale fixed-band value must not come back as a band
            }
            juce::MemoryBlock block;
            a.proc.getStateInformation (block);
            Harness b;
            b.proc.setStateInformation (block.getData(), (int) block.getSize());
            expectEquals (b.get (pid::eqLegacyResponse), 0.0f);
            for (int i = 0; i < 16; ++i)
                expect (bandOf (b, i) == bandOf (a, i), "band " + juce::String (i + 1));
            expect (bandOf (b, 6).on && bandOf (b, 6).bandType() == dsp::EqBandType::notch);
            expectEquals (b.proc.readSettings().eq.findRole (dsp::EqRole::lowCut), -1);
        }

        beginTest ("Further EQ instances are migrated only when they were used");
        {
            legacy::v05::EqSettings second;
            second.hpfOn = false;
            second.highGain = 4.0f;
            auto values = legacyValuesOf (typicalOldEq());
            for (auto& [id, v] : legacyValuesOf (second, 1))
                values[id] = v;
            const auto state = oldProjectState (values, "v2,eq,comp,eq:1");
            Harness h;
            h.proc.setStateInformation (state.getData(), (int) state.getSize());
            expectEquals (h.get ("eqLegacyResponse_2"), 1.0f);
            expect (bandOf (h, 3, 1).on && bandOf (h, 3, 1).bandRole() == dsp::EqRole::air);
            expectWithinAbsoluteError (bandOf (h, 3, 1).gain, 4.0f, 1.0e-4f);
            expect (! bandOf (h, 0, 1).on, "low cut of EQ 2 was off");
            expectEquals (h.get ("eqLegacyResponse_3"), 0.0f, "EQ 3 untouched");
            for (int i = 0; i < 16; ++i)
                expect (bandOf (h, i, 2).isFree());
        }

        beginTest ("0.5 presets and snapshots apply sound-identically and are not reported as modified");
        {
            ParameterSnapshot old;
            old.name = "0.5 preset";
            for (auto& [id, v] : legacyValuesOf (typicalOldEq()))
                old.values[id] = v;
            old.values[pid::eqOn] = 1.0f;
            Harness h;
            h.set ("eqBand9On", 1.0f);   // something to be replaced
            old.applyTo (h.proc.getState(), true);
            expectEquals (h.get (pid::eqLegacyResponse), 1.0f);
            expect (bandOf (h, 1).bandRole() == dsp::EqRole::warmth);
            expectWithinAbsoluteError (bandOf (h, 3).gain, 5.0f, 1.0e-4f);
            expect (! bandOf (h, 8).on, "missing band values reset to their defaults");
            expect (! old.differsFrom (h.proc.getState()), "the preset counts as unmodified");
            h.set ("eqBand4Gain", 2.0f);
            expect (old.differsFrom (h.proc.getState()), "a band edit counts as a modification");
        }

        beginTest ("Factory presets carry bands with roles (matched response)");
        {
            int withBands = 0;
            for (const auto& p : getFactoryPresets())
            {
                expect (p.validate().isEmpty(), p.name + ": " + p.validate().joinIntoString ("; "));
                if (p.values.count ("eqBand1On") > 0)
                    ++withBands;
                expect (p.values.count (pid::eqLegacyResponse) == 0 || p.values.at (pid::eqLegacyResponse) < 0.5f, p.name);
            }
            expectEquals (withBands, (int) getFactoryPresets().size(), "every factory preset lists its EQ bands");
            Harness h;
            getFactoryPresets().front().applyTo (h.proc.getState(), true);
            expect (bandOf (h, 0).bandRole() == dsp::EqRole::lowCut);
            expectEquals (h.get (pid::eqLegacyResponse), 0.0f);
        }

        beginTest ("clearLegacyResponse switches a migrated EQ to the matched response");
        {
            const auto state = oldProjectState (legacyValuesOf (typicalOldEq()));
            Harness h;
            h.proc.setStateInformation (state.getData(), (int) state.getSize());
            const double before = dsp::EqualizerModule::responseDb (h.proc.readSettings().eq, 16000.0, 48000.0);
            eqbands::clearLegacyResponse (h.proc.getState(), 0);
            expectEquals (h.get (pid::eqLegacyResponse), 0.0f);
            const double after = dsp::EqualizerModule::responseDb (h.proc.readSettings().eq, 16000.0, 48000.0);
            expect (std::abs (after - before) > 0.2, "no longer cramped: " + juce::String (before, 2) + " -> " + juce::String (after, 2) + " dB at 16 kHz");
        }
    }
};

static EqMigrationTests eqMigrationTests;

//==============================================================================
class EqRoleBandTests final : public juce::UnitTest
{
public:
    EqRoleBandTests() : juce::UnitTest ("Velvox EQ role bands", "Velvox") {}

    void runTest() override
    {
        using dsp::EqRole;

        beginTest ("findOrCreateEqBand: creates, reuses, never touches user bands, -1 when full");
        {
            Harness h;
            auto& apvts = h.proc.getState();
            h.set ("eqBand1On", 1.0f);   // a user band in slot 1
            h.set ("eqBand1Freq", 300.0f);
            h.set ("eqBand1Gain", -4.0f);
            const auto user = bandOf (h, 0);

            expectEquals (findOrCreateEqBand (apvts, EqRole::mud, EqBandType::bell, 320.0f, -3.0f, 1.4f), 1, "first free slot");
            const auto b = bandOf (h, 1);
            expect (b.on && b.bandRole() == EqRole::mud && b.bandType() == EqBandType::bell);
            expectWithinAbsoluteError (b.freq, 320.0f, 0.05f);
            expectWithinAbsoluteError (b.gain, -3.0f, 1.0e-4f);
            expectWithinAbsoluteError (b.q, 1.4f, 1.0e-4f);
            expect (bandOf (h, 0) == user, "user band untouched");

            expectEquals (findOrCreateEqBand (apvts, EqRole::mud, EqBandType::bell, 280.0f, -5.0f, 2.0f), 1, "same role -> same band");
            expectWithinAbsoluteError (bandOf (h, 1).gain, -5.0f, 1.0e-4f);
            expectEquals (findOrCreateEqBand (apvts, EqRole::presence, EqBandType::bell, 3000.0f, 2.0f, 1.0f), 2);

            // deleted by the user (reset to the defaults) -> created again
            for (auto* field : { pid::eqband::on, pid::eqband::type, pid::eqband::freq, pid::eqband::gain, pid::eqband::q, pid::eqband::slope, pid::eqband::role })
            {
                auto* p = apvts.getParameter (pid::eqBandId (2, field));
                p->setValueNotifyingHost (p->getDefaultValue());
            }
            expect (bandOf (h, 1).isFree());
            expectEquals (findOrCreateEqBand (apvts, EqRole::mud, EqBandType::bell, 300.0f, -2.0f, 1.0f), 1, "re-created in the free slot");

            // instance 2
            expectEquals (findOrCreateEqBand (apvts, EqRole::air, EqBandType::highShelf, 10000.0f, 2.0f, 0.7f, 1), 0);
            expect (bandOf (h, 0, 1).bandRole() == EqRole::air && ! bandOf (h, 0, 2).on);
            expectEquals (findOrCreateEqBand (apvts, EqRole::none, EqBandType::bell, 300.0f, -2.0f, 1.0f), -1, "role none is not a role band");

            // all 16 slots used by the user: nothing happens
            Harness full;
            for (int n = 1; n <= 16; ++n)
                full.set (pid::eqBandId (n, pid::eqband::on).toRawUTF8(), 1.0f);
            const auto before = full.proc.readSettings().eq;
            expectEquals (findOrCreateEqBand (full.proc.getState(), EqRole::mud, EqBandType::bell, 300.0f, -3.0f, 1.0f), -1);
            for (int i = 0; i < 16; ++i)
                expect (bandOf (full, i) == before.bands[(size_t) i]);
        }

        beginTest ("Edits of the hidden 0.5 band parameters (old UI, assistant) reach the role bands");
        {
            Harness h;
            h.set (pid::hpfFreq, 150.0f);
            const int lowCut = h.proc.readSettings().eq.findRole (EqRole::lowCut);
            expect (lowCut >= 0, "low cut band created");
            if (lowCut >= 0)
            {
                const auto b = bandOf (h, lowCut);
                expect (b.on && b.bandType() == EqBandType::lowCut && b.slope == 3);   // 0.5 default: on, 24 dB/oct
                expectWithinAbsoluteError (b.freq, 150.0f, 0.05f);
                h.set (pid::hpfOn, 0.0f);
                expect (! bandOf (h, lowCut).on, "switched off");
            }
            h.set (pid::lowGain, 4.0f);
            const int warmth = h.proc.readSettings().eq.findRole (EqRole::warmth);
            expect (warmth >= 0 && bandOf (h, std::max (0, warmth)).bandType() == EqBandType::lowShelf);
            if (warmth >= 0)
            {
                expectWithinAbsoluteError (bandOf (h, warmth).gain, 4.0f, 1.0e-4f);
                expectWithinAbsoluteError (bandOf (h, warmth).freq, 150.0f, 0.05f);
            }
            h.set (pid::midQ, 3.0f);   // a flat mid band is not created
            expectEquals (h.proc.readSettings().eq.findRole (EqRole::autoMix), -1);
            h.set (dsp::instanceParamId (pid::highGain, 1).toRawUTF8(), 3.0f);   // instance 2
            expect (h.proc.readSettings().extra[0].eq.findRole (EqRole::air) >= 0);
            expectEquals (h.proc.readSettings().eq.findRole (EqRole::air), -1);
        }

        beginTest ("The bridge suspend is per plugin instance (two processors)");
        {
            Harness a, b;
            {
                eqbands::ScopedBridgeSuspend loadingA (a.proc.getState());   // e.g. A restores its state
                expect (eqbands::ScopedBridgeSuspend::isActive (a.proc.getState()));
                expect (! eqbands::ScopedBridgeSuspend::isActive (b.proc.getState()), "B is not suspended by A");
                b.set (pid::lowGain, 4.0f);   // meanwhile the user edits B
                a.set (pid::lowGain, 4.0f);
            }
            expect (b.proc.readSettings().eq.findRole (EqRole::warmth) >= 0, "B's bridge still works");
            expectEquals (a.proc.readSettings().eq.findRole (EqRole::warmth), -1, "A's bridge was suspended");
            expect (! eqbands::ScopedBridgeSuspend::isActive (a.proc.getState()), "released");
        }
    }
};

static EqRoleBandTests eqRoleBandTests;
