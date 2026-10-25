#include "CheckEval.h"
#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginEditor.h"
#include "UI/UserSettings.h"
#include "UI/EqPanel.h"
#include "AutoMix/AutoMixEngine.h"
#include "SyntheticVocal.h"
#include "PopScan.h"
#include "FxLab.h"

struct StdoutLogger final : juce::Logger
{
    void logMessage (const juce::String& message) override
    {
        std::printf ("%s\n", message.toRawUTF8());
        std::fflush (stdout);
    }
};

static void writeWav24 (const juce::File& f, const juce::AudioBuffer<float>& audio, double sampleRate)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (f.createOutputStream().release());
    if (stream != nullptr)
        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sampleRate)
                                                                                     .withNumChannels (audio.getNumChannels()).withBitsPerSample (24)))
            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

// One line of the benchmark (--bench): what was measured, chosen and reached
struct BenchRow
{
    juce::String file, chosen, chosenScore;
    double seconds = 0.0, analysisSeconds = 0.0;
    int renders = 0;
    float inLufs = 0.0f, outLufs = 0.0f, inPeak = 0.0f, outPeak = 0.0f;
    float topIn = 0.0f, topOut = 0.0f, presenceIn = 0.0f, presenceOut = 0.0f, bodyIn = 0.0f, bodyOut = 0.0f;
    float sibIn = 0.0f, sibOut = 0.0f, baseTotal = 0.0f, chosenTotal = 0.0f;
    juce::File original, processed;   // whole-file audio: original (level matched) and the mix
};

// Runs the automatic mix on a real recording, exactly like the plugin would:
// the file is streamed through the bounded capture (as during playback), the
// captured section is analysed and mixed, and then the WHOLE file is rendered
// with the result. Writes <name>_velvox.wav and <name>_report.txt.
//   VelvoxTests --automix in.wav outDir
// With a BenchRow it also writes <name>_original.wav, level matched to the mix,
// and prints nothing.
static int runAutoMixOnFile (const juce::File& input, const juce::File& outDir, BenchRow* row = nullptr)
{
    using namespace velvox::automix;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (input));
    if (reader == nullptr)
    {
        std::printf ("Cannot read %s\n", input.getFullPathName().toRawUTF8());
        return 1;
    }

    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    const int total = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 300.0));
    juce::AudioBuffer<float> file (channels, total);
    reader->read (&file, 0, total, 0, true, channels > 1);

    // 1) Bounded capture, block by block (512) - the same code path as in the plugin
    CaptureBuffer capture;
    capture.arm (reader->sampleRate, channels, CaptureConfig {});
    for (int start = 0; start < total && capture.getState() != CaptureBuffer::State::complete; start += 512)
    {
        const int n = std::min (512, total - start);
        std::array<const float*, 2> p { file.getReadPointer (0, start), file.getReadPointer (channels - 1, start) };
        capture.push (p.data(), channels, nullptr, 0, n, true);
    }
    if (capture.getState() != CaptureBuffer::State::complete && ! capture.finishNow())
    {
        std::printf ("Not enough signal in %s\n", input.getFileName().toRawUTF8());
        return 1;
    }
    const auto captured = capture.copyOut();

    // 2) Analysis, plan, verification
    std::map<juce::String, float> current;
    for (auto& s : velvox::getParameterSpecs())
        current[s.id] = s.defaultValue;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto r = AutoMixEngine::run (captured, current);
    const double ms = juce::Time::getMillisecondCounterHiRes() - start;
    if (! r.ok)
    {
        std::printf ("Auto mix failed: %s\n", r.error.toRawUTF8());
        return 1;
    }

    // Diagnostics: VELVOX_OVERRIDE="rideOn=0,compAttack=3" changes the finished plan before
    // the whole-file render (the captured-section result above stays as mixed)
    auto finalValues = r.plan.values;
    for (auto& kv : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("VELVOX_OVERRIDE", {}), ",", {}))
        if (kv.containsChar ('='))
            finalValues[kv.upToFirstOccurrenceOf ("=", false, false).trim()] = kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue();

    // 3) Render the whole file with the result
    CapturedAudio whole;
    whole.sampleRate = reader->sampleRate;
    whole.channels = channels;
    for (int ch = 0; ch < channels; ++ch)
        whole.vocal.emplace_back (file.getReadPointer (ch), file.getReadPointer (ch) + total);
    CapturedAudio rendered;
    const auto wholeIn = AutoMixEngine::measure (whole);
    const auto wholeOut = AutoMixEngine::render (whole, current, finalValues, r.plan.routing, &rendered);

    outDir.createDirectory();
    const auto base = input.getFileNameWithoutExtension();
    {
        juce::AudioBuffer<float> out (channels, rendered.numSamples());
        for (int ch = 0; ch < channels; ++ch)
            out.copyFrom (ch, 0, rendered.vocal[(size_t) ch].data(), rendered.numSamples());
        for (int ch = 0; ch < channels; ++ch)                 // the plugin's clip guard
            for (int i = 0; i < out.getNumSamples(); ++i)
                out.setSample (ch, i, velvox::dsp::VocalChain::safetyClip (out.getSample (ch, i)));
        writeWav24 (outDir.getChildFile (base + "_velvox.wav"), out, reader->sampleRate);
    }

    if (row != nullptr)
    {
        // The original at the loudness of the mix, so a listener compares sound, not level
        juce::AudioBuffer<float> orig (file);
        orig.applyGain (juce::Decibels::decibelsToGain (wholeOut.lufs - wholeIn.lufs));
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < orig.getNumSamples(); ++i)
                orig.setSample (ch, i, velvox::dsp::VocalChain::safetyClip (orig.getSample (ch, i)));
        row->original = outDir.getChildFile (base + "_original.wav");
        row->processed = outDir.getChildFile (base + "_velvox.wav");
        writeWav24 (row->original, orig, reader->sampleRate);

        row->file = input.getFileName();
        row->seconds = total / reader->sampleRate;
        row->analysisSeconds = ms / 1000.0;
        row->renders = r.iterations;
        row->inLufs = wholeIn.lufs;    row->outLufs = wholeOut.lufs;
        row->inPeak = wholeIn.peakDb;  row->outPeak = wholeOut.peakDb;
        row->topIn = r.original.topDb; row->topOut = r.processed.topDb;
        row->presenceIn = r.original.presenceDb; row->presenceOut = r.processed.presenceDb;
        row->bodyIn = r.original.bodyDb; row->bodyOut = r.processed.bodyDb;
        row->sibIn = r.original.sibilanceVsVowelDb; row->sibOut = r.processed.sibilanceVsVowelDb;
        if (! r.candidates.empty())
        {
            const auto& chosen = r.candidates[(size_t) juce::jlimit (0, (int) r.candidates.size() - 1, r.chosenCandidate)];
            row->chosen = chosen.name;
            row->chosenScore = chosen.score.describe();
            row->baseTotal = r.candidates.front().score.total();
            row->chosenTotal = chosen.score.total();
        }
    }

    juce::String rep;
    const auto& p = r.profile;
    rep << "Velvox auto mix report - " << input.getFileName() << "\n"
        << "File: " << juce::String (total / reader->sampleRate, 1) << " s, " << channels << " ch, " << reader->sampleRate << " Hz\n"
        << "Captured: " << juce::String (p.capturedSeconds, 1) << " s, voiced " << juce::String (p.voicedSeconds, 1) << " s"
        << (r.plan.conservative ? "  (conservative)" : "") << "; analysis + " << r.iterations << " renders: " << juce::String (ms / 1000.0, 2) << " s\n\n"
        << "MEASURED\n"
        << "  integrated " << juce::String (p.integratedLufs, 1) << " LUFS, LRA " << juce::String (p.loudnessRangeDb, 1) << " LU, peak "
        << juce::String (p.peakDb, 1) << " dBFS, crest " << juce::String (p.crestDb, 1) << " dB, clip runs " << p.clipRuns << "\n"
        << "  voice RMS " << juce::String (p.voiceRmsDb, 1) << " dBFS (p10 " << juce::String (p.voiceRmsLowDb, 1) << ", p90 " << juce::String (p.voiceRmsHighDb, 1)
        << "), noise floor " << (p.snrValid ? juce::String (p.noiseFloorDb, 1) + " dBFS, SNR " + juce::String (p.snrDb, 1) + " dB" : juce::String ("n/a")) << "\n"
        << "  f0 " << (p.pitchValid ? juce::String (p.f0LowHz, 0) + " / " + juce::String (p.f0MedianHz, 0) + " / " + juce::String (p.f0HighHz, 0) + " Hz (p10/median/p90)" : juce::String ("n/a")) << "\n"
        << "  bands vs core: rumble " << juce::String (p.rumbleDb, 1) << ", body " << juce::String (p.bodyDb, 1) << ", lowmid " << juce::String (p.lowMidDb, 1)
        << ", presence " << juce::String (p.presenceDb, 1) << ", sibilance " << juce::String (p.sibilanceBandDb, 1) << ", air " << juce::String (p.airDb, 1)
        << ", top " << juce::String (p.topDb, 1) << " dB; tilt " << juce::String (p.tiltDbPerOctave, 1) << " dB/oct\n"
        << "  S vs vowels " << (p.sibilanceValid ? juce::String (p.sibilanceVsVowelDb, 1) + " dB at " + juce::String (p.sibilanceHz, 0) + " Hz" : juce::String ("n/a"))
        << ", tail " << juce::String (p.tailSeconds, 2) << " s (" << p.tailCount << "), stereo " << (p.stereoContent ? "yes, corr " + juce::String (p.correlation, 2) : juce::String ("no")) << "\n\n"
        << "ISSUES\n";
    for (auto& i : p.issues)
        rep << "  [" << (i.confidence == Confidence::measured ? "measured" : i.confidence == Confidence::likely ? "likely" : "possible") << "] " << i.title << " - " << i.detail << "\n";
    rep << "\nDECISIONS\n";
    for (auto& d : r.plan.decisions)
        rep << "  " << (d.outputStage ? juce::String ("Output") : juce::String (velvox::dsp::moduleDisplayName (d.module))) << ": " << d.title
            << (d.measured ? "  (measured)" : "  (rule)") << "\n      " << d.reason << "\n";
    for (auto& n : r.plan.notes)
        rep << "  note: " << n << "\n";
    for (auto& a : r.adjustments)
        rep << "  verify: " << a << "\n";
    rep << "\nRESULT (captured section)\n"
        << "  " << juce::String (r.original.lufs, 1) << " -> " << juce::String (r.processed.lufs, 1) << " LUFS, peak " << juce::String (r.processed.peakDb, 1)
        << " dBFS, comp GR median " << juce::String (r.processed.compGrMedianDb, 1) << " dB (max " << juce::String (r.processed.compGrMaxDb, 1)
        << "), de-ess max " << juce::String (r.processed.deessGrMaxDb, 1) << " dB\n"
        << "  top " << juce::String (r.original.topDb, 1) << " -> " << juce::String (r.processed.topDb, 1) << ", body " << juce::String (r.original.bodyDb, 1)
        << " -> " << juce::String (r.processed.bodyDb, 1) << ", S vs vowels " << juce::String (r.original.sibilanceVsVowelDb, 1) << " -> "
        << juce::String (r.processed.sibilanceVsVowelDb, 1) << ", mono loss " << juce::String (r.processed.monoLossDb, 2) << " dB\n"
        << "RESULT (whole file)\n"
        << "  " << juce::String (wholeIn.lufs, 1) << " -> " << juce::String (wholeOut.lufs, 1) << " LUFS, peak " << juce::String (wholeIn.peakDb, 1) << " -> "
        << juce::String (wholeOut.peakDb, 1) << " dBFS (before clip guard)\n\nVALUES\n";
    for (auto& [id, v] : r.plan.values)
        rep << "  " << id << " = " << juce::String (v, 2) << "\n";

    outDir.getChildFile (base + "_report.txt").replaceWithText (rep);
    if (row == nullptr)
        std::printf ("%s\n", rep.toRawUTF8());
    return 0;
}

static bool loadCaptured (const juce::File& f, velvox::automix::CapturedAudio& out)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr)
        return false;
    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    const int total = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 300.0));
    juce::AudioBuffer<float> buffer (channels, total);
    reader->read (&buffer, 0, total, 0, true, channels > 1);
    out.sampleRate = reader->sampleRate;
    out.channels = channels;
    out.vocal.clear();
    for (int ch = 0; ch < channels; ++ch)
        out.vocal.emplace_back (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + total);
    return total > 0;
}

// Scores any finished mix with the current objective, e.g. renders of an older version:
//   VelvoxTests --score original.wav mix.wav
// Prints "score <total> | <breakdown> | <mix LUFS> <mix peak>" (whole files, effects assumed planned)
static int runScore (const juce::File& originalFile, const juce::File& mixFile)
{
    using namespace velvox::automix;
    CapturedAudio original, mix;
    if (! loadCaptured (originalFile, original) || ! loadCaptured (mixFile, mix))
    {
        std::printf ("Cannot read the input files\n");
        return 1;
    }
    const auto profile = VocalProfiler::analyse (original);
    auto mo = AutoMixEngine::measure (original), mm = AutoMixEngine::measure (mix);
    mo.sibilanceVsVowelDb = profile.sibilanceVsVowelDb;   // as the engine does
    mo.sibilanceValid = profile.sibilanceValid;
    if (auto sib = VocalProfiler::sibilanceAt (mix, profile))
    {
        mm.sibilanceVsVowelDb = *sib;
        mm.sibilanceValid = true;
    }
    const auto s = evaluate (mm, mo, profile, AutoMixEngine::targetLufs (mo.lufs), true, true);
    std::printf ("score %.2f | %s | %.1f LUFS %.1f dBFS\n", s.total(), s.describe().toRawUTF8(), mm.lufs, mm.peakDb);
    return 0;
}

// Benchmark over a folder of vocals (recursive, *.wav), for comparing versions and
// for a blind listening test:
//   VelvoxTests --bench <dir> <outDir>
// Writes outDir/bench.csv (one row per file), outDir/renders/ (mix + report per file)
// and outDir/listening/: NN_A.wav / NN_B.wav = original and mix in random order at the
// same loudness, the answer in listening_key.csv (open it only after listening).
static int runBench (const juce::File& dir, const juce::File& outDir)
{
    auto files = dir.findChildFiles (juce::File::findFiles, true, "*.wav");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFullPathName() < b.getFullPathName(); });
    if (files.isEmpty())
    {
        std::printf ("No .wav files in %s\n", dir.getFullPathName().toRawUTF8());
        return 1;
    }

    const auto renders = outDir.getChildFile ("renders"), listening = outDir.getChildFile ("listening");
    renders.createDirectory();
    listening.createDirectory();

    const auto csvField = [] (const juce::String& s) { return "\"" + s.replace ("\"", "\"\"") + "\""; };
    const auto num = [] (double v) { return juce::String (v, 2); };
    juce::String csv ("file,seconds,analysis_s,renders,in_lufs,out_lufs,in_peak,out_peak,top_in,top_out,presence_in,presence_out,"
                      "body_in,body_out,s_vs_vowel_in,s_vs_vowel_out,base_score,chosen_score,chosen,chosen_breakdown\n");
    juce::String key ("pair,file,A,B\n");
    juce::Random random (20261024);   // fixed seed: the same folder always gives the same kit
    int ok = 0, failed = 0;
    double scoreBase = 0.0, scoreChosen = 0.0;

    for (auto& f : files)
    {
        std::printf ("[%d/%d] %s ... ", ok + failed + 1, files.size(), f.getFileName().toRawUTF8());
        std::fflush (stdout);
        BenchRow row;
        if (runAutoMixOnFile (f, renders, &row) != 0)
        {
            ++failed;
            std::printf ("skipped\n");
            csv << csvField (f.getFileName()) << ",,,,,,,,,,,,,,,,,,\"failed\",\n";
            continue;
        }
        ++ok;
        scoreBase += row.baseTotal;
        scoreChosen += row.chosenTotal;
        std::printf ("%.2f -> %.2f (%s)\n", row.baseTotal, row.chosenTotal, row.chosen.toRawUTF8());
        csv << csvField (row.file) << "," << num (row.seconds) << "," << num (row.analysisSeconds) << "," << row.renders << ","
            << num (row.inLufs) << "," << num (row.outLufs) << "," << num (row.inPeak) << "," << num (row.outPeak) << ","
            << num (row.topIn) << "," << num (row.topOut) << "," << num (row.presenceIn) << "," << num (row.presenceOut) << ","
            << num (row.bodyIn) << "," << num (row.bodyOut) << "," << num (row.sibIn) << "," << num (row.sibOut) << ","
            << num (row.baseTotal) << "," << num (row.chosenTotal) << "," << csvField (row.chosen) << "," << csvField (row.chosenScore) << "\n";

        const auto pair = juce::String (ok).paddedLeft ('0', 2);
        const bool mixIsA = random.nextBool();
        row.original.copyFileTo (listening.getChildFile (pair + (mixIsA ? "_B.wav" : "_A.wav")));
        row.processed.copyFileTo (listening.getChildFile (pair + (mixIsA ? "_A.wav" : "_B.wav")));
        key << pair << "," << csvField (row.file) << "," << (mixIsA ? "mix,original" : "original,mix") << "\n";
    }

    outDir.getChildFile ("bench.csv").replaceWithText (csv);
    outDir.getChildFile ("listening_key.csv").replaceWithText (key);
    std::printf ("\n%d files mixed, %d skipped. Mean score: base plan %.2f -> chosen %.2f\nWritten to %s\n", ok, failed,
                 ok > 0 ? scoreBase / ok : 0.0, ok > 0 ? scoreChosen / ok : 0.0, outDir.getFullPathName().toRawUTF8());
    return ok > 0 ? 0 : 1;
}

// Renders the real editor to a PNG (for visual review without a DAW):
//   VelvoxTests --snapshot out.png [presetName] [width] [beginner|pro] [moduleId|-1] [overlay]
// overlay: "help:<page>", "welcome", "tutorial:<step>", "bypass", "hide:<key,key>", "focus:<moduleId>", "edit",
//          "intro" (analysis popup), "listening", "capturing", "analysing", "mixed" (overview after a synthetic auto mix),
//          "mixed+done" (with the confirmation card), "more" (the panel's Mehr drawer open), "details" (auto-mix report),
//          "eq" (EQ panel with user bands, a macro band and a selection), "eqmore" (+ Mehr, migrated legacy curve),
//          "eqcard" (+ a point being dragged: its value card)
//          "set:<id>=<value>,..." (parameters in real units)
//          prefixes, joined with ';': "reso" (the live vocal gets a 3.15 kHz resonance), "more" (+ Mehr open),
//          "route:<routing string, ',' or '|'>"; e.g. "reso;more;route:v2|eq|smooth;set:smoothOn=1"
static int renderSnapshot (const juce::File& file, const juce::String& presetName, int width,
                           const juce::String& mode, int selection, const juce::String& rawOverlay)
{
    using namespace velvox;
    bool reso = false, moreOpen = false;
    juce::String overlay, routeText;
    for (auto& token : juce::StringArray::fromTokens (rawOverlay, ";", ""))
    {
        if (token == "reso") reso = true;
        else if (token.startsWith ("route:")) routeText = token.fromFirstOccurrenceOf (":", false, false);
        else if (token == "more" && rawOverlay.contains (";")) moreOpen = true;
        else overlay = token;
    }
    // Never touch the real user preferences; the welcome card only when asked for
    const auto prefs = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("velvox-snapshot.settings");
    prefs.deleteFile();
    ui::settings::useFileForTesting (prefs);
    ui::settings::set (ui::settings::tutorialDone, overlay != "welcome");

    VelvoxAudioProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    proc.getState().state.setProperty ("uiMode", mode.isEmpty() ? juce::String ("beginner") : mode, nullptr);
    proc.getState().state.setProperty ("uiSelection", selection, nullptr);

    auto& pm = proc.getPresetManager();
    for (int i = 0; i < pm.getEntries().size(); ++i)
        if (pm.getEntries()[i].name == presetName)
            pm.loadPreset (i);

    auto& am = proc.getAutoMix();
    if (overlay != "intro" && overlay != "welcome")
        am.dismissPrompt();
    if (overlay.startsWith ("mixed"))
    {
        test::VocalSpec s;
        s.seconds = 14.0;
        s.channels = 2;
        s.sibilance = 1.2f;
        s.rumbleDb = -30.0f;
        am.applyResult (am.runSynchronously (test::makeVocal (s)));
    }

    if (routeText.isNotEmpty())   // e.g. "route:v2,eq,smooth,smooth#2,pitch"
        proc.setRouting (dsp::Routing::fromString (routeText.replaceCharacter ('|', ',')));

    // Feed a synthetic vocal so meters show activity
    test::VocalSpec live;
    live.seconds = 3.0;
    live.channels = 2;
    if (reso)
    {
        live.resonanceHz = 3150.0f;
        live.resonanceDb = 14.0f;
    }
    const auto voice = test::makeVocal (live);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    const auto pump = [&] (int blocks)
    {
        static int pos = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, voice.vocal[(size_t) ch].data() + pos, 512);
            pos = (pos + 512) % (voice.numSamples() - 512);
            proc.processBlock (buffer, midi);
        }
    };
    pump (40);

    // "check": VOCAL CHECK in the chain after a problematic take (rumble, resonance, noise, big level range)
    if (overlay == "check")
    {
        proc.getState().getParameter ("checkOn")->setValueNotifyingHost (1.0f);
        test::VocalSpec s;
        s.seconds = 14.0;
        s.channels = 2;
        s.rumbleDb = -26.0f;
        s.resonanceHz = 3150.0f;
        s.resonanceDb = 16.0f;
        s.noiseDb = -46.0f;
        s.phraseRangeDb = 18.0f;
        const auto v = test::makeVocal (s);
        for (int start = 0; start + 512 <= v.numSamples(); start += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                buffer.copyFrom (ch, 0, v.vocal[(size_t) ch].data() + start, 512);
            proc.processBlock (buffer, midi);
            proc.pumpAnalyzer();
        }
        proc.refreshAnalysis();
        proc.getState().state.setProperty ("uiSelection", (int) velvox::dsp::ModuleId::check, nullptr);
    }
    if (overlay.startsWith ("eq"))
    {
        // A typical vocal EQ: low cut, a mud cut, a presence bell, an air shelf from the Presence macro, a notch
        using velvox::dsp::EqBandType;
        auto& pr = proc;
        const auto cut = ui::eqedit::addBand (pr, 0, 90.0f, 0.0f);
        ui::eqedit::setType (pr, 0, cut, EqBandType::lowCut);
        ui::eqedit::addBand (pr, 0, 320.0f, -3.5f);
        const auto presence = ui::eqedit::addBand (pr, 0, 3200.0f, 2.5f);
        pr.getState().getParameter (ui::eqedit::paramId (0, presence, velvox::pid::eqband::q))->setValueNotifyingHost (
            pr.getState().getParameter (ui::eqedit::paramId (0, presence, velvox::pid::eqband::q))->convertTo0to1 (0.8f));
        const auto notch = ui::eqedit::addBand (pr, 0, 6500.0f, 0.0f);
        ui::eqedit::setType (pr, 0, notch, EqBandType::notch);
        pr.getState().getParameter ("macroPresence")->setValueNotifyingHost (0.5f);
        pr.getState().getParameter ("eqOn")->setValueNotifyingHost (1.0f);
        if (overlay == "eqmore")
            pr.getState().getParameter ("eqLegacyResponse")->setValueNotifyingHost (1.0f);
        proc.getState().state.setProperty ("uiSelection", (int) velvox::dsp::ModuleId::eq, nullptr);
        proc.getState().state.setProperty ("uiMode", "pro", nullptr);
    }
    if (overlay == "bypass")
        proc.getState().getParameter ("bypass")->setValueNotifyingHost (1.0f);
    if (overlay.startsWith ("hide:"))
        proc.getState().state.setProperty ("uiHiddenModules", overlay.fromFirstOccurrenceOf (":", false, false), nullptr);
    // "set:<id>=<value>,<id>=<value>": parameters in real units (e.g. set:reverbType=3,reverbMix=40)
    if (overlay.startsWith ("set:"))
        for (auto& pair : juce::StringArray::fromTokens (overlay.fromFirstOccurrenceOf (":", false, false), ",", ""))
            if (auto* p = proc.getState().getParameter (pair.upToFirstOccurrenceOf ("=", false, false).trim()))
                p->setValueNotifyingHost (p->convertTo0to1 (pair.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    if (overlay == "edit")
        proc.getState().getParameter ("compRatio")->setValueNotifyingHost (0.6f);
    if (overlay == "listening" || overlay == "capturing" || overlay == "analysing")
        am.start();

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    if (width > 0)
        editor->setSize (width, width * ui::baseHeight / ui::baseWidth);

    if (auto* ed = dynamic_cast<VelvoxAudioProcessorEditor*> (editor.get()))
    {
        if (overlay.startsWith ("help:"))
            ed->showHelp (overlay.fromFirstOccurrenceOf (":", false, false));
        if (overlay.startsWith ("tutorial:"))
        {
            ed->startTutorial (false);
            ed->getTutorial().goTo (overlay.fromFirstOccurrenceOf (":", false, false).getIntValue());
        }
        if (overlay.startsWith ("focus:"))
            ed->getChainView().onSelect (overlay.fromFirstOccurrenceOf (":", false, false).getIntValue());
        if ((overlay == "more" || moreOpen) && ed->getDetail() != nullptr)
            ed->getDetail()->setMoreOpen (true);
        if (auto* eqp = dynamic_cast<ui::EqPanel*> (ed->getDetail()))
        {
            eqp->selectBand (2);
            if (overlay == "eqmore")
                eqp->setMoreOpen (true);
            if (overlay == "eqcard")
                if (auto* g = eqp->getGraph())
                {
                    const auto at = g->pointFor (2);
                    const auto now = juce::Time::getCurrentTime();
                    g->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, {}, 1.0f, 0, 0, 0, 0,
                                                    g, g, now, at, now, 1, false));
                }
        }
        if (overlay == "details")
            ed->showMixDetails();
        if (overlay == "mixed+done")
            ed->getAnalysisOverlay().showProgress();
    }

    // Let the editor timer run a few frames (and the capture fill up for "capturing")
    for (int i = 0; i < (overlay == "capturing" ? 30 : 8); ++i)
    {
        pump (overlay == "capturing" ? 12 : 1);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream (image, out);
    std::printf ("%s %s\n", ok ? "Wrote" : "FAILED to write", file.getFullPathName().toRawUTF8());
    return ok ? 0 : 1;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit; // message manager for APVTS / parameter listeners

    if (argc >= 2 && juce::String (argv[1]) == "--sizes")
    {
        std::printf ("processor %d, chain %d, automix %d, capture %d, editor %d\n", (int) sizeof (VelvoxAudioProcessor),
                     (int) sizeof (velvox::dsp::VocalChain), (int) sizeof (velvox::automix::AutoMixController),
                     (int) sizeof (velvox::automix::CaptureBuffer), (int) sizeof (VelvoxAudioProcessorEditor));
        return 0;
    }

    // VelvoxTests --popscan <silence|floor|tone|vocal> <automix 0/1> <editor 0/1> <seconds> <sampleRate> <realtime 0/1>
    if (argc >= 8 && juce::String (argv[1]) == "--popscan")
    {
        const auto r = velvox::test::runPopScan (argv[2], juce::String (argv[3]).getIntValue() != 0, juce::String (argv[4]).getIntValue() != 0,
                                                 juce::String (argv[5]).getDoubleValue(), juce::String (argv[6]).getDoubleValue(),
                                                 juce::String (argv[7]).getIntValue() != 0);
        std::printf ("popscan %s: %d events during silence (max %.1f dBFS), %d discontinuities\n%s\n", argv[2], r.silentEvents,
                     r.maxSilentDb, r.discontinuities, r.log.joinIntoString ("\n").toRawUTF8());
        return 0;
    }

    // VelvoxTests --fxrender outDir [vocal.wav]: reverb / delay impulse responses and examples
    if (argc >= 3 && juce::String (argv[1]) == "--fxrender")
        return velvox::test::runFxRender (juce::File (juce::String::fromUTF8 (argv[2])),
                                          argc >= 4 ? juce::File (juce::String::fromUTF8 (argv[3])) : juce::File());

    // VOCAL CHECK calibration (Tests/CheckEval.cpp); an argument ending in .txt is a list of files
    if (argc >= 4 && (juce::String (argv[1]) == "--check-template" || juce::String (argv[1]) == "--check-eval"))
    {
        juce::StringArray files;
        for (int i = 3; i < argc; ++i)
        {
            const juce::String arg = juce::String::fromUTF8 (argv[i]);
            if (arg.endsWithIgnoreCase (".txt"))
                files.addLines (juce::File (arg).loadFileAsString());
            else
                files.add (arg);
        }
        files.trim();
        files.removeEmptyStrings();
        const juce::File out (juce::String::fromUTF8 (argv[2]));
        return juce::String (argv[1]) == "--check-template" ? velvox::test::runCheckTemplate (out, files)
                                                            : velvox::test::runCheckEval (out, files);
    }

    if (argc >= 4 && juce::String (argv[1]) == "--automix")
        return runAutoMixOnFile (juce::File (juce::String::fromUTF8 (argv[2])), juce::File (juce::String::fromUTF8 (argv[3])));

    if (argc >= 4 && juce::String (argv[1]) == "--score")
        return runScore (juce::File (juce::String::fromUTF8 (argv[2])), juce::File (juce::String::fromUTF8 (argv[3])));

    if (argc >= 4 && juce::String (argv[1]) == "--bench")
        return runBench (juce::File (juce::String::fromUTF8 (argv[2])), juce::File (juce::String::fromUTF8 (argv[3])));

    if (argc >= 3 && juce::String (argv[1]) == "--snapshot")
        return renderSnapshot (juce::File (juce::String (argv[2])),
                               argc >= 4 ? juce::String (argv[3]) : juce::String(),
                               argc >= 5 ? juce::String (argv[4]).getIntValue() : 0,
                               argc >= 6 ? juce::String (argv[5]) : juce::String(),
                               argc >= 7 ? juce::String (argv[6]).getIntValue() : 0,
                               argc >= 8 ? juce::String (argv[7]) : juce::String());

    // Tests use their own preferences file (tutorial state, saved layouts)
    const auto prefs = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("velvox-tests.settings");
    prefs.deleteFile();
    velvox::ui::settings::useFileForTesting (prefs);

    StdoutLogger logger;
    juce::Logger::setCurrentLogger (&logger);

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    if (argc >= 3 && juce::String (argv[1]) == "--category")
        runner.runTestsInCategory (argv[2]);
    else if (argc >= 3 && juce::String (argv[1]) == "--test")
    {
        for (auto* t : juce::UnitTest::getAllTests())
            if (t->getName().containsIgnoreCase (argv[2]))
            {
                juce::Array<juce::UnitTest*> one { t };
                runner.runTests (one);
            }
    }
    else if (argc >= 3 && juce::String (argv[1]) == "--seq")
    {
        // Suites in the given order, names separated by '|' (exact names; for order effects)
        juce::Array<juce::UnitTest*> tests;
        for (auto& name : juce::StringArray::fromTokens (juce::String (argv[2]), "|", ""))
            for (auto* t : juce::UnitTest::getAllTests())
                if (t->getName() == name.trim())
                    tests.add (t);
        runner.runTests (tests);
    }
    else if (argc >= 3 && juce::String (argv[1]) == "--from")
    {
        // All suites starting with the first whose name contains the text (for isolating order effects)
        juce::Array<juce::UnitTest*> tests;
        bool on = false;
        for (auto* t : juce::UnitTest::getAllTests())
        {
            on = on || t->getName().containsIgnoreCase (argv[2]);
            if (on)
                tests.add (t);
        }
        runner.runTests (tests);
    }
    else
        runner.runAllTests();

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        failures += runner.getResult (i)->failures;
        passes += runner.getResult (i)->passes;
    }

    std::printf ("\n==== %d checks passed, %d failed ====\n", passes, failures);
    juce::Logger::setCurrentLogger (nullptr);
    return failures == 0 ? 0 : 1;
}
