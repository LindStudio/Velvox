#pragma once

#include "Widgets.h"
#include "ModuleViews.h"
#include "Segments.h"
#include "ValueText.h"
#include "../DSP/Instances.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // The common module panel frame of Headliner 0.6 (spec 2.4, style A "Grafik zuerst"):
    //
    //   header     power · S · icon · NAME n · [SEND] ·················· Presets ▾ · Mehr · ?
    //   visual     the module's live picture, as large as possible (+ meters / big controls a panel
    //              places there with placeInVisual)
    //   main row   3 - 5 main controls (large knobs, segments, switches), centred in one row
    //   Mehr       a drawer that slides in from the right and holds every other control as a small knob
    //              (segments and switches above them); while open, visual and main row use the rest
    //
    // How a module panel uses it (the API later per-module panels build on):
    //   1. create every control with knob() / toggle() / segments() / choice()  -> they start in Mehr
    //   2. setMainControls ({ ... })   -> 3 - 5 of them move to the main row
    //   3. optional: setVisual() is done by the factory; placeInVisual ({ ... }) + override layoutVisual()
    //      for extras inside the visual area (gain-reduction meters, a big knob)
    //   4. optional: setMixKnob (k) -> labelled "SEND" while a delay / reverb instance runs as a send
    // No parameter may be left out: whatever is not main stays reachable in Mehr.
    //
    // Right-click anywhere in the panel (and on every control) adds the panel items: reset module,
    // save module preset, and "Alte Mix-Kurve – umstellen" while the instance uses the legacy mix law.
    class DetailPanel : public juce::Component, public juce::SettableTooltipClient, public gestures::PanelMenuHost
    {
    public:
        DetailPanel (VelvoxAudioProcessor& p, juce::String title, juce::String subtitle, const char* onParamId);
        ~DetailPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        virtual void refresh();

        void setVisual (std::unique_ptr<ModuleView> view);
        void setLiveData (const LiveData* data) { live = data; }
        // Module identity (selection code: module + instance): icon, title, module presets
        void setModule (int selection);
        int getInstance() const noexcept { return instance; }
        int getModuleId() const noexcept { return moduleId; }

        // Instance the next constructed panel belongs to (set by createDetailPanel; message thread)
        inline static int constructionInstance = 0;

        // Asks the editor to show another module (e.g. from a VOCAL CHECK finding)
        std::function<void (int moduleId)> onOpenModule;
        // Opens the help page for this module (the editor shows the help overlay)
        std::function<void (int moduleIdOrOutput)> onHelp;
        // Asks for a name (module preset); without it the panel picks "Eigenes n"
        std::function<void (const juce::String& title, std::function<void (const juce::String&)> onName)> askForName;
        // Asks the editor to switch to the VOCAL CHECK tab (link in the Check module's summary)
        std::function<void()> onShowVocalCheck;

        juce::TextButton& getHelpButton() noexcept { return helpButton; }
        juce::TextButton& getPresetsButton() noexcept { return presetsButton; }
        juce::TextButton& getMoreButton() noexcept { return moreButton; }
        // Module solo (0.6.1) next to the power switch; nullptr for the Output stage and Check
        juce::Button* getSoloButton() noexcept { return solo.get(); }

        // ---- Frame state (also for tests)
        bool isMoreOpen() const noexcept { return moreOpen; }
        void setMoreOpen (bool open);
        const std::vector<juce::Component*>& getMainControls() const noexcept { return mainControls; }
        std::vector<juce::Component*> getMoreControls() const;
        juce::Component& getMoreDrawer() noexcept;
        juce::Rectangle<int> getVisualArea() const noexcept { return visualArea; }
        juce::Rectangle<int> getMainRowArea() const noexcept { return mainRow; }
        juce::Rectangle<int> getHeaderArea() const noexcept { return headerArea; }
        bool isSend() const;                      // delay / reverb instance running as a parallel send
        juce::String getHeaderBadge() const;      // "SEND", "AUS" or empty
        bool showsLegacyMixLawAction() const;     // "Alte Mix-Kurve – umstellen" offered
        juce::Button* getLegacyMixLawButton() noexcept { return legacyButton.get(); }

        // Panel menu items (PanelMenuHost): reset module, save module preset, mix law
        void addPanelItems (juce::PopupMenu& menu) override;
        void resetModule();
        void saveModulePreset (const juce::String& name);

    protected:
        // ID of a module parameter for this panel's instance ("lowGain" -> "lowGain_2")
        juce::String pidFor (const char* baseId) const;

        // Controls bound to this instance's parameters; they start in the Mehr drawer
        Knob& knob (const char* paramId, const juce::String& label);
        Toggle& toggle (const char* paramId, const juce::String& label);
        Choice& choice (const char* paramId, const juce::String& caption);
        // Segment group: labels map onto the choices by name; none = the parameter's own
        Segments& segments (const char* paramId, juce::StringArray labels = {}, bool withOverflow = false);
        // A non-parameter control owned by the panel (buttons, labels); starts in Mehr like the others
        template <typename C> C& own (std::unique_ptr<C> c)
        {
            auto& ref = *c;
            adopt (std::move (c));
            return ref;
        }

        // The 3 - 5 main controls, left to right
        void setMainControls (std::initializer_list<juce::Component*> controls);
        // Controls the panel places itself inside the visual area (in layoutVisual)
        void placeInVisual (std::initializer_list<juce::Component*> controls);
        // Caption shown under a main segment group / switch (knobs carry their own name)
        void setCaption (juce::Component& control, const juce::String& caption);
        // Mix knob of delay / reverb: shows "SEND" while the instance is a send
        void setMixKnob (Knob* k) { mixKnob = k; }

        // Place extras inside the visual area; the visual itself already fills `area`
        virtual void layoutVisual (juce::Rectangle<int> area) { juce::ignoreUnused (area); }
        ModuleView* getVisual() noexcept { return visual.get(); }
        // A slim meter (gain reduction, ducking) at the right edge of the visual area; the visual keeps the rest
        void placeMeter (juce::Component& meter, juce::Rectangle<int> area);

        const LiveData* live = nullptr;
        VelvoxAudioProcessor& proc;
        APVTS& apvts;

    private:
        class MoreDrawer;
        void adopt (std::unique_ptr<juce::Component> c);
        void layoutMainRow (juce::Rectangle<int> row);
        void showModulePresets();
        void updateSendState();
        juce::String moduleName() const;

        juce::String title, subtitle;
        const int instance;
        juce::String onParamId;   // empty: no power switch

        std::unique_ptr<PowerButton> power;
        std::unique_ptr<juce::Button> solo;
        std::unique_ptr<ModuleView> visual;
        int moduleId = -2;
        juce::TextButton presetsButton { "Presets" }, moreButton { "Mehr" }, helpButton { "?" };
        std::unique_ptr<MoreDrawer> drawer;
        std::unique_ptr<juce::TextButton> legacyButton;
        std::vector<std::unique_ptr<juce::Component>> owned;
        std::vector<Knob*> knobs;
        std::vector<Toggle*> toggles;
        std::vector<Segments*> segmentGroups;
        std::vector<juce::Component*> mainControls, visualControls;
        std::map<juce::Component*, juce::String> captions;
        Knob* mixKnob = nullptr;
        juce::String mixLabel;
        std::unique_ptr<juce::Drawable> icon;
        juce::Rectangle<int> headerArea, visualArea, mainRow;
        bool moreOpen = false;
        int lastOn = -1, lastSend = -1, lastLegacy = -1;
    };

    // Factory: the detail panel for a module or the output stage (id < 0)
    std::unique_ptr<DetailPanel> createDetailPanel (VelvoxAudioProcessor& p, int moduleIdOrOutput,
                                                    const SpectrumSource* spectrum = nullptr, const LiveData* live = nullptr);
}
