#pragma once

#include "Widgets.h"
#include "LiveData.h"
#include "../DSP/Instances.h"
#include "../Presets/ParameterSnapshot.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // Chain row of Advanced mode (spec 2.4): one slim tile per module instance in processing order,
    // each showing ONLY its name, then its icon, then its on/off switch (top to bottom). No knobs or
    // values in the chain.
    //   - click = select (the panel below shows it), drag = reorder, power = on/off
    //   - right-click = menu: on/off, duplicate, remove, move left/right, reset, "Als Send" / "Als Insert"
    //     (delay, reverb), undo / redo, help
    //   - the "+" tile at the end (and a "+" on the gap between two tiles) opens the module browser
    //   - delay / reverb running as parallel sends sit behind a divider and carry a "SEND" badge;
    //     dragging one there makes it a send
    //   - the OUTPUT tile at the right end opens the output stage
    //   - more tiles than fit: the row scrolls (wheel, drag near the edges)
    // Every edit goes through the processor's routing (lock-free to the audio thread, switched under
    // a short fade) and is one step of the global undo history.
    class ChainView final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        static constexpr int outputTile = -1; // selection id for the output stage

        // Layout (design units)
        static constexpr int preferredHeight = 88;
        static constexpr float tileW = 96.0f, minTileW = 84.0f, addTileW = 56.0f, outputW = 64.0f, sendGap = 14.0f;
        static constexpr float nameH = 24.0f, iconH = 36.0f, powerH = 24.0f;

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

        // Modules set up by the last automatic mix (module keys) - shown with an "Auto" mark
        void setAutoMixModules (const juce::StringArray& keys) { if (keys != autoKeys) { autoKeys = keys; repaint(); } }
        bool isAutoMixModule (dsp::ModuleId id) const { return autoKeys.contains (dsp::moduleKey (id)); }

        // Selection codes: dsp::selectionCode (module, instance), outputTile for the output
        std::function<void (int selection)> onSelect;
        std::function<void (int selection)> onHelp;           // "Hilfe" in the tile menu
        std::function<void (int insertIndex, bool fromEnd)> onAddModule;   // open the module browser
        void setSelected (int selection);
        void setLiveData (const LiveData* data) { live = data; }
        int getSelected() const noexcept { return selected; }

        // Parameter controlling each module's on/off state (instance 1; see onParamId)
        static const char* onParamFor (dsp::ModuleId id);
        static juce::String onParamId (dsp::ModuleId id, int instance) { return dsp::instanceParamId (onParamFor (id), instance); }

        // ---- Chain edits (also used by the module browser, the panels and the tests). Each one is one undo step.
        // Inserts a fresh instance (default settings, switched on) at serial position `index`
        // (-1 = end), as a send when `asSend` (delay / reverb only). Returns the selection code
        // or -1 when every instance of that module is in use.
        int insertModule (dsp::ModuleId id, int index = -1, bool asSend = false);
        int duplicateInstance (dsp::ModuleId id, int instance);   // copy right after the original
        bool removeInstance (dsp::ModuleId id, int instance);     // switches it off and takes it out
        bool moveInstance (dsp::ModuleId id, int instance, int newIndex);
        bool setSendMode (dsp::ModuleId id, int instance, bool send);
        bool undo();
        bool redo();
        bool canUndo() const;
        bool canRedo() const;

        // Every parameter of one instance back to its default (on/off stays): one undo step
        static void resetInstance (VelvoxAudioProcessor& p, dsp::ModuleId id, int instance);

        // The tile menu's actions (right-click), callable directly
        enum class TileAction { toggle, duplicate, remove, moveLeft, moveRight, reset, asSend, asInsert, undo, redo, help };
        bool performTileAction (int selectionCode, TileAction action);
        juce::PopupMenu buildTileMenu (int selectionCode);

        // Tile geometry (local coordinates; tiles scrolled out of the row can lie outside it)
        juce::Rectangle<float> getTileBounds (dsp::ModuleId id, int instance = 0);
        juce::Rectangle<float> getNameBounds (dsp::ModuleId id, int instance = 0);
        juce::Rectangle<float> getIconBounds (dsp::ModuleId id, int instance = 0);
        juce::Rectangle<float> getPowerBounds (dsp::ModuleId id, int instance = 0);    // on/off of a tile
        juce::Rectangle<float> getAddCardBounds();
        juce::Rectangle<float> getOutputBounds() const { return outputArea(); }
        juce::Rectangle<float> getLaneBounds() const { return laneArea(); }
        juce::Rectangle<float> getRowViewport() const { return rowArea(); }
        juce::String getHint() const { return hint; }

        // Horizontal scrolling of the row
        float getScroll() const noexcept { return scroll.target; }
        float getMaxScroll();
        void scrollTo (float x, bool animate = true);
        void scrollToShow (dsp::ModuleId id, int instance = 0, bool animate = true);
        bool isTileFullyVisible (dsp::ModuleId id, int instance = 0);

        void restoreDefaultChain();          // the usual order (settings stay)
        void saveLayoutAsDefault();           // personal default chain (user settings, all projects)
        bool loadDefaultLayout();             // false when none was saved

        // Name shown on a tile: "EQ", or "EQ 2" when the chain holds several EQs
        static juce::String cardName (const dsp::Routing& routing, dsp::ModuleId id, int instance);

    private:
        struct Tile
        {
            dsp::ModuleId id;
            int instance = 0;
            juce::Rectangle<float> bounds;   // on screen (scroll applied)
            bool inLane = false;             // runs as a send
            int position = 0;                // 1-based position among the serial tiles
            int code() const noexcept { return dsp::selectionCode (id, instance); }
        };

        void layoutTiles();
        void toggleOn (dsp::ModuleId id, int instance);
        void showTileMenu (const Tile& t);
        void showChainMenu();
        void applyRouting (const dsp::Routing& r, const juce::String& message = {});
        int tileAt (juce::Point<float> p) const;
        int gapAt (juce::Point<float> p) const;   // serial insert index of the "+" under the mouse, or -1
        juce::Point<float> gapCentre (int insertIndex) const;
        juce::Rectangle<float> nameBounds (const Tile& t) const;
        juce::Rectangle<float> iconBounds (const Tile& t) const;
        juce::Rectangle<float> powerBounds (const Tile& t) const;
        bool moveBy (dsp::ModuleId id, int instance, int steps);
        void drawIcon (juce::Graphics&, dsp::ModuleId id, juce::Rectangle<float> area, float alpha) const;
        juce::Rectangle<float> addCardArea() const;
        juce::Rectangle<float> laneArea() const;
        juce::Rectangle<float> rowArea() const;
        juce::Rectangle<float> outputArea() const;
        float contentWidth() const;
        int insertIndexForX (float x) const;
        int serialIndexToRoutingIndex (const dsp::Routing& r, int serialIndex, dsp::ModuleId exceptId, int exceptInstance) const;
        void showHint (const juce::String& text);
        bool isOn (dsp::ModuleId id, int instance) const;
        const Tile* findTile (dsp::ModuleId id, int instance) const;
        void drawTile (juce::Graphics&, const Tile&, juce::Rectangle<float> where, bool ghost) const;
        void drawOutput (juce::Graphics&) const;
        void drawAddCard (juce::Graphics&, juce::Rectangle<float> r) const;
        float activityOf (const Tile& t) const;

        VelvoxAudioProcessor& proc;
        dsp::Routing routing;
        std::vector<Tile> tiles;
        int numSerial = 0;
        int numSends = 0;
        float serialW = tileW;

        // Per selection code (module + 256 * instance)
        std::map<int, float> activity;
        std::map<int, bool> onState;
        std::array<bool, dsp::numModules> macroEngaged {};
        int selected = 0;
        int hovered = -2;
        int hoveredGap = -1;

        // dragging a tile
        int dragTile = -1;
        juce::Point<float> dragOffset, dragPos;
        bool dragging = false;

        juce::String hint;
        juce::uint32 hintUntil = 0;

        // Animation: scrolling glides, hover fades in, a drag auto-scrolls near the row edges
        void tick();
        const LiveData* live = nullptr;
        Smoothed scroll;
        bool pendingReveal = true;   // show the selected tile once the view has a size
        std::map<int, Smoothed> hoverGlow;
        Smoothed gapGlow;
        juce::VBlankAttachment vblank { this, [this] { tick(); } };
        juce::StringArray autoKeys;
        bool globalBypass = false;
        mutable std::map<int, std::unique_ptr<juce::Drawable>> iconCache;
    };
}
