// v0.2 preset library, categories, format-1 compatibility and preset switching.

#include "TestHelpers.h"
#include "StateKeys.h"

using namespace velvox;
using namespace velvox::test;

namespace
{
    const ParameterSnapshot* findPreset (const juce::String& name)
    {
        for (auto& p : getFactoryPresets())
            if (p.name == name)
                return &p;
        return nullptr;
    }
}

class PresetTests final : public juce::UnitTest
{
public:
    PresetTests() : juce::UnitTest ("Velvox presets 0.2", "Velvox") {}

    void runTest() override
    {
        beginTest ("Library: categories, required presets, unique names");
        {
            juce::StringArray names, categories;
            for (auto& p : getFactoryPresets())
            {
                expect (! names.contains (p.name), "duplicate name " + p.name);
                names.add (p.name);
                categories.addIfNotAlreadyThere (p.category);
                expect (p.description.isNotEmpty(), p.name + " needs a description");
            }
            for (auto* c : { "Clean", "Punchy", "Aggressive", "Wide", "Creative", "Classic (v0.1)", "Utility" })
                expect (categories.contains (c), juce::String ("missing category ") + c);
            for (auto* n : { "Clean Lead", "Hypertechno Lead", "Aggressive Party Vocal", "Wide Festival Vocal", "Bright Pop Vocal",
                             "Telephone Vocal", "Vocal Chop", "Wide Adlibs", "Heavy Parallel Compression",
                             "Clean Vocal", "Modern Pop", "Hypertechno Lead v0.1", "Aggressive Vocal", "Wide Vocal" })
                expect (names.contains (n), juce::String ("missing preset ") + n);
        }

        beginTest ("New presets use the new modules");
        {
            expectEquals (findPreset ("Hypertechno Lead")->values.at (pid::doublerOn), 1.0f);
            expectEquals (findPreset ("Vocal Chop")->values.at (pid::chopOn), 1.0f);
            expectEquals (findPreset ("Wide Festival Vocal")->values.at (pid::reverbDuckAmount), 7.0f);
            expectEquals (findPreset ("Octave Down Layer")->values.at (pid::pitchSemitones), -12.0f);
            expectEquals (findPreset ("Telephone Vocal")->values.at (pid::lpfOn), 1.0f);
            expectEquals (findPreset ("Heavy Parallel Compression")->values.at (pid::compMix), 40.0f);
        }

        beginTest ("Telephone Vocal really band-limits the voice");
        {
            const auto level = [] (double freq)
            {
                Harness h;
                findPreset ("Telephone Vocal")->applyTo (h.proc.getState());
                const auto out = h.run (h.sine (freq, 0.02f, 1.0));
                return rmsDb (out, 24000, 24000);
            };
            const float mid = level (1500.0), low = level (100.0), high = level (9000.0);
            logMessage ("Telephone: 100 Hz " + juce::String (low - mid, 1) + " dB, 9 kHz " + juce::String (high - mid, 1) + " dB relative to 1.5 kHz");
            expectLessThan (low - mid, -25.0f);
            expectLessThan (high - mid, -25.0f);
        }

        beginTest ("Vocal Chop really chops");
        {
            Harness h;
            findPreset ("Vocal Chop")->applyTo (h.proc.getState());
            const auto out = h.run (h.sine (300.0, 0.2f, 2.0));
            float minWin = 0.0f, maxWin = -200.0f;
            minWin = 200.0f;
            for (int s = 24000; s + 1200 < out.getNumSamples(); s += 1200) // 25 ms windows
            {
                const float l = rmsDb (out, s, 1200);
                minWin = std::min (minWin, l);
                maxWin = std::max (maxWin, l);
            }
            logMessage ("Chop windows: loudest " + juce::String (maxWin, 1) + " dB, quietest " + juce::String (minWin, 1) + " dB");
            expectGreaterThan (maxWin - minWin, 20.0f);
        }

        beginTest ("Presets never touch input gain or the level-match preference");
        {
            Harness h;
            h.set (pid::inputGain, 6.0f);
            h.set (pid::levelMatchMode, 1.0f);
            h.set (pid::levelMatchRange, 9.0f);
            auto& pm = h.proc.getPresetManager();
            for (int i = 0; i < pm.getEntries().size(); ++i)
                pm.loadPreset (i);
            expectWithinAbsoluteError (h.get (pid::inputGain), 6.0f, 0.01f);
            expectEquals ((int) h.get (pid::levelMatchMode), 1);
            expectWithinAbsoluteError (h.get (pid::levelMatchRange), 9.0f, 0.01f);
        }

        beginTest ("Format-1 user preset file (v0.1) still loads");
        {
            Harness h;
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("VelvoxV1Presets_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
            dir.createDirectory();
            // Exactly what v0.1 wrote: version 1, no routing, includes inputGain
            const juce::String xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<VelvoxPreset name="Old One" category="User" description="" version="1">
  <Param id="compThreshold" value="-33.0"/>
  <Param id="inputGain" value="12.0"/>
  <Param id="reverbOn" value="1.0"/>
  <Param id="reverbMix" value="40.0"/>
</VelvoxPreset>)";
            expect (dir.getChildFile ("Old One" + PresetManager::fileExtension).replaceWithText (xml));

            PresetManager pm (h.proc.getState(), dir);
            auto r = dsp::Routing::defaults();
            r.insertAt (0, dsp::ModuleId::sat, 0);
            h.proc.setRouting (r);
            h.set (pid::doublerOn, 1.0f);   // a v0.2 module the old file does not know

            int idx = -1;
            for (int i = 0; i < pm.getEntries().size(); ++i)
                if (pm.getEntries()[i].name == "Old One")
                    idx = i;
            expect (idx >= 0 && pm.loadPreset (idx));
            expectWithinAbsoluteError (h.get (pid::compThreshold), -33.0f, 0.01f);
            expectWithinAbsoluteError (h.get (pid::reverbMix), 40.0f, 0.01f);
            expectEquals ((int) h.get (pid::doublerOn), 0, "unknown new modules reset to default (off)");
            expectWithinAbsoluteError (h.get (pid::inputGain), 0.0f, 0.01f, "input gain is not taken from presets");
            expect (h.proc.getRouting() == dsp::Routing::legacyDefaults(), "format 1 -> default routing");
            dir.deleteRecursively();
        }

        beginTest ("Switching presets while playing: no invalid values, no overs with clip guard");
        {
            Harness h;
            auto& pm = h.proc.getPresetManager();
            const auto in = h.sine (220.0, 0.5f, 0.25 * pm.getEntries().size() + 0.5);
            juce::MidiBuffer midi;
            float peak = 0.0f;
            int preset = 0, block = 0;
            for (int start = 0; start + 256 <= in.getNumSamples(); start += 256, ++block)
            {
                if (block % 47 == 0 && preset < pm.getEntries().size()) // about every 0.25 s
                {
                    pm.loadPreset (preset++);
                    h.set (pid::safetyClip, 1.0f);
                }
                juce::AudioBuffer<float> buf (2, 256);
                for (int ch = 0; ch < 2; ++ch)
                    buf.copyFrom (ch, 0, in, ch, start, 256);
                h.proc.processBlock (buf, midi);
                expect (allFinite (buf));
                peak = std::max (peak, peakOf (buf, 0, 256));
            }
            logMessage ("Switched through " + juce::String (preset) + " presets, output peak " + juce::String (peak, 3));
            expectEquals (preset, pm.getEntries().size());
            expectLessOrEqual (peak, 1.0f);
        }
    }
};

static PresetTests presetTests;
