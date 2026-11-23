#include "EmberPresets.h"
#include "EmberParams.h"
#include "Text/I18n.h"

namespace ember
{
    const std::vector<FactoryPreset>& factoryPresets()
    {
        // Values in real units; detection 0 = Smart, mode 0 = Split / 1 = Wideband. Thresholds as the detector reads
        // since 2026-11-23 (the same at every frequency, detectorTiltDbPerOct)
        static const std::vector<FactoryPreset> presets {
            { "Standard", "Default", {} },
            { "Sanft", "Gentle", { { pid::threshold, -28.0f }, { pid::range, 3.0f }, { pid::release, 40.0f } } },
            { "Pop-Gesang", "Pop vocal", { { pid::freq, 6800.0f }, { pid::threshold, -32.0f }, { pid::range, 5.0f }, { pid::release, 35.0f } } },
            { "Rap", "Rap", { { pid::freq, 5600.0f }, { pid::threshold, -34.5f }, { pid::range, 6.0f }, { pid::release, 30.0f } } },
            { "Tiefe Stimme", "Deep voice", { { pid::freq, voiceLowHz }, { pid::threshold, voiceLowThrDb }, { pid::range, 5.0f } } },
            { "Hohe Stimme", "High voice", { { pid::freq, voiceHighHz }, { pid::threshold, voiceHighThrDb }, { pid::range, 5.0f } } },
            { "Backing Vocals", "Backing vocals", { { pid::freq, 6200.0f }, { pid::threshold, -37.0f }, { pid::range, 8.0f }, { pid::amount, 60.0f } } },
            { "Sprache / Podcast", "Speech / podcast", { { pid::freq, 5500.0f }, { pid::threshold, -31.5f }, { pid::range, 5.0f }, { pid::mode, 1.0f }, { pid::release, 50.0f } } },
            { "Kräftig", "Strong", { { pid::threshold, -38.0f }, { pid::range, 7.0f }, { pid::release, 45.0f } } },
            // 2026-11-23: Allround for a mix or a bus (measured in a mix: S -2.9 dB, vowels untouched)
            { "Mix / Bus", "Mix / bus", { { pid::detection, 3.0f }, { pid::threshold, -40.0f }, { pid::range, 4.0f }, { pid::release, 40.0f } } },
        };
        return presets;
    }

    juce::String presetName (const FactoryPreset& p)
    {
        return vocalypse::i18n::tr (p.nameDe, p.nameEn);
    }

    bool isSetting (const juce::String& id)
    {
        return id != pid::bypass && id != pid::listen;
    }

    ParamValues capture (const juce::AudioProcessorValueTreeState& state)
    {
        ParamValues v;
        for (auto* p : state.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (isSetting (rp->getParameterID()))
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
                if (! isSetting (id))
                    continue;
                const auto it = values.find (id);
                all[id] = it != values.end() ? it->second : rp->convertFrom0to1 (rp->getDefaultValue());
            }
        return all;
    }

    void apply (juce::AudioProcessorValueTreeState& state, const ParamValues& values)
    {
        for (const auto& [id, real] : withDefaults (state, values))
            if (auto* p = state.getParameter (id); p != nullptr && std::isfinite (real))
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
        {
            const auto v = (float) e->getDoubleAttribute ("value");
            if (std::isfinite (v))   // "nan" / "inf" in a shared preset: that control keeps its default
                out[e->getStringAttribute ("id")] = v;
        }
        return true;
    }

    juce::Result renameUserPresetFile (const juce::File& file, const juce::String& newName, juce::File& renamed)
    {
        using vocalypse::i18n::tr;
        const auto name = juce::File::createLegalFileName (newName.trim()).trim();
        if (name.isEmpty())
            return juce::Result::fail (tr ("Bitte gib einen Namen ein.", "Please enter a name."));
        if (! file.existsAsFile())
            return juce::Result::fail (tr ("Das Preset gibt es nicht mehr.", "The preset no longer exists."));
        renamed = file.getSiblingFile (name + presetExtension);
        if (renamed.getFullPathName() == file.getFullPathName())
            return juce::Result::ok();
        // the same name in other letter case is a rename of the same file (Windows); any other existing file stays
        if (renamed.exists() && renamed != file)
            return juce::Result::fail (vocalypse::i18n::format (tr ("Ein Preset „{0}“ gibt es schon.", "A preset “{0}” already exists."), { name }));
        if (! file.moveFileTo (renamed))
            return juce::Result::fail (tr ("Umbenennen ging nicht. Ist die Datei schreibgeschützt oder gerade geöffnet?",
                                           "Renaming failed. Is the file read-only or open in another program?"));
        return juce::Result::ok();
    }

    juce::Result deleteUserPresetFile (const juce::File& file)
    {
        using vocalypse::i18n::tr;
        if (! file.existsAsFile())
            return juce::Result::ok();   // already gone
        if (file.moveToTrash() || file.deleteFile())
            return juce::Result::ok();
        return juce::Result::fail (tr ("Löschen ging nicht. Ist die Datei schreibgeschützt oder gerade geöffnet?",
                                       "Deleting failed. Is the file read-only or open in another program?"));
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
    void UserSettings::setEditorWidth (int w) { props->setValue ("editorWidth", w); }   // saved with the next change or on close
    bool UserSettings::wasAutoUsed() { return props->getBoolValue ("autoUsed", false); }
    void UserSettings::setAutoUsed() { props->setValue ("autoUsed", true); props->saveIfNeeded(); }
}
