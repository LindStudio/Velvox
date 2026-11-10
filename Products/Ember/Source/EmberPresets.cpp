#include "EmberPresets.h"
#include "EmberParams.h"
#include "Text/I18n.h"

namespace ember
{
    const std::vector<FactoryPreset>& factoryPresets()
    {
        // Values in real units; detection 0 = Smart, mode 0 = Split / 1 = Wideband
        static const std::vector<FactoryPreset> presets {
            { "Standard", "Default", {} },
            { "Sanft", "Gentle", { { pid::threshold, -26.0f }, { pid::range, 3.0f }, { pid::release, 40.0f } } },
            { "Pop-Gesang", "Pop vocal", { { pid::freq, 6800.0f }, { pid::threshold, -31.0f }, { pid::range, 5.0f }, { pid::release, 35.0f } } },
            { "Rap", "Rap", { { pid::freq, 5600.0f }, { pid::threshold, -33.0f }, { pid::range, 6.0f }, { pid::release, 30.0f } } },
            { "Tiefe Stimme", "Deep voice", { { pid::freq, 5000.0f }, { pid::threshold, -30.0f }, { pid::range, 5.0f } } },
            { "Hohe Stimme", "High voice", { { pid::freq, 8000.0f }, { pid::threshold, -30.0f }, { pid::range, 5.0f } } },
            { "Backing Vocals", "Backing vocals", { { pid::freq, 6200.0f }, { pid::threshold, -36.0f }, { pid::range, 8.0f }, { pid::amount, 60.0f } } },
            { "Sprache / Podcast", "Speech / podcast", { { pid::freq, 5500.0f }, { pid::threshold, -30.0f }, { pid::range, 5.0f }, { pid::mode, 1.0f }, { pid::release, 50.0f } } },
            { "Kräftig", "Strong", { { pid::threshold, -40.0f }, { pid::range, 9.0f }, { pid::release, 45.0f } } },
        };
        return presets;
    }

    juce::String presetName (const FactoryPreset& p)
    {
        return vocalypse::i18n::tr (p.nameDe, p.nameEn);
    }

    ParamValues capture (const juce::AudioProcessorValueTreeState& state)
    {
        ParamValues v;
        for (auto* p : state.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (rp->getParameterID() != pid::bypass)
                    v[rp->getParameterID()] = rp->convertFrom0to1 (rp->getValue());
        return v;
    }

    ParamValues withDefaults (const juce::AudioProcessorValueTreeState& state, const ParamValues& values)
    {
        ParamValues all;
        for (auto* p : state.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto id = rp->getParameterID();
                if (id == pid::bypass)
                    continue;
                const auto it = values.find (id);
                all[id] = it != values.end() ? it->second : rp->convertFrom0to1 (rp->getDefaultValue());
            }
        return all;
    }

    void apply (juce::AudioProcessorValueTreeState& state, const ParamValues& values)
    {
        for (const auto& [id, real] : withDefaults (state, values))
            if (auto* p = state.getParameter (id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (real));
                p->endChangeGesture();
            }
    }

    bool sameValues (const ParamValues& a, const ParamValues& b)
    {
        for (const auto& [id, v] : a)
        {
            const auto it = b.find (id);
            if (it == b.end() || std::abs (it->second - v) > 0.011f * std::max (1.0f, std::abs (v)))
                return false;
        }
        return true;
    }

    juce::File userPresetFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("Lind Audio").getChildFile ("EMBER").getChildFile ("Presets");
    }

    juce::Array<juce::File> userPresets()
    {
        auto files = userPresetFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + presetExtension);
        files.sort();
        return files;
    }

    bool saveUserPreset (const juce::File& file, const ParamValues& values)
    {
        juce::XmlElement xml ("EmberPreset");
        xml.setAttribute ("version", 1);
        for (const auto& [id, v] : values)
        {
            auto* e = xml.createNewChildElement ("Param");
            e->setAttribute ("id", id);
            e->setAttribute ("value", (double) v);
        }
        file.getParentDirectory().createDirectory();
        return xml.writeTo (file);
    }

    bool loadUserPreset (const juce::File& file, ParamValues& out)
    {
        const auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName ("EmberPreset"))
            return false;
        out.clear();
        for (auto* e : xml->getChildWithTagNameIterator ("Param"))
            out[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");
        return true;
    }

    // ------------------------------------------------------------------ settings
    namespace
    {
        juce::File& testFile()
        {
            static juce::File f;
            return f;
        }
    }

    void UserSettings::useFileForTests (const juce::File& f) { testFile() = f; }

    UserSettings::UserSettings()
    {
        if (testFile() != juce::File())
        {
            props = std::make_unique<juce::PropertiesFile> (testFile(), juce::PropertiesFile::Options());
            return;
        }
        juce::PropertiesFile::Options o;
        o.applicationName = "EMBER";
        o.folderName = "Lind Audio";
        o.filenameSuffix = "settings";
        o.osxLibrarySubFolder = "Application Support";
        props = std::make_unique<juce::PropertiesFile> (o);
    }

    juce::String UserSettings::getLanguage() { return props->getValue ("language", "system"); }
    void UserSettings::setLanguage (const juce::String& l) { props->setValue ("language", l); props->saveIfNeeded(); }
    int UserSettings::getEditorWidth (int fallback) { return props->getIntValue ("editorWidth", fallback); }
    void UserSettings::setEditorWidth (int w) { props->setValue ("editorWidth", w); props->saveIfNeeded(); }
    bool UserSettings::wasAutoUsed() { return props->getBoolValue ("autoUsed", false); }
    void UserSettings::setAutoUsed() { props->setValue ("autoUsed", true); props->saveIfNeeded(); }
}
