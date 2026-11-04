#include "DecisionLog.h"
#include "../Parameters/ParameterLayout.h"
#include "../DSP/Instances.h"

namespace vocalypse::automix
{
    namespace
    {
        juce::var listToVar (const std::vector<Measured>& list)
        {
            juce::Array<juce::var> a;
            for (auto& m : list)
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("key", m.key);
                o->setProperty ("value", (double) m.value);
                o->setProperty ("unit", m.unit);
                a.add (juce::var (o));
            }
            return a;
        }

        std::vector<Measured> listFromVar (const juce::var& v)
        {
            std::vector<Measured> out;
            if (auto* a = v.getArray())
                for (auto& e : *a)
                    out.push_back ({ e.getProperty ("key", {}).toString(), (float) (double) e.getProperty ("value", 0.0), e.getProperty ("unit", {}).toString() });
            return out;
        }
    }

    juce::var toVar (const ModuleLog& l)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("module", juce::String (dsp::moduleKey (l.module)));
        o->setProperty ("instance", l.instance);
        o->setProperty ("globalStage", l.globalStage);
        o->setProperty ("active", l.active);
        o->setProperty ("problem", l.problem);
        o->setProperty ("measurements", listToVar (l.measurements));
        o->setProperty ("need", (double) l.need);
        o->setProperty ("intensity", (double) l.intensity);
        o->setProperty ("parameters", listToVar (l.parameters));
        o->setProperty ("result", listToVar (l.result));
        o->setProperty ("skipReason", l.skipReason);
        return juce::var (o);
    }

    ModuleLog fromVar (const juce::var& v)
    {
        ModuleLog l;
        if (const auto m = dsp::moduleFromKey (v.getProperty ("module", {}).toString()); m.has_value())
            l.module = *m;
        l.instance = (int) v.getProperty ("instance", 0);
        l.globalStage = (bool) v.getProperty ("globalStage", false);
        l.active = (bool) v.getProperty ("active", false);
        l.problem = v.getProperty ("problem", {}).toString();
        l.measurements = listFromVar (v.getProperty ("measurements", {}));
        l.need = (float) (double) v.getProperty ("need", 0.0);
        l.intensity = (float) (double) v.getProperty ("intensity", 0.0);
        l.parameters = listFromVar (v.getProperty ("parameters", {}));
        l.result = listFromVar (v.getProperty ("result", {}));
        l.skipReason = v.getProperty ("skipReason", {}).toString();
        return l;
    }

    void setMeasured (std::vector<Measured>& list, const juce::String& key, float value, const juce::String& unit)
    {
        for (auto& m : list)
            if (m.key == key)
            {
                m.value = value;
                m.unit = unit;
                return;
            }
        list.push_back ({ key, value, unit });
    }

    void fillParameters (std::vector<ModuleLog>& log, const std::map<juce::String, float>& values)
    {
        for (auto& l : log)
        {
            l.parameters.clear();
            if (l.globalStage)
            {
                if (auto it = values.find (pid::outputGain); it != values.end())
                    l.parameters.push_back ({ pid::outputGain, it->second, "dB" });
                continue;
            }
            for (auto& [id, value] : values)
            {
                const auto* spec = findParameterSpec (id);
                if (spec == nullptr)
                    continue;
                const auto module = moduleOfParameter (*spec);
                if (module.has_value() && *module == l.module && dsp::splitInstanceParamId (id).second == l.instance)
                    l.parameters.push_back ({ id, value, spec->unit });
            }
        }
    }
}
