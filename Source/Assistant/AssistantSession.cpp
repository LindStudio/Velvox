#include "AssistantSession.h"
#include "AssistantPlanner.h"
#include "../Parameters/ParameterLayout.h"

namespace velvox::assistant
{
    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }

        // Tolerance for comparing a parameter with an expected value (normalised
        // storage may round slightly)
        bool same (const juce::String& id, float a, float b)
        {
            const auto* spec = findParameterSpec (id);
            const float range = spec != nullptr ? spec->maxValue - spec->minValue : 1.0f;
            return std::abs (a - b) <= std::max (1.0e-4f, range * 1.0e-4f);
        }

        // Module switch that has to be on for a parameter to be heard
        const char* switchFor (const juce::String& id)
        {
            const auto* spec = findParameterSpec (id);
            if (spec == nullptr)
                return nullptr;
            const juce::String m (spec->module);
            if (m == "EQ")         return pid::eqOn;
            if (m == "Compressor") return pid::compOn;
            if (m == "De-Esser")   return pid::deessOn;
            if (m == "Saturation") return pid::satOn;
            if (m == "Delay")      return pid::delayOn;
            if (m == "Reverb")     return pid::reverbOn;
            if (m == "Doubler")    return pid::doublerOn;
            if (m == "Smash")      return pid::smashOn;
            if (m == "Smooth")     return pid::smoothOn;
            if (m == "Ride")       return pid::rideOn;
            if (m == "Ducking")    return id.startsWith ("delay") ? pid::delayOn : (id.startsWith ("reverb") ? pid::reverbOn : nullptr);
            return nullptr;
        }
    }

    float AssistantSession::read (const juce::String& id) const
    {
        if (auto* p = apvts.getParameter (id))
            return p->convertFrom0to1 (p->getValue());
        return std::numeric_limits<float>::quiet_NaN();
    }

    bool AssistantSession::write (const juce::String& id, float value)
    {
        auto* p = apvts.getParameter (id);
        if (p == nullptr)
            return false;
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
        return same (id, read (id), value);
    }

    std::map<juce::String, float> AssistantSession::readValues (juce::AudioProcessorValueTreeState& state)
    {
        std::map<juce::String, float> values;
        for (const auto& spec : getParameterSpecs())
            if (auto* p = state.getParameter (spec.id))
                values[spec.id] = p->convertFrom0to1 (p->getValue());
        return values;
    }

    juce::StringArray AssistantSession::validate (const Plan& plan) const
    {
        juce::StringArray problems;
        for (const auto& c : plan.changes)
        {
            if (! c.selected)
                continue;
            const auto* spec = findParameterSpec (c.paramId);
            if (spec == nullptr || apvts.getParameter (c.paramId) == nullptr)
            {
                problems.add (de ("Unbekannter Parameter: ") + c.paramId);
                continue;
            }
            if (! std::isfinite (c.proposed))
                problems.add (juce::String (spec->name) + de (": ungültiger Wert"));
            else if (c.proposed < spec->minValue - 1.0e-4f || c.proposed > spec->maxValue + 1.0e-4f)
                problems.add (juce::String (spec->name) + de (": Wert außerhalb des gültigen Bereichs"));
            else if (! same (c.paramId, legaliseValue (c.paramId, c.proposed), c.proposed))
                problems.add (juce::String (spec->name) + de (": Wert passt nicht zum Raster des Parameters"));
        }
        return problems;
    }

    bool AssistantSession::isStale (const Plan& plan) const
    {
        for (const auto& c : plan.changes)
            if (c.selected && ! same (c.paramId, read (c.paramId), c.current))
                return true;
        return false;
    }

    AssistantSession::Result AssistantSession::apply (const Plan& plan)
    {
        Result r;
        if (plan.numSelected() == 0)
        {
            r.status = Result::Status::nothingSelected;
            r.messages.add (de ("Keine Änderung ausgewählt."));
            return r;
        }

        const auto problems = validate (plan);
        if (! problems.isEmpty())
        {
            r.status = Result::Status::invalid;
            r.messages = problems;
            return r;
        }

        if (isStale (plan))
        {
            r.status = Result::Status::stale;
            r.messages.add (de ("Einige Werte wurden seit der Analyse verändert. Die Vorschläge wurden nicht angewendet - bitte neu analysieren."));
            return r;
        }

        // Apply; on any failure roll back what was already written
        std::vector<Step> steps;
        for (const auto& c : plan.changes)
        {
            if (! c.selected)
                continue;
            const float before = read (c.paramId);
            if (! write (c.paramId, c.proposed))
            {
                for (auto it = steps.rbegin(); it != steps.rend(); ++it)
                    write (it->id, it->before);
                write (c.paramId, before);
                r.status = Result::Status::failed;
                r.messages.add (de ("Änderung an ") + c.paramName + de (" konnte nicht gesetzt werden - alles wurde zurückgenommen."));
                return r;
            }
            steps.push_back ({ c.paramId, before, read (c.paramId) });
        }

        // Selected changes in a module that stays off are not audible yet
        for (const auto& c : plan.changes)
        {
            if (! c.selected)
                continue;
            if (const auto* sw = switchFor (c.paramId); sw != nullptr && c.paramId != juce::String (sw) && read (sw) < 0.5f)
                r.messages.addIfNotAlreadyThere (c.module + de (" ist ausgeschaltet - die Änderung wird erst hörbar, wenn du das Modul einschaltest."));
        }

        history.push_back (std::move (steps));
        if (history.size() > maxHistory)
            history.erase (history.begin());

        r.status = Result::Status::applied;
        r.count = (int) history.back().size();
        return r;
    }

    AssistantSession::Result AssistantSession::undo()
    {
        Result r;
        if (history.empty())
        {
            r.status = Result::Status::nothingSelected;
            r.messages.add (de ("Es gibt keine Assistant-Aktion zum Rückgängigmachen."));
            return r;
        }

        auto steps = std::move (history.back());
        history.pop_back();

        int kept = 0;
        for (auto it = steps.rbegin(); it != steps.rend(); ++it)
        {
            const float now = read (it->id);
            if (! same (it->id, now, it->after))
            {
                ++kept;
                const auto* spec = findParameterSpec (it->id);
                r.messages.add ((spec != nullptr ? juce::String (spec->name) : it->id) + de (" wurde inzwischen geändert (jetzt ")
                                + displayValue (it->id, now) + de (") und bleibt so."));
                continue;
            }
            if (write (it->id, it->before))
                ++r.count;
        }

        r.status = Result::Status::applied;
        if (kept > 0 && r.count == 0)
            r.messages.insert (0, de ("Nichts zurückgesetzt - alle betroffenen Werte wurden seitdem verändert."));
        return r;
    }
}
