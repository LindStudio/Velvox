#include "EmberPresets.h"
#include "EmberParams.h"
#include "Text/I18n.h"

namespace ember
{
    namespace
    {
        // One factory preset's sound. Every preset names all of these (so a later change of a default never moves a
        // preset); the rest (mix, output, mode, stereo, link) stays at its default unless `extra` sets it.
        // amount in % (50 = as set), detection 0 Smart / 1 Relative / 2 Absolute / 3 Allround, width in octaves.
        ParamValues sound (float freq, float thr, float range, float amount, float attack, float release, float lookahead,
                           float width, float detection = 0.0f, ParamValues extra = {})
        {
            ParamValues v { { pid::freq, freq }, { pid::threshold, thr }, { pid::range, range }, { pid::amount, amount },
                            { pid::attack, attack }, { pid::release, release }, { pid::lookahead, lookahead },
                            { pid::width, width }, { pid::detection, detection } };
            for (const auto& [id, x] : extra)
                v[id] = x;
            return v;
        }
    }

    const std::vector<FactoryPreset>& factoryPresets()
    {
        // 2026-11-23 (r2/ember-presets): 30 presets in 5 categories, each measured on fitting takes (15 real dry
        // vocals + compressed / bright "club" and "trap" versions of them), see docs/products/ember-r2/PRESETS.md.
        // Thresholds as the detector reads since 2026-11-23 (the same at every frequency, detectorTiltDbPerOct).
        // No frequency within 100 Hz of the voice range borders (5.75 / 7 kHz), and no threshold on a voice value
        // (-35 / -34 / -33), so a voice button never moves a preset's threshold.
        using C = PresetCategory;
        static const std::vector<FactoryPreset> presets {
            { C::none, "Default",
              "Der Startzustand: für die meisten Stimmen ein natürlicher Anfang.",
              "The starting point: a natural start for most voices.",
              sound (voiceMidHz, voiceMidThrDb, 5.0f, 50.0f, 0.5f, 60.0f, defaultLookaheadMs, defaultWidthOct) },
            { C::vocalBasics, "Gentle Vocal",
              "Leichte, unauffällige Absenkung für Stimmen mit nur wenig scharfen S.",
              "Light, unobtrusive reduction for voices with only a few sharp S sounds.",
              sound (6000.0f, -31.0f, 3.5f, 50.0f, 0.5f, 70.0f, 5.0f, 2.5f) },
            { C::vocalBasics, "Natural De-Essing",
              "Etwas bestimmter als Default, mit schmalerem Band: das S wird ruhiger, die Luft bleibt.",
              "A little firmer than Default with a narrower band: the S calms down, the air stays.",
              sound (6000.0f, -35.5f, 5.5f, 50.0f, 0.5f, 50.0f, 5.0f, 2.5f) },
            { C::vocalBasics, "Transparent",
              "Schmales Band nur auf dem S-Kern: kaum Klangfärbung, Luft und Glanz bleiben fast ganz.",
              "Narrow band on the core of the S only: hardly any colouring, air and sheen stay almost untouched.",
              sound (6000.0f, -34.5f, 4.5f, 50.0f, 0.3f, 60.0f, 6.0f, 1.8f) },
            { C::vocalBasics, "Subtle Reduction",
              "Die leichteste Einstellung: nimmt nur die Spitzen der S etwas zurück.",
              "The lightest setting: only takes the tips of the S sounds back a little.",
              sound (6000.0f, -30.0f, 2.5f, 50.0f, 0.5f, 70.0f, 5.0f, 3.0f) },
            { C::vocalBasics, "Strong Sibilance",
              "Für deutlich zu scharfe S: kräftige Absenkung, schnellere Erholung.",
              "For clearly too sharp S sounds: strong reduction, faster recovery.",
              sound (6000.0f, -38.0f, 7.5f, 50.0f, 0.5f, 45.0f, 5.0f, 3.0f) },
            { C::maleVocals, "Male Rap",
              "Tiefer S-Bereich und schnelle Erholung für Rap mit tiefer bis mittlerer Männerstimme.",
              "Low S range and fast recovery for rap with a low to mid male voice.",
              sound (5300.0f, -36.5f, 6.0f, 50.0f, 0.3f, 35.0f, 5.0f, 3.0f) },
            { C::maleVocals, "Deep Male Vocal",
              "S-Bereich ab 4,8 kHz für tiefe Männerstimmen, deren S tief und dunkel liegen.",
              "S range from 4.8 kHz for deep male voices whose S sounds sit low and dark.",
              sound (4800.0f, -36.0f, 5.0f, 50.0f, 0.5f, 60.0f, 5.0f, 3.0f) },
            { C::maleVocals, "Bright Male Vocal",
              "Höherer, schmalerer S-Bereich für helle Männerstimmen und Tenöre.",
              "Higher, narrower S range for bright male voices and tenors.",
              sound (6600.0f, -34.5f, 5.0f, 50.0f, 0.5f, 55.0f, 5.0f, 2.5f) },
            { C::maleVocals, "Aggressive Rap",
              "Kräftig und schnell für harten, dichten Rap mit vielen scharfen S.",
              "Strong and fast for hard, dense rap with many sharp S sounds.",
              sound (5400.0f, -39.0f, 8.0f, 60.0f, 0.3f, 30.0f, 5.0f, 3.0f) },
            { C::maleVocals, "Modern Pop Male",
              "Für glänzend produzierte Pop-Männerstimmen: bestimmt, Luft bleibt.",
              "For shiny, produced male pop vocals: firm, the air stays.",
              sound (6300.0f, -35.5f, 5.5f, 50.0f, 0.5f, 45.0f, 5.0f, 2.5f) },
            { C::femaleVocals, "Female Pop",
              "Ausgewogen für Pop-Frauenstimmen: S ruhiger, Glanz bleibt.",
              "Balanced for female pop vocals: calmer S, the sheen stays.",
              sound (6300.0f, -34.5f, 5.0f, 50.0f, 0.5f, 45.0f, 5.0f, 2.5f) },
            { C::femaleVocals, "Bright Female Vocal",
              "Hoher, schmaler S-Bereich für sehr helle Frauenstimmen mit spitzen S.",
              "High, narrow S range for very bright female voices with piercing S sounds.",
              sound (6400.0f, -36.0f, 6.0f, 50.0f, 0.5f, 45.0f, 5.0f, 2.0f) },
            { C::femaleVocals, "Airy Vocal",
              "Sehr schmales Band nur auf dem S: gehauchte, luftige Stimmen behalten ihre Luft.",
              "Very narrow band on the S only: breathy, airy voices keep their air.",
              sound (6200.0f, -34.5f, 5.0f, 50.0f, 0.5f, 50.0f, 5.0f, 1.5f) },
            { C::femaleVocals, "Soft Female Vocal",
              "Sanft und langsam für leise, weiche Frauenstimmen und Balladen.",
              "Gentle and slow for quiet, soft female voices and ballads.",
              sound (6200.0f, -31.0f, 3.5f, 50.0f, 0.7f, 80.0f, 5.0f, 2.5f) },
            { C::femaleVocals, "Powerful Female Vocal",
              "Kräftig für laute, gepresste Frauenstimmen mit harten S.",
              "Strong for loud, belting female voices with hard S sounds.",
              sound (6200.0f, -37.5f, 7.0f, 50.0f, 0.4f, 40.0f, 5.0f, 2.5f) },
            { C::genre, "Hypertechno",
              "Für stark komprimierte, hell gemischte Vocals bei hohem Tempo: kräftig, schnelle Erholung.",
              "For heavily compressed, brightly mixed vocals at high tempo: strong, fast recovery.",
              sound (6400.0f, -37.0f, 7.0f, 50.0f, 0.3f, 40.0f, 5.0f, 3.0f) },
            { C::genre, "EDM Lead Vocal",
              "Für komprimierte, helle EDM-Lead-Vocals: bestimmt und schnell.",
              "For compressed, bright EDM lead vocals: firm and fast.",
              sound (6300.0f, -36.0f, 6.0f, 50.0f, 0.4f, 45.0f, 5.0f, 3.0f) },
            { C::genre, "Dance Pop",
              "Für helle, komprimierte Dance-Pop-Vocals: deutlich, aber natürlich.",
              "For bright, compressed dance pop vocals: clear but natural.",
              sound (6200.0f, -35.5f, 5.5f, 50.0f, 0.4f, 35.0f, 5.0f, 2.5f) },
            { C::genre, "Hard Dance",
              "Die stärkste Genre-Einstellung: für extrem komprimierte, sehr helle Vocals, breites Band, sehr schnell.",
              "The strongest genre setting: for extremely compressed, very bright vocals, wide band, very fast.",
              sound (6200.0f, -38.5f, 8.0f, 55.0f, 0.2f, 30.0f, 5.0f, 3.0f) },
            { C::genre, "Melodic Techno",
              "Weich und langsam für flächige, hallige Vocals: kein Pumpen.",
              "Smooth and slow for wide, reverb-heavy vocals: no pumping.",
              sound (6100.0f, -34.5f, 5.0f, 50.0f, 0.6f, 80.0f, 5.0f, 2.5f) },
            { C::genre, "Trap Vocals",
              "Tiefer S-Bereich, kräftig und schnell für komprimierte Trap-Vocals mit viel Höhen.",
              "Low S range, strong and fast for compressed trap vocals with a lot of top end.",
              sound (5400.0f, -37.0f, 6.5f, 50.0f, 0.3f, 30.0f, 5.0f, 3.0f) },
            { C::genre, "Modern Hip-Hop",
              "Tiefer S-Bereich, bestimmt für komprimierte Hip-Hop-Vocals.",
              "Low S range, firm for compressed hip-hop vocals.",
              sound (5500.0f, -35.5f, 5.5f, 50.0f, 0.4f, 45.0f, 5.0f, 3.0f) },
            { C::genre, "Acoustic Vocals",
              "Behutsam und langsam für natürliche, wenig bearbeitete Aufnahmen.",
              "Careful and slow for natural, lightly processed recordings.",
              sound (6300.0f, -31.5f, 3.5f, 45.0f, 0.6f, 90.0f, 5.0f, 2.5f) },
            { C::specialPurpose, "Harsh S Sounds",
              "Schmal und kräftig auf dem S-Kern: zähmt stechende S, ohne die Luft zu nehmen.",
              "Narrow and strong on the core of the S: tames stinging S sounds without taking the air.",
              sound (6000.0f, -38.0f, 8.0f, 50.0f, 0.3f, 45.0f, 5.0f, 2.0f) },
            { C::specialPurpose, "Extreme Sibilance",
              "Maximale Absenkung für extrem scharfe Aufnahmen. Mit Delta prüfen, ob es lispelt.",
              "Maximum reduction for extremely sharp recordings. Check with Delta that it does not lisp.",
              sound (6000.0f, -42.0f, 10.0f, 65.0f, 0.3f, 45.0f, 5.0f, 2.5f) },
            { C::specialPurpose, "Sharp T Sounds",
              "Sehr schneller Attack: fängt auch kurze t-, k- und z-Spitzen am Anfang.",
              "Very fast attack: also catches short t, k and ts peaks right at their start.",
              sound (6400.0f, -38.0f, 6.0f, 50.0f, 0.1f, 50.0f, 5.0f, 2.5f) },
            { C::specialPurpose, "Vocal Recording Rescue",
              "Für problematische Aufnahmen (helles Mikrofon, zu nah): großer Range, früher Einsatz.",
              "For problem recordings (bright microphone, too close): wide range, early start.",
              sound (6000.0f, -38.5f, 10.0f, 50.0f, 0.2f, 55.0f, 8.0f, 3.0f) },
            { C::specialPurpose, "Mix / Bus",
              "Allround-Erkennung für einen ganzen Mix, eine Gruppe oder Instrumente statt einer Solostimme.",
              "Allround detection for a whole mix, a group or instruments instead of a single vocal.",
              sound (6000.0f, -41.0f, 4.5f, 50.0f, 0.5f, 45.0f, 5.0f, 3.0f, 3.0f) }
        };
        return presets;
    }

    juce::String presetName (const FactoryPreset& p)
    {
        return p.name;
    }

    juce::String presetInfo (const FactoryPreset& p)
    {
        return vocalypse::i18n::tr (p.infoDe, p.infoEn);
    }

    juce::String categoryName (PresetCategory c)
    {
        using vocalypse::i18n::tr;
        switch (c)
        {
            case PresetCategory::vocalBasics:    return tr ("Gesang-Grundlagen", "Vocal Basics");
            case PresetCategory::maleVocals:     return tr ("Männerstimmen", "Male Vocals");
            case PresetCategory::femaleVocals:   return tr ("Frauenstimmen", "Female Vocals");
            case PresetCategory::genre:          return tr ("Genres", "Genre");
            case PresetCategory::specialPurpose: return tr ("Spezialfälle", "Special Purpose");
            case PresetCategory::none:           break;
        }
        return {};
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
