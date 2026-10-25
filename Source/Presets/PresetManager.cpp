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
        const auto company = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (product::companyName);
        const auto dir = company.getChildFile (product::name).getChildFile ("Presets");

        // Renamed product: take the user presets saved under the old name along (copy, never move)
        const auto legacy = company.getChildFile (product::legacyName).getChildFile ("Presets");
        if (! dir.isDirectory() && legacy.isDirectory())
            legacy.copyDirectoryTo (dir);
        return dir;
    }

    void PresetManager::refresh()
    {
        entries.clear();

        for (const auto& p : getFactoryPresets())
            entries.add ({ p.name, p.category, true, {}, p.description });

        if (userDirectory.isDirectory())
        {
            auto files = userDirectory.findChildFiles (juce::File::findFiles, false, "*" + fileExtension);
            files.sort();
            for (const auto& f : files)
            {
                juce::String description;
                if (const auto xml = juce::XmlDocument::parse (f))
                    description = xml->getStringAttribute ("description");
                entries.add ({ f.getFileNameWithoutExtension(), "User", false, f, description });
            }
        }
    }

    bool PresetManager::loadPreset (int index)
    {
        if (! juce::isPositiveAndBelow (index, entries.size()))
            return false;
        if (undo == nullptr)
            return applyEntry (index);

        bool ok = false;
        undo->perform ("Preset: " + entries.getReference (index).name, [&] { ok = applyEntry (index); });
        return ok;
    }

    bool PresetManager::applyEntry (int index)
    {
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

        referenceName.clear();
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
        referenceName.clear();
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

    bool PresetManager::renameUserPreset (int index, const juce::String& rawName)
    {
        if (! juce::isPositiveAndBelow (index, entries.size()))
            return false;
        const auto entry = entries[index];
        const auto name = juce::File::createLegalFileName (rawName.trim());
        if (entry.isFactory || name.isEmpty())
            return false;
        const auto target = userDirectory.getChildFile (name + fileExtension);
        if (target.exists() || ! entry.file.moveFileTo (target))
            return false;

        // The name inside the file follows
        if (const auto xml = juce::XmlDocument::parse (target))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            tree.setProperty ("name", name, nullptr);
            if (const auto out = tree.createXml())
                out->writeTo (target);
        }
        const bool wasCurrent = getCurrentPresetName() == entry.name;
        refresh();
        if (wasCurrent)
        {
            referenceName.clear();
            setCurrentPresetName (name);
        }
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

namespace velvox
{
    bool PresetManager::isCurrentPresetModified()
    {
        const auto name = getCurrentPresetName();
        if (name.isEmpty())
            return false;

        if (name != referenceName)
        {
            referenceName = name;
            reference.reset();
            const int idx = getCurrentIndex();
            if (idx >= 0)
            {
                const auto& entry = entries.getReference (idx);
                if (entry.isFactory)
                {
                    const auto& factory = getFactoryPresets();
                    if (juce::isPositiveAndBelow (idx, (int) factory.size()))
                        reference = factory[(size_t) idx];
                }
                else if (const auto xml = juce::XmlDocument::parse (entry.file))
                {
                    reference = ParameterSnapshot::fromValueTree (juce::ValueTree::fromXml (*xml));
                }
            }
        }

        return reference.has_value() && reference->differsFrom (apvts);
    }
}
