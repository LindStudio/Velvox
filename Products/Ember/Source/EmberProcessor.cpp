#include "EmberProcessor.h"
#include "EmberEditor.h"
#include "Text/I18n.h"

namespace ember
{
    using vocalypse::i18n::tr;
    using vocalypse::i18n::format;
    using vocalypse::i18n::number;

    EmberAudioProcessor::EmberAudioProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state (*this, nullptr, stateType, createLayout())
    {
        bypassParam = state.getParameter (pid::bypass);
        presetValues = ember::capture (state);
        engine.prepare (currentRate, 512);
        capture.prepare (currentRate);
        setLatencySamples (engine.getLatencySamples());
        startTimerHz (10);
    }

    EmberAudioProcessor::~EmberAudioProcessor()
    {
        stopTimer();
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
        currentRate = sampleRate;
        engine.setSettings (readSettings (state));
        engine.prepare (sampleRate, samplesPerBlock);
        capture.prepare (sampleRate);
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

        capture.push (buffer.getArrayOfReadPointers(), channels, buffer.getNumSamples());
        engine.setSettings (params.read());
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
        state.replaceState (juce::ValueTree::fromXml (*xml));
        factoryIndex = (int) state.state.getProperty ("factoryPreset", 0);
        userPresetName = state.state.getProperty ("userPreset", juce::String()).toString();
        // replaceState leaves parameters that the saved state does not mention untouched and does not
        // always resend values; snap every parameter to the stored value (VOCALYPSE lesson 0.6.1)
        for (auto* p : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (auto* raw = state.getRawParameterValue (rp->getParameterID()))
                    rp->setValueNotifyingHost (rp->convertTo0to1 (raw->load()));
        // a project reopens as it was saved: the restored values count as the preset's
        presetValues = ember::capture (state);
    }

    // ---------------------------------------------------------------- presets
    void EmberAudioProcessor::loadFactoryPreset (int index)
    {
        const auto& list = factoryPresets();
        if (! juce::isPositiveAndBelow (index, (int) list.size()))
            return;
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
        const auto target = file.withFileExtension (presetExtension);
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
    void EmberAudioProcessor::switchSlot (int slot)
    {
        if (slot == activeSlot || ! juce::isPositiveAndBelow (slot, 2))
            return;
        slots[activeSlot] = ember::capture (state);
        slotUsed[activeSlot] = true;
        activeSlot = slot;
        if (slotUsed[slot])
            ember::apply (state, slots[slot]);
        else
        {
            slots[slot] = ember::capture (state);   // first visit: B starts as a copy of A
            slotUsed[slot] = true;
        }
    }

    void EmberAudioProcessor::copyActiveToOther()
    {
        const int other = 1 - activeSlot;
        slots[other] = ember::capture (state);
        slotUsed[other] = true;
    }

    // ------------------------------------------------------------------- AUTO
    void EmberAudioProcessor::startAuto()
    {
        capture.prepare (currentRate);
        capture.start();
        autoPhase = AutoPhase::listening;
    }

    void EmberAudioProcessor::cancelAuto()
    {
        if (autoPhase == AutoPhase::analysing)
            return;
        capture.stop();
        autoPhase = AutoPhase::idle;
    }

    double EmberAudioProcessor::getAutoProgress() const noexcept
    {
        return autoPhase == AutoPhase::listening ? juce::jlimit (0.0, 1.0, capture.getSeconds() / autoEnoughSeconds) : 0.0;
    }

    void EmberAudioProcessor::finishAuto()
    {
        if (autoPhase != AutoPhase::listening)
            return;
        capture.stop();
        std::vector<float> take (capture.data(), capture.data() + capture.getNumSamples());
        const double rate = capture.getSampleRate();
        autoPhase = AutoPhase::analysing;
        pending = std::async (std::launch::async, [take = std::move (take), rate]
        {
            return analyse (take.data(), (int) take.size(), rate);
        });
    }

    bool EmberAudioProcessor::pollAuto()
    {
        if (autoPhase != AutoPhase::analysing || ! pending.valid()
            || pending.wait_for (std::chrono::seconds (0)) != std::future_status::ready)
            return false;
        lastAuto = pending.get();
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
        if (pending.valid())
            pending.wait();
        pollAuto();
    }

    void EmberAudioProcessor::applyAuto (const AutoResult& r)
    {
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
        set (pid::amount, 50.0f);
        set (pid::mode, 0.0f);
        set (pid::detection, 0.0f);
        set (pid::listen, 0.0f);
        factoryIndex = -1;
        userPresetName = {};
        presetValues = ember::capture (state);
        state.state.setProperty ("factoryPreset", -1, nullptr);
        state.state.setProperty ("userPreset", juce::String(), nullptr);
    }

    juce::String EmberAudioProcessor::getAutoMessage() const
    {
        switch (autoPhase)
        {
            case AutoPhase::idle: return {};
            case AutoPhase::listening:
                return tr ("Ember hört zu … sing einfach weiter.", "Ember is listening … keep singing.");
            case AutoPhase::analysing:
                return tr ("Ember probiert Einstellungen aus …", "Ember is trying settings …");
            case AutoPhase::done:
            {
                const auto khz = number (lastAuto.freq / 1000.0, 1);
                return format (tr ("Eingestellt: ab {0} kHz, S-Laute −{1} dB, Vokale bleiben unberührt.",
                                   "Set: from {0} kHz, S sounds −{1} dB, vowels stay untouched."),
                               { khz, number (lastAuto.expectedReductionDb, 1) });
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
        pollAuto();
        if (autoPhase != AutoPhase::listening)
            return;
        if (capture.getSeconds() >= autoEnoughSeconds || ! capture.isCapturing())
            finishAuto();
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
