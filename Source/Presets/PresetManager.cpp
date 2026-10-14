#include "PresetManager.h"
#include "FactoryPresets.h"
#include "../ProductInfo.h"

namespace velvox
{
    PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state, juce::File userPresetDirectory)
        : apvts (state), userDirectory (std::move (userPresetDirectory))
    {
        refresh();
    }

    juce::File PresetManager::getDefaultUserPresetDirectory()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile (product::companyName)
            .getChildFile (product::name)
            .getChildFile ("Presets");
    }

    void PresetManager::refresh()
    {
        entries.clear();

        for (const auto& p : getFactoryPresets())
            entries.add ({ p.name, true, {} });

        if (userDirectory.isDirectory())
        {
            auto files = userDirectory.findChildFiles (juce::File::findFiles, false, "*" + fileExtension);
            files.sort();
            for (const auto& f : files)
                entries.add ({ f.getFileNameWithoutExtension(), false, f });
        }
    }

    bool PresetManager::loadPreset (int index)
    {
        if (! juce::isPositiveAndBelow (index, entries.size()))
            return false;

        const auto& entry = entries.getReference (index);

        if (entry.isFactory)
        {
            const auto& factory = getFactoryPresets();
            if (! juce::isPositiveAndBelow (index, (int) factory.size()))
                return false;
            factory[(size_t) index].applyTo (apvts, true);
        }
        else
        {
            const auto xml = juce::XmlDocument::parse (entry.file);
            if (xml == nullptr)
                return false;

            const auto snapshot = ParameterSnapshot::fromValueTree (juce::ValueTree::fromXml (*xml));
            if (snapshot.values.empty())
                return false;

            snapshot.applyTo (apvts, true);
        }

        setCurrentPresetName (entry.name);
        return true;
    }

    bool PresetManager::loadNext()
    {
        if (entries.isEmpty())
            return false;
        const int idx = getCurrentIndex();
        return loadPreset (idx < 0 ? 0 : (idx + 1) % entries.size());
    }

    bool PresetManager::loadPrevious()
    {
        if (entries.isEmpty())
            return false;
        const int idx = getCurrentIndex();
        return loadPreset (idx <= 0 ? entries.size() - 1 : idx - 1);
    }

    bool PresetManager::saveUserPreset (const juce::String& rawName)
    {
        const auto name = juce::File::createLegalFileName (rawName.trim());
        if (name.isEmpty())
            return false;

        if (! userDirectory.isDirectory() && ! userDirectory.createDirectory())
            return false;

        auto snapshot = ParameterSnapshot::capture (apvts, name);
        snapshot.category = "User";

        const auto file = userDirectory.getChildFile (name + fileExtension);
        const auto xml = snapshot.toValueTree().createXml();
        if (xml == nullptr || ! xml->writeTo (file))
            return false;

        refresh();
        setCurrentPresetName (name);
        return true;
    }

    bool PresetManager::deleteUserPreset (int index)
    {
        if (! juce::isPositiveAndBelow (index, entries.size()))
            return false;

        const auto entry = entries[index];
        if (entry.isFactory || ! entry.file.deleteFile())
            return false;

        refresh();
        if (getCurrentPresetName() == entry.name)
            setCurrentPresetName ({});
        return true;
    }

    juce::String PresetManager::getCurrentPresetName() const
    {
        return apvts.state.getProperty (presetNameProperty).toString();
    }

    int PresetManager::getCurrentIndex() const
    {
        const auto name = getCurrentPresetName();
        for (int i = 0; i < entries.size(); ++i)
            if (entries.getReference (i).name == name)
                return i;
        return -1;
    }

    void PresetManager::setCurrentPresetName (const juce::String& name)
    {
        apvts.state.setProperty (presetNameProperty, name, nullptr);
    }
}
