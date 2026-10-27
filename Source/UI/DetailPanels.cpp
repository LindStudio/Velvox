#include "DetailPanels.h"
#include "CleanupPanels.h"
#include "EqPanel.h"
#include "ReverbPanel.h"
#include "SmoothPanel.h"
#include "SatPanel.h"
#include "ChainView.h"
#include "../PluginProcessor.h"
#include "../Presets/ModulePresets.h"
#include "Icons.h"
#include "HelpContent.h"
#include "UserSettings.h"
#include "VocalCheckView.h"
#include "TimePanels.h"
#include "VoicesPanels.h"
#include "DynPanels.h"
#include "SoloButton.h"
#include "../DSP/Solo.h"
#include "../Text/I18n.h"
#include "../Text/ParamText.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        // Frame of the module panel (design units)
        constexpr int headerHeight = 44;
        constexpr int padding = 14;
        constexpr int mainRowHeight = 96;
        constexpr int drawerWidth = 430;
        constexpr int captionH = 14;
        constexpr int mainDial = 50, moreDial = 32;

        // User module presets: one XML list in the user settings (all projects)
        const juce::String userModulePresetsKey { "userModulePresets" };

        juce::ValueTree loadUserModulePresets()
        {
            if (auto xml = juce::parseXML (settings::getString (userModulePresetsKey)))
                return juce::ValueTree::fromXml (*xml);
            return juce::ValueTree ("UserModulePresets");
        }

        std::vector<ModulePreset> userModulePresets (ModuleId id)
        {
            std::vector<ModulePreset> out;
            const auto tree = loadUserModulePresets();
            for (const auto& p : tree)
            {
                if (p.getProperty ("module").toString() != dsp::moduleKey (id))
                    continue;
                ModulePreset preset;
                preset.name = p.getProperty ("name").toString();
                for (const auto& v : p)
                    preset.values.push_back ({ v.getProperty ("id").toString(), (float) v.getProperty ("value") });
                out.push_back (std::move (preset));
            }
            return out;
        }
    }

    //==========================================================================
    // The Mehr drawer: switches and segment groups on top, then the small knobs in a grid
    class DetailPanel::MoreDrawer final : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (colours::raisedHi);
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (colours::border.brighter (0.2f));
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
            g.setColour (colours::textFaint);
            g.setFont (semiBold (9.5f, 0.2f));
            g.drawText (i18n::tr ("MEHR", "MORE"), getLocalBounds().reduced (14, 0).removeFromTop (24), juce::Justification::centredLeft, false);
            for (const auto& [c, text] : captionsToDraw)
                if (c->isVisible())
                {
                    g.setFont (semiBold (9.0f, 0.12f));
                    g.drawText (i18n::upper (text), c->getBounds().translated (0, -captionH).withHeight (captionH), juce::Justification::centredLeft, false);
                }
        }

        void layout (const std::vector<juce::Component*>& items, const std::map<juce::Component*, juce::String>& captions, juce::Component* extra)
        {
            captionsToDraw.clear();
            auto r = getLocalBounds().reduced (14, 0).withTrimmedTop (26).withTrimmedBottom (10);

            std::vector<juce::Component*> wide, knobs;
            for (auto* c : items)
                (dynamic_cast<Knob*> (c) != nullptr ? knobs : wide).push_back (c);

            // Segments, choices and switches: one per line; two switches share a line
            std::vector<juce::Component*> switches;
            for (auto* c : wide)
            {
                if (dynamic_cast<Toggle*> (c) != nullptr || dynamic_cast<juce::Button*> (c) != nullptr)
                {
                    switches.push_back (c);
                    continue;
                }
                const bool hasCaption = captions.count (c) > 0;
                if (hasCaption)
                    r.removeFromTop (captionH);
                c->setBounds (r.removeFromTop (dynamic_cast<Choice*> (c) != nullptr ? 28 : 26));
                if (hasCaption)
                    captionsToDraw.push_back ({ c, captions.at (c) });
                r.removeFromTop (8);
            }
            for (size_t i = 0; i < switches.size(); i += 2)
            {
                auto line = r.removeFromTop (24);
                switches[i]->setBounds (line.removeFromLeft (line.getWidth() / 2 - 4));
                if (i + 1 < switches.size())
                    switches[i + 1]->setBounds (line.withTrimmedLeft (8));
                r.removeFromTop (8);
            }

            if (extra != nullptr && extra->isVisible())
            {
                extra->setBounds (r.removeFromBottom (26));
                r.removeFromBottom (6);
            }

            // Small knobs: as many columns as fit, rows as tall as the space allows (60 - 74)
            if (! knobs.empty())
            {
                const int columns = juce::jlimit (1, 6, r.getWidth() / 70);
                const int rows = ((int) knobs.size() + columns - 1) / columns;
                const int cellH = juce::jlimit (52, 74, r.getHeight() / std::max (1, rows));
                const int cellW = r.getWidth() / columns;
                for (int i = 0; i < (int) knobs.size(); ++i)
                    knobs[(size_t) i]->setBounds (r.getX() + (i % columns) * cellW, r.getY() + (i / columns) * cellH, cellW, cellH);
            }
        }

    private:
        std::vector<std::pair<juce::Component*, juce::String>> captionsToDraw;
    };

    //==========================================================================
    DetailPanel::DetailPanel (VelvoxAudioProcessor& p, juce::String t, juce::String s, const char* onId)
        : proc (p), apvts (p.getState()), title (std::move (t)), subtitle (std::move (s)), instance (constructionInstance)
    {
        if (onId != nullptr)
            onParamId = pidFor (onId);
        if (onParamId.isNotEmpty())
        {
            power = std::make_unique<PowerButton> (apvts, onParamId.toRawUTF8());
            power->setTooltip (i18n::tr ("Ein / Aus", "On / Off"));
            addAndMakeVisible (*power);
        }

        drawer = std::make_unique<MoreDrawer>();
        addChildComponent (*drawer);

        helpButton.setTooltip (i18n::tr ("Hilfe zu diesem Modul", "Help for this module"));
        helpButton.onClick = [this] { if (onHelp) onHelp (moduleId == -2 ? -1 : moduleId); };
        addAndMakeVisible (helpButton);

        moreButton.setButtonText (i18n::tr ("Mehr", "More"));
        moreButton.setClickingTogglesState (false);
        moreButton.setTooltip (i18n::tr ("Alle weiteren Regler dieses Moduls", "All other controls of this module"));
        moreButton.onClick = [this] { setMoreOpen (! moreOpen); };
        addAndMakeVisible (moreButton);

        presetsButton.setTooltip (i18n::tr ("Startpunkte nur für dieses Modul, eigene Modul-Presets speichern",
                                           "Starting points for this module only, save your own module presets"));
        presetsButton.onClick = [this] { showModulePresets(); };
    }

    DetailPanel::~DetailPanel() = default;

    void DetailPanel::setVisual (std::unique_ptr<ModuleView> view)
    {
        visual = std::move (view);
        if (visual != nullptr)
        {
            addAndMakeVisible (*visual);
            visual->toBack();
        }
        resized();
    }

    juce::String DetailPanel::pidFor (const char* baseId) const
    {
        if (instance > 0)
        {
            const auto id = dsp::instanceParamId (baseId, instance);
            if (apvts.getParameter (id) != nullptr)
                return id;
        }
        return baseId;
    }

    juce::String DetailPanel::moduleName() const
    {
        return moduleId >= 0 ? juce::String (dsp::moduleDisplayName ((ModuleId) moduleId)) : juce::String ("Output");
    }

    void DetailPanel::setModule (int selection)
    {
        const int id = selection < 0 ? selection : (int) dsp::selectionModule (selection);
        moduleId = id;
        if (id >= 0)
        {
            // Every control of the panel glows in the module colour
            const auto hue = moduleColour ((ModuleId) id);
            if (power != nullptr)
                power->setHue (hue);
            for (auto* k : knobs)
                k->slider.setColour (juce::Slider::rotarySliderFillColourId, hue);
            for (auto* t : toggles)
                t->setColour (juce::ToggleButton::tickColourId, hue);
            for (auto* s : segmentGroups)
                s->setAccent (hue);

            const auto svg = moduleIconSvg ((ModuleId) id).replace ("#FFFFFF", "#" + hue.toDisplayString (false));
            const auto utf8 = svg.toUTF8();
            icon = juce::Drawable::createFromImageData (utf8.getAddress(), utf8.sizeInBytes() - 1);

            // Several instances of this module in the chain: the title says which one
            if (instance > 0 || proc.getRouting().countOf ((ModuleId) id) > 1)
                title << " " << (instance + 1);
            addAndMakeVisible (presetsButton);

            // Solo (0.6.1): only this instance heard; the same state as the S on its chain tile
            if (dsp::canSolo ((ModuleId) id) && solo == nullptr)
            {
                solo = std::make_unique<SoloButton>();
                solo->onClick = [this]
                {
                    proc.toggleSolo (dsp::selectionCode ((ModuleId) moduleId, instance));
                    solo->setToggleState (proc.isSoloed (dsp::selectionCode ((ModuleId) moduleId, instance)), juce::dontSendNotification);
                };
                solo->setToggleState (proc.isSoloed (dsp::selectionCode ((ModuleId) id, instance)), juce::dontSendNotification);
                addAndMakeVisible (*solo);
            }

            // Old-law instances (0.5 projects): "Alte Mix-Kurve – umstellen" in Mehr
            if (dsp::Routing::canBeSend ((ModuleId) id) || id == (int) ModuleId::doubler || id == (int) ModuleId::smash)
            {
                legacyButton = std::make_unique<juce::TextButton> (i18n::tr ("Alte Mix-Kurve – umstellen", "Old mix curve – switch"));
                legacyButton->setTooltip (i18n::tr ("Dieses Modul nutzt noch die Mix-Kurve von 0.5 (Mix legt den Effekt zum vollen Original). "
                                                    "Umstellen: echtes Dry/Wet - bis 50 % klingt es gleich, darüber wird das Original leiser.",
                                                    "This module still uses the mix curve of 0.5 (Mix adds the effect to the full original). "
                                                    "Switch: true dry/wet - up to 50 % it sounds the same, above that the original gets quieter."));
                legacyButton->onClick = [this] { proc.switchToTrueMixLaw ((ModuleId) moduleId, instance); refresh(); };
                drawer->addChildComponent (*legacyButton);
            }
        }
        setTooltip (subtitle);
        updateSendState();
        resized();
        repaint();
    }

    //==========================================================================
    // Controls

    void DetailPanel::adopt (std::unique_ptr<juce::Component> c)
    {
        drawer->addAndMakeVisible (*c);   // Mehr until it is made a main control
        owned.push_back (std::move (c));
    }

    Knob& DetailPanel::knob (const char* paramId, const juce::String& label)
    {
        const auto mapped = pidFor (paramId);
        auto k = std::make_unique<Knob> (apvts, mapped.toRawUTF8(), label, moduleId >= 0 ? moduleColour ((ModuleId) moduleId) : colours::teal);
        k->setDialSize (moreDial);
        auto& ref = *k;
        knobs.push_back (k.get());
        adopt (std::move (k));
        return ref;
    }

    Toggle& DetailPanel::toggle (const char* paramId, const juce::String& label)
    {
        auto t = std::make_unique<Toggle> (apvts, pidFor (paramId).toRawUTF8(), label);
        auto& ref = *t;
        toggles.push_back (t.get());
        adopt (std::move (t));
        return ref;
    }

    Segments& DetailPanel::segments (const char* paramId, juce::StringArray labels, bool withOverflow)
    {
        const auto id = pidFor (paramId);
        if (labels.isEmpty())
        {
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
                labels = c->choices;
            else
                labels = { i18n::tr ("Aus", "Off"), i18n::tr ("An", "On") };
        }
        auto s = std::make_unique<Segments> (apvts, id.toRawUTF8(), labels, colours::teal, withOverflow);
        auto& ref = *s;
        segmentGroups.push_back (s.get());
        if (auto* p = apvts.getParameter (id))
            captions[&ref] = i18n::paramName (id, p->getName (32));
        adopt (std::move (s));
        return ref;
    }

    Choice& DetailPanel::choice (const char* paramId, const juce::String& caption)
    {
        auto c = std::make_unique<Choice> (apvts, pidFor (paramId).toRawUTF8(), caption);
        auto& ref = *c;
        adopt (std::move (c));
        return ref;
    }

    void DetailPanel::setCaption (juce::Component& control, const juce::String& caption)
    {
        captions[&control] = caption;
    }

    void DetailPanel::setMainControls (std::initializer_list<juce::Component*> controls)
    {
        jassert (controls.size() >= 3 && controls.size() <= 5);   // spec 2.4: 3 - 5 main controls
        mainControls.assign (controls.begin(), controls.end());
        for (auto* c : mainControls)
        {
            addAndMakeVisible (c);   // moves it out of the drawer
            if (auto* k = dynamic_cast<Knob*> (c))
                k->setProminence (Knob::Prominence::primary);
        }
        moreButton.setVisible (! getMoreControls().empty());
        resized();
    }

    void DetailPanel::placeInVisual (std::initializer_list<juce::Component*> controls)
    {
        for (auto* c : controls)
        {
            visualControls.push_back (c);
            addAndMakeVisible (c);
        }
        moreButton.setVisible (! getMoreControls().empty());
    }

    juce::Component& DetailPanel::getMoreDrawer() noexcept { return *drawer; }

    std::vector<juce::Component*> DetailPanel::getMoreControls() const
    {
        std::vector<juce::Component*> out;
        for (auto* c : drawer->getChildren())
            if (c != legacyButton.get())
                out.push_back (c);
        return out;
    }

    void DetailPanel::setMoreOpen (bool open)
    {
        if (open == moreOpen)
            return;
        moreOpen = open;
        moreButton.setToggleState (open, juce::dontSendNotification);
        resized();
        repaint();
    }

    //==========================================================================
    // Send / mix law state

    bool DetailPanel::isSend() const
    {
        return moduleId >= 0 && dsp::Routing::canBeSend ((ModuleId) moduleId) && proc.getRouting().contains ((ModuleId) moduleId, instance)
               && ! proc.getRouting().isInsert ((ModuleId) moduleId, instance);
    }

    bool DetailPanel::showsLegacyMixLawAction() const
    {
        if (legacyButton == nullptr || ! proc.usesLegacyMixLaw ((ModuleId) moduleId, instance))
            return false;
        // Delay / reverb: the law only matters while it is an insert (a send is a send)
        return ! isSend();
    }

    juce::String DetailPanel::getHeaderBadge() const
    {
        const bool on = onParamId.isEmpty() || apvts.getRawParameterValue (onParamId)->load() > 0.5f;
        if (isSend())
            return "SEND";
        return on ? juce::String() : i18n::tr ("AUS", "OFF");
    }

    void DetailPanel::updateSendState()
    {
        const bool send = isSend();
        if (mixKnob != nullptr)
        {
            if (mixLabel.isEmpty())
                mixLabel = mixKnob->getLabel();
            mixKnob->setLabel (send ? juce::String ("SEND") : mixLabel);
            // Tooltip text by routing and law (the parameter's own text describes the 0.5 send)
            const bool legacy = proc.usesLegacyMixLaw ((ModuleId) moduleId, instance);
            mixKnob->setDescription (send ? i18n::tr ("Send-Pegel: 100 % = voller Send. Das Original bleibt immer voll.",
                                                    "Send level: 100 % = full send. The original always stays at full level.")
                                          : legacy ? i18n::tr ("Alte Mix-Kurve: der Effekt wird zum vollen Original addiert.",
                                                               "Old mix curve: the effect is added to the full original.")
                                                   : i18n::tr ("Dry/Wet: 0 % nur Original, 100 % nur Effekt.",
                                                               "Dry/wet: 0 % original only, 100 % effect only."));
        }
        if (legacyButton != nullptr)
            legacyButton->setVisible (showsLegacyMixLawAction());
    }

    void DetailPanel::addPanelItems (juce::PopupMenu& menu)
    {
        juce::Component::SafePointer<DetailPanel> safe (this);
        menu.addItem (i18n::tr ("Modul zurücksetzen", "Reset module"), moduleId >= 0, false, [safe] { if (safe != nullptr) safe->resetModule(); });
        menu.addItem (i18n::tr ("Modul-Preset speichern …", "Save module preset …"), moduleId >= 0, false, [safe]
        {
            if (safe == nullptr)
                return;
            const auto fallback = i18n::format (i18n::tr ("Eigenes {0}", "Custom {0}"),
                                                { juce::String ((int) userModulePresets ((ModuleId) safe->moduleId).size() + 1) });
            if (safe->askForName)
                safe->askForName (i18n::tr ("Modul-Preset speichern", "Save module preset"), [safe] (const juce::String& name) { if (safe != nullptr) safe->saveModulePreset (name); });
            else
                safe->saveModulePreset (fallback);
        });
        if (showsLegacyMixLawAction())
            menu.addItem (i18n::tr ("Alte Mix-Kurve – umstellen", "Old mix curve – switch"), [safe] { if (safe != nullptr) { safe->proc.switchToTrueMixLaw ((ModuleId) safe->moduleId, safe->instance); safe->refresh(); } });
    }

    void DetailPanel::resetModule()
    {
        if (moduleId >= 0)
            ChainView::resetInstance (proc, (ModuleId) moduleId, instance);
    }

    void DetailPanel::saveModulePreset (const juce::String& rawName)
    {
        const auto name = rawName.trim();
        if (name.isEmpty() || moduleId < 0)
            return;
        auto tree = loadUserModulePresets();
        for (int i = tree.getNumChildren(); --i >= 0;)
            if (tree.getChild (i).getProperty ("module").toString() == dsp::moduleKey ((ModuleId) moduleId)
                && tree.getChild (i).getProperty ("name").toString() == name)
                tree.removeChild (i, nullptr);

        juce::ValueTree preset ("Preset");
        preset.setProperty ("module", dsp::moduleKey ((ModuleId) moduleId), nullptr);
        preset.setProperty ("name", name, nullptr);
        const auto onBase = juce::String (ChainView::onParamFor ((ModuleId) moduleId));
        for (const auto& spec : getParameterSpecs())
        {
            if (moduleOfParameter (spec) != (ModuleId) moduleId || dsp::splitInstanceParamId (spec.id).second != 0 || onBase == spec.id)
                continue;
            if (auto* p = apvts.getParameter (pidFor (spec.id)))
            {
                juce::ValueTree v ("Value");
                v.setProperty ("id", spec.id, nullptr);   // instance-1 id: applies to any instance
                v.setProperty ("value", p->convertFrom0to1 (p->getValue()), nullptr);
                preset.appendChild (v, nullptr);
            }
        }
        tree.appendChild (preset, nullptr);
        if (auto xml = tree.createXml())
            settings::set (userModulePresetsKey, xml->toString (juce::XmlElement::TextFormat().singleLine()));
    }

    void DetailPanel::showModulePresets()
    {
        if (moduleId < 0)
            return;
        const auto& factory = getModulePresets ((ModuleId) moduleId);
        const auto user = userModulePresets ((ModuleId) moduleId);
        juce::Component::SafePointer<DetailPanel> safe (this);
        const auto apply = [safe] (ModulePreset preset)
        {
            return [safe, preset]
            {
                if (safe == nullptr)
                    return;
                safe->proc.getUndo().perform (i18n::format (i18n::tr ("Modul-Preset: {0}", "Module preset: {0}"), { preset.name }),
                                              [&] { applyModulePreset (preset, safe->apvts, safe->instance); });
            };
        };

        juce::PopupMenu menu;
        menu.addSectionHeader (moduleName() + " Presets");
        for (const auto& p : factory)
            menu.addItem (p.name, apply (p));
        if (! user.empty())
        {
            menu.addSectionHeader (i18n::tr ("Eigene", "User"));
            for (const auto& p : user)
                menu.addItem (p.name, apply (p));
        }
        menu.addSeparator();
        addPanelItems (menu);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton).withMinimumWidth (200));
    }

    void DetailPanel::mouseDown (const juce::MouseEvent& e)
    {
        if (! e.mods.isPopupMenu())
            return;
        juce::PopupMenu menu;
        menu.addSectionHeader (title);
        addPanelItems (menu);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

    //==========================================================================
    // Layout and drawing

    void DetailPanel::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();
        const bool on = onParamId.isEmpty() || apvts.getRawParameterValue (onParamId)->load() > 0.5f;
        const auto hue = moduleId >= 0 && moduleId < dsp::numModules ? moduleColour ((ModuleId) moduleId) : colours::teal;

        // Panel: #07080D, border #161925, radius 12
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (colours::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

        // Header: icon, NAME n, badge
        auto header = headerArea;
        header.removeFromLeft (power != nullptr ? 34 : 0);
        header.removeFromLeft (solo != nullptr ? 30 : 0);
        if (icon != nullptr)
        {
            const auto iconArea = header.removeFromLeft (26).toFloat().withSizeKeepingCentre (24.0f, 24.0f);
            if (on)
                drawGlow (g, iconArea.getCentre(), 20.0f, 0.22f, hue);
            icon->drawWithin (g, iconArea, juce::RectanglePlacement::centred, on ? 1.0f : 0.4f);
            header.removeFromLeft (10);
        }
        const auto shownTitle = i18n::upper (title);
        const auto titleFont = semiBold (13.0f, 2.0f / 13.0f);
        g.setColour (on ? colours::textStrong : colours::textMuted);
        g.setFont (titleFont);
        const int titleW = juce::GlyphArrangement::getStringWidthInt (titleFont, shownTitle) + 14;
        g.drawText (shownTitle, header.removeFromLeft (titleW), juce::Justification::centredLeft, false);
        if (const auto badge = getHeaderBadge(); badge.isNotEmpty())
        {
            const auto f = semiBold (9.0f, 0.15f);
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (f, badge) + 16.0f;
            const auto b = juce::Rectangle<float> (w, 18.0f).withPosition ((float) header.getX(), (float) header.getCentreY() - 9.0f);
            const bool send = badge == "SEND";
            g.setColour ((send ? hue : colours::textFaint).withAlpha (0.16f));
            g.fillRoundedRectangle (b, 9.0f);
            g.setColour (send ? colours::textStrong : colours::textMuted);
            g.setFont (f);
            g.drawText (badge, b, juce::Justification::centred, false);
        }

        // Visual box: #040509, border #13161E, radius 8
        if (! visualArea.isEmpty())
        {
            const auto v = visualArea.toFloat();
            g.setColour (colours::panelInset);
            g.fillRoundedRectangle (v, 8.0f);
            g.setColour (colours::borderInset);
            g.drawRoundedRectangle (v.reduced (0.5f), 8.0f, 1.0f);
        }

        // Captions under main segment groups / switches
        for (auto* c : mainControls)
            if (dynamic_cast<Knob*> (c) == nullptr && captions.count (c) > 0)
            {
                g.setColour (colours::knobName);
                g.setFont (semiBold (9.0f, 0.06f));
                g.drawText (i18n::upper (captions[c]), c->getBounds().translated (0, c->getHeight() + 4).withHeight (14),
                            juce::Justification::centred, false);
            }
    }

    void DetailPanel::layoutMainRow (juce::Rectangle<int> row)
    {
        if (mainControls.empty())
            return;
        // Preferred widths: knobs 96, segment groups by their number of segments, switches 124. When the row is
        // too narrow (Mehr open), everything shrinks alike - except switches, which keep room for their label
        std::vector<int> widths, keep;
        int total = 0;
        for (auto* c : mainControls)
        {
            int w = 96;
            if (auto* s = dynamic_cast<Segments*> (c))
                w = juce::jlimit (150, 440, 58 * (s->getNumSegments() + (s->getOverflowSegment() >= 0 ? 1 : 0)));
            else if (auto* t = dynamic_cast<Toggle*> (c))
                w = std::min (124, 38 + juce::GlyphArrangement::getStringWidthInt (medium (12.0f), t->getButtonText()));
            else if (dynamic_cast<Choice*> (c) != nullptr)
                w = 170;
            else if (dynamic_cast<juce::Button*> (c) != nullptr)
                w = 104;
            widths.push_back (w);
            keep.push_back (dynamic_cast<Toggle*> (c) != nullptr ? 1 : 0);
            total += w;
        }
        const int gapW = 18;
        total += gapW * ((int) mainControls.size() - 1);
        int kept = 0;
        for (size_t i = 0; i < widths.size(); ++i)
            kept += keep[i] != 0 ? widths[i] : 0;
        const float shrink = total > row.getWidth() ? (float) std::max (0, row.getWidth() - kept) / (float) std::max (1, total - kept) : 1.0f;
        float x = (float) row.getX() + (float) std::max (0, (row.getWidth() - total) / 2);
        for (size_t i = 0; i < mainControls.size(); ++i)
        {
            const float scale = keep[i] != 0 ? 1.0f : shrink;
            const int w = std::min ((int) ((float) widths[i] * scale), row.getRight() - (int) x);
            auto cell = juce::Rectangle<int> ((int) x, row.getY(), w, row.getHeight());
            auto* c = mainControls[i];
            if (dynamic_cast<Knob*> (c) != nullptr)
                c->setBounds (cell);
            else
                c->setBounds (cell.withSizeKeepingCentre (w, dynamic_cast<Choice*> (c) != nullptr ? 30 : 28).translated (0, -8));
            x += (float) widths[i] * scale + (float) gapW * shrink;
        }
    }

    void DetailPanel::resized()
    {
        auto r = getLocalBounds();
        headerArea = r.removeFromTop (headerHeight).reduced (padding, 0);
        auto header = headerArea;
        if (power != nullptr)
            power->setBounds (header.removeFromLeft (26).withSizeKeepingCentre (26, 26));
        if (solo != nullptr)
        {
            header.removeFromLeft (power != nullptr ? 6 : 0);
            solo->setBounds (header.removeFromLeft (24).withSizeKeepingCentre (24, 24));
        }
        helpButton.setBounds (header.removeFromRight (28).withSizeKeepingCentre (28, 26));
        header.removeFromRight (8);
        moreButton.setBounds (header.removeFromRight (72).withSizeKeepingCentre (72, 26));
        header.removeFromRight (8);
        presetsButton.setBounds (header.removeFromRight (90).withSizeKeepingCentre (90, 26));

        auto content = r.reduced (padding, 0).withTrimmedBottom (padding);
        drawer->setVisible (moreOpen);
        if (moreOpen)
        {
            auto d = content.removeFromRight (std::min (drawerWidth, content.getWidth() / 2));
            content.removeFromRight (12);
            drawer->setBounds (d);
            std::vector<juce::Component*> items;
            for (auto* c : getMoreControls())
                items.push_back (c);
            drawer->layout (items, captions, legacyButton.get());
        }

        mainRow = mainControls.empty() ? juce::Rectangle<int>() : content.removeFromBottom (mainRowHeight);
        if (! mainRow.isEmpty())
            content.removeFromBottom (6);
        visualArea = content;
        if (visual != nullptr)
            visual->setBounds (visualArea.reduced (1));
        layoutMainRow (mainRow.withTrimmedTop (6));
        layoutVisual (visualArea);
    }

    void DetailPanel::placeMeter (juce::Component& meter, juce::Rectangle<int> area)
    {
        auto r = area.reduced (8);
        meter.setBounds (r.removeFromRight (34));
        if (visual != nullptr)
            visual->setBounds (area.withTrimmedRight (50).reduced (1));
    }

    void DetailPanel::refresh()
    {
        for (auto* k : knobs)
            k->refreshEffective();
        if (solo != nullptr)
        {
            const bool soloed = proc.isSoloed (dsp::selectionCode ((ModuleId) moduleId, instance));
            if (soloed != solo->getToggleState())
                solo->setToggleState (soloed, juce::dontSendNotification);
        }
        if (visual != nullptr && live != nullptr)
            visual->update (*live);

        const int on = onParamId.isEmpty() || apvts.getRawParameterValue (onParamId)->load() > 0.5f ? 1 : 0;
        if (on != lastOn)
        {
            lastOn = on;
            for (auto* c : getChildren())
                if (c != power.get() && c != solo.get() && c != visual.get() && c != &presetsButton && c != &helpButton && c != &moreButton)
                    c->setAlpha (on ? 1.0f : 0.55f);
            repaint();
        }

        const int send = isSend() ? 1 : 0;
        const int legacy = moduleId >= 0 && proc.usesLegacyMixLaw ((ModuleId) moduleId, instance) ? 1 : 0;
        if (send != lastSend || legacy != lastLegacy)
        {
            lastSend = send;
            lastLegacy = legacy;
            updateSendState();
            if (moreOpen)
                resized();
            repaint();
        }
    }

    //==========================================================================
    namespace
    {
        // OUTPUT: stereo image + loudness; Input Gain, Vocal Level, Width, Level Match;
        // Mehr: mono below, max correction, measure window, clip guard, mono listening
        class OutputPanel final : public DetailPanel
        {
        public:
            explicit OutputPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Output", help::shortDescription (-1), nullptr)
            {
                // Labels by position (Off, Bypass match, Auto gain)
                auto& mode = segments (pid::levelMatchMode, { i18n::tr ("Aus", "Off"), i18n::tr ("Wie Bypass", "Like bypass"), "Auto-Gain" });
                setCaption (mode, "Level Match");
                segments (pid::levelMatchWindow);
                toggle (pid::safetyClip, "Clip Guard");
                auto& inGain = knob (pid::inputGain, "Input Gain");
                auto& level = knob (pid::outputGain, "Vocal Level");
                auto& width = knob (pid::stereoWidth, "Width");
                knob (pid::monoBelow, i18n::tr ("Mono unter", "Mono Below"));
                knob (pid::levelMatchRange, i18n::tr ("Max. Korrektur", "Max Correction"));
                auto& mono = own (std::make_unique<juce::TextButton> ("Mono"));
                mono.setClickingTogglesState (true);
                mono.setTooltip (i18n::tr ("In Mono abhören (wird nicht mit dem Projekt gespeichert)", "Listen in mono (not saved with the project)"));
                mono.setToggleState (p.isMonoMonitor(), juce::dontSendNotification);
                mono.onClick = [this, &mono] { proc.setMonoMonitor (mono.getToggleState()); };
                monoButton = &mono;
                setMainControls ({ &inGain, &level, &width, &mode });

                correlation.setTooltip (i18n::tr ("Phasenkorrelation: +1 = monokompatibel, unter 0 = Anteile löschen sich in Mono aus",
                                                  "Phase correlation: +1 = mono-compatible, below 0 = parts cancel out in mono"));
                correlationCaption.setText (i18n::tr ("KORRELATION", "CORRELATION"), juce::dontSendNotification);
                correlationCaption.setFont (caps (8.5f));
                correlationCaption.setColour (juce::Label::textColourId, colours::textFaint);
                correlationCaption.setJustificationType (juce::Justification::centredLeft);
                correlationCaption.setInterceptsMouseClicks (false, false);
                for (auto* l : { &inValue, &procValue, &matchValue })
                {
                    l->setFont (monoFont (13.0f));
                    l->setColour (juce::Label::textColourId, colours::text);
                    l->setJustificationType (juce::Justification::centredRight);
                }
                inValue.setTooltip (i18n::tr ("Eingang (LUFS)", "Input (LUFS)"));
                procValue.setTooltip (i18n::tr ("Bearbeitet (LUFS)", "Processed (LUFS)"));
                matchValue.setTooltip (i18n::tr ("Level Match (dB)", "Level match (dB)"));
                placeInVisual ({ &correlation, &correlationCaption, &inValue, &procValue, &matchValue });
            }

            void refresh() override
            {
                DetailPanel::refresh();
                const auto& m = proc.getChain().meters;
                correlation.push (m.correlation.load());
                const auto fmt = [] (float lufs) { return lufs <= -99.0f ? juce::String ("- LUFS") : juce::String (lufs, 1) + " LUFS"; };
                const auto set = [] (juce::Label& l, const juce::String& t) { if (l.getText() != t) l.setText (t, juce::dontSendNotification); };
                set (inValue, "IN " + fmt (m.inputLufs.load()));
                set (procValue, "FX " + fmt (m.processedLufs.load()));
                const float match = m.levelMatchDb.load();
                set (matchValue, "MATCH " + juce::String (match > 0.05f ? "+" : "") + juce::String (match, 1) + " dB");
                if (monoButton != nullptr && monoButton->getToggleState() != proc.isMonoMonitor())
                    monoButton->setToggleState (proc.isMonoMonitor(), juce::dontSendNotification);
            }

        private:
            void layoutVisual (juce::Rectangle<int> area) override
            {
                // Read-outs in their own column right of the picture
                auto r = area.reduced (10).removeFromRight (140);
                if (auto* v = getVisual())
                    v->setBounds (area.withTrimmedRight (156).reduced (1));
                r.removeFromTop (14);
                inValue.setBounds (r.removeFromTop (20));
                procValue.setBounds (r.removeFromTop (20));
                matchValue.setBounds (r.removeFromTop (20));
                correlation.setBounds (r.removeFromBottom (18));
                correlationCaption.setBounds (r.removeFromBottom (16));
            }

            CorrelationView correlation;
            juce::Label correlationCaption;
            juce::Label inValue, procValue, matchValue;
            juce::TextButton* monoButton = nullptr;
        };

        // VOCAL CHECK module in the chain: its live picture (spectrum, balance) and a compact summary
        // (score, number of findings, link to the VOCAL CHECK tab, where the tiles with Ignorieren /
        // Anwenden live); analysis mode, Start / Stop, Reset as the main row
        class CheckPanel final : public DetailPanel
        {
        public:
            explicit CheckPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Vocal Check", help::shortDescription ((int) ModuleId::check), pid::checkOn),
                  findings (p)
            {
                auto& mode = segments (pid::checkMode, { "Continuous", "Capture" });   // shown as on the VOCAL CHECK tab ("Laufend")
                setCaption (mode, i18n::tr ("Analyse", "Analysis"));
                auto& startStop = own (std::make_unique<juce::TextButton> ("Start"));
                auto& resetButton = own (std::make_unique<juce::TextButton> ("Reset"));
                start = &startStop;
                reset = &resetButton;
                setMainControls ({ &mode, &startStop, &resetButton });
                placeInVisual ({ &findings, &statusLabel });
                statusLabel.setFont (font (12.0f));
                statusLabel.setColour (juce::Label::textColourId, colours::textDim);
                statusLabel.setJustificationType (juce::Justification::centredLeft);

                startStop.setTooltip (i18n::tr ("Capture: analysiert alles zwischen Start und Stop", "Capture: analyzes everything between Start and Stop"));
                startStop.onClick = [this]
                {
                    auto& a = proc.getAnalyzer();
                    if (a.isCapturing()) a.stopCapture(); else a.startCapture();
                    proc.refreshAnalysis();
                };
                resetButton.setTooltip (i18n::tr ("Alles bisher Analysierte vergessen (auch Spitzen und Clips)", "Forget everything analyzed so far (also peaks and clips)"));
                resetButton.onClick = [this] { proc.getAnalyzer().clear(); proc.refreshAnalysis(); };
                findings.onOpenTab = [this] { if (onShowVocalCheck) onShowVocalCheck(); };
            }

            void refresh() override
            {
                DetailPanel::refresh();
                auto& a = proc.getAnalyzer();
                const bool on = apvts.getRawParameterValue (pid::checkOn)->load() > 0.5f;
                const bool capture = a.getMode() == dsp::VocalAnalyzer::Mode::capture;
                start->setEnabled (on && capture);
                start->setButtonText (a.isCapturing() ? "Stop" : "Start");
                start->setToggleState (a.isCapturing(), juce::dontSendNotification);
                reset->setEnabled (on);

                const auto& r = proc.getLatestAnalysis();
                const auto seconds = i18n::number (r.analysedSeconds, 1);
                const juce::String text = ! on ? i18n::tr ("Aus", "Off")
                                        : capture ? (a.isCapturing() ? i18n::format ("Capture {0} s", { seconds })
                                                                     : i18n::format (i18n::tr ("Gestoppt, {0} s", "Stopped, {0} s"), { seconds }))
                                                  : i18n::format (i18n::tr ("Laufend, letzte {0} s", "Continuous, last {0} s"),
                                                                  { juce::String (juce::roundToInt (std::min (30.0f, r.analysedSeconds))) });
                if (statusLabel.getText() != text)
                    statusLabel.setText (text, juce::dontSendNotification);
                if (++summaryTick % 6 == 0)
                    findings.refresh();
            }

        private:
            void layoutVisual (juce::Rectangle<int> area) override
            {
                auto r = area.reduced (10);
                auto right = r.removeFromRight (std::min (250, r.getWidth() / 3));
                findings.setBounds (right.removeFromTop (96));
                statusLabel.setBounds (right.removeFromTop (22).withTrimmedLeft (6));
                r.removeFromRight (12);
                if (auto* v = getVisual())
                    v->setBounds (r.reduced (1));
            }

            CheckSummary findings;
            int summaryTick = 0;
            juce::Label statusLabel;
            juce::TextButton* start = nullptr;
            juce::TextButton* reset = nullptr;
        };
    }

    std::unique_ptr<DetailPanel> createDetailPanel (VelvoxAudioProcessor& p, int id, const SpectrumSource* spectrum, const LiveData* live)
    {
        std::unique_ptr<DetailPanel> panel;
        DetailPanel::constructionInstance = id < 0 ? 0 : dsp::selectionInstance (id);
        if (id < 0)
        {
            panel = std::make_unique<OutputPanel> (p);
        }
        else
        {
            switch (dsp::selectionModule (id))
            {
                case ModuleId::eq:      panel = createEqPanel (p); break;
                case ModuleId::comp:    panel = createDynPanel (p, ModuleId::comp); break;    // DynPanels.cpp
                case ModuleId::deess:   panel = createDynPanel (p, ModuleId::deess); break;
                case ModuleId::sat:     panel = createSatPanel (p); break;   // SatPanel.cpp
                case ModuleId::doubler: panel = createVoicesModulePanel (p, ModuleId::doubler); break;
                case ModuleId::delay:   panel = createTimeModulePanel (p, ModuleId::delay); break;
                case ModuleId::reverb:  panel = createReverbPanel (p); break;
                case ModuleId::smash:   panel = createDynPanel (p, ModuleId::smash); break;
                case ModuleId::smooth:  panel = createSmoothPanel (p); break;
                case ModuleId::check:   panel = std::make_unique<CheckPanel> (p); break;
                case ModuleId::ride:    panel = createDynPanel (p, ModuleId::ride); break;
                case ModuleId::denoise:
                case ModuleId::breath:  panel = createCleanupPanel (p, dsp::selectionModule (id)); break;   // CleanupPanels.cpp
            }
            if (panel == nullptr)
                panel = std::make_unique<OutputPanel> (p);
        }
        DetailPanel::constructionInstance = 0;
        panel->setLiveData (live);
        panel->setVisual (createModuleView (p, id, spectrum));
        panel->setModule (id);
        return panel;
    }
}
