#include "RoutingMigration.h"
#include "SettingsReader.h"

namespace velvox
{
    bool isLegacyRoutingString (const juce::String& text)
    {
        return ! text.trimStart().startsWith ("v2");
    }

    dsp::Routing pruneInaudible (const dsp::Routing& full, const dsp::ChainSettings& e)
    {
        using M = dsp::ModuleId;
        const auto audible = [&e] (M id)
        {
            switch (id)
            {
                case M::eq: case M::comp: case M::deess:
                case M::delay: case M::reverb:  return true;   // the familiar core and the send lane
                case M::sat:     return e.sat.enabled && e.sat.mix > 0.0f;
                case M::doubler: return e.doubler.enabled && e.doubler.mix > 0.0f;
                case M::pitch:   return e.pitch.enabled;
                case M::chop:    return e.chop.enabled;
                case M::pulse:   return e.pulse.enabled;
                case M::smash:   return e.smash.enabled;
                case M::choir:   return e.choir.enabled;
                case M::smooth:  return e.smooth.enabled;
                case M::check:   return e.checkOn;
                case M::ride:    return e.ride.enabled;
            }
            return true;
        };

        dsp::Routing r;
        for (const auto& slot : full)
            if (slot.instance > 0 || audible (slot.id))
                r.insertAt (r.count, slot.id, slot.instance, slot.insert);
        return r;
    }

    juce::String migrateRoutingString (const juce::String& text, juce::AudioProcessorValueTreeState& apvts)
    {
        if (! isLegacyRoutingString (text))
            return text;

        const auto full = text.isEmpty() ? dsp::Routing::legacyFull() : dsp::Routing::fromString (text);

        SettingsReader reader;
        reader.bind ([&apvts] (const char* id) { return apvts.getRawParameterValue (id); });
        const auto effective = dsp::applyMacros (reader.read (full, false), reader.readMacros());
        return pruneInaudible (full, effective).toString();
    }
}
