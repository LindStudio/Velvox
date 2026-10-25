#include "EqBands.h"
#include "ParameterLayout.h"
#include "../DSP/Instances.h"
#include <mutex>

namespace velvox::eqbands
{
    namespace
    {
        const char* const fixedBandIds[] { pid::hpfOn, pid::hpfFreq, pid::hpfSlope, pid::lowType, pid::lowFreq, pid::lowGain, pid::lowQ,
                                           pid::midFreq, pid::midGain, pid::midQ, pid::highType, pid::highFreq, pid::highGain, pid::highQ,
                                           pid::lpfOn, pid::lpfFreq, pid::lpfSlope };

        const char* const bandFields[] { pid::eqband::on, pid::eqband::type, pid::eqband::freq, pid::eqband::gain,
                                         pid::eqband::q, pid::eqband::slope, pid::eqband::role };

        int numEqInstances() noexcept { return dsp::maxInstances (dsp::ModuleId::eq); }

        float specDefault (const juce::String& id)
        {
            const auto* spec = findParameterSpec (id);
            return spec != nullptr ? spec->defaultValue : 0.0f;
        }

        // The 0.5 fixed-band values of one instance as EqSettings fields
        dsp::EqSettings fixedBandsOf (const Access& a, int instance)
        {
            const auto v = [&] (const char* base) { return a.get (dsp::instanceParamId (base, instance)); };
            dsp::EqSettings s;
            s.hpfOn = v (pid::hpfOn) > 0.5f;
            s.hpfFreq = v (pid::hpfFreq);
            s.hpfSlope = juce::roundToInt (v (pid::hpfSlope));
            s.lowType = juce::roundToInt (v (pid::lowType));
            s.lowFreq = v (pid::lowFreq);
            s.lowGain = v (pid::lowGain);
            s.lowQ = v (pid::lowQ);
            s.midFreq = v (pid::midFreq);
            s.midGain = v (pid::midGain);
            s.midQ = v (pid::midQ);
            s.highType = juce::roundToInt (v (pid::highType));
            s.highFreq = v (pid::highFreq);
            s.highGain = v (pid::highGain);
            s.highQ = v (pid::highQ);
            s.lpfOn = v (pid::lpfOn) > 0.5f;
            s.lpfFreq = v (pid::lpfFreq);
            s.lpfSlope = juce::roundToInt (v (pid::lpfSlope));
            return s;
        }

        // Suspend depth per plugin instance (keyed by its APVTS); never touched by the audio thread
        struct SuspendRegistry
        {
            std::mutex lock;
            std::map<const juce::AudioProcessorValueTreeState*, int> depth;
        };

        SuspendRegistry& suspendRegistry()
        {
            static SuspendRegistry registry;
            return registry;
        }
    }

    //==========================================================================
    Access accessFor (juce::AudioProcessorValueTreeState& apvts)
    {
        Access a;
        a.get = [&apvts] (const juce::String& id)
        {
            auto* raw = apvts.getRawParameterValue (id);
            return raw != nullptr ? raw->load() : specDefault (id);
        };
        a.set = [&apvts] (const juce::String& id, float value)
        {
            auto* p = apvts.getParameter (id);
            if (p == nullptr || ! std::isfinite (value))
                return;
            const float norm = p->convertTo0to1 (value);
            if (std::abs (norm - p->getValue()) < 1.0e-7f)
                return;
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        };
        return a;
    }

    Access accessFor (std::map<juce::String, float>& values, const std::map<juce::String, float>* fallback)
    {
        Access a;
        a.get = [&values, fallback] (const juce::String& id)
        {
            if (auto it = values.find (id); it != values.end())
                return it->second;
            if (fallback != nullptr)
                if (auto it = fallback->find (id); it != fallback->end())
                    return it->second;
            return specDefault (id);
        };
        a.set = [&values] (const juce::String& id, float value)
        {
            const auto* spec = findParameterSpec (id);
            if (spec == nullptr || ! std::isfinite (value))
                return;
            float v = juce::jlimit (spec->minValue, spec->maxValue, value);
            if (spec->type != ParameterSpec::Type::Float)
                v = (float) juce::roundToInt (v);
            values[id] = v;
        };
        return a;
    }

    juce::String bandParamId (int instance, int band, const char* field)
    {
        return dsp::instanceParamId (pid::eqBandId (band + 1, field), instance);
    }

    dsp::EqBand readBand (const Access& a, int instance, int band)
    {
        const auto v = [&] (const char* field) { return a.get (bandParamId (instance, band, field)); };
        dsp::EqBand b;
        b.on = v (pid::eqband::on) > 0.5f;
        b.type = juce::roundToInt (v (pid::eqband::type));
        b.freq = v (pid::eqband::freq);
        b.gain = v (pid::eqband::gain);
        b.q = v (pid::eqband::q);
        b.slope = juce::roundToInt (v (pid::eqband::slope));
        b.role = juce::roundToInt (v (pid::eqband::role));
        return b;
    }

    void writeBand (const Access& a, int instance, int band, const dsp::EqBand& b)
    {
        const auto set = [&] (const char* field, float value) { a.set (bandParamId (instance, band, field), value); };
        set (pid::eqband::role, (float) b.role);   // role first: whoever reacts to "On" sees the owner
        set (pid::eqband::type, (float) b.type);
        set (pid::eqband::freq, b.freq);
        set (pid::eqband::gain, b.gain);
        set (pid::eqband::q, b.q);
        set (pid::eqband::slope, (float) b.slope);
        set (pid::eqband::on, b.on ? 1.0f : 0.0f);
    }

    int findBand (const Access& a, int instance, dsp::EqRole role)
    {
        if (role == dsp::EqRole::none)
            return -1;
        for (int i = 0; i < dsp::maxEqBands; ++i)
            if (juce::roundToInt (a.get (bandParamId (instance, i, pid::eqband::role))) == (int) role)
                return i;
        return -1;
    }

    int findFreeBand (const Access& a, int instance)
    {
        for (int i = 0; i < dsp::maxEqBands; ++i)
            if (readBand (a, instance, i).isFree())
                return i;
        return -1;
    }

    int findOrCreateBand (const Access& a, int instance, dsp::EqRole role, dsp::EqBandType type,
                          float freqHz, float gainDb, float q, int slope)
    {
        if (role == dsp::EqRole::none)
        {
            jassertfalse;   // role bands only; user bands belong to the user
            return -1;
        }
        int band = findBand (a, instance, role);
        if (band < 0)
            band = findFreeBand (a, instance);
        if (band < 0)
            return -1;

        auto b = readBand (a, instance, band);
        b.on = true;
        b.type = (int) type;
        b.freq = freqHz;
        b.gain = gainDb;
        b.q = q;
        if (slope >= 0)
            b.slope = slope;
        b.role = (int) role;
        writeBand (a, instance, band, b);
        return band;
    }

    //==========================================================================
    bool containsBandValues (const juce::StringArray& ids)
    {
        for (auto& id : ids)
            if (id.startsWith ("eqBand"))
                return true;
        return false;
    }

    bool containsBandValues (const std::map<juce::String, float>& values)
    {
        for (auto& [id, v] : values)
            if (id.startsWith ("eqBand"))
                return true;
        return false;
    }

    bool fixedBandsAtDefaults (const Access& a, int instance)
    {
        for (auto* base : fixedBandIds)
        {
            const auto id = dsp::instanceParamId (base, instance);
            const float d = specDefault (id);
            if (std::abs (a.get (id) - d) > 1.0e-4f * std::max (1.0f, std::abs (d)))
                return false;
        }
        return true;
    }

    void migrateInstance (const Access& a, int instance, bool legacyResponse)
    {
        const auto bands = dsp::EqualizerModule::legacyBands (fixedBandsOf (a, instance));
        for (int i = 0; i < (int) bands.size(); ++i)
            writeBand (a, instance, i, bands[(size_t) i]);
        if (legacyResponse)
            a.set (dsp::instanceParamId (pid::eqLegacyResponse, instance), 1.0f);
    }

    void migrateState (const Access& a, bool legacyResponse)
    {
        for (int k = 0; k < numEqInstances(); ++k)
            if (k == 0 || ! fixedBandsAtDefaults (a, k))
                migrateInstance (a, k, legacyResponse);
    }

    void clearLegacyResponse (juce::AudioProcessorValueTreeState& apvts, int instance)
    {
        accessFor (apvts).set (dsp::instanceParamId (pid::eqLegacyResponse, instance), 0.0f);
    }

    //==========================================================================
    ScopedBridgeSuspend::ScopedBridgeSuspend (const juce::AudioProcessorValueTreeState& s) : state (s)
    {
        auto& r = suspendRegistry();
        const std::lock_guard<std::mutex> g (r.lock);
        ++r.depth[&state];
    }

    ScopedBridgeSuspend::~ScopedBridgeSuspend()
    {
        auto& r = suspendRegistry();
        const std::lock_guard<std::mutex> g (r.lock);
        if (auto it = r.depth.find (&state); it != r.depth.end() && --it->second <= 0)
            r.depth.erase (it);   // no stale key for a later instance at the same address
    }

    bool ScopedBridgeSuspend::isActive (const juce::AudioProcessorValueTreeState& s)
    {
        auto& r = suspendRegistry();
        const std::lock_guard<std::mutex> g (r.lock);
        return r.depth.count (&s) > 0;
    }

    LegacyBridge::LegacyBridge (juce::AudioProcessorValueTreeState& state) : apvts (state)
    {
        for (int k = 0; k < numEqInstances(); ++k)
            for (auto* base : fixedBandIds)
                ids.add (dsp::instanceParamId (base, k));
        for (auto& id : ids)
        {
            auto* raw = apvts.getRawParameterValue (id);
            lastValues[id].store (raw != nullptr ? raw->load() : 0.0f);
            apvts.addParameterListener (id, this);
        }
    }

    LegacyBridge::~LegacyBridge()
    {
        for (auto& id : ids)
            apvts.removeParameterListener (id, this);
    }

    void LegacyBridge::parameterChanged (const juce::String& parameterID, float newValue)
    {
        const auto last = lastValues.find (parameterID);
        if (last == lastValues.end() || last->second.exchange (newValue) == newValue)
            return;
        if (! juce::MessageManager::existsAndIsCurrentThread() || ScopedBridgeSuspend::isActive (apvts))
            return;

        const auto [base, instance] = dsp::splitInstanceParamId (parameterID);
        int group = -1;
        if (base.startsWith ("hpf"))       group = 0;
        else if (base.startsWith ("low"))  group = 1;
        else if (base.startsWith ("mid"))  group = 2;
        else if (base.startsWith ("high")) group = 3;
        else if (base.startsWith ("lpf"))  group = 4;
        if (group < 0)
            return;

        const auto access = accessFor (apvts);
        auto desired = dsp::EqualizerModule::legacyBands (fixedBandsOf (access, instance))[(size_t) group];
        int band = findBand (access, instance, desired.bandRole());
        if (band < 0)
        {
            const bool audible = desired.isCut() ? desired.on : std::abs (desired.gain) > 1.0e-4f;
            if (! audible)
                return;
            band = findFreeBand (access, instance);
            if (band < 0)
                return;
        }

        ScopedBridgeSuspend noRecursion (apvts);
        writeBand (access, instance, band, desired);
    }
}

namespace velvox
{
    int findOrCreateEqBand (juce::AudioProcessorValueTreeState& apvts, dsp::EqRole role, dsp::EqBandType type,
                            float freqHz, float gainDb, float q, int instance)
    {
        return eqbands::findOrCreateBand (eqbands::accessFor (apvts), instance, role, type, freqHz, gainDb, q);
    }
}
