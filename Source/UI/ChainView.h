#pragma once

#include "Widgets.h"
#include "LiveData.h"
#include "../DSP/Routing.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // Module navigation, signal-flow overview and routing editor.
    //  - Top row: serial modules as large cards in processing order (drag to
    //    reorder). The row scrolls horizontally when not every card fits; the
    //    cards never shrink below a readable width.
    //  - Input and output sit at the ends as fixed ports, outside the scrolling row.
    //  - Lower lane: delay / reverb running as parallel sends.
    //  - Drag delay or reverb between the row (insert) and the lane (send).
    //  - Click a card to select it, click its power button to switch it on/off.
    class ChainView final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        static constexpr int outputTile = -1; // selection id for the output stage

        // Layout (design units)
        static constexpr int preferredHeight = 136;
        static constexpr float cardH = 80.0f, laneH = 40.0f, laneOffset = 14.0f;
        static constexpr float minCardW = 168.0f, maxCardW = 220.0f, endW = 92.0f, gap = 10.0f;

        explicit ChainView (VelvoxAudioProcessor& processor);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        void refresh();   // once per frame from the editor

        std::function<void (int moduleIdOrOutput)> onSelect;
        std::function<void (int moduleIdOrOutput)> onHelp;   // "Help for this module" in the menus
        void setSelected (int id);
        void setLiveData (const LiveData* data) { live = data; }
        int getSelected() const noexcept { return selected; }

        // Parameter controlling each module's on/off state
        static const char* onParamFor (dsp::ModuleId id);

        // Current card rectangle on screen (local coordinates) - for tests and hit testing.
        // Cards scrolled out of the row can lie outside the visible row area.
        juce::Rectangle<float> getTileBounds (dsp::ModuleId id);
        juce::Rectangle<float> getPowerBounds (dsp::ModuleId id);   // on/off button of a card
        juce::Rectangle<float> getModeBounds (dsp::ModuleId id);    // send / insert switch (delay, reverb)
        juce::Rectangle<float> getLaneBounds() const { return laneArea(); }
        juce::Rectangle<float> getRowViewport() const { return rowArea(); }
        juce::String getHint() const { return hint; }

        // Horizontal scrolling of the module row
        float getScroll() const noexcept { return scroll.target; }
        float getMaxScroll();
        void scrollTo (float x, bool animate = true);
        void scrollToShow (dsp::ModuleId id, bool animate = true);
        bool isTileFullyVisible (dsp::ModuleId id);

        // View-only organisation: hidden tiles keep processing exactly as before.
        // Stored on the state tree, so every instance / project keeps its own view.
        bool isHidden (dsp::ModuleId id) const noexcept { return hidden[(size_t) id]; }
        void setHidden (dsp::ModuleId id, bool shouldHide);
        void showAllModules();
        int getNumHidden() const noexcept;
        void restoreDefaultOrder();          // order only; send / insert modes stay
        void saveLayoutAsDefault();           // personal default (user settings, all projects)
        bool loadDefaultLayout();             // false when none was saved
        juce::PopupMenu createModulesMenu();  // the overview behind the Modules button
        juce::TextButton& getModulesButton() noexcept { return modulesButton; }
        void resized() override;

    private:
        struct Tile
        {
            dsp::ModuleId id;
            juce::Rectangle<float> bounds;   // on screen (scroll applied)
            bool inLane = false;
            int position = 0;                // 1-based position among the serial cards
        };

        void layoutTiles();
        void readHidden();
        void writeHidden();
        void toggleOn (dsp::ModuleId id);
        void showTileMenu (dsp::ModuleId id);
        void handleMenuResult (int result);
        void updateModulesButton();
        int tileAt (juce::Point<float> p) const;
        juce::Rectangle<float> dotBounds (const Tile& t) const;
        juce::Rectangle<float> modeBounds (const Tile& t) const;
        juce::Rectangle<float> laneArea() const;
        juce::Rectangle<float> rowArea() const;
        juce::Rectangle<float> leftArrowArea() const;
        juce::Rectangle<float> rightArrowArea() const;
        juce::Rectangle<float> scrollTrack() const;
        juce::Rectangle<float> scrollThumb();
        float cardWidth (int numCards) const;
        float contentWidth() const;
        int insertIndexForX (float x) const;
        void showHint (const juce::String& text);
        bool isOn (dsp::ModuleId id) const;
        const Tile* findTile (dsp::ModuleId id) const;
        void drawTile (juce::Graphics&, const Tile&, juce::Rectangle<float> where, bool ghost) const;
        void drawPort (juce::Graphics&, juce::Rectangle<float> box, bool isOutput) const;

        VelvoxAudioProcessor& proc;
        dsp::Routing routing;
        std::vector<Tile> tiles;
        juce::Rectangle<float> inBox, outBox;
        int numSerial = 0;
        float serialW = minCardW;

        std::array<float, dsp::numModules> activity {};
        std::array<bool, dsp::numModules> onState {}, macroEngaged {}, hidden {};
        juce::String hiddenKeys;
        juce::TextButton modulesButton;
        juce::String modulesButtonText;
        int selected = 0;
        int hovered = -2;

        // dragging a card
        int dragTile = -1;
        juce::Point<float> dragOffset, dragPos;
        bool dragging = false;

        // dragging the scroll bar
        bool draggingThumb = false;
        float thumbGrabOffset = 0.0f;

        juce::String hint;
        juce::uint32 hintUntil = 0;

        // Animation: scrolling glides, hover fades in, a drag auto-scrolls near the row edges
        void tick();
        const LiveData* live = nullptr;
        Smoothed scroll;
        bool pendingReveal = true;   // show the selected card once the view has a size
        std::array<Smoothed, dsp::numModules> hoverGlow {};
        juce::VBlankAttachment vblank { this, [this] { tick(); } };
    };
}
