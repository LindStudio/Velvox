#include "DetailPanels.h"
#include "EqPanel.h"
#include "ReverbPanel.h"
#include "SmoothPanel.h"
#include "ChainView.h"
#include "../PluginProcessor.h"
#include "../Presets/ModulePresets.h"
#include "Icons.h"
#include "HelpContent.h"
#include "UserSettings.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }

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
            g.drawText ("MEHR", getLocalBounds().reduced (14, 0).removeFromTop (24), juce::Justification::centredLeft, false);
            for (const auto& [c, text] : captionsToDraw)
                if (c->isVisible())
                {
                    g.setFont (semiBold (9.0f, 0.12f));
                    g.drawText (text.toUpperCase(), c->getBounds().translated (0, -captionH).withHeight (captionH), juce::Justification::centredLeft, false);
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
            power->setTooltip ("Ein / Aus");
            addAndMakeVisible (*power);
        }

        drawer = std::make_unique<MoreDrawer>();
        addChildComponent (*drawer);

        helpButton.setTooltip ("Hilfe zu diesem Modul");
        helpButton.onClick = [this] { if (onHelp) onHelp (moduleId == -2 ? -1 : moduleId); };
        addAndMakeVisible (helpButton);

        moreButton.setClickingTogglesState (false);
        moreButton.setTooltip (de ("Alle weiteren Regler dieses Moduls"));
        moreButton.onClick = [this] { setMoreOpen (! moreOpen); };
        addAndMakeVisible (moreButton);

        presetsButton.setTooltip (de ("Startpunkte nur für dieses Modul, eigene Modul-Presets speichern"));
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

            // Old-law instances (0.5 projects): "Alte Mix-Kurve – umstellen" in Mehr
            if (dsp::Routing::canBeSend ((ModuleId) id) || id == (int) ModuleId::doubler || id == (int) ModuleId::smash)
            {
                legacyButton = std::make_unique<juce::TextButton> (de ("Alte Mix-Kurve – umstellen"));
                legacyButton->setTooltip (de ("Dieses Modul nutzt noch die Mix-Kurve von 0.5 (Mix legt den Effekt zum vollen Original). "
                                              "Umstellen: echtes Dry/Wet - bis 50 % klingt es gleich, darüber wird das Original leiser."));
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
        // Macro markers belong to the first instance only (macros never move the others)
        if (instance == 0 && proc.effectiveValueOf (paramId).has_value())
        {
            const juce::String id (paramId);
            auto* procPtr = &proc;
            k->effectiveValue = [procPtr, id] { return procPtr->effectiveValueOf (id).value_or (0.0f); };
        }
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
                labels = { "Off", "On" };
        }
        auto s = std::make_unique<Segments> (apvts, id.toRawUTF8(), labels, colours::teal, withOverflow);
        auto& ref = *s;
        segmentGroups.push_back (s.get());
        if (auto* p = apvts.getParameter (id))
            captions[&ref] = p->getName (32);
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
        return on ? juce::String() : juce::String ("AUS");
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
            mixKnob->setDescription (send ? de ("Send-Pegel: 100 % = voller Send. Das Original bleibt immer voll.")
                                          : legacy ? de ("Alte Mix-Kurve: der Effekt wird zum vollen Original addiert.")
                                                   : de ("Dry/Wet: 0 % nur Original, 100 % nur Effekt."));
        }
        if (legacyButton != nullptr)
            legacyButton->setVisible (showsLegacyMixLawAction());
    }

    void DetailPanel::addPanelItems (juce::PopupMenu& menu)
    {
        juce::Component::SafePointer<DetailPanel> safe (this);
        menu.addItem (de ("Modul zurücksetzen"), moduleId >= 0, false, [safe] { if (safe != nullptr) safe->resetModule(); });
        menu.addItem (de ("Modul-Preset speichern …"), moduleId >= 0, false, [safe]
        {
            if (safe == nullptr)
                return;
            const auto fallback = "Eigenes " + juce::String ((int) userModulePresets ((ModuleId) safe->moduleId).size() + 1);
            if (safe->askForName)
                safe->askForName (de ("Modul-Preset speichern"), [safe] (const juce::String& name) { if (safe != nullptr) safe->saveModulePreset (name); });
            else
                safe->saveModulePreset (fallback);
        });
        if (showsLegacyMixLawAction())
            menu.addItem (de ("Alte Mix-Kurve – umstellen"), [safe] { if (safe != nullptr) { safe->proc.switchToTrueMixLaw ((ModuleId) safe->moduleId, safe->instance); safe->refresh(); } });
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
                safe->proc.getUndo().perform ("Modul-Preset: " + preset.name,
                                              [&] { applyModulePreset (preset, safe->apvts, safe->instance); });
            };
        };

        juce::PopupMenu menu;
        menu.addSectionHeader (moduleName() + " Presets");
        for (const auto& p : factory)
            menu.addItem (p.name, apply (p));
        if (! user.empty())
        {
            menu.addSectionHeader ("Eigene");
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
        if (icon != nullptr)
        {
            const auto iconArea = header.removeFromLeft (26).toFloat().withSizeKeepingCentre (24.0f, 24.0f);
            if (on)
                drawGlow (g, iconArea.getCentre(), 20.0f, 0.22f, hue);
            icon->drawWithin (g, iconArea, juce::RectanglePlacement::centred, on ? 1.0f : 0.4f);
            header.removeFromLeft (10);
        }
        const auto shownTitle = title.toUpperCase();
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
                g.drawText (captions[c].toUpperCase(), c->getBounds().translated (0, c->getHeight() + 4).withHeight (14),
                            juce::Justification::centred, false);
            }
    }

    void DetailPanel::layoutMainRow (juce::Rectangle<int> row)
    {
        if (mainControls.empty())
            return;
        // Preferred widths: knobs 96, segment groups by their number of segments, switches 120
        std::vector<int> widths;
        int total = 0;
        for (auto* c : mainControls)
        {
            int w = 96;
            if (auto* s = dynamic_cast<Segments*> (c))
                w = juce::jlimit (150, 440, 58 * (s->getNumSegments() + (s->getOverflowSegment() >= 0 ? 1 : 0)));
            else if (dynamic_cast<Toggle*> (c) != nullptr)
                w = 124;
            else if (dynamic_cast<Choice*> (c) != nullptr)
                w = 170;
            else if (dynamic_cast<juce::Button*> (c) != nullptr)
                w = 104;
            widths.push_back (w);
            total += w;
        }
        const int gapW = 18;
        total += gapW * ((int) mainControls.size() - 1);
        const float shrink = total > row.getWidth() ? (float) row.getWidth() / (float) total : 1.0f;
        float x = (float) row.getX() + (float) std::max (0, (row.getWidth() - total) / 2);
        for (size_t i = 0; i < mainControls.size(); ++i)
        {
            const int w = std::min ((int) ((float) widths[i] * shrink), row.getRight() - (int) x);
            auto cell = juce::Rectangle<int> ((int) x, row.getY(), w, row.getHeight());
            auto* c = mainControls[i];
            if (dynamic_cast<Knob*> (c) != nullptr)
                c->setBounds (cell);
            else
                c->setBounds (cell.withSizeKeepingCentre (w, dynamic_cast<Choice*> (c) != nullptr ? 30 : 28).translated (0, -8));
            x += (float) widths[i] * shrink + (float) gapW * shrink;
        }
    }

    void DetailPanel::resized()
    {
        auto r = getLocalBounds();
        headerArea = r.removeFromTop (headerHeight).reduced (padding, 0);
        auto header = headerArea;
        if (power != nullptr)
            power->setBounds (header.removeFromLeft (26).withSizeKeepingCentre (26, 26));
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
        if (visual != nullptr && live != nullptr)
            visual->update (*live);

        const int on = onParamId.isEmpty() || apvts.getRawParameterValue (onParamId)->load() > 0.5f ? 1 : 0;
        if (on != lastOn)
        {
            lastOn = on;
            for (auto* c : getChildren())
                if (c != power.get() && c != visual.get() && c != &presetsButton && c != &helpButton && c != &moreButton)
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
        float grFrom (const LiveData* live, const LiveData::History LiveData::* h) { return live != nullptr ? (live->*h).ago (0) : 0.0f; }


        // COMPRESSOR: history + transfer curve, GR meter; Threshold, Ratio, Attack, Release
        class CompPanel final : public DetailPanel
        {
        public:
            explicit CompPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Compressor", help::shortDescription ((int) ModuleId::comp), pid::compOn)
            {
                auto& threshold = knob (pid::compThreshold, "Threshold");
                auto& ratio = knob (pid::compRatio, "Ratio");
                auto& attack = knob (pid::compAttack, "Attack");
                auto& release = knob (pid::compRelease, "Release");
                knob (pid::compKnee, "Knee");
                knob (pid::compMakeup, "Makeup");
                knob (pid::compScHpf, "SC Filter");
                knob (pid::compMix, "Mix");
                segments (pid::compDetector);
                setMainControls ({ &threshold, &ratio, &attack, &release });
                gr.setTooltip ("Gain Reduction (dB)");
                placeInVisual ({ &gr });
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (grFrom (live, &LiveData::compGr));
            }

        private:
            void layoutVisual (juce::Rectangle<int> area) override { placeMeter (gr, area); }
            GainReductionMeter gr { colours::accent, "GR" };
        };

        // DE-ESSER: Frequency, Threshold, Range, Mode, Listen
        class DeEssPanel final : public DetailPanel
        {
        public:
            explicit DeEssPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "De-esser", help::shortDescription ((int) ModuleId::deess), pid::deessOn)
            {
                auto& mode = segments (pid::deessMode);
                auto& listen = toggle (pid::deessListen, "Listen");
                auto& freq = knob (pid::deessFreq, "Frequency");
                auto& threshold = knob (pid::deessThreshold, "Threshold");
                auto& range = knob (pid::deessRange, "Range");
                setMainControls ({ &freq, &threshold, &range, &mode, &listen });
                gr.setTooltip ("Gain Reduction (dB)");
                placeInVisual ({ &gr });
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (grFrom (live, &LiveData::deessGr));
            }

        private:
            void layoutVisual (juce::Rectangle<int> area) override { placeMeter (gr, area); }
            GainReductionMeter gr { colours::accent, "GR" };
        };

        // SATURATION: Character, Drive, Mix; Output in Mehr
        class SatPanel final : public DetailPanel
        {
        public:
            explicit SatPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Saturation", help::shortDescription ((int) ModuleId::sat), pid::satOn)
            {
                auto& character = segments (pid::satCharacter);
                auto& drive = knob (pid::satDrive, "Drive");
                knob (pid::satOutput, "Output");
                auto& mix = knob (pid::satMix, "Mix");
                setMainControls ({ &character, &drive, &mix });
            }
        };

        // DOUBLER (the dsp tracks replace the engine): Width, Detune, Timing, Mix
        class DoublerPanel final : public DetailPanel
        {
        public:
            explicit DoublerPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Doubler", help::shortDescription ((int) ModuleId::doubler), pid::doublerOn)
            {
                auto& width = knob (pid::doublerWidth, "Width");
                auto& delay = knob (pid::doublerDelay, "Timing");
                auto& detune = knob (pid::doublerDetune, "Detune");
                knob (pid::doublerMonoSafe, "Mono Safe");
                knob (pid::doublerTone, "Tone");
                auto& mix = knob (pid::doublerMix, "Mix");
                setMainControls ({ &width, &detune, &delay, &mix });
                setMixKnob (nullptr);
                mix.setDescription (juce::String::fromUTF8 ("Anteil der Dopplungen am Signal."));
            }
        };

        // PITCH: Pitch, Formant, Fine; keep formants and Mix in Mehr
        class PitchPanel final : public DetailPanel
        {
        public:
            explicit PitchPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Pitch", help::shortDescription ((int) ModuleId::pitch), pid::pitchOn)
            {
                toggle (pid::pitchPreserveFormants, "Formanten erhalten");
                semis = &knob (pid::pitchSemitones, "Pitch");
                auto& fine = knob (pid::pitchCents, "Fine");
                auto& formant = knob (pid::pitchFormant, "Formant");
                knob (pid::pitchMix, "Mix");
                setMainControls ({ semis, &formant, &fine });
            }

            void refresh() override
            {
                DetailPanel::refresh();
                // The latency note lives in the hover card, not on the panel
                if (++frame % 30 != 1)
                    return;
                auto withPitch = proc.getRouting();
                if (! withPitch.contains (dsp::ModuleId::pitch))
                    withPitch.insertDefault (dsp::ModuleId::pitch, 0);
                const int latency = proc.getChain().getLatencyFor (withPitch, true) - proc.getChain().getLatencyFor (withPitch, false);
                const double ms = 1000.0 * latency / std::max (1.0, proc.getSampleRate());
                const auto* spec = findParameterSpec (pid::pitchSemitones);
                semis->setDescription (juce::String (spec != nullptr ? spec->description : "") + "\n\n"
                                       + juce::String::fromUTF8 ("Solange Pitch an ist: ") + juce::String (juce::roundToInt (ms)) + " ms Latenz.");
            }

        private:
            Knob* semis = nullptr;
            int frame = 0;
        };

        // STUTTER (chop): Rate, Gate, Smooth, Pattern
        class ChopPanel final : public DetailPanel
        {
        public:
            explicit ChopPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Stutter", help::shortDescription ((int) ModuleId::chop), pid::chopOn)
            {
                auto& rate = segments (pid::chopRate);
                auto& pattern = choice (pid::chopPattern, {});
                setCaption (pattern, "Pattern");
                auto& depth = knob (pid::chopDepth, "Gate");
                auto& smooth = knob (pid::chopSmooth, "Smooth");
                setMainControls ({ &rate, &depth, &smooth, &pattern });
            }
        };

        // DELAY: Note value, Sync, Feedback, Mix, Ping-Pong; everything else in Mehr
        class DelayPanel final : public DetailPanel
        {
        public:
            explicit DelayPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Delay", help::shortDescription ((int) ModuleId::delay), pid::delayOn)
            {
                division = &segments (pid::delayDivision, { "1/4", "1/4D", "1/8", "1/8D", "1/8T", "1/16" }, true);
                auto& sync = toggle (pid::delaySync, "Sync");
                auto& pingPong = toggle (pid::delayPingPong, "Ping-Pong");
                segments (pid::duckSource, { "Vocal", "Sidechain" });
                time = &knob (pid::delayTime, "Time");
                auto& feedback = knob (pid::delayFeedback, "Feedback");
                knob (pid::delaySpread, "Offset");
                knob (pid::delayLowCut, "Low Cut");
                knob (pid::delayHighCut, "High Cut");
                knob (pid::delayMod, "Wobble");
                knob (pid::delayDrive, "Drive");
                knob (pid::delayDiffusion, "Diffusion");
                knob (pid::delayToReverb, "Into Reverb");
                auto& mix = knob (pid::delayMix, "Mix");
                knob (pid::delayDuckAmount, "Ducking");
                knob (pid::delayDuckThreshold, "Duck Thresh");
                knob (pid::delayDuckAttack, "Duck Attack");
                knob (pid::delayDuckRelease, "Duck Release");
                setCaption (*division, "Time");
                setMainControls ({ division, &sync, &feedback, &mix, &pingPong });
                setMixKnob (&mix);
                gr.setTooltip ("Ducking (dB)");
                placeInVisual ({ &gr });
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (grFrom (live, &LiveData::delayDuck));
                const bool synced = apvts.getRawParameterValue (pidFor (pid::delaySync))->load() > 0.5f;
                time->setEnabled (! synced);
                division->setEnabled (synced);
            }

        private:
            void layoutVisual (juce::Rectangle<int> area) override { placeMeter (gr, area); }
            Segments* division = nullptr;
            Knob* time = nullptr;
            GainReductionMeter gr { colours::accent, "Duck" };
        };

        // OUTPUT: stereo image + loudness; Input Gain, Vocal Level, Width, Level Match;
        // Mehr: mono below, max correction, measure window, clip guard, mono listening
        class OutputPanel final : public DetailPanel
        {
        public:
            explicit OutputPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Output", help::shortDescription (-1), nullptr)
            {
                auto& mode = segments (pid::levelMatchMode);
                setCaption (mode, "Level Match");
                segments (pid::levelMatchWindow);
                toggle (pid::safetyClip, "Clip Guard");
                auto& inGain = knob (pid::inputGain, "Input Gain");
                auto& level = knob (pid::outputGain, "Vocal Level");
                auto& width = knob (pid::stereoWidth, "Width");
                knob (pid::monoBelow, "Mono Below");
                knob (pid::levelMatchRange, "Max Correction");
                auto& mono = own (std::make_unique<juce::TextButton> ("Mono"));
                mono.setClickingTogglesState (true);
                mono.setTooltip (juce::String::fromUTF8 ("In Mono abhören (wird nicht mit dem Projekt gespeichert)"));
                mono.setToggleState (p.isMonoMonitor(), juce::dontSendNotification);
                mono.onClick = [this, &mono] { proc.setMonoMonitor (mono.getToggleState()); };
                monoButton = &mono;
                setMainControls ({ &inGain, &level, &width, &mode });

                correlation.setTooltip ("Phasenkorrelation: +1 = monokompatibel, unter 0 = Anteile löschen sich in Mono aus");
                for (auto* l : { &inValue, &procValue, &matchValue })
                {
                    l->setFont (monoFont (13.0f));
                    l->setColour (juce::Label::textColourId, colours::text);
                    l->setJustificationType (juce::Justification::centredRight);
                }
                inValue.setTooltip ("Eingang (LUFS)");
                procValue.setTooltip ("Bearbeitet (LUFS)");
                matchValue.setTooltip ("Level Match (dB)");
                placeInVisual ({ &correlation, &inValue, &procValue, &matchValue });
            }

            void refresh() override
            {
                DetailPanel::refresh();
                const auto& m = proc.getChain().meters;
                correlation.push (m.correlation.load());
                const auto fmt = [] (float lufs) { return lufs <= -99.0f ? juce::String ("-") : juce::String (lufs, 1); };
                const auto set = [] (juce::Label& l, const juce::String& t) { if (l.getText() != t) l.setText (t, juce::dontSendNotification); };
                set (inValue, "IN " + fmt (m.inputLufs.load()));
                set (procValue, "FX " + fmt (m.processedLufs.load()));
                const float match = m.levelMatchDb.load();
                set (matchValue, "MATCH " + juce::String (match > 0.05f ? "+" : "") + juce::String (match, 1));
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
            }

            CorrelationView correlation;
            juce::Label inValue, procValue, matchValue;
            juce::TextButton* monoButton = nullptr;
        };

        //======================================================================
        // PULSE: Depth, Rate, Curve; shape, timing and manual tempo in Mehr
        class PulsePanel final : public DetailPanel
        {
        public:
            explicit PulsePanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Pulse", help::shortDescription ((int) ModuleId::pulse), pid::pulseOn)
            {
                auto& rate = segments (pid::pulseRate, pulseRateLabels (p), true);
                auto& interp = segments (pid::pulseInterp);
                auto& depth = knob (pid::pulseDepth, "Depth");
                knob (pid::pulseTension, "Shape");
                knob (pid::pulseSmooth, "Smooth");
                knob (pid::pulseAttack, "Attack");
                knob (pid::pulseRelease, "Release");
                bpm = &knob (pid::pulseBpm, "Manual BPM");
                knob (pid::pulseMix, "Mix");
                setMainControls ({ &depth, &rate, &interp });
            }

            void refresh() override
            {
                DetailPanel::refresh();
                // The manual tempo only matters when the host sends none
                const bool host = proc.getChain().getPulse().displayHostTempo.load();
                bpm->setAlpha (host ? 0.4f : (apvts.getRawParameterValue (pid::pulseOn)->load() > 0.5f ? 1.0f : 0.55f));
            }

        private:
            // The design's five rates, mapped onto the parameter's names where they exist
            static juce::StringArray pulseRateLabels (VelvoxAudioProcessor& p)
            {
                juce::StringArray out;
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p.getState().getParameter (pid::pulseRate)))
                    for (auto* name : { "1/2", "1/4", "1/8", "1/16", "1/32" })
                        if (c->choices.contains (name))
                            out.add (name);
                return out;
            }

            Knob* bpm = nullptr;
        };

        // VOCAL SMASH: the big SMASH control in the visual next to the GR meter; Mix, Tone, Output
        class SmashPanel final : public DetailPanel
        {
        public:
            explicit SmashPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Vocal Smash", help::shortDescription ((int) ModuleId::smash), pid::smashOn)
            {
                amount = &knob (pid::smashAmount, "Smash");
                knob (pid::smashAttack, "Attack");
                knob (pid::smashRelease, "Release");
                auto& tone = knob (pid::smashTone, "Tone");
                auto& output = knob (pid::smashOutput, "Output");
                auto& blend = knob (pid::smashBlend, "Mix");
                setMainControls ({ &blend, &tone, &output });
                placeInVisual ({ amount, &gr });
                amount->setDialSize (120);
                gr.setTooltip ("Gain Reduction des gequetschten Signals");
            }

            void refresh() override
            {
                DetailPanel::refresh();
                gr.push (grFrom (live, &LiveData::smashGr));
            }

        private:
            void layoutVisual (juce::Rectangle<int> area) override
            {
                // Meter at the edge, the big SMASH control in its own column, the picture gets the rest
                placeMeter (gr, area);
                auto big = area.reduced (8).withTrimmedRight (44);
                const int column = std::min (200, big.getWidth() / 3);
                amount->setBounds (big.removeFromRight (column).withSizeKeepingCentre (column, std::min (big.getHeight(), 180)));
                if (auto* v = getVisual())
                    v->setBounds (area.withTrimmedRight (52 + column).reduced (1));
            }

            Knob* amount = nullptr;
            GainReductionMeter gr { colours::accent, "GR" };
        };

        // CHOIR: Voices, Detune, Width, Mix; timing, humanize, density in Mehr
        class ChoirPanel final : public DetailPanel
        {
        public:
            explicit ChoirPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Choir", help::shortDescription ((int) ModuleId::choir), pid::choirOn)
            {
                auto& voices = segments (pid::choirVoices);
                setCaption (voices, "Stimmen");
                auto& detune = knob (pid::choirDetune, "Detune");
                knob (pid::choirTiming, "Timing");
                knob (pid::choirHumanize, "Humanize");
                auto& width = knob (pid::choirWidth, "Width");
                knob (pid::choirDensity, "Density");
                auto& mix = knob (pid::choirMix, "Mix");
                setMainControls ({ &voices, &detune, &width, &mix });
            }
        };

        // LEVEL RIDE: Ausgleich (Amount), Target, Speed; limits and detection in Mehr
        class RidePanel final : public DetailPanel
        {
        public:
            explicit RidePanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Level Ride", help::shortDescription ((int) ModuleId::ride), pid::rideOn)
            {
                auto& target = knob (pid::rideTarget, "Target");
                knob (pid::rideMaxBoost, "Max Boost");
                knob (pid::rideMaxCut, "Max Cut");
                auto& response = knob (pid::rideResponse, "Speed");
                auto& amount = knob (pid::rideAmount, "Ausgleich");
                knob (pid::ridePhrase, "Phrase");
                knob (pid::rideTransient, "Transients");
                knob (pid::rideSilence, "Gate Floor");
                setMainControls ({ &amount, &target, &response });
            }
        };

        //======================================================================
        // VOCAL CHECK results: measured facts, what the last automatic mix changed, and the
        // diagnoses (issue, evidence, confidence, why it matters, suggestion, whether the
        // chain already takes care of it). Scrolls when it does not fit.
        class FindingsList final : public juce::Component
        {
        public:
            explicit FindingsList (VelvoxAudioProcessor& p) : proc (p), content (*this)
            {
                viewport.setViewedComponent (&content, false);
                viewport.setScrollBarsShown (true, false);
                viewport.setScrollBarThickness (8);
                addAndMakeVisible (viewport);
            }

            std::function<void (int moduleId)> onOpen;

            void resized() override
            {
                viewport.setBounds (getLocalBounds());
                content.layout (getWidth() - 10);
            }

            void refresh (bool moduleOn)
            {
                const auto& r = proc.getLatestAnalysis();
                juce::String key = juce::String ((int) moduleOn) + juce::String ((int) r.enoughMaterial) + juce::String (juce::roundToInt (r.voicedSeconds * 2.0f))
                                   + juce::String (juce::roundToInt (r.peakDb)) + juce::String (r.clipEvents) + juce::String ((int) proc.getAutoMix().getReport().valid);
                for (auto& f : r.findings)
                    key << f.title << (int) f.certainty << (int) f.handling << juce::roundToInt (f.confidence * 20.0f);
                if (key != lastKey)
                {
                    lastKey = key;
                    on = moduleOn;
                    content.layout (getWidth() - 10);
                }
            }

        private:
            struct Content final : public juce::Component
            {
                explicit Content (FindingsList& o) : owner (o) {}

                struct Card
                {
                    juce::Rectangle<float> bounds, link;
                    int module = -1;
                    juce::TextLayout body;
                };

                void layout (int width)
                {
                    width = std::max (300, width);
                    cards.clear();
                    const auto& r = owner.proc.getLatestAnalysis();
                    float y = 0.0f;

                    // Facts strip and auto-mix summary
                    factsHeight = owner.on && (r.analysedSeconds > 0.0f || r.peakDb > -99.0f) ? 58.0f : 0.0f;
                    y += factsHeight;
                    const auto& report = owner.proc.getAutoMix().getReport();
                    summary = {};
                    if (owner.on && report.valid && ! report.steps.empty())
                    {
                        juce::AttributedString a;
                        a.append ("What the automatic mix changed\n", font (13.5f, true), colours::text);
                        int n = 0;
                        for (auto& s : report.steps)
                            if (n++ < 6)
                                a.append (juce::String::fromUTF8 ("\xe2\x80\xa2  ") + s.title + "\n", font (13.0f), colours::textDim);
                        summary.createLayout (a, (float) width - 32.0f);
                        summaryBounds = { 0.0f, y, (float) width, summary.getHeight() + 22.0f };
                        y += summaryBounds.getHeight() + 10.0f;
                    }

                    for (auto& f : r.findings)
                    {
                        Card c;
                        juce::AttributedString a;
                        const auto label = [&] (const juce::String& head, const juce::String& text, juce::Colour colour)
                        {
                            if (text.isEmpty())
                                return;
                            a.append (head + "  ", caps (10.5f), colours::textFaint);
                            a.append (text + "\n", font (13.5f), colour);
                        };
                        a.append (f.title + "\n", font (15.5f, true), colours::text);
                        label ("EVIDENCE", f.detail, colours::textDim);
                        label ("WHY IT MATTERS", f.why, colours::textDim);
                        label ("SUGGESTION", f.advice, colours::cyan.interpolatedWith (colours::text, 0.45f));
                        if (f.handlingText.isNotEmpty())
                            label (f.handling == dsp::Finding::Handling::appliedBefore ? "ALREADY BEFORE" : "HANDLED", f.handlingText,
                                   f.handling == dsp::Finding::Handling::appliedBefore ? colours::bypass : colours::ok);
                        else
                            label ("STATUS", f.module.has_value() ? juce::String ("Not applied automatically - your choice.")
                                                                  : juce::String ("Needs a change before the plugin (recording or track level)."), colours::textFaint);
                        c.body.createLayout (a, (float) width - 150.0f);
                        c.bounds = { 0.0f, y, (float) width, std::max (64.0f, c.body.getHeight() + 22.0f) };
                        c.module = f.module.has_value() ? (int) *f.module : -1;
                        if (c.module >= 0)
                            c.link = { c.bounds.getRight() - 130.0f, c.bounds.getY() + 44.0f, 118.0f, 24.0f };
                        y += c.bounds.getHeight() + 8.0f;
                        cards.push_back (std::move (c));
                    }

                    status.clear();
                    if (! owner.on)
                        status = "VOCAL CHECK is off. Switch it on with the power button - it analyses the signal at its place in the chain and never changes the sound.";
                    else if (! r.enoughMaterial)
                        status = "Collecting material: " + juce::String (r.voicedSeconds, 1) + " of " + juce::String (juce::roundToInt (dsp::VocalAnalyzer::minVoicedSeconds))
                                 + " s of voice. Play the vocal - pauses are ignored.";
                    else if (r.findings.empty())
                        status = "Nothing stands out in " + juce::String (r.voicedSeconds, 1) + " s of voice: level, tone, sibilance, dynamics"
                                 + (r.stereo ? ", stereo" : "") + " and noise are inside the usual range.";
                    if (status.isNotEmpty())
                        y += 46.0f;

                    setSize (width, (int) std::ceil (y) + 4);
                    repaint();
                }

                void paint (juce::Graphics& g) override
                {
                    const auto& r = owner.proc.getLatestAnalysis();
                    float y = 0.0f;

                    // Measured facts (no judgement)
                    if (factsHeight > 0.0f)
                    {
                        drawCaption (g, "Measured at this point of the chain", { 0.0f, 0.0f, (float) getWidth(), 16.0f },
                                     juce::Justification::centredLeft, colours::textFaint, 9.5f);
                        juce::StringArray facts;
                        facts.add ("Peak " + (r.peakDb > -99.0f ? juce::String (r.peakDb, 1) + " dBFS" : juce::String ("-")));
                        if (r.loudnessRangeDb > 0.0f)
                            facts.add ("Level range " + juce::String (r.loudnessRangeDb, 1) + " dB");
                        if (r.crestDb > 0.0f)
                            facts.add ("Crest " + juce::String (r.crestDb, 1) + " dB");
                        if (r.sibilanceVsVowelDb > -99.0f)
                            facts.add ("S vs vowels " + juce::String (r.sibilanceVsVowelDb, 1) + " dB");
                        if (r.noiseFloorDb > -199.0f)
                            facts.add ("Noise floor " + juce::String (r.noiseFloorDb, 0) + " dBFS (" + juce::String (r.snrDb, 0) + " dB below voice)");
                        if (r.stereo)
                            facts.add ("Correlation " + juce::String (r.correlation, 2));
                        facts.add ("Voice " + juce::String (r.voicedSeconds, 1) + " s");
                        float x = 0.0f;
                        for (auto& t : facts)
                        {
                            const float w = (float) juce::GlyphArrangement::getStringWidthInt (medium (12.0f), t) + 22.0f;
                            if (x + w > (float) getWidth())
                                break;
                            const auto chip = juce::Rectangle<float> (x, 22.0f, w, 26.0f);
                            g.setColour (colours::accent.withAlpha (0.035f));
                            g.fillRoundedRectangle (chip, 13.0f);
                            g.setColour (colours::lineSoft.brighter (0.3f));
                            g.drawRoundedRectangle (chip.reduced (0.5f), 13.0f, 1.0f);
                            g.setColour (colours::textDim);
                            g.setFont (medium (12.0f));
                            g.drawText (t, chip, juce::Justification::centred, false);
                            x += w + 8.0f;
                        }
                        y = factsHeight;
                    }

                    if (! summaryBounds.isEmpty())
                    {
                        g.setColour (colours::ok.withAlpha (0.05f));
                        g.fillRoundedRectangle (summaryBounds, 10.0f);
                        g.setColour (colours::ok.withAlpha (0.35f));
                        g.drawRoundedRectangle (summaryBounds.reduced (0.5f), 10.0f, 1.0f);
                        summary.draw (g, summaryBounds.reduced (16.0f, 11.0f));
                    }

                    for (size_t i = 0; i < cards.size() && i < r.findings.size(); ++i)
                    {
                        const auto& c = cards[i];
                        const auto& f = r.findings[i];
                        const bool sure = f.certainty == dsp::Finding::Certainty::sure;
                        g.setColour (colours::accent.withAlpha (0.028f));
                        g.fillRoundedRectangle (c.bounds, 10.0f);
                        g.setColour (sure ? colours::cyan.withAlpha (0.28f) : colours::lineSoft.brighter (0.25f));
                        g.drawRoundedRectangle (c.bounds.reduced (0.5f), 10.0f, 1.0f);

                        // Kind and confidence, as text (never colour alone)
                        auto right = c.bounds.withTrimmedLeft (c.bounds.getWidth() - 130.0f).reduced (6.0f, 10.0f);
                        auto badge = right.removeFromTop (22.0f).withSizeKeepingCentre (118.0f, 22.0f);
                        if (sure)
                        {
                            g.setGradientFill (auroraGradient (badge.getTopLeft(), badge.getTopRight(), 0.85f));
                            g.fillRoundedRectangle (badge, 11.0f);
                            drawCaption (g, "Likely problem", badge, juce::Justification::centred, colours::background, 9.0f);
                        }
                        else
                        {
                            g.setColour (colours::line.brighter (0.5f));
                            g.drawRoundedRectangle (badge.reduced (0.5f), 11.0f, 1.0f);
                            drawCaption (g, "Observation", badge, juce::Justification::centred, colours::textDim, 9.0f);
                        }
                        drawCaption (g, "Confidence " + juce::String (juce::roundToInt (f.confidence * 100.0f)) + " %",
                                     right.removeFromTop (16.0f).translated (0.0f, 2.0f), juce::Justification::centred, colours::textFaint, 9.0f);
                        if (c.module >= 0)
                        {
                            g.setColour (colours::cyan.withAlpha (hoveredLink == (int) i ? 0.18f : 0.08f));
                            g.fillRoundedRectangle (c.link, 12.0f);
                            drawCaption (g, juce::String ("Open ") + dsp::moduleDisplayName ((dsp::ModuleId) c.module), c.link,
                                         juce::Justification::centred, colours::cyan, 9.0f);
                        }
                        c.body.draw (g, c.bounds.reduced (16.0f, 11.0f).withTrimmedRight (124.0f));
                        y = c.bounds.getBottom() + 8.0f;
                    }

                    if (status.isNotEmpty())
                    {
                        g.setColour (colours::textDim);
                        g.setFont (font (14.0f));
                        g.drawFittedText (status, juce::Rectangle<float> (0.0f, y + 4.0f, (float) getWidth(), 40.0f).toNearestInt(),
                                          juce::Justification::centredLeft, 2);
                    }
                }

                void mouseMove (const juce::MouseEvent& e) override
                {
                    int over = -1;
                    for (size_t i = 0; i < cards.size(); ++i)
                        if (cards[i].module >= 0 && cards[i].link.contains (e.position))
                            over = (int) i;
                    if (over != hoveredLink)
                    {
                        hoveredLink = over;
                        setMouseCursor (over >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
                        repaint();
                    }
                }

                void mouseUp (const juce::MouseEvent& e) override
                {
                    for (auto& c : cards)
                        if (c.module >= 0 && c.link.contains (e.position) && owner.onOpen)
                            owner.onOpen (c.module);
                }

                FindingsList& owner;
                std::vector<Card> cards;
                juce::TextLayout summary;
                juce::Rectangle<float> summaryBounds;
                float factsHeight = 0.0f;
                juce::String status;
                int hoveredLink = -1;
            };

            VelvoxAudioProcessor& proc;
            bool on = false;
            juce::String lastKey;
            juce::Viewport viewport;
            Content content;
        };

        // VOCAL CHECK (the check track replaces this with score + tiles): live view and the findings in the
        // visual area; analysis mode, Start / Stop, Reset and the status as the main row
        class CheckPanel final : public DetailPanel
        {
        public:
            explicit CheckPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Vocal Check", help::shortDescription ((int) ModuleId::check), pid::checkOn),
                  findings (p)
            {
                auto& mode = segments (pid::checkMode);
                setCaption (mode, "Analyse");
                auto& startStop = own (std::make_unique<juce::TextButton> ("Start"));
                auto& resetButton = own (std::make_unique<juce::TextButton> ("Reset"));
                start = &startStop;
                reset = &resetButton;
                setMainControls ({ &mode, &startStop, &resetButton });
                placeInVisual ({ &findings, &statusLabel });
                statusLabel.setFont (font (12.0f));
                statusLabel.setColour (juce::Label::textColourId, colours::textDim);
                statusLabel.setJustificationType (juce::Justification::centredLeft);

                startStop.setTooltip (juce::String::fromUTF8 ("Capture: analysiert alles zwischen Start und Stop"));
                startStop.onClick = [this]
                {
                    auto& a = proc.getAnalyzer();
                    if (a.isCapturing()) a.stopCapture(); else a.startCapture();
                    proc.refreshAnalysis();
                };
                resetButton.setTooltip (juce::String::fromUTF8 ("Alles bisher Analysierte vergessen (auch Spitzen und Clips)"));
                resetButton.onClick = [this] { proc.getAnalyzer().clear(); proc.refreshAnalysis(); };
                findings.onOpen = [this] (int id) { if (onOpenModule) onOpenModule (id); };
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
                const juce::String text = ! on ? juce::String ("Aus")
                                        : capture ? (a.isCapturing() ? "Capture " + juce::String (r.analysedSeconds, 1) + " s"
                                                                     : "Gestoppt, " + juce::String (r.analysedSeconds, 1) + " s")
                                                  : "Laufend, letzte " + juce::String (juce::roundToInt (std::min (30.0f, r.analysedSeconds))) + " s";
                if (statusLabel.getText() != text)
                    statusLabel.setText (text, juce::dontSendNotification);
                findings.refresh (on);
            }

        private:
            void layoutVisual (juce::Rectangle<int> area) override
            {
                auto r = area.reduced (10);
                auto left = r.removeFromLeft (std::min (300, r.getWidth() / 3));
                statusLabel.setBounds (left.removeFromBottom (20));
                r.removeFromLeft (12);
                findings.setBounds (r);
                if (auto* v = getVisual())
                    v->setBounds (left.reduced (1));
            }

            FindingsList findings;
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
                case ModuleId::comp:    panel = std::make_unique<CompPanel> (p); break;
                case ModuleId::deess:   panel = std::make_unique<DeEssPanel> (p); break;
                case ModuleId::sat:     panel = std::make_unique<SatPanel> (p); break;
                case ModuleId::doubler: panel = std::make_unique<DoublerPanel> (p); break;
                case ModuleId::pitch:   panel = std::make_unique<PitchPanel> (p); break;
                case ModuleId::chop:    panel = std::make_unique<ChopPanel> (p); break;
                case ModuleId::delay:   panel = std::make_unique<DelayPanel> (p); break;
                case ModuleId::reverb:  panel = createReverbPanel (p); break;
                case ModuleId::pulse:   panel = std::make_unique<PulsePanel> (p); break;
                case ModuleId::smash:   panel = std::make_unique<SmashPanel> (p); break;
                case ModuleId::choir:   panel = std::make_unique<ChoirPanel> (p); break;
                case ModuleId::smooth:  panel = createSmoothPanel (p); break;
                case ModuleId::check:   panel = std::make_unique<CheckPanel> (p); break;
                case ModuleId::ride:    panel = std::make_unique<RidePanel> (p); break;
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
