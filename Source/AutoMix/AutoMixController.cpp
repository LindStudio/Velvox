#include "AutoMixController.h"
#include "../Parameters/ParameterLayout.h"
#include "../StateKeys.h"

namespace velvox::automix
{
    namespace
    {
        const juce::Identifier promptAttr { "prompt" };
        const juce::Identifier reportType { "Report" };
        const juce::Identifier previousType { "Previous" };
        const juce::Identifier mixType { "Mix" };
        const juce::Identifier stepType { "Step" }, findingType { "Finding" }, noteType { "Note" }, adjustType { "Adjust" };

        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }
    }

    //==========================================================================
    juce::ValueTree MixReport::toValueTree() const
    {
        juce::ValueTree t (reportType);
        t.setProperty ("valid", valid, nullptr);
        t.setProperty ("time", timestamp, nullptr);
        t.setProperty ("voiced", voicedSeconds, nullptr);
        t.setProperty ("captured", capturedSeconds, nullptr);
        t.setProperty ("conservative", conservative, nullptr);
        t.setProperty ("beat", beatAnalysed, nullptr);
        t.setProperty ("inLufs", inputLufs, nullptr);
        t.setProperty ("outLufs", outputLufs, nullptr);
        t.setProperty ("outPeak", outputPeakDb, nullptr);
        t.setProperty ("iterations", iterations, nullptr);
        t.setProperty ("modules", changedModules.joinIntoString (","), nullptr);
        for (auto& s : steps)
        {
            juce::ValueTree c (stepType);
            c.setProperty ("module", s.moduleKey, nullptr);
            c.setProperty ("title", s.title, nullptr);
            c.setProperty ("reason", s.reason, nullptr);
            c.setProperty ("measured", s.measured, nullptr);
            t.appendChild (c, nullptr);
        }
        for (auto& f : findings)
        {
            juce::ValueTree c (findingType);
            c.setProperty ("title", f.title, nullptr);
            c.setProperty ("detail", f.detail, nullptr);
            c.setProperty ("confidence", (int) f.confidence, nullptr);
            t.appendChild (c, nullptr);
        }
        for (auto& n : notes)
            t.appendChild (juce::ValueTree (noteType, { { "text", n } }), nullptr);
        for (auto& a : adjustments)
            t.appendChild (juce::ValueTree (adjustType, { { "text", a } }), nullptr);
        return t;
    }

    MixReport MixReport::fromValueTree (const juce::ValueTree& t)
    {
        MixReport r;
        if (! t.hasType (reportType))
            return r;
        r.valid = (bool) t.getProperty ("valid", false);
        r.timestamp = t.getProperty ("time").toString();
        r.voicedSeconds = (float) t.getProperty ("voiced", 0.0f);
        r.capturedSeconds = (float) t.getProperty ("captured", 0.0f);
        r.conservative = (bool) t.getProperty ("conservative", false);
        r.beatAnalysed = (bool) t.getProperty ("beat", false);
        r.inputLufs = (float) t.getProperty ("inLufs", -100.0f);
        r.outputLufs = (float) t.getProperty ("outLufs", -100.0f);
        r.outputPeakDb = (float) t.getProperty ("outPeak", -100.0f);
        r.iterations = (int) t.getProperty ("iterations", 0);
        r.changedModules.addTokens (t.getProperty ("modules").toString(), ",", {});
        r.changedModules.removeEmptyStrings();
        for (const auto& c : t)
        {
            if (c.hasType (stepType))
                r.steps.push_back ({ c.getProperty ("module").toString(), c.getProperty ("title").toString(),
                                     c.getProperty ("reason").toString(), (bool) c.getProperty ("measured", false) });
            else if (c.hasType (findingType))
                r.findings.push_back ({ c.getProperty ("title").toString(), c.getProperty ("detail").toString(),
                                        (Confidence) juce::jlimit (0, 2, (int) c.getProperty ("confidence", 0)) });
            else if (c.hasType (noteType))
                r.notes.add (c.getProperty ("text").toString());
            else if (c.hasType (adjustType))
                r.adjustments.add (c.getProperty ("text").toString());
        }
        return r;
    }

    //==========================================================================
    class AutoMixController::Worker final : public juce::Thread
    {
    public:
        Worker (CapturedAudio a, std::map<juce::String, float> values)
            : juce::Thread ("Headliner auto mix"), audio (std::move (a)), current (std::move (values)) {}

        ~Worker() override { stopThread (10000); }

        void run() override
        {
            auto r = AutoMixEngine::run (audio, current, [this] { return threadShouldExit(); },
                                         [this] (float f, const juce::String& stage)
                                         {
                                             fraction.store (f);
                                             const juce::SpinLock::ScopedLockType l (stageLock);
                                             stageText = stage;
                                         });
            result = std::move (r);
            finished.store (true);
        }

        juce::String stage() const
        {
            const juce::SpinLock::ScopedLockType l (stageLock);
            return stageText;
        }

        CapturedAudio audio;
        std::map<juce::String, float> current;
        AutoMixResult result;
        std::atomic<bool> finished { false };
        std::atomic<float> fraction { 0.0f };

    private:
        mutable juce::SpinLock stageLock;
        juce::String stageText;
    };

    //==========================================================================
    AutoMixController::AutoMixController (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& state)
        : processor (p), apvts (state)
    {
    }

    AutoMixController::~AutoMixController()
    {
        stopTimer();
        capture.cancel();
        worker.reset();   // asks the thread to stop and joins it
    }

    void AutoMixController::setPhase (Phase p)
    {
        if (p == phase)
            return;
        phase = p;
        sendChangeMessage();
    }

    std::map<juce::String, float> AutoMixController::currentValues() const
    {
        std::map<juce::String, float> values;
        for (const auto& spec : getParameterSpecs())
            if (auto* param = apvts.getParameter (spec.id))
                values[spec.id] = param->convertFrom0to1 (param->getValue());
        return values;
    }

    void AutoMixController::start (const CaptureConfig& cfg)
    {
        worker.reset();
        if (showingPrevious)
            showPrevious (false);

        config = cfg;
        const double fs = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
        const int channels = juce::jlimit (1, 2, processor.getMainBusNumInputChannels());
        capture.arm (fs, channels, config);
        error.clear();
        startedMs = juce::Time::getMillisecondCounterHiRes();
        setPhase (Phase::listening);
        startTimerHz (15);
    }

    void AutoMixController::cancel()
    {
        capture.cancel();
        worker.reset();
        stopTimer();
        setPhase (report.valid ? Phase::done : Phase::idle);
    }

    bool AutoMixController::analyseNow()
    {
        const auto p = capture.getProgress();
        if (! p.enough() || ! capture.finishNow())
            return false;
        timerCallback();
        return true;
    }

    void AutoMixController::dismissPrompt()
    {
        if (promptState == "pending")
            promptState = "dismissed";
        sendChangeMessage();
    }

    AutoMixController::Prompt AutoMixController::getPrompt() const
    {
        if (promptState == "done") return Prompt::done;
        if (promptState == "dismissed") return Prompt::dismissed;
        return Prompt::pending;
    }

    AutoMixController::Status AutoMixController::getStatus() const
    {
        Status s;
        s.phase = phase;
        s.capture = capture.getProgress();
        s.error = error;
        s.secondsListening = (juce::Time::getMillisecondCounterHiRes() - startedMs) * 0.001;
        if (worker != nullptr)
        {
            s.analysisFraction = worker->fraction.load();
            s.stage = worker->stage();
        }
        return s;
    }

    void AutoMixController::startWorker (CapturedAudio audio)
    {
        worker = std::make_unique<Worker> (std::move (audio), currentValues());
        worker->startThread (juce::Thread::Priority::low);
        setPhase (Phase::analysing);
    }

    void AutoMixController::timerCallback()
    {
        if (phase == Phase::listening || phase == Phase::capturing)
        {
            const auto st = capture.getState();
            if (st == CaptureBuffer::State::capturing)
                setPhase (Phase::capturing);
            if (st == CaptureBuffer::State::complete)
                startWorker (capture.copyOut());
            sendChangeMessage();   // progress
            return;
        }

        if (phase == Phase::analysing && worker != nullptr)
        {
            if (! worker->finished.load())
            {
                sendChangeMessage();
                return;
            }

            const auto result = std::move (worker->result);
            worker.reset();
            stopTimer();
            capture.cancel();
            if (result.ok)
            {
                applyResult (result);
            }
            else
            {
                error = result.error.isNotEmpty() ? result.error : de ("Die Analyse ist fehlgeschlagen.");
                setPhase (Phase::failed);
            }
            return;
        }

        stopTimer();
    }

    AutoMixResult AutoMixController::runSynchronously (const CapturedAudio& audio)
    {
        return AutoMixEngine::run (audio, currentValues());
    }

    void AutoMixController::applyResult (const AutoMixResult& result)
    {
        // Keep what was there before, so it can be compared and restored
        previousSnapshot = ParameterSnapshot::capture (apvts, de ("Vor dem Auto-Mix"));
        hasPreviousSnapshot = true;
        showingPrevious = false;

        // Apply through the normal parameters (gestures: the host sees every change)
        for (const auto& [id, value] : result.plan.values)
            if (auto* param = apvts.getParameter (id))
            {
                const float norm = param->convertTo0to1 (value);
                if (std::abs (norm - param->getValue()) < 1.0e-7f)
                    continue;
                param->beginChangeGesture();
                param->setValueNotifyingHost (norm);
                param->endChangeGesture();
            }
        apvts.state.setProperty (statekeys::routing, result.plan.routing.toString(), nullptr);

        mixSnapshot = ParameterSnapshot::capture (apvts, de ("Headliner Auto-Mix"));
        if (onMixApplied)
            onMixApplied (previousSnapshot, mixSnapshot);

        // Report for the UI (only what was really set up)
        MixReport r;
        r.valid = true;
        r.timestamp = juce::Time::getCurrentTime().toString (true, true, false, true);
        r.voicedSeconds = result.profile.voicedSeconds;
        r.capturedSeconds = result.profile.capturedSeconds;
        r.conservative = result.plan.conservative;
        r.beatAnalysed = result.profile.beatPresent;
        r.inputLufs = result.original.lufs;
        r.outputLufs = result.processed.lufs;
        r.outputPeakDb = result.processed.peakDb;
        r.iterations = result.iterations;
        for (auto& d : result.plan.decisions)
            r.steps.push_back ({ d.outputStage ? juce::String ("output") : juce::String (dsp::moduleKey (d.module)), d.title, d.reason, d.measured });
        for (auto& i : result.profile.issues)
            r.findings.push_back ({ i.title, i.detail, i.confidence });
        for (auto& n : result.plan.notes)
            r.notes.add (n);
        for (auto& a : result.adjustments)
            r.adjustments.add (a);
        for (auto m : result.plan.activeModules())
            r.changedModules.add (dsp::moduleKey (m));
        report = std::move (r);

        promptState = "done";
        error.clear();
        setPhase (Phase::done);
        sendChangeMessage();
    }

    void AutoMixController::showPrevious (bool previous)
    {
        if (selectCompare)
        {
            if (! hasPreviousSnapshot || ! hasMix())
                return;
            selectCompare (previous);
            showingPrevious = previous;
            sendChangeMessage();
            return;
        }
        if (! hasPreviousSnapshot || ! hasMix() || previous == showingPrevious)
            return;
        if (previous)
        {
            mixSnapshot = ParameterSnapshot::capture (apvts, mixSnapshot.name); // keep tweaks made to the mix
            previousSnapshot.applyTo (apvts, true);
        }
        else
        {
            mixSnapshot.applyTo (apvts, true);
        }
        showingPrevious = previous;
        sendChangeMessage();
    }

    void AutoMixController::restorePrevious()
    {
        if (! hasPreviousSnapshot)
            return;
        previousSnapshot.applyTo (apvts, true);
        hasPreviousSnapshot = false;
        showingPrevious = false;
        report = {};
        mixSnapshot = {};
        setPhase (Phase::idle);
        sendChangeMessage();
    }

    //==========================================================================
    void AutoMixController::writeState (juce::ValueTree& pluginState) const
    {
        auto existing = pluginState.getChildWithName (treeType);
        if (existing.isValid())
            pluginState.removeChild (existing, nullptr);

        juce::ValueTree t (treeType);
        t.setProperty (promptAttr, promptState, nullptr);
        if (report.valid)
            t.appendChild (report.toValueTree(), nullptr);
        if (hasPreviousSnapshot)
        {
            juce::ValueTree prev (previousType);
            prev.appendChild (previousSnapshot.toValueTree(), nullptr);
            t.appendChild (prev, nullptr);
        }
        if (hasMix())
        {
            // While "previous" is being auditioned, the mix (with tweaks) lives in mixSnapshot
            juce::ValueTree mix (mixType);
            mix.appendChild (mixSnapshot.toValueTree(), nullptr);
            t.appendChild (mix, nullptr);
            t.setProperty ("showingPrevious", showingPrevious, nullptr);
        }
        pluginState.appendChild (t, nullptr);
    }

    void AutoMixController::readState (const juce::ValueTree& pluginState, bool isOlderProject)
    {
        capture.cancel();
        worker.reset();
        stopTimer();

        report = {};
        previousSnapshot = {};
        mixSnapshot = {};
        hasPreviousSnapshot = showingPrevious = false;
        error.clear();

        const auto t = pluginState.getChildWithName (treeType);
        if (! t.isValid())
        {
            // A project from a version without the automatic mix already has its
            // sound: never ask for an analysis there. A fresh instance stays pending.
            promptState = isOlderProject ? "done" : promptState;
            phase = Phase::idle;
            sendChangeMessage();
            return;
        }

        promptState = t.getProperty (promptAttr, "done").toString();
        report = MixReport::fromValueTree (t.getChildWithName (reportType));
        if (auto prev = t.getChildWithName (previousType); prev.isValid() && prev.getNumChildren() > 0)
        {
            previousSnapshot = ParameterSnapshot::fromValueTree (prev.getChild (0));
            hasPreviousSnapshot = true;
        }
        if (auto mix = t.getChildWithName (mixType); mix.isValid() && mix.getNumChildren() > 0)
        {
            mixSnapshot = ParameterSnapshot::fromValueTree (mix.getChild (0));
            // The project was saved while auditioning the previous state: come back on the mix
            if ((bool) t.getProperty ("showingPrevious", false))
                mixSnapshot.applyTo (apvts, true);
        }
        phase = report.valid ? Phase::done : Phase::idle;
        sendChangeMessage();
    }
}
