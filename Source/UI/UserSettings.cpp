#include "UserSettings.h"

namespace velvox::ui::settings
{
    namespace
    {
        juce::File& overrideFile()
        {
            static juce::File f;
            return f;
        }

        // Opened per access: settings are read at editor start and written on a
        // few explicit actions only. Nothing outlives the message manager.
        std::unique_ptr<juce::PropertiesFile> open()
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "Velvox";
            o.folderName = "Velvox";
            o.filenameSuffix = ".settings";
            o.osxLibrarySubFolder = "Application Support";
            o.storageFormat = juce::PropertiesFile::storeAsXML;
            o.millisecondsBeforeSaving = -1;   // saved explicitly in set()

            return overrideFile() != juce::File() ? std::make_unique<juce::PropertiesFile> (overrideFile(), o)
                                                  : std::make_unique<juce::PropertiesFile> (o);
        }
    }

    juce::String getString (const juce::String& key, const juce::String& fallback)
    {
        return open()->getValue (key, fallback);
    }

    bool getBool (const juce::String& key, bool fallback)
    {
        return open()->getBoolValue (key, fallback);
    }

    void set (const juce::String& key, const juce::var& value)
    {
        auto p = open();
        p->setValue (key, value);
        p->saveIfNeeded();
    }

    void useFileForTesting (const juce::File& file)
    {
        overrideFile() = file;
    }

    juce::File getFile()
    {
        return open()->getFile();
    }
}
