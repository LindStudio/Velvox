#include "SatPanel.h"
#include "../PluginProcessor.h"
#include "../DSP/SaturationModule.h"

namespace velvox::ui
{
    namespace
    {
        juce::RangedAudioParameter* paramFor (APVTS& state, const char* baseId, int instance)
        {
            if (auto* p = state.getParameter (dsp::instanceParamId (baseId, instance)))
                return p;
            return state.getParameter (baseId);
        }

        float realValue (const juce::RangedAudioParameter& p) { return p.convertFrom0to1 (p.getValue()); }
    }

    //==========================================================================
    SatLink::SatLink (APVTS& state, int instance)
    {
        drives = { paramFor (state, pid::satLowDrive, instance), paramFor (state, pid::satMidDrive, instance),
                   paramFor (state, pid::satHighDrive, instance) };
        link = paramFor (state, pid::satLink, instance);
        for (auto* d : drives)
            if (d != nullptr)
                d->addListener (this);
    }

    SatLink::~SatLink()
    {
        for (auto* d : drives)
            if (d != nullptr)
                d->removeListener (this);
    }

    int SatLink::bandOf (int parameterIndex) const noexcept
    {
        for (int b = 0; b < 3; ++b)
            if (drives[(size_t) b] != nullptr && drives[(size_t) b]->getParameterIndex() == parameterIndex)
                return b;
        return -1;
    }

    void SatLink::parameterGestureChanged (int parameterIndex, bool starting)
    {
        if (writing || ! juce::MessageManager::existsAndIsCurrentThread())
            return;
        const int band = bandOf (parameterIndex);
        if (band < 0)
            return;

        if (starting)
        {
            if (driving >= 0 || link == nullptr || link->getValue() < 0.5f)
                return;
            driving = band;
            for (int b = 0; b < 3; ++b)
                start[(size_t) b] = realValue (*drives[(size_t) b]);
            const juce::ScopedValueSetter<bool> own (writing, true);
            for (int b = 0; b < 3; ++b)
                if (b != band)
                    drives[(size_t) b]->beginChangeGesture();
        }
        else if (band == driving)
        {
            const juce::ScopedValueSetter<bool> own (writing, true);
            for (int b = 0; b < 3; ++b)
                if (b != band)
                    drives[(size_t) b]->endChangeGesture();
            driving = -1;
        }
    }

    void SatLink::parameterValueChanged (int parameterIndex, float newValue)
    {
        // Host automation may arrive on the audio thread: never act there (and never without a gesture)
        if (writing || driving < 0 || ! juce::MessageManager::existsAndIsCurrentThread())
            return;
        const int band = bandOf (parameterIndex);
        if (band != driving)
            return;

        const auto& range = drives[(size_t) band]->getNormalisableRange();
        const auto linked = dsp::linkedBandDrives (start, band, drives[(size_t) band]->convertFrom0to1 (newValue), range.start, range.end);
        const juce::ScopedValueSetter<bool> own (writing, true);
        for (int b = 0; b < 3; ++b)
            if (b != band)
            {
                auto* p = drives[(size_t) b];
                const float target = p->convertTo0to1 (linked[(size_t) b]);
                if (std::abs (target - p->getValue()) > 1.0e-6f)
                    p->setValueNotifyingHost (target);
            }
    }
}
