#include "ParameterSnapshot.h"
#include "../Parameters/ParameterLayout.h"
#include "../DSP/Routing.h"
#include "../StateKeys.h"

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

        bool isExcludedFromSnapshots (const juce::String& id)
        {
            return id == pid::bypass;
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

        if (routing.isNotEmpty())
            apvts.state.setProperty (statekeys::routing, dsp::Routing::fromString (routing).toString(), nullptr);
        else if (resetMissingToDefault)
            apvts.state.setProperty (statekeys::routing, dsp::Routing::defaults().toString(), nullptr);
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
