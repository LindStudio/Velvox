#include "CheckFixPlanner.h"
#include "../DSP/Instances.h"
#include "../Parameters/ParameterLayout.h"
#include "../StateKeys.h"
#include <map>

namespace velvox::check
{
    using dsp::ModuleId;

    const char* moduleOnParam (ModuleId m) noexcept
    {
        switch (m)
        {
            case ModuleId::eq:      return pid::eqOn;
            case ModuleId::comp:    return pid::compOn;
            case ModuleId::deess:   return pid::deessOn;
            case ModuleId::sat:     return pid::satOn;
            case ModuleId::doubler: return pid::doublerOn;
            case ModuleId::pitch:   return pid::pitchOn;
            case ModuleId::delay:   return pid::delayOn;
            case ModuleId::reverb:  return pid::reverbOn;
            case ModuleId::chop:    return pid::chopOn;
            case ModuleId::pulse:   return pid::pulseOn;
            case ModuleId::smash:   return pid::smashOn;
            case ModuleId::choir:   return pid::choirOn;
            case ModuleId::smooth:  return pid::smoothOn;
            case ModuleId::check:   return pid::checkOn;
            case ModuleId::ride:    return pid::rideOn;
        }
        return pid::eqOn;
    }

    namespace
    {
        bool near (float a, float b) { return a > 0.0f && b > 0.0f && std::abs (std::log2 (a / b)) <= 1.0f / 3.0f + 1.0e-3f; }

        float clampTo (const juce::String& id, float v)
        {
            if (const auto* spec = findParameterSpec (dsp::splitInstanceParamId (id).first))
                return juce::jlimit (spec->minValue, spec->maxValue, v);
            return v;
        }

        // Legacy EQ slots
        enum class Slot { low, mid, high };
        juce::String idOf (const char* base, int k) { return dsp::instanceParamId (base, k); }

        Slot bellSlot (float freq, int k, const ValueLookup& v)
        {
            const bool lowShelfInUse = v (idOf (pid::lowType, k)) < 0.5f && std::abs (v (idOf (pid::lowGain, k))) > 0.01f;
            if (freq < 600.0f)
                return lowShelfInUse ? Slot::mid : Slot::low;
            return freq <= 8000.0f ? Slot::mid : Slot::high;
        }

        struct Planner
        {
            const FixContext& c;
            dsp::Routing working;
            bool routingChanged = false;
            std::map<juce::String, float> out;            // full id -> value
            std::vector<juce::String> order;              // stable output order
            std::vector<ModuleId> ensured;

            explicit Planner (const FixContext& context) : c (context), working (context.routing) {}

            float current (const juce::String& id) const
            {
                const auto it = out.find (id);
                return it != out.end() ? it->second : c.value (id);
            }

            void put (const juce::String& id, float v)
            {
                v = clampTo (id, v);
                if (out.find (id) == out.end())
                    order.push_back (id);
                out[id] = v;
            }

            static int instanceIn (const dsp::Routing& r, ModuleId m)
            {
                for (auto& s : r)
                    if (s.id == m)
                        return s.instance;
                return 0;
            }

            int instanceOf (ModuleId m) const { return instanceIn (working, m); }

            bool measured (ModuleId m, int k) const
            {
                const int at = c.routing.indexOf (m, k), checkAt = c.routing.indexOf (ModuleId::check, 0);
                return at >= 0 && checkAt >= 0 && at < checkAt && c.audible && c.audible (m, k);
            }

            bool isEnsured (ModuleId m) const { return std::find (ensured.begin(), ensured.end(), m) != ensured.end(); }

            void ensure (ModuleId m)
            {
                if (isEnsured (m))
                    return;
                ensured.push_back (m);
                const int k = instanceOf (m);
                if (! working.contains (m, k))
                {
                    working.insertDefault (m, k);
                    // Delay / reverb join as inserts when VOCAL CHECK adds them (it never does today)
                    routingChanged = true;
                }
                const auto on = idOf (moduleOnParam (m), k);
                if (current (on) < 0.5f)
                    put (on, 1.0f);
            }

            // A module change is applied when the fix switches the module on, or it is audible
            bool usable (ModuleId m, int k) const { return isEnsured (m) || (c.audible && c.audible (m, k)); }

            // Value for one parameter change, planned against the CURRENT state (not other changes)
            std::optional<std::pair<juce::String, float>> plan (const ParamChange& p) const
            {
                const auto* spec = findParameterSpec (p.paramId);
                if (spec == nullptr)
                    return std::nullopt;
                const auto module = moduleOfParameter (*spec);
                juce::String id = p.paramId;
                bool inMeasurement = false;
                if (module.has_value())
                {
                    const int k = instanceOf (*module);
                    if (! usable (*module, k))
                        return std::nullopt;
                    id = dsp::instanceParamId (p.paramId, k);
                    inMeasurement = measured (*module, k);
                }
                else
                {
                    // Global parameters: the input gain acts before every module, the output
                    // stage (width, output gain) after the chain
                    inMeasurement = p.paramId == pid::inputGain;
                }
                const float cur = c.value (id);
                float v = p.value;
                switch (p.kind)
                {
                    case ParamChange::Kind::set:     v = p.value; break;
                    case ParamChange::Kind::atLeast: v = std::max (cur, p.value); break;
                    case ParamChange::Kind::atMost:  v = std::min (cur, p.value); break;
                    case ParamChange::Kind::amount:
                    {
                        if (inMeasurement)
                            v = cur + p.value;
                        else
                        {
                            const float target = p.neutral + p.value;
                            v = p.value < 0.0f ? std::min (cur, target) : std::max (cur, target);
                        }
                        break;
                    }
                }
                v = clampTo (id, v);
                if (std::abs (v - cur) < 1.0e-4f)
                    return std::nullopt;
                return std::make_pair (id, v);
            }

            void planEq (const EqBandRequest& req)
            {
                if (c.eq == nullptr)
                    return;
                const int k = instanceOf (ModuleId::eq);
                if (! usable (ModuleId::eq, k))
                    return;
                const ValueLookup lookup = [this] (const juce::String& id) { return current (id); };
                const auto band = c.eq->find (req, k, lookup);
                const bool inMeasurement = measured (ModuleId::eq, k) && band.has_value() && band->on;

                EqBandRequest abs = req;
                if (req.shape == EqShape::lowCut || req.shape == EqShape::highCut)
                {
                    const bool low = req.shape == EqShape::lowCut;
                    if (band.has_value() && band->on)
                    {
                        // Before VOCAL CHECK the existing cut is in the measurement: go a third higher
                        const float existing = inMeasurement ? band->freqHz * (low ? 1.26f : 0.79f) : band->freqHz;
                        abs.freqHz = low ? std::max (req.freqHz, existing) : std::min (req.freqHz, existing);
                        abs.slopeDbPerOct = std::max (req.slopeDbPerOct, band->slopeDbPerOct);
                    }
                    abs.gainDb = 0.0f;
                }
                else
                {
                    const float cur = band.has_value() ? band->gainDb : 0.0f;
                    abs.gainDb = inMeasurement ? cur + req.gainDb : (req.gainDb < 0.0f ? std::min (cur, req.gainDb) : std::max (cur, req.gainDb));
                }
                for (auto& p : c.eq->write (abs, k, lookup))
                    if (std::abs (clampTo (p.paramId, p.value) - c.value (p.paramId)) > 1.0e-4f || out.count (p.paramId) > 0)
                        put (p.paramId, p.value);
            }

            ResolvedFix result (const juce::String& name) const
            {
                ResolvedFix f;
                f.name = name;
                for (auto& id : order)
                    if (std::abs (out.at (id) - c.value (id)) > 1.0e-4f)
                        f.params.push_back ({ id, out.at (id) });
                if (routingChanged)
                    f.routing = working;
                return f;
            }
        };

        // Requests that address the same band (see the resolver contract)
        bool sameBand (const EqBandRequest& a, const EqBandRequest& b)
        {
            if (a.role != b.role || (a.shape == EqShape::lowCut) != (b.shape == EqShape::lowCut))
                return false;
            return a.role != EqRole::autoRole || near (a.freqHz, b.freqHz);
        }
    }

    ResolvedFix planFix (const FixAction& action, const juce::String& name, const FixContext& context)
    {
        return planFixes ({ { action, name } }, context, name);
    }

    ResolvedFix planFixes (const std::vector<std::pair<FixAction, juce::String>>& fixes, const FixContext& context,
                           const juce::String& name)
    {
        Planner p (context);
        for (auto& [action, n] : fixes)
            for (auto m : action.modules)
                p.ensure (m);

        // Parameters: each planned against the current state; the same parameter twice keeps
        // the value farthest from the current one
        std::map<juce::String, float> params;
        std::vector<juce::String> order;
        for (auto& [action, n] : fixes)
            for (auto& change : action.params)
                if (const auto v = p.plan (change))
                {
                    const auto it = params.find (v->first);
                    if (it == params.end())
                    {
                        params[v->first] = v->second;
                        order.push_back (v->first);
                    }
                    else if (std::abs (v->second - context.value (v->first)) > std::abs (it->second - context.value (v->first)))
                        it->second = v->second;
                }
        for (auto& id : order)
            p.put (id, params[id]);

        // EQ: one request per band, the stronger one
        std::vector<EqBandRequest> bands;
        for (auto& [action, n] : fixes)
            for (auto& r : action.eq)
            {
                auto it = std::find_if (bands.begin(), bands.end(), [&r] (const EqBandRequest& b) { return sameBand (b, r); });
                if (it == bands.end())
                    bands.push_back (r);
                else if (r.shape == EqShape::lowCut || r.shape == EqShape::highCut)
                {
                    it->freqHz = r.shape == EqShape::lowCut ? std::max (it->freqHz, r.freqHz) : std::min (it->freqHz, r.freqHz);
                    it->slopeDbPerOct = std::max (it->slopeDbPerOct, r.slopeDbPerOct);
                }
                else if (std::abs (r.gainDb) > std::abs (it->gainDb))
                    *it = r;
            }
        for (auto& b : bands)
            p.planEq (b);

        return p.result (name);
    }

    //==========================================================================
    std::optional<EqBandResolver::Band> LegacyEqResolver::find (const EqBandRequest& r, int k, const ValueLookup& v) const
    {
        const bool eqOn = v (idOf (pid::eqOn, k)) > 0.5f;
        const auto band = [&] (const char* freq, const char* gain, const char* q) -> std::optional<Band>
        {
            Band b;
            b.freqHz = v (idOf (freq, k));
            b.gainDb = v (idOf (gain, k));
            b.q = v (idOf (q, k));
            b.on = eqOn;
            return b;
        };
        switch (r.shape)
        {
            case EqShape::lowCut:
                if (v (idOf (pid::hpfOn, k)) > 0.5f)
                    return Band { v (idOf (pid::hpfFreq, k)), 0.0f, 0.7071f, v (idOf (pid::hpfSlope, k)) > 0.5f ? 24 : 12, eqOn };
                return std::nullopt;
            case EqShape::highCut:
                if (v (idOf (pid::lpfOn, k)) > 0.5f)
                    return Band { v (idOf (pid::lpfFreq, k)), 0.0f, 0.7071f, 12, eqOn };
                return std::nullopt;
            case EqShape::lowShelf:
                if (v (idOf (pid::lowType, k)) < 0.5f && std::abs (v (idOf (pid::lowGain, k))) > 0.01f)
                    return band (pid::lowFreq, pid::lowGain, pid::lowQ);
                return std::nullopt;
            case EqShape::highShelf:
                if (v (idOf (pid::highType, k)) < 0.5f && std::abs (v (idOf (pid::highGain, k))) > 0.01f)
                    return band (pid::highFreq, pid::highGain, pid::highQ);
                return std::nullopt;
            case EqShape::bell:
                switch (bellSlot (r.freqHz, k, v))
                {
                    case Slot::low:
                        if (v (idOf (pid::lowType, k)) > 0.5f && std::abs (v (idOf (pid::lowGain, k))) > 0.01f && near (v (idOf (pid::lowFreq, k)), r.freqHz))
                            return band (pid::lowFreq, pid::lowGain, pid::lowQ);
                        return std::nullopt;
                    case Slot::mid:
                        if (std::abs (v (idOf (pid::midGain, k))) > 0.01f && near (v (idOf (pid::midFreq, k)), r.freqHz))
                            return band (pid::midFreq, pid::midGain, pid::midQ);
                        return std::nullopt;
                    case Slot::high:
                        if (v (idOf (pid::highType, k)) > 0.5f && std::abs (v (idOf (pid::highGain, k))) > 0.01f && near (v (idOf (pid::highFreq, k)), r.freqHz))
                            return band (pid::highFreq, pid::highGain, pid::highQ);
                        return std::nullopt;
                }
        }
        return std::nullopt;
    }

    std::vector<ResolvedParam> LegacyEqResolver::write (const EqBandRequest& r, int k, const ValueLookup& v) const
    {
        std::vector<ResolvedParam> p;
        const auto add = [&p, k] (const char* base, float value) { p.push_back ({ idOf (base, k), value }); };
        switch (r.shape)
        {
            case EqShape::lowCut:
                add (pid::hpfOn, 1.0f);
                add (pid::hpfFreq, r.freqHz);
                add (pid::hpfSlope, r.slopeDbPerOct >= 24 ? 1.0f : 0.0f);
                break;
            case EqShape::highCut:
                add (pid::lpfOn, 1.0f);
                add (pid::lpfFreq, r.freqHz);
                break;
            case EqShape::lowShelf:
                add (pid::lowType, 0.0f);
                add (pid::lowFreq, r.freqHz);
                add (pid::lowGain, r.gainDb);
                add (pid::lowQ, 0.7071f);
                break;
            case EqShape::highShelf:
                add (pid::highType, 0.0f);
                add (pid::highFreq, r.freqHz);
                add (pid::highGain, r.gainDb);
                add (pid::highQ, 0.7071f);
                break;
            case EqShape::bell:
                switch (bellSlot (r.freqHz, k, v))
                {
                    case Slot::low:
                        add (pid::lowType, 1.0f);
                        add (pid::lowFreq, r.freqHz);
                        add (pid::lowGain, r.gainDb);
                        add (pid::lowQ, r.q);
                        break;
                    case Slot::mid:
                        add (pid::midFreq, r.freqHz);
                        add (pid::midGain, r.gainDb);
                        add (pid::midQ, r.q);
                        break;
                    case Slot::high:
                        add (pid::highType, 1.0f);
                        add (pid::highFreq, r.freqHz);
                        add (pid::highGain, r.gainDb);
                        add (pid::highQ, r.q);
                        break;
                }
                break;
        }
        return p;
    }

    //==========================================================================
    void applyResolvedFix (juce::AudioProcessorValueTreeState& apvts, const ResolvedFix& fix)
    {
        if (fix.empty())
            return;
        auto* undo = apvts.undoManager;
        if (undo != nullptr)
            undo->beginNewTransaction (fix.name);
        for (auto& p : fix.params)
            if (auto* param = apvts.getParameter (p.paramId))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->convertTo0to1 (p.value));
                param->endChangeGesture();
            }
        if (fix.routing.has_value())
            apvts.state.setProperty (statekeys::routing, fix.routing->toString(), undo);
        // Parameter values reach the state tree on the APVTS timer; flush now so they are part
        // of this transaction (and not of whatever the user does next)
        if (undo != nullptr)
            apvts.copyState();
    }
}
