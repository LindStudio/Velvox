#include "EmberProcessor.h"
#include "EmberEditor.h"
#include "Text/I18n.h"

namespace ember
{
    using vocalypse::i18n::tr;
    using vocalypse::i18n::format;
    using vocalypse::i18n::number;

    // Every edit in the editor starts with a host gesture: that is where the values before it are remembered
    struct EmberAudioProcessor::UndoListener final : juce::AudioProcessorParameter::Listener
    {
        explicit UndoListener (EmberAudioProcessor& p) : owner (p) {}
        void parameterValueChanged (int, float) override {}
        void parameterGestureChanged (int index, bool starting) override
        {
            if (! starting || ! juce::MessageManager::existsAndIsCurrentThread())
                return;
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (owner.getParameters()[index]); p != nullptr && isSetting (p->getParameterID()))
                owner.beginUndoStep (index);   // bypass and listening are no undo steps
        }
        EmberAudioProcessor& owner;
    };

    struct EmberAudioProcessor::UndoGroup
    {
        explicit UndoGroup (EmberAudioProcessor& p) : owner (p) { ++owner.undoGroupDepth; }
        ~UndoGroup() { --owner.undoGroupDepth; }
        EmberAudioProcessor& owner;
    };

    EmberAudioProcessor::EmberAudioProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, stateType, createLayout())
    {
        bypassParam = state.getParameter (pid::bypass);
        presetValues = ember::capture (state);
        undoListener = std::make_unique<UndoListener> (*this);
        for (auto* p : getParameters())
            p->addListener (undoListener.get());
        engine.prepare (currentRate, 512);
        capture.prepare (currentRate);
        setLatencySamples (engine.getLatencySamples());
        startTimerHz (10);
    }

    EmberAudioProcessor::~EmberAudioProcessor()
    {
        stopTimer();
        autoCancel->store (true);   // the pending analysis skips its remaining renders; ~future joins it quickly
        for (auto* p : getParameters())
            p->removeListener (undoListener.get());
    }

    bool EmberAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
            return false;
        return in == out;
    }

    void EmberAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        currentRate.store (sampleRate);
        engine.setSettings (readSettings (state));
        engine.prepare (sampleRate, samplesPerBlock);
        capture.prepare (sampleRate);   // a new rate while AUTO listens: it listens again from the start
        setLatencySamples (engine.getLatencySamples());
    }

    void EmberAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
    {
        juce::ScopedNoDenormals noDenormals;
        const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
        for (int ch = numIn; ch < numOut; ++ch)
            buffer.clear (ch, 0, buffer.getNumSamples());

        const int channels = std::min (buffer.getNumChannels(), std::min (numOut, (int) maxChannels));
        if (channels <= 0)
            return;

        // Transport for Ember (Jakob 2026-11-15: he danced on in pause - FL keeps processing when stopped).
        // Stopped only once the host has been seen playing: some hosts never report it, the standalone app
        // has no play head.
        if (auto* ph = getPlayHead())
            if (const auto pos = ph->getPosition())
            {
                const bool playing = pos->getIsPlaying();
                hostEverPlayed = hostEverPlayed || playing;
                hostStopped.store (hostEverPlayed && ! playing, std::memory_order_relaxed);
                if (const auto bpm = pos->getBpm())
                    hostBpm.store ((float) *bpm, std::memory_order_relaxed);
            }

        capture.push (buffer.getArrayOfReadPointers(), channels, buffer.getNumSamples());
        auto settings = params.read();
        if (licence->isLocked())
            settings.bypass = true;   // trial over / key invalid: unprocessed, same latency
        engine.setSettings (settings);
        engine.process (buffer.getArrayOfWritePointers(), channels, buffer.getNumSamples());
    }

    // ------------------------------------------------------------------ state
    void EmberAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
    {
        auto copy = state.copyState();
        copy.setProperty ("version", EMBER_VERSION_STRING, nullptr);
        if (auto xml = copy.createXml())
            copyXmlToBinary (*xml, dest);
    }

    void EmberAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return;
        const UndoGroup group (*this);
        state.replaceState (juce::ValueTree::fromXml (*xml));
        factoryIndex = (int) state.state.getProperty ("factoryPreset", 0);   // never set = still the default preset
        activeSlot = 0;                       // the slots held values from before this state
        slotUsed[0] = slotUsed[1] = false;
        undoSteps.clear();
        redoSteps.clear();
        userPresetName = state.state.getProperty ("userPreset", juce::String()).toString();
        // replaceState leaves parameters that the saved state does not mention untouched and does not
        // always resend values; snap every parameter to the stored value (VOCALYPSE lesson 0.6.1)
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (auto* raw = state.getRawParameterValue (rp->getParameterID()))
                    rp->setValueNotifyingHost (rp->convertTo0to1 (raw->load()));
        // Delta / S range listening is a tool, not a setting: a project never reopens listening to the S alone
        setListenOff();
        // a project reopens as it was saved: the restored values count as the preset's
        presetValues = ember::capture (state);
        // an AUTO analysis (or result) from before this state does not belong to it
        projectLoads.fetch_add (1);
        if (juce::MessageManager::existsAndIsCurrentThread())
            autoProjectLoaded();
    }

    // ---------------------------------------------------------------- presets
    void EmberAudioProcessor::loadFactoryPreset (int index)
    {
        const auto& list = factoryPresets();
        if (! juce::isPositiveAndBelow (index, (int) list.size()))
            return;
        beginUndoStep (-1);
        discardPendingAuto();
        const UndoGroup group (*this);
        ember::apply (state, list[(size_t) index].values);
        factoryIndex = index;
        userPresetName = {};
        presetValues = ember::capture (state);
        state.state.setProperty ("factoryPreset", index, nullptr);
        state.state.setProperty ("userPreset", juce::String(), nullptr);
    }

    bool EmberAudioProcessor::loadUserPreset (const juce::File& file)
    {
        ParamValues v;
        if (! ember::loadUserPreset (file, v))
            return false;
        beginUndoStep (-1);
        discardPendingAuto();
        const UndoGroup group (*this);
        ember::apply (state, v);
        factoryIndex = -1;
        userPresetName = file.getFileNameWithoutExtension();
        presetValues = ember::capture (state);
        state.state.setProperty ("factoryPreset", -1, nullptr);
        state.state.setProperty ("userPreset", userPresetName, nullptr);
        return true;
    }

    bool EmberAudioProcessor::saveUserPreset (const juce::File& file)
    {
        // "Lead 1.5" must not become "Lead 1.emberpreset" (and overwrite "Lead 1" without asking)
        const auto target = file.hasFileExtension (presetExtension) ? file : file.getSiblingFile (file.getFileName() + presetExtension);
        if (! ember::saveUserPreset (target, ember::capture (state)))
            return false;
        factoryIndex = -1;
        userPresetName = target.getFileNameWithoutExtension();
        presetValues = ember::capture (state);
        state.state.setProperty ("factoryPreset", -1, nullptr);
        state.state.setProperty ("userPreset", userPresetName, nullptr);
        return true;
    }

    juce::String EmberAudioProcessor::getPresetName() const
    {
        if (factoryIndex >= 0 && factoryIndex < (int) factoryPresets().size())
            return presetName (factoryPresets()[(size_t) factoryIndex]);
        if (userPresetName.isNotEmpty())
            return userPresetName;
        return tr ("AUTO-Einstellung", "AUTO setting");
    }

    bool EmberAudioProcessor::isPresetModified() const
    {
        return ! ember::sameValues (presetValues, ember::capture (state));
    }

    void EmberAudioProcessor::stepPreset (int direction)
    {
        // order: factory presets, then user presets
        const int numFactory = (int) factoryPresets().size();
        const auto users = userPresets();
        const int total = numFactory + users.size();
        int current = factoryIndex;
        if (current < 0)
        {
            current = numFactory;
            for (int i = 0; i < users.size(); ++i)
                if (users[i].getFileNameWithoutExtension() == userPresetName)
                    current = numFactory + i;
        }
        const int next = ((current + direction) % total + total) % total;
        if (next < numFactory)
            loadFactoryPreset (next);
        else
            loadUserPreset (users[next - numFactory]);
    }

    // -------------------------------------------------------------------- A/B
    EmberAudioProcessor::Slot EmberAudioProcessor::currentSlot() const
    {
        return { ember::capture (state), presetValues, factoryIndex, userPresetName };
    }

    void EmberAudioProcessor::restoreSlot (const Slot& s)
    {
        discardPendingAuto();
        {
            const UndoGroup group (*this);   // switching back is the way back, not an undo step
            ember::apply (state, s.values);
        }
        presetValues = s.presetValues;
        factoryIndex = s.factoryIndex;
        userPresetName = s.userPresetName;
        state.state.setProperty ("factoryPreset", factoryIndex, nullptr);
        state.state.setProperty ("userPreset", userPresetName, nullptr);
    }

    void EmberAudioProcessor::switchSlot (int slot)
    {
        if (slot == activeSlot || ! juce::isPositiveAndBelow (slot, 2))
            return;
        slots[activeSlot] = currentSlot();
        slotUsed[activeSlot] = true;
        activeSlot = slot;
        if (slotUsed[slot])
            restoreSlot (slots[slot]);
        else
        {
            slots[slot] = currentSlot();   // first visit: B starts as a copy of A
            slotUsed[slot] = true;
        }
    }

    void EmberAudioProcessor::copyActiveToOther()
    {
        const int other = 1 - activeSlot;
        slots[other] = currentSlot();
        slotUsed[other] = true;
    }

    // ------------------------------------------------------------------- undo
    void EmberAudioProcessor::beginUndoStep (int parameterIndex)
    {
        if (undoGroupDepth > 0)
            return;
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        // the same control again within a second (mouse wheel, a second drag): still the same step
        const bool sameEdit = parameterIndex >= 0 && parameterIndex == lastUndoParam && now - lastUndoTime < 1.0;
        lastUndoParam = parameterIndex;
        lastUndoTime = now;
        if (sameEdit)
            return;
        auto values = ember::capture (state);
        if (! undoSteps.empty() && undoSteps.back() == values)
            return;   // the edit before changed nothing
        redoSteps.clear();
        undoSteps.push_back (std::move (values));
        if (undoSteps.size() > 100)
            undoSteps.erase (undoSteps.begin());
    }

    bool EmberAudioProcessor::canUndo() const
    {
        const auto now = ember::capture (state);
        for (const auto& step : undoSteps)
            if (step != now)
                return true;
        return false;
    }

    bool EmberAudioProcessor::undo()
    {
        const auto now = ember::capture (state);
        while (! undoSteps.empty() && undoSteps.back() == now)
            undoSteps.pop_back();   // an edit that changed nothing
        if (undoSteps.empty())
            return false;
        redoSteps.push_back (now);
        const auto target = undoSteps.back();
        undoSteps.pop_back();
        {
            const UndoGroup group (*this);
            ember::apply (state, target);
        }
        lastUndoParam = -1;
        return true;
    }

    bool EmberAudioProcessor::redo()
    {
        if (redoSteps.empty())
            return false;
        undoSteps.push_back (ember::capture (state));
        const auto target = redoSteps.back();
        redoSteps.pop_back();
        {
            const UndoGroup group (*this);
            ember::apply (state, target);
        }
        lastUndoParam = -1;
        return true;
    }

    void EmberAudioProcessor::setListenOff()
    {
        if (auto* p = state.getParameter (pid::listen); p != nullptr && p->getValue() > 0.0f)
        {
            const UndoGroup group (*this);
            p->beginChangeGesture();
            p->setValueNotifyingHost (0.0f);
            p->endChangeGesture();
        }
    }

    // ------------------------------------------------------------------- AUTO
    void EmberAudioProcessor::startAuto()
    {
        if (licence->isLocked())
            return;   // the audio passes through unprocessed: AUTO would claim a result nobody hears
        discardPendingAuto();
        capture.prepare (currentRate.load());
        capture.start();
        autoPhase = AutoPhase::listening;
        autoNow = autoLastGrowth = juce::Time::getMillisecondCounterHiRes() * 0.001;
        autoLastSamples = 0;
    }

    void EmberAudioProcessor::updateAuto (double nowSeconds)
    {
        if (autoPhase != AutoPhase::listening)
            return;
        if (nowSeconds < autoNow)
            autoLastGrowth = nowSeconds;   // another clock (tests)
        autoNow = nowSeconds;
        if (const int samples = capture.getNumSamples(); samples != autoLastSamples)
        {
            autoLastSamples = samples;     // (a new sample rate restarts the capture: it shrinks, which counts as news)
            autoLastGrowth = nowSeconds;
        }
        const double seconds = capture.getVoicedSeconds();
        if (seconds >= autoEnoughSeconds || ! capture.isCapturing())
            finishAuto();
        else if (seconds >= autoMinSeconds && nowSeconds - autoLastGrowth >= autoQuietSeconds)
            finishAuto();   // playback stopped (or a long break) after enough voice: work with what is there
    }

    bool EmberAudioProcessor::isAutoWaitingForAudio() const noexcept
    {
        return autoPhase == AutoPhase::listening && getAutoQuietSeconds() >= autoQuietSeconds && capture.getVoicedSeconds() < autoMinSeconds;
    }

    bool EmberAudioProcessor::isShowingAutoResult() const
    {
        return autoPhase == AutoPhase::done && factoryIndex < 0 && userPresetName.isEmpty() && ! isPresetModified();
    }

    void EmberAudioProcessor::cancelAuto()
    {
        if (autoPhase == AutoPhase::analysing)
            discardPendingAuto();   // the background analysis stops within a few ms, nothing is applied
        else if (autoPhase == AutoPhase::listening)
        {
            capture.stop();
            autoPhase = AutoPhase::idle;
        }
    }

    void EmberAudioProcessor::discardPendingAuto()
    {
        if (autoPhase != AutoPhase::analysing)
            return;
        autoCancel->store (true);
        autoTake.reset();
        autoPhase = AutoPhase::idle;
    }

    void EmberAudioProcessor::autoProjectLoaded()
    {
        seenProjectLoads = projectLoads.load();
        discardPendingAuto();
        if (autoPhase == AutoPhase::done || autoPhase == AutoPhase::failed)
        {
            autoPhase = AutoPhase::idle;   // "Set: from 6.2 kHz ..." was about the settings before this project
            lastAuto = {};
        }
    }

    double EmberAudioProcessor::getAutoProgress() const noexcept
    {
        return autoPhase == AutoPhase::listening ? juce::jlimit (0.0, 1.0, capture.getVoicedSeconds() / autoEnoughSeconds) : 0.0;
    }

    void EmberAudioProcessor::finishAuto()
    {
        if (autoPhase != AutoPhase::listening)
            return;
        capture.stop();   // when it returns, the audio thread no longer writes
        autoTake = std::make_shared<const AutoCapture::Take> (capture.copyTake());
        autoPhase = AutoPhase::analysing;
        launchAuto();
    }

    void EmberAudioProcessor::launchAuto()
    {
        autoCancel->store (true);   // a run with older settings stops (its future is replaced below)
        autoCancel = std::make_shared<std::atomic<bool>> (false);
        // measured with the user's own mode, Amount, detection and times (only an Amount too low to work with is raised)
        autoBase = readSettings (state);
        autoUserAmount = autoBase.amount;
        autoRaisedAmount = autoBase.amount < autoMinAmount;
        if (autoRaisedAmount)
            autoBase.amount = 0.5f;
        autoLoadsAtStart = projectLoads.load();
        ++autoRuns;
        pending = std::async (std::launch::async, [take = autoTake, base = autoBase, cancel = autoCancel]
        {
            if (take == nullptr || take->numChannels <= 0)
                return AutoResult {};
            const float* ch[2] { take->channel[0].data(), take->numChannels > 1 ? take->channel[1].data() : nullptr };
            return analyse (ch, take->numChannels, take->numSamples(), take->sampleRate, base, cancel.get());
        });
    }

    bool EmberAudioProcessor::autoBaseChanged() const
    {
        const auto now = readSettings (state);
        return now.mode != autoBase.mode || now.relative != autoBase.relative || now.smart != autoBase.smart
            || std::abs (now.attackMs - autoBase.attackMs) > 1.0e-4f || std::abs (now.lookaheadMs - autoBase.lookaheadMs) > 1.0e-4f
            || std::abs (now.amount - autoUserAmount) > 1.0e-4f;
    }

    bool EmberAudioProcessor::pollAuto()
    {
        if (autoPhase != AutoPhase::analysing || ! pending.valid())
            return false;
        // a project loaded on another thread meanwhile: this analysis belongs to the settings before it
        if (projectLoads.load() != autoLoadsAtStart)
        {
            autoProjectLoaded();
            return false;
        }
        // mode, detection, Amount or times changed while analysing: measured again with them, on the same take
        if (autoBaseChanged())
        {
            launchAuto();
            return false;
        }
        if (pending.wait_for (std::chrono::seconds (0)) != std::future_status::ready)
            return false;
        auto result = pending.get();
        autoTake.reset();
        if (result.status == AutoResult::Status::cancelled)
        {
            autoPhase = AutoPhase::idle;
            return false;
        }
        lastAuto = result;
        if (lastAuto.status == AutoResult::Status::ok)
        {
            applyAuto (lastAuto);
            autoPhase = AutoPhase::done;
        }
        else
        {
            autoPhase = AutoPhase::failed;
        }
        return true;
    }

    void EmberAudioProcessor::waitForAuto()
    {
        for (int i = 0; i < 8 && autoPhase == AutoPhase::analysing && pending.valid(); ++i)
        {
            pending.wait();
            if (pollAuto())
                break;
        }
    }

    void EmberAudioProcessor::applyAuto (const AutoResult& r)
    {
        // before: what was set when AUTO finished (goes to slot A)
        const auto before = currentSlot();
        beginUndoStep (-1);
        const UndoGroup group (*this);
        const auto set = [this] (const char* id, float realValue)
        {
            if (auto* p = state.getParameter (id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (realValue));
                p->endChangeGesture();
            }
        };
        set (pid::freq, r.freq);
        set (pid::threshold, r.thresholdDb);
        set (pid::range, r.rangeDb);
        set (pid::release, autoReleaseMs);
        // Mode, detection, attack, lookahead and Amount were measured as they are (analyse with the user's settings);
        // mix and output only scale what comes out. Only an Amount too low to reach anything is raised.
        if (autoRaisedAmount)
            set (pid::amount, autoBase.amount * 100.0f);
        set (pid::listen, 0.0f);
        factoryIndex = -1;
        userPresetName = {};
        presetValues = ember::capture (state);
        state.state.setProperty ("factoryPreset", -1, nullptr);
        state.state.setProperty ("userPreset", juce::String(), nullptr);

        // A = before, B = AUTO (active): one click on A takes AUTO back, A/B compares the two
        slots[0] = before;
        slots[1] = currentSlot();
        slotUsed[0] = slotUsed[1] = true;
        activeSlot = 1;
    }

    juce::String EmberAudioProcessor::getAutoMessage() const
    {
        switch (autoPhase)
        {
            case AutoPhase::idle: return {};
            case AutoPhase::listening:
                if (isAutoWaitingForAudio())
                    return tr ("Ember hört nichts. Drück Play in deiner DAW, damit er deinen Gesang hört.",
                               "Ember can't hear anything. Press play in your DAW so he hears your vocal.");
                return tr ("Ember hört zu … lass den Gesang weiterlaufen, er ist von selbst fertig.",
                           "Ember is listening … keep the vocal playing, he finishes on his own.");
            case AutoPhase::analysing:
                return tr ("Ember probiert Einstellungen aus …", "Ember is trying settings …");
            case AutoPhase::done:
            {
                // expectedReductionDb is what the S frames lose above autoSBandHz (their sharpness), not the whole S
                const auto khz = number (lastAuto.freq / 1000.0, 1);
                return format (tr ("Eingestellt: ab {0} kHz, Schärfe der S-Laute (über {2} kHz) −{1} dB, Vokale bleiben unberührt.",
                                   "Set: from {0} kHz, S sharpness (above {2} kHz) −{1} dB, vowels stay untouched."),
                               { khz, number (lastAuto.expectedReductionDb, 1), number (autoSBandHz / 1000.0, 0) })
                       + (autoRaisedAmount ? tr (" Amount stand zu tief und ist jetzt 50 %.", " Amount was too low and is now 50 %.") : juce::String())
                       + tr (" Vorher liegt auf A.", " Before is on A.");
            }
            case AutoPhase::failed:
                if (lastAuto.status == AutoResult::Status::noSibilance)
                    return tr ("Kaum S-Laute gehört. Spiel eine Stelle mit vielen S, Z oder Sch ab.",
                               "Hardly any S sounds heard. Play a part with many S, Z or Sh sounds.");
                return tr ("Zu kurz. Spiel mindestens 3 Sekunden Gesang ab.", "Too short. Play at least 3 seconds of vocals.");
        }
        return {};
    }

    void EmberAudioProcessor::timerCallback()
    {
        if (projectLoads.load() != seenProjectLoads)
            autoProjectLoaded();   // setStateInformation ran on another thread
        pollAuto();
        updateAuto (juce::Time::getMillisecondCounterHiRes() * 0.001);
    }

    juce::AudioProcessorEditor* EmberAudioProcessor::createEditor()
    {
        return new EmberEditor (*this);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ember::EmberAudioProcessor();
}
