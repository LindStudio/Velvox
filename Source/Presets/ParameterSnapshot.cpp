#include "ParameterSnapshot.h"
#include "../Parameters/ParameterLayout.h"
#include "../DSP/Routing.h"
#include "../StateKeys.h"
#include "../Parameters/RoutingMigration.h"
#include "../Parameters/EqBands.h"

namespace velvox
{
    namespace
    {
        const juce::Identifier paramType { "Param" };
        const juce::Identifier idAttr { "id" };
        const juce::Identifier valueAttr { "value" };
        const juce::Identifier nameAttr { "name" };
        const juce::Identifier categoryAttr { "category" };
        const juce::Identifier descriptionAttr { "description" };
        const juce::Identifier versionAttr { "version" };

        // Not part of a "sound": global bypass, the recording's input level and
        // the user's monitoring preference (level matching). Presets and A/B
        // never touch these.
        bool isExcludedFromSnapshots (const juce::String& id)
        {
            return id == pid::bypass || id == pid::inputGain
                || id == pid::levelMatchMode || id == pid::levelMatchWindow || id == pid::levelMatchRange;
        }

        bool containsFixedEqBands (const std::map<juce::String, float>& values)
        {
            for (auto& [id, v] : values)
                for (auto* base : { "hpf", "low", "mid", "high", "lpf" })
                    if (id.startsWith (base))
                        return true;
            return false;
        }

        // 0.1-0.5 snapshots (presets, A/B slots) have no EQ bands: their fixed bands become bands 1-5
        // with the 0.5 response, exactly like a loaded project. Missing values are the defaults (a full
        // snapshot) or the current values (a partial one).
        std::map<juce::String, float> withEqBands (const std::map<juce::String, float>& values, juce::AudioProcessorValueTreeState& apvts,
                                                   bool resetMissingToDefault)
        {
            if (eqbands::containsBandValues (values) || ! (resetMissingToDefault || containsFixedEqBands (values)))
                return values;

            auto effective = values;
            std::map<juce::String, float> fallback;
            if (! resetMissingToDefault)
                for (auto* p : apvts.processor.getParameters())
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                        fallback[rp->getParameterID()] = rp->convertFrom0to1 (rp->getValue());
            eqbands::migrateState (eqbands::accessFor (effective, resetMissingToDefault ? nullptr : &fallback), true);
            return effective;
        }
    }

    ParameterSnapshot ParameterSnapshot::capture (juce::AudioProcessorValueTreeState& apvts, const juce::String& name)
    {
        ParameterSnapshot s;
        s.name = name;

        for (auto* p : apvts.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (! isExcludedFromSnapshots (rp->getParameterID()))
                    s.values[rp->getParameterID()] = rp->convertFrom0to1 (rp->getValue());

        s.routing = apvts.state.getProperty (statekeys::routing).toString();
        return s;
    }

    void ParameterSnapshot::applyTo (juce::AudioProcessorValueTreeState& apvts, bool resetMissingToDefault) const
    {
        eqbands::ScopedBridgeSuspend noEqBridge;   // the snapshot sets the bands itself
        const auto values = withEqBands (this->values, apvts, resetMissingToDefault);

        for (auto* p : apvts.processor.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (rp == nullptr || isExcludedFromSnapshots (rp->getParameterID()))
                continue;

            float normalised;
            const auto it = values.find (rp->getParameterID());

            if (it != values.end() && std::isfinite (it->second))
                normalised = rp->convertTo0to1 (rp->getNormalisableRange().snapToLegalValue (it->second));
            else if (resetMissingToDefault)
                normalised = rp->getDefaultValue();
            else
                continue;

            if (std::abs (normalised - rp->getValue()) < 1.0e-7f)
                continue;

            rp->beginChangeGesture();
            rp->setValueNotifyingHost (normalised);
            rp->endChangeGesture();
        }

        // 0.1 - 0.3 snapshots list every module (or none): migrated against the values just set
        if (routing.isNotEmpty() || resetMissingToDefault)
            apvts.state.setProperty (statekeys::routing,
                                     dsp::Routing::fromString (migrateRoutingString (routing, apvts)).toString(), nullptr);
    }

    bool ParameterSnapshot::differsFrom (juce::AudioProcessorValueTreeState& apvts) const
    {
        const auto values = withEqBands (this->values, apvts, true);

        for (auto* p : apvts.processor.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
            if (rp == nullptr || isExcludedFromSnapshots (rp->getParameterID()))
                continue;

            const auto it = values.find (rp->getParameterID());
            const float expected = it != values.end() && std::isfinite (it->second)
                                     ? rp->convertTo0to1 (rp->getNormalisableRange().snapToLegalValue (it->second))
                                     : rp->getDefaultValue();
            if (std::abs (expected - rp->getValue()) > 1.0e-4f)
                return true;
        }

        const auto expectedRouting = dsp::Routing::fromString (migrateRoutingString (routing, apvts));
        const auto current = dsp::Routing::fromString (apvts.state.getProperty (statekeys::routing).toString());
        return expectedRouting != current;
    }

    juce::StringArray ParameterSnapshot::validate() const
    {
        juce::StringArray problems;

        for (const auto& [id, v] : values)
        {
            const auto* spec = findParameterSpec (id);
            if (spec == nullptr)
            {
                problems.add (name + ": unknown parameter '" + id + "'");
                continue;
            }

            if (! std::isfinite (v) || v < spec->minValue - 1.0e-4f || v > spec->maxValue + 1.0e-4f)
                problems.add (name + ": value " + juce::String (v) + " out of range for '" + id + "'");
        }

        return problems;
    }

    juce::ValueTree ParameterSnapshot::toValueTree() const
    {
        juce::ValueTree tree (treeType);
        tree.setProperty (nameAttr, name, nullptr);
        tree.setProperty (categoryAttr, category, nullptr);
        tree.setProperty (descriptionAttr, description, nullptr);
        tree.setProperty (versionAttr, formatVersion, nullptr);
        if (routing.isNotEmpty())
            tree.setProperty (statekeys::routing, routing, nullptr);

        for (const auto& [id, v] : values)
        {
            juce::ValueTree p (paramType);
            p.setProperty (idAttr, id, nullptr);
            p.setProperty (valueAttr, v, nullptr);
            tree.appendChild (p, nullptr);
        }

        return tree;
    }

    ParameterSnapshot ParameterSnapshot::fromValueTree (const juce::ValueTree& tree)
    {
        ParameterSnapshot s;
        if (! tree.hasType (treeType))
            return s;

        s.name = tree.getProperty (nameAttr).toString();
        s.category = tree.getProperty (categoryAttr).toString();
        s.description = tree.getProperty (descriptionAttr).toString();
        s.routing = tree.getProperty (statekeys::routing).toString(); // absent in format 1

        for (const auto& child : tree)
        {
            if (! child.hasType (paramType))
                continue;

            const auto id = child.getProperty (idAttr).toString();
            const auto value = (float) (double) child.getProperty (valueAttr, 0.0);
            if (id.isNotEmpty() && std::isfinite (value))
                s.values[id] = value;
        }

        return s;
    }
}
