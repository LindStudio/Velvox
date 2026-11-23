#include "EmberProcessor.h"
#include "EmberEditor.h"
#include "Text/I18n.h"
#include <optional>

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
    namespace
    {
        constexpr const char* abTag = "EmberAB";

        void writeValues (juce::XmlElement& parent, const char* tag, const ParamValues& values)
        {
            auto* e = parent.createNewChildElement (tag);
            for (const auto& [id, v] : values)
            {
                auto* p = e->createNewChildElement ("Param");
                p->setAttribute ("id", id);
                p->setAttribute ("value", (double) v);
            }
        }

        ParamValues readValues (const juce::XmlElement& parent, const char* tag)
        {
            ParamValues out;
            if (auto* e = parent.getChildByName (tag))
                for (auto* p : e->getChildWithTagNameIterator ("Param"))
                    if (const auto v = (float) p->getDoubleAttribute ("value"); std::isfinite (v))
                        out[p->getStringAttribute ("id")] = v;
            return out;
        }

        bool isMessageThread()
        {
            auto* mm = juce::MessageManager::getInstanceWithoutCreating();
            return mm == nullptr || mm->isThisTheMessageThread();
        }
    }

    void EmberAudioProcessor::writeMeta (juce::XmlElement& xml, const Meta& m)
    {
        xml.setAttribute ("factoryPreset", m.current.factoryIndex);
        xml.setAttribute ("userPreset", m.current.userPresetName);
        xml.setAttribute ("fromAuto", m.current.fromAuto);
        auto* ab = xml.createNewChildElement (abTag);
        ab->setAttribute ("active", m.activeSlot);
        for (int i = 0; i < 2; ++i)
        {
            auto* s = ab->createNewChildElement ("Slot");
            s->setAttribute ("used", m.slotUsed[i]);
            if (! m.slotUsed[i])
                continue;
            s->setAttribute ("factoryPreset", m.slots[i].factoryIndex);
            s->setAttribute ("userPreset", m.slots[i].userPresetName);
            s->setAttribute ("fromAuto", m.slots[i].fromAuto);
            writeValues (*s, "Values", m.slots[i].values);
            writeValues (*s, "Preset", m.slots[i].presetValues);
        }
    }

    EmberAudioProcessor::Meta EmberAudioProcessor::readMeta (const juce::XmlElement& xml, const juce::XmlElement* ab, const ParamValues& restored)
    {
        Meta m;
        m.current.factoryIndex = xml.getIntAttribute ("factoryPreset", 0);   // never set = still the default preset
        m.current.userPresetName = xml.getStringAttribute ("userPreset");
        m.current.fromAuto = xml.getBoolAttribute ("fromAuto", false);
        m.current.presetValues = restored;   // a project reopens as it was saved: the restored values count as the preset's
        if (ab != nullptr)
        {
            m.activeSlot = juce::jlimit (0, 1, ab->getIntAttribute ("active", 0));
            int i = 0;
            for (auto* s : ab->getChildWithTagNameIterator ("Slot"))
            {
                if (i >= 2)
                    break;
                if (s->getBoolAttribute ("used", false))
                {
                    m.slotUsed[i] = true;
                    m.slots[i].factoryIndex = s->getIntAttribute ("factoryPreset", 0);
                    m.slots[i].userPresetName = s->getStringAttribute ("userPreset");
                    m.slots[i].fromAuto = s->getBoolAttribute ("fromAuto", false);
                    m.slots[i].values = readValues (*s, "Values");
                    m.slots[i].presetValues = readValues (*s, "Preset");
                }
                ++i;
            }
        }
        return m;
    }

    EmberAudioProcessor::Meta EmberAudioProcessor::currentMeta() const
    {
        Meta m;
        m.current.presetValues = presetValues;
        m.current.factoryIndex = factoryIndex;
        m.current.userPresetName = userPresetName;
        m.current.fromAuto = fromAuto;
        for (int i = 0; i < 2; ++i)
        {
            m.slots[i] = slots[i];
            m.slotUsed[i] = slotUsed[i];
        }
        m.activeSlot = activeSlot;
        return m;
    }

    void EmberAudioProcessor::applyMeta (const Meta& m)
    {
        {
            const juce::ScopedLock sl (metaLock);
            presetValues = m.current.presetValues;
            factoryIndex = m.current.factoryIndex;
            userPresetName = m.current.userPresetName;
            fromAuto = m.current.fromAuto;
            for (int i = 0; i < 2; ++i)
            {
                slots[i] = m.slots[i];
                slotUsed[i] = m.slotUsed[i];
            }
            activeSlot = m.activeSlot;
            autoBeforeSlot = 0;
        }
        undoSteps.clear();   // the steps belong to the settings from before this state
        redoSteps.clear();
        lastUndoParam = -1;
    }

    void EmberAudioProcessor::applyPendingMeta()
    {
        std::unique_ptr<Meta> m;
        {
            const juce::ScopedLock sl (metaLock);
            m = std::move (pendingMeta);
        }
        if (m != nullptr)
            applyMeta (*m);
    }

    void EmberAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
    {
        auto copy = state.copyState();
        copy.setProperty ("version", EMBER_VERSION_STRING, nullptr);
        auto xml = copy.createXml();
        if (xml == nullptr)
            return;
        {
            // the host may ask from any thread; the message thread writes the meta under the same lock
            const juce::ScopedLock sl (metaLock);
            writeMeta (*xml, pendingMeta != nullptr ? *pendingMeta : currentMeta());
        }
        copyXmlToBinary (*xml, dest);
    }

    void EmberAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        auto xml = getXmlFromBinary (data, sizeInBytes);
        if (xml == nullptr || ! xml->hasTagName (state.state.getType()))
            return;
        // A/B is not part of the parameter tree
        std::unique_ptr<juce::XmlElement> ab;
        if (auto* e = xml->getChildByName (abTag))
        {
            ab = std::make_unique<juce::XmlElement> (*e);
            xml->removeChildElement (e, true);
        }
        const bool messageThread = isMessageThread();
        {
            std::optional<UndoGroup> group;   // off the message thread no gesture becomes an undo step anyway
            if (messageThread)
                group.emplace (*this);
            state.replaceState (juce::ValueTree::fromXml (*xml));
            // replaceState leaves parameters that the saved state does not mention untouched and does not
            // always resend values; snap every parameter to the stored value (VOCALYPSE lesson 0.6.1)
            for (auto* p : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                    if (auto* raw = state.getRawParameterValue (rp->getParameterID()))
                        rp->setValueNotifyingHost (rp->convertTo0to1 (raw->load()));
        }
        // Delta / S range listening is a tool, not a setting: a project never reopens listening to the S alone
        setListenOff();

        auto meta = readMeta (*xml, ab.get(), ember::capture (state));
        if (messageThread)
        {
            {
                const juce::ScopedLock sl (metaLock);
                pendingMeta.reset();
            }
            applyMeta (meta);
        }
        else
        {
            // the editor, undo and A/B live on the message thread: it takes the restored meta over (timerCallback)
            const juce::ScopedLock sl (metaLock);
            pendingMeta = std::make_unique<Meta> (std::move (meta));
        }
        // an AUTO analysis (or result) from before this state does not belong to it
        projectLoads.fetch_add (1);
        if (messageThread)
            autoProjectLoaded();
    }

    // ---------------------------------------------------------------- presets
    void EmberAudioProcessor::setPresetMeta (int factory, const juce::String& user, bool isAuto)
    {
        auto values = ember::capture (state);
        const juce::ScopedLock sl (metaLock);
        factoryIndex = factory;
        userPresetName = user;
        fromAuto = isAuto;
        presetValues = std::move (values);
    }

    void EmberAudioProcessor::loadFactoryPreset (int index)
    {
        const auto& list = factoryPresets();
        if (! juce::isPositiveAndBelow (index, (int) list.size()))
            return;
        beginUndoStep (-1);
        discardPendingAuto();
        {
            const UndoGroup group (*this);
            ember::apply (state, list[(size_t) index].values);
        }
        setPresetMeta (index, {}, false);
    }

    bool EmberAudioProcessor::loadUserPreset (const juce::File& file)
    {
        ParamValues v;
        if (! ember::loadUserPreset (file, v))
            return false;
        beginUndoStep (-1);
        discardPendingAuto();
        {
            const UndoGroup group (*this);
            ember::apply (state, v);
        }
        setPresetMeta (-1, file.getFileNameWithoutExtension(), false);
        return true;
    }

    bool EmberAudioProcessor::saveUserPreset (const juce::File& file)
    {
        // "Lead 1.5" must not become "Lead 1.emberpreset" (and overwrite "Lead 1" without asking)
        const auto target = file.hasFileExtension (presetExtension) ? file : file.getSiblingFile (file.getFileName() + presetExtension);
        if (! ember::saveUserPreset (target, ember::capture (state)))
            return false;
        setPresetMeta (-1, target.getFileNameWithoutExtension(), false);
        return true;
    }

    juce::String EmberAudioProcessor::renameUserPreset (const juce::File& file, const juce::String& newName)
    {
        juce::File renamed;
        if (const auto r = renameUserPresetFile (file, newName, renamed); r.failed())
            return r.getErrorMessage();
        const auto oldName = file.getFileNameWithoutExtension(), name = renamed.getFileNameWithoutExtension();
        const juce::ScopedLock sl (metaLock);
        if (factoryIndex < 0 && userPresetName == oldName)
            userPresetName = name;
        for (auto& s : slots)
            if (s.factoryIndex < 0 && s.userPresetName == oldName)
                s.userPresetName = name;
        return {};
    }

    juce::String EmberAudioProcessor::deleteUserPreset (const juce::File& file)
    {
        // the setting itself stays as it is (also its name in the bar): only the file goes
        const auto r = deleteUserPresetFile (file);
        return r.failed() ? r.getErrorMessage() : juce::String();
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
        if (total <= 0 || direction == 0)
            return;
        int current = factoryIndex < numFactory ? factoryIndex : -1;
        if (current < 0 && userPresetName.isNotEmpty())
            for (int i = 0; i < users.size(); ++i)
                if (users[i].getFileNameWithoutExtension() == userPresetName)
                    current = numFactory + i;
        // not in the list (an AUTO setting, a deleted preset): forward starts at the first preset, back at the last
        const int next = current < 0 ? (direction > 0 ? 0 : total - 1)
                                     : ((current + direction) % total + total) % total;
        if (next < numFactory)
            loadFactoryPreset (next);
        else
            loadUserPreset (users[next - numFactory]);
    }

    // -------------------------------------------------------------------- A/B
    EmberAudioProcessor::Slot EmberAudioProcessor::currentSlot() const
    {
        return { ember::capture (state), presetValues, factoryIndex, userPresetName, fromAuto };
    }

    void EmberAudioProcessor::restoreMeta (const Slot& s)
    {
        const juce::ScopedLock sl (metaLock);
        presetValues = s.presetValues;
        factoryIndex = s.factoryIndex;
        userPresetName = s.userPresetName;
        fromAuto = s.fromAuto;
    }

    void EmberAudioProcessor::restoreSlot (const Slot& s)
    {
        discardPendingAuto();
        {
            const UndoGroup group (*this);   // switching back is the way back, not an undo step
            ember::apply (state, s.values);
        }
        restoreMeta (s);
    }

    void EmberAudioProcessor::switchSlot (int slot)
    {
        if (slot == activeSlot || ! juce::isPositiveAndBelow (slot, 2))
            return;
        const auto now = currentSlot();
        const bool visited = slotUsed[slot];
        {
            const juce::ScopedLock sl (metaLock);
            slots[activeSlot] = now;
            slotUsed[activeSlot] = true;
            activeSlot = slot;
            if (! visited)
            {
                slots[slot] = now;   // first visit: B starts as a copy of A
                slotUsed[slot] = true;
            }
        }
        if (visited)
            restoreSlot (slots[slot]);
    }

    void EmberAudioProcessor::copyActiveToOther()
    {
        const auto now = currentSlot();
        const juce::ScopedLock sl (metaLock);
        const int other = 1 - activeSlot;
        slots[other] = now;
        slotUsed[other] = true;
    }

    void EmberAudioProcessor::assignAutoSlots (const Slot& before)
    {
        // Undo of AUTO also brings A/B back as they were: the step beginUndoStep made for AUTO gets the slots
        if (! undoSteps.empty())
        {
            auto& step = undoSteps.back();
            step.restoresSlots = true;
            for (int i = 0; i < 2; ++i)
            {
                step.slots[i] = slots[i];
                step.slotUsed[i] = slotUsed[i];
            }
            step.activeSlot = activeSlot;
        }
        // AUTO never overwrites a setting parked on the other slot. Other slot free (never used, or the same as
        // before): before stays on the slot you are on, the result goes to the other one and is active (the first
        // time: A = before, B = AUTO). Something else parked there: the result goes into the active slot, before is
        // one undo away.
        const int other = 1 - activeSlot;
        const bool parked = slotUsed[other] && ! ember::sameValues (slots[other].values, before.values);
        const auto result = currentSlot();
        const juce::ScopedLock sl (metaLock);
        if (parked)
        {
            slots[activeSlot] = result;
            slotUsed[activeSlot] = true;
            autoBeforeSlot = -1;
        }
        else
        {
            slots[activeSlot] = before;
            slots[other] = result;
            slotUsed[0] = slotUsed[1] = true;
            autoBeforeSlot = activeSlot;
            activeSlot = other;
        }
    }

    // ----------------------------------------------------------- voice buttons
    int EmberAudioProcessor::getVoiceRange() const
    {
        return voiceRangeOf (state.getRawParameterValue (pid::freq)->load());
    }

    EmberAudioProcessor::VoiceResult EmberAudioProcessor::setVoice (int voice)
    {
        voice = juce::jlimit (0, 2, voice);
        const float hz[] { voiceLowHz, voiceMidHz, voiceHighHz };
        const float thr[] { voiceLowThrDb, voiceMidThrDb, voiceHighThrDb };
        const float freqNow = state.getRawParameterValue (pid::freq)->load();
        const float thrNow = state.getRawParameterValue (pid::threshold)->load();
        const auto presetValue = [this] (const char* id)
        {
            const auto it = presetValues.find (id);
            return it != presetValues.end() ? it->second : std::numeric_limits<float>::quiet_NaN();
        };
        // a click on the voice that is already lit keeps the frequency AUTO found in that range (e.g. 7.2 kHz in MID)
        if (fromAuto && std::abs (freqNow - presetValue (pid::freq)) < 1.0f && voiceRangeOf (freqNow) == voice)
            return VoiceResult::keptAutoFrequency;

        // The voice's threshold comes along (a deep voice's S sit lower against the voice) only while the threshold
        // is still one of the voice values (the default is MID's): one set by hand, by a preset or by AUTO stays
        bool atVoiceValue = false;
        for (float t : thr)
            atVoiceValue = atVoiceValue || std::abs (thrNow - t) < 0.05f;
        const bool autoThreshold = fromAuto && std::abs (thrNow - presetValue (pid::threshold)) < 0.05f;
        const bool moveThreshold = atVoiceValue && ! autoThreshold;

        beginUndoStep (-1);   // one click = one undo step
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
        set (pid::freq, hz[voice]);
        if (moveThreshold)
            set (pid::threshold, thr[voice]);
        return moveThreshold || std::abs (thrNow - thr[voice]) < 0.05f ? VoiceResult::set : VoiceResult::setKeptThreshold;
    }

    // ------------------------------------------------------------------- undo
    EmberAudioProcessor::Snapshot EmberAudioProcessor::snapshot() const
    {
        Snapshot s;
        s.current = currentSlot();
        for (int i = 0; i < 2; ++i)
        {
            s.slots[i] = slots[i];
            s.slotUsed[i] = slotUsed[i];
        }
        s.activeSlot = activeSlot;
        return s;
    }

    void EmberAudioProcessor::restoreSnapshot (const Snapshot& s)
    {
        {
            const UndoGroup group (*this);
            ember::apply (state, s.current.values);
        }
        restoreMeta (s.current);   // the preset name, its "*" and the AUTO result come back with the values
        if (s.restoresSlots)
        {
            const juce::ScopedLock sl (metaLock);
            for (int i = 0; i < 2; ++i)
            {
                slots[i] = s.slots[i];
                slotUsed[i] = s.slotUsed[i];
            }
            activeSlot = s.activeSlot;
        }
    }

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
        auto step = snapshot();
        if (! undoSteps.empty() && undoSteps.back().current == step.current)
            return;   // the edit before changed nothing
        redoSteps.clear();
        undoSteps.push_back (std::move (step));
        if (undoSteps.size() > 100)
            undoSteps.erase (undoSteps.begin());
    }

    bool EmberAudioProcessor::canUndo() const
    {
        const auto now = currentSlot();
        for (const auto& step : undoSteps)
            if (step.current != now)
                return true;
        return false;
    }

    bool EmberAudioProcessor::undo()
    {
        const auto now = currentSlot();
        while (! undoSteps.empty() && undoSteps.back().current == now)
            undoSteps.pop_back();   // an edit that changed nothing
        if (undoSteps.empty())
            return false;
        const auto target = undoSteps.back();
        undoSteps.pop_back();
        auto back = snapshot();
        back.restoresSlots = target.restoresSlots;
        redoSteps.push_back (std::move (back));
        restoreSnapshot (target);
        lastUndoParam = -1;
        return true;
    }

    bool EmberAudioProcessor::redo()
    {
        if (redoSteps.empty())
            return false;
        const auto target = redoSteps.back();
        redoSteps.pop_back();
        auto back = snapshot();
        back.restoresSlots = target.restoresSlots;
        undoSteps.push_back (std::move (back));
        restoreSnapshot (target);
        lastUndoParam = -1;
        return true;
    }

    void EmberAudioProcessor::setListenOff()
    {
        if (auto* p = state.getParameter (pid::listen); p != nullptr && p->getValue() > 0.0f)
        {
            std::optional<UndoGroup> group;
            if (isMessageThread())
                group.emplace (*this);
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
        return autoPhase == AutoPhase::done && fromAuto && factoryIndex < 0 && userPresetName.isEmpty() && ! isPresetModified();
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
        // ---- preset name and A/B (fix-ux): AUTO never overwrites a setting parked on the other slot, see assignAutoSlots
        setPresetMeta (-1, {}, true);
        assignAutoSlots (before);
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
                       + (autoBeforeSlot == 0   ? tr (" Vorher liegt auf A.", " Before is on A.")
                          : autoBeforeSlot == 1 ? tr (" Vorher liegt auf B.", " Before is on B.")
                                                : format (tr (" Vorher: Strg+Z. Deine Einstellung auf {0} bleibt.", " Before: Ctrl+Z. Your setting on {0} stays."),
                                                          { activeSlot == 0 ? "B" : "A" }));
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
        applyPendingMeta();   // a state the host restored off the message thread
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
