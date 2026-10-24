// HeadlinerRefHost: renders a WAV offline through an installed VST3 (incl. WaveShell sub-plugins)
// or through one Headliner module, with latency compensation, and writes the result plus a JSON
// sidecar (latency, CPU per block, parameter values read back). Used by tools/refhost/bench.py.
//
//   HeadlinerRefHost --subs   --plugin <file.vst3>
//   HeadlinerRefHost --list   --plugin <file.vst3> [--sub <name>] [--set k=v ...] [--filter text]
//   HeadlinerRefHost --list   --headliner <moduleKey> [--filter text]
//   HeadlinerRefHost --render --plugin <file.vst3> [--sub <name>] --in a.wav --out b.wav
//                             [--set key=text]... [--setn key=0..1]... [--tail sec] [--block n] [--bpm x]
//   HeadlinerRefHost --render --headliner <moduleKey> [--instance k] --in a.wav --out b.wav [--set id=value]...
//
// key = parameter index, ID or name (unique prefix allowed). Exit codes: 0 ok, 1 usage / IO,
// 2 plugin not loadable, 3 parameter not found or not settable.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include "RefHostCore.h"
#include "HeadlinerModule.h"
#include <cstdio>

using namespace velvox;

namespace
{
    struct Args
    {
        juce::String mode, plugin, sub, headliner, in, out, filter;
        int instance = 0, block = 512;
        double tail = 0.0, bpm = 120.0;
        juce::StringArray sets, setns;
    };

    bool parseArgs (int argc, char* argv[], Args& a)
    {
        for (int i = 1; i < argc; ++i)
        {
            const juce::String k = juce::String::fromUTF8 (argv[i]);
            auto next = [&] () -> juce::String { return i + 1 < argc ? juce::String::fromUTF8 (argv[++i]) : juce::String(); };
            if (k == "--subs" || k == "--list" || k == "--render") a.mode = k.substring (2);
            else if (k == "--plugin")    a.plugin = next();
            else if (k == "--sub")       a.sub = next();
            else if (k == "--headliner") a.headliner = next();
            else if (k == "--instance")  a.instance = next().getIntValue();
            else if (k == "--in")        a.in = next();
            else if (k == "--out")       a.out = next();
            else if (k == "--filter")    a.filter = next();
            else if (k == "--set")       a.sets.add (next());
            else if (k == "--setn")      a.setns.add (next());
            else if (k == "--tail")      a.tail = next().getDoubleValue();
            else if (k == "--block")     a.block = std::max (16, next().getIntValue());
            else if (k == "--bpm")       a.bpm = next().getDoubleValue();
            else
            {
                std::fprintf (stderr, "unknown argument: %s\n", argv[i]);
                return false;
            }
        }
        return a.mode.isNotEmpty();
    }

    juce::File resolvePlugin (const juce::String& p)
    {
        if (juce::File::isAbsolutePath (p))
            return juce::File (p);
        return juce::File ("C:\\Program Files\\Common Files\\VST3").getChildFile (p);
    }

    juce::OwnedArray<juce::PluginDescription> typesIn (juce::VST3PluginFormat& fmt, const juce::File& f)
    {
        juce::OwnedArray<juce::PluginDescription> types;
        fmt.findAllTypesForFile (types, f.getFullPathName());
        return types;
    }

    std::unique_ptr<juce::AudioPluginInstance> loadPlugin (const Args& a, double fs, int block, juce::String& error)
    {
        juce::VST3PluginFormat fmt;
        const auto file = resolvePlugin (a.plugin);
        if (! file.exists())
        {
            error = "no such plugin: " + file.getFullPathName();
            return nullptr;
        }
        auto types = typesIn (fmt, file);
        if (types.isEmpty())
        {
            error = "no plugin types found in " + file.getFullPathName() + " (scan refused or crashed)";
            return nullptr;
        }
        const juce::PluginDescription* chosen = nullptr;
        if (a.sub.isEmpty())
            chosen = types[0];
        else
        {
            for (auto* t : types)
                if (t->name.equalsIgnoreCase (a.sub))
                    chosen = t;
            if (chosen == nullptr)
                for (auto* t : types)
                    if (t->name.containsIgnoreCase (a.sub))
                    {
                        chosen = t;
                        break;
                    }
        }
        if (chosen == nullptr)
        {
            error = "no sub-plugin named '" + a.sub + "' in " + file.getFileName();
            return nullptr;
        }
        auto inst = fmt.createInstanceFromDescription (*chosen, fs, block, error);
        // Some plugins finish their initialisation on the message thread
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        return inst;
    }

    std::unique_ptr<juce::AudioProcessor> makeHeadliner (const Args& a, juce::String& error)
    {
        const auto id = dsp::moduleFromKey (a.headliner);
        if (! id)
        {
            error = "unknown Headliner module key '" + a.headliner + "'";
            return nullptr;
        }
        return refhost::makeSingleModule (*id, a.instance);
    }

    // Headliner: plain numbers are real-unit values (choices: index); otherwise the text path
    bool setOne (juce::AudioProcessor& proc, const juce::String& assignment, bool normalized, bool headliner, juce::String& error)
    {
        const int eq = assignment.indexOfChar ('=');
        if (eq <= 0)
        {
            error = "expected key=value, got '" + assignment + "'";
            return false;
        }
        const auto key = assignment.substring (0, eq).trim();
        const auto value = assignment.substring (eq + 1).trim();
        auto* p = refhost::findParameter (proc, key);
        if (p == nullptr)
        {
            error = "parameter not found: '" + key + "'";
            return false;
        }
        if (normalized)
        {
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value.getFloatValue()));
            return true;
        }
        if (headliner && value.containsOnly ("0123456789.-+eE") && value.containsAnyOf ("0123456789"))
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                r->setValueNotifyingHost (r->convertTo0to1 (value.getFloatValue()));
                return true;
            }
        juce::String e;
        if (! refhost::setParameterText (*p, value, e))
        {
            error = e;
            return false;
        }
        return true;
    }

    bool applySettings (juce::AudioProcessor& proc, const Args& a, bool headliner, juce::var& readback)
    {
        auto* obj = new juce::DynamicObject();
        readback = juce::var (obj);
        for (int pass = 0; pass < 2; ++pass)
            for (auto& s : pass == 0 ? a.sets : a.setns)
            {
                juce::String err;
                if (! setOne (proc, s, pass == 1, headliner, err))
                {
                    std::fprintf (stderr, "error: %s\n", err.toRawUTF8());
                    return false;
                }
                auto* p = refhost::findParameter (proc, s.upToFirstOccurrenceOf ("=", false, false).trim());
                auto text = p->getCurrentValueAsText().trim();
                if (p->getLabel().isNotEmpty() && ! text.endsWithIgnoreCase (p->getLabel()))
                    text << " " << p->getLabel();
                obj->setProperty (p->getName (64), text);
            }
        return true;
    }

    bool readWav (const juce::File& f, juce::AudioBuffer<float>& b, double& fs)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr)
            return false;
        b.setSize ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
        fs = r->sampleRate;
        return true;
    }

    bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double fs)
    {
        f.deleteFile();
        f.getParentDirectory().createDirectory();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
        if (stream == nullptr)
            return false;
        if (auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (fs)
                                                      .withNumChannels (b.getNumChannels()).withBitsPerSample (32)
                                                      .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint)))
            return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return false;
    }

    int run (const Args& a)
    {
        if (a.mode == "subs")
        {
            juce::VST3PluginFormat fmt;
            auto types = typesIn (fmt, resolvePlugin (a.plugin));
            for (auto* t : types)
                std::printf ("%s\t%s\t%s\n", t->name.toRawUTF8(), t->manufacturerName.toRawUTF8(), t->category.toRawUTF8());
            return types.isEmpty() ? 2 : 0;
        }

        const bool headliner = a.headliner.isNotEmpty();
        double fs = 48000.0;
        juce::AudioBuffer<float> in;
        if (a.mode == "render")
        {
            if (! readWav (juce::File (a.in), in, fs))
            {
                std::fprintf (stderr, "error: cannot read %s\n", a.in.toRawUTF8());
                return 1;
            }
        }

        juce::String error;
        std::unique_ptr<juce::AudioProcessor> proc;
        if (headliner)
            proc = makeHeadliner (a, error);
        else
            proc = loadPlugin (a, fs, a.block, error);
        if (proc == nullptr)
        {
            std::fprintf (stderr, "error: %s\n", error.toRawUTF8());
            return headliner ? 1 : 2;
        }

        juce::var readback;
        if (! applySettings (*proc, a, headliner, readback))
            return 3;

        if (a.mode == "list")
        {
            auto text = refhost::describeParameters (*proc);
            if (a.filter.isNotEmpty())
            {
                juce::StringArray lines;
                lines.addLines (text);
                juce::StringArray keep;
                for (auto& l : lines)
                    if (l.containsIgnoreCase (a.filter))
                        keep.add (l);
                text = keep.joinIntoString ("\n");
            }
            std::printf ("%s: %d parameters, latency %d\n%s\n", proc->getName().toRawUTF8(), proc->getParameters().size(),
                         proc->getLatencySamples(), text.toRawUTF8());
            return 0;
        }

        refhost::RenderStats st;
        const auto out = refhost::renderOffline (*proc, in, fs, a.block, (int) std::lround (a.tail * fs), st, a.bpm);
        const juce::File outFile (a.out);
        if (! writeWav (outFile, out, fs))
        {
            std::fprintf (stderr, "error: cannot write %s\n", a.out.toRawUTF8());
            return 1;
        }

        auto* info = new juce::DynamicObject();
        info->setProperty ("processor", proc->getName());
        info->setProperty ("source", headliner ? "headliner:" + a.headliner : a.plugin + (a.sub.isNotEmpty() ? " / " + a.sub : ""));
        info->setProperty ("sampleRate", fs);
        info->setProperty ("block", a.block);
        info->setProperty ("latency", st.latency);
        info->setProperty ("blocks", st.blocks);
        info->setProperty ("meanBlockUs", st.meanBlockMicros);
        info->setProperty ("maxBlockUs", st.maxBlockMicros);
        info->setProperty ("params", readback);
        juce::File (a.out + ".json").replaceWithText (juce::JSON::toString (juce::var (info)));
        std::printf ("rendered %s: latency %d, %.1f us/block mean\n", outFile.getFileName().toRawUTF8(), st.latency, st.meanBlockMicros);
        return 0;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    Args a;
    if (! parseArgs (argc, argv, a))
    {
        std::fprintf (stderr, "usage: see the header of tools/refhost/RefHostMain.cpp\n");
        return 1;
    }
    const int rc = run (a);
    juce::DeletedAtShutdown::deleteAll();
    return rc;
}
