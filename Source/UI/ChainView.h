#pragma once

#include "Widgets.h"
#include "LiveData.h"
#include "../DSP/Instances.h"
#include "../Presets/ParameterSnapshot.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // Chain editor: the processing chain as an ordered row of module instances.
    //  - Top row: serial instances as cards in processing order (drag to reorder). The
    //    row scrolls horizontally; cards never shrink below a readable width.
    //  - Input and output sit at the ends as fixed ports, outside the scrolling row.
    //  - Lower lane: delay / reverb instances running as parallel sends, plus the
    //    "Add module" button and undo / redo.
    //  - "+" between two cards (on hover) and the "Add module" card at the end open the
    //    module browser for that position. Repeated modules show their instance number
    //    ("EQ 1", "EQ 2") - each is an independent instance with its own settings.
    //  - Card hover buttons: duplicate (copies the settings into a new instance) and
    //    remove. Click a card to open it, its power button to bypass it.
    // Every edit goes through the processor's routing (lock-free to the audio thread,
    // switched under a short fade) and can be undone.
    class ChainView final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        static constexpr int outputTile = -1; // selection id for the output stage

        // Layout (design units)
        static constexpr int preferredHeight = 136;
        static constexpr float cardH = 80.0f, laneH = 40.0f, laneOffset = 14.0f;
        static constexpr float minCardW = 168.0f, maxCardW = 220.0f, endW = 92.0f, gap = 14.0f, addCardW = 132.0f;

        explicit ChainView (VelvoxAudioProcessor& processor);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void resized() override;

        void refresh();   // once per frame from the editor

        // Modules set up by the last automatic mix (module keys) - shown with an "Auto" badge
        void setAutoMixModules (const juce::StringArray& keys) { if (keys != autoKeys) { autoKeys = keys; repaint(); } }
        bool isAutoMixModule (dsp::ModuleId id) const { return autoKeys.contains (dsp::moduleKey (id)); }

        // Selection codes: dsp::selectionCode (module, instance), outputTile for the output
        std::function<void (int selection)> onSelect;
        std::function<void (int selection)> onHelp;           // "Help for this module" in the menus
        std::function<void (int insertIndex, bool fromEnd)> onAddModule;   // open the module browser
        void setSelected (int selection);
        void setLiveData (const LiveData* data) { live = data; }
        int getSelected() const noexcept { return selected; }

        // Parameter controlling each module's on/off state (instance 1; see onParamId)
        static const char* onParamFor (dsp::ModuleId id);
        static juce::String onParamId (dsp::ModuleId id, int instance) { return dsp::instanceParamId (onParamFor (id), instance); }

        // ---- Chain edits (also used by the module browser and the tests). Each one can be undone.
        // Inserts a fresh instance (default settings, switched on) at serial position `index`
        // (-1 = end). Delay / reverb added at the end join as sends. Returns the selection code
        // or -1 when every instance of that module is in use.
        int insertModule (dsp::ModuleId id, int index = -1, bool asSend = false);
        int duplicateInstance (dsp::ModuleId id, int instance);   // copy right after the original
        bool removeInstance (dsp::ModuleId id, int instance);     // switches it off and takes it out
        bool moveInstance (dsp::ModuleId id, int instance, int newIndex);
        bool setSendMode (dsp::ModuleId id, int instance, bool send);
        bool undo();
        bool redo();
        bool canUndo() const noexcept { return ! undoStack.empty(); }
        bool canRedo() const noexcept { return ! redoStack.empty(); }

        // Current card rectangle (local coordinates) - for tests and hit testing.
        // Cards scrolled out of the row can lie outside the visible row area.
        juce::Rectangle<float> getTileBounds (dsp::ModuleId id, int instance = 0);
        juce::Rectangle<float> getPowerBounds (dsp::ModuleId id, int instance = 0);    // on/off button of a card
        juce::Rectangle<float> getModeBounds (dsp::ModuleId id, int instance = 0);     // send / insert switch (delay, reverb)
        juce::Rectangle<float> getRemoveBounds (dsp::ModuleId id, int instance = 0);   // hover button
        juce::Rectangle<float> getDuplicateBounds (dsp::ModuleId id, int instance = 0);
        juce::Rectangle<float> getAddCardBounds();
        juce::Rectangle<float> getLaneBounds() const { return laneArea(); }
        juce::Rectangle<float> getRowViewport() const { return rowArea(); }
        juce::String getHint() const { return hint; }
        juce::TextButton& getAddButton() noexcept { return addButton; }

        // Horizontal scrolling of the module row
        float getScroll() const noexcept { return scroll.target; }
        float getMaxScroll();
        void scrollTo (float x, bool animate = true);
        void scrollToShow (dsp::ModuleId id, int instance = 0, bool animate = true);
        bool isTileFullyVisible (dsp::ModuleId id, int instance = 0);

        void restoreDefaultChain();          // the chain of a new instance (settings stay)
        void saveLayoutAsDefault();           // personal default chain (user settings, all projects)
        bool loadDefaultLayout();             // false when none was saved

        // Name shown on a card: "EQ", or "EQ 2" when the chain holds several EQs
        static juce::String cardName (const dsp::Routing& routing, dsp::ModuleId id, int instance);

    private:
        struct Tile
        {
            dsp::ModuleId id;
            int instance = 0;
            juce::Rectangle<float> bounds;   // on screen (scroll applied)
            bool inLane = false;
            int position = 0;                // 1-based position among the serial cards
            int code() const noexcept { return dsp::selectionCode (id, instance); }
        };

        void layoutTiles();
        void toggleOn (dsp::ModuleId id, int instance);
        void showTileMenu (const Tile& t);
        void showChainMenu();
        void applyRouting (const dsp::Routing& r, const juce::String& message = {});
        void pushUndo();
        int tileAt (juce::Point<float> p) const;
        int gapAt (juce::Point<float> p) const;   // serial insert index of the "+" under the mouse, or -1
        juce::Point<float> gapCentre (int insertIndex) const;
        juce::Rectangle<float> dotBounds (const Tile& t) const;
        juce::Rectangle<float> modeBounds (const Tile& t) const;
        juce::Rectangle<float> removeBounds (const Tile& t) const;
        juce::Rectangle<float> duplicateBounds (const Tile& t) const;
        juce::Rectangle<float> addCardArea() const;
        juce::Rectangle<float> laneArea() const;
        juce::Rectangle<float> rowArea() const;
        juce::Rectangle<float> leftArrowArea() const;
        juce::Rectangle<float> rightArrowArea() const;
        juce::Rectangle<float> scrollTrack() const;
        juce::Rectangle<float> scrollThumb();
        float cardWidth (int numCards) const;
        float contentWidth() const;
        int insertIndexForX (float x) const;
        int serialIndexToRoutingIndex (const dsp::Routing& r, int serialIndex, dsp::ModuleId exceptId, int exceptInstance) const;
        void showHint (const juce::String& text);
        bool isOn (dsp::ModuleId id, int instance) const;
        const Tile* findTile (dsp::ModuleId id, int instance) const;
        void drawTile (juce::Graphics&, const Tile&, juce::Rectangle<float> where, bool ghost) const;
        void drawPort (juce::Graphics&, juce::Rectangle<float> box, bool isOutput) const;
        void drawAddCard (juce::Graphics&, juce::Rectangle<float> r) const;
        float activityOf (const Tile& t) const;

        VelvoxAudioProcessor& proc;
        dsp::Routing routing;
        std::vector<Tile> tiles;
        juce::Rectangle<float> inBox, outBox;
        int numSerial = 0;
        float serialW = minCardW;

        // Per selection code (module + 256 * instance)
        std::map<int, float> activity;
        std::map<int, bool> onState;
        std::array<bool, dsp::numModules> macroEngaged {};
        juce::TextButton addButton, undoButton, redoButton, chainMenuButton;
        int selected = 0;
        int hovered = -2;
        int hoveredGap = -1;

        // dragging a card
        int dragTile = -1;
        juce::Point<float> dragOffset, dragPos;
        bool dragging = false;

        // dragging the scroll bar
        bool draggingThumb = false;
        float thumbGrabOffset = 0.0f;

        juce::String hint;
        juce::uint32 hintUntil = 0;

        // Undo / redo of chain edits: complete snapshots (values + routing)
        std::vector<ParameterSnapshot> undoStack, redoStack;
        static constexpr size_t maxUndo = 40;

        // Animation: scrolling glides, hover fades in, a drag auto-scrolls near the row edges
        void tick();
        const LiveData* live = nullptr;
        Smoothed scroll;
        bool pendingReveal = true;   // show the selected card once the view has a size
        std::map<int, Smoothed> hoverGlow;
        Smoothed gapGlow;
        juce::VBlankAttachment vblank { this, [this] { tick(); } };
        juce::StringArray autoKeys;
        bool globalBypass = false;
    };
}
