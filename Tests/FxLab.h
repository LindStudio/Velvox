#pragma once

// Effect lab (VelvoxTests --fxrender <outDir> [vocal.wav]): renders impulse responses and
// example audio through the production ReverbModule / DelayModule, for measurement
// (tools/bench/fx_analyse.py) and listening. Nothing here is used by the plugin.

#include <juce_audio_formats/juce_audio_formats.h>
#include "DSP/ReverbModule.h"
#include "DSP/DelayModule.h"
#include "SyntheticVocal.h"
#include "LegacyReverbV03.h"

namespace velvox::test
{
    inline bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double fs, int bits = 32)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
        if (stream == nullptr)
            return false;
        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (fs)
                                                           .withNumChannels (b.getNumChannels()).withBitsPerSample (bits)))
            return writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return false;
    }

    // Typical settings per reverb character (decay chosen like a mixing engineer would)
    inline dsp::ReverbSettings reverbExample (int type)
    {
        static constexpr float decays[] { 0.9f, 1.8f, 2.6f, 4.0f, 0.5f, 8.0f, 6.0f, 7.0f };
        static constexpr float sizes[]  { 0.4f, 0.6f, 0.7f, 0.8f, 0.3f, 0.9f, 0.85f, 0.8f };
        static constexpr float damp[]   { 7000, 9000, 6500, 6000, 9000, 7000, 3500, 12000 };
        dsp::ReverbSettings s;
        s.type = type;
        s.decaySeconds = decays[type];
        s.size = sizes[type];
        s.dampingHz = damp[type];
        s.preDelayMs = 20.0f;
        s.lowCutHz = 150.0f;
        s.highCutHz = type == 6 ? 7000.0f : 14000.0f;
        s.modulation = type >= 5 ? 0.6f : 0.3f;
        s.bassMultiplier = type >= 5 ? 0.8f : 1.0f;
        return s;
    }

    template <typename Fx>
    inline juce::AudioBuffer<float> renderWet (Fx& fx, const juce::AudioBuffer<float>& in, int tailSamples)
    {
        const int total = in.getNumSamples() + tailSamples;
        juce::AudioBuffer<float> src (2, total), out (2, total);
        src.clear();
        for (int ch = 0; ch < 2; ++ch)
            src.copyFrom (ch, 0, in, std::min (ch, in.getNumChannels() - 1), 0, in.getNumSamples());
        for (int start = 0; start < total; start += 256)
        {
            const int n = std::min (256, total - start);
            const float* ip[2] { src.getReadPointer (0, start), src.getReadPointer (1, start) };
            float* op[2] { out.getWritePointer (0, start), out.getWritePointer (1, start) };
            fx.process (ip, op, 2, n);
        }
        return out;
    }

    inline int runFxRender (const juce::File& outDir, const juce::File& vocalFile)
    {
        constexpr double fs = 48000.0;
        outDir.createDirectory();

        juce::AudioBuffer<float> impulse (2, 1);
        impulse.setSample (0, 0, 0.5f);
        impulse.setSample (1, 0, 0.5f);

        // Vocal for listening: a real file when given, else the synthetic test vocal
        juce::AudioBuffer<float> vocal;
        if (vocalFile.existsAsFile())
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor (vocalFile) })
            {
                const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 20.0));
                vocal.setSize ((int) std::min (2u, r->numChannels), n);
                r->read (&vocal, 0, n, 0, true, true);
                if (std::abs (r->sampleRate - fs) > 1.0)
                    std::printf ("note: %s is %.0f Hz, rendered as if 48 kHz\n", vocalFile.getFileName().toRawUTF8(), r->sampleRate);
            }
        }
        if (vocal.getNumSamples() == 0)
        {
            VocalSpec spec;
            spec.seconds = 8.0;
            spec.sampleRate = fs;
            spec.channels = 1;
            const auto v = makeVocal (spec);
            vocal.setSize (1, v.numSamples());
            vocal.copyFrom (0, 0, v.vocal[0].data(), v.numSamples());
        }

        // Reverb: impulse response and vocal (dry + wet at a typical send level) per character
        for (int type = 0; type < dsp::ReverbModule::numTypes; ++type)
        {
            const auto s = reverbExample (type);
            const auto name = juce::String (dsp::ReverbModule::voicing (type).name).replaceCharacter (' ', '_');

            dsp::ReverbModule rv;
            rv.setSettings (s);
            rv.prepare (fs, 256);
            writeWav (outDir.getChildFile ("ir_reverb_" + juce::String (type) + "_" + name + ".wav"),
                      renderWet (rv, impulse, (int) (fs * (s.decaySeconds * 1.6 + 1.0))), fs);

            dsp::ReverbModule rv2;
            rv2.setSettings (s);
            rv2.prepare (fs, 256);
            auto wet = renderWet (rv2, vocal, (int) (fs * (s.decaySeconds + 0.5)));
            juce::AudioBuffer<float> mix (2, wet.getNumSamples());
            mix.clear();
            for (int ch = 0; ch < 2; ++ch)
            {
                mix.copyFrom (ch, 0, vocal, std::min (ch, vocal.getNumChannels() - 1), 0, vocal.getNumSamples());
                mix.addFrom (ch, 0, wet, ch, 0, wet.getNumSamples(), 0.3f);
            }
            writeWav (outDir.getChildFile ("vocal_reverb_" + juce::String (type) + "_" + name + ".wav"), mix, fs, 24);
            writeWav (outDir.getChildFile ("wet_reverb_" + juce::String (type) + "_" + name + ".wav"), wet, fs, 32);
        }

        // Baseline: the 0.3 reverb (Room / Plate) with the same settings
        for (int type = 0; type < 2; ++type)
        {
            const auto s = reverbExample (type);
            legacy::ReverbV03 rv;
            rv.setSettings (s);
            rv.prepare (fs, 256);
            writeWav (outDir.getChildFile ("ir_v03reverb_" + juce::String (type) + ".wav"),
                      renderWet (rv, impulse, (int) (fs * (s.decaySeconds * 1.6 + 1.0))), fs);
            legacy::ReverbV03 rv2;
            rv2.setSettings (s);
            rv2.prepare (fs, 256);
            auto wet = renderWet (rv2, vocal, (int) (fs * (s.decaySeconds + 0.5)));
            writeWav (outDir.getChildFile ("wet_v03reverb_" + juce::String (type) + ".wav"), wet, fs, 32);
        }

        // Delay: impulse responses for timing / feedback / spread checks and a vocal example
        struct DelayCase { const char* name; dsp::DelaySettings s; };
        std::vector<DelayCase> cases;
        {
            dsp::DelaySettings s;
            s.timeMs = 375.0f; s.feedback = 0.5f; s.lowCutHz = 20.0f; s.highCutHz = 20000.0f;
            cases.push_back ({ "plain", s });
            auto pp = s; pp.pingPong = true;
            cases.push_back ({ "pingpong", pp });
            auto sp = s; sp.spreadMs = 20.0f;
            cases.push_back ({ "spread", sp });
            auto amb = s; amb.diffusion = 0.8f; amb.drive = 0.4f; amb.modulation = 0.4f; amb.lowCutHz = 300.0f; amb.highCutHz = 5000.0f;
            cases.push_back ({ "ambient", amb });
        }
        for (auto& c : cases)
        {
            dsp::DelayModule d;
            d.setSettings (c.s);
            d.prepare (fs, 256);
            writeWav (outDir.getChildFile (juce::String ("ir_delay_") + c.name + ".wav"), renderWet (d, impulse, (int) (fs * 4.0)), fs);

            dsp::DelayModule d2;
            d2.setSettings (c.s);
            d2.prepare (fs, 256);
            auto wet = renderWet (d2, vocal, (int) (fs * 2.0));
            for (int ch = 0; ch < 2; ++ch)
            {
                wet.applyGain (ch, 0, wet.getNumSamples(), 0.3f);
                wet.addFrom (ch, 0, vocal, std::min (ch, vocal.getNumChannels() - 1), 0, vocal.getNumSamples());
            }
            writeWav (outDir.getChildFile (juce::String ("vocal_delay_") + c.name + ".wav"), wet, fs, 24);
        }

        std::printf ("fxrender: wrote %d files to %s\n", outDir.getNumberOfChildFiles (juce::File::findFiles, "*.wav"),
                     outDir.getFullPathName().toRawUTF8());
        return 0;
    }
}
