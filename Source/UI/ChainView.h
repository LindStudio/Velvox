#pragma once

#include "Widgets.h"
#include "../DSP/Routing.h"

class VelvoxAudioProcessor;

namespace velvox::ui
{
    // Signal-flow overview and routing editor.
    //  - Top row: serial modules in processing order (drag to reorder).
    //  - Lower lane: delay / reverb running as parallel sends.
    //  - Drag delay or reverb between the row (insert) and the lane (send).
    //  - Click a tile to select it, click its dot to switch it on/off.
    class ChainView final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        static constexpr int outputTile = -1; // selection id for the output stage

        explicit ChainView (VelvoxAudioProcessor& processor);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        void refresh();   // ~30 Hz from the editor

        std::function<void (int moduleIdOrOutput)> onSelect;
        void setSelected (int id) { selected = id; repaint(); }
        int getSelected() const noexcept { return selected; }

        // Parameter controlling each module's on/off state
        static const char* onParamFor (dsp::ModuleId id);

        // Current tile rectangle (local coordinates) - for tests and hit testing
        juce::Rectangle<float> getTileBounds (dsp::ModuleId id);
        juce::Rectangle<float> getLaneBounds() const { return laneArea(); }
        juce::String getHint() const { return hint; }

    private:
        struct Tile
        {
            dsp::ModuleId id;
            juce::Rectangle<float> bounds;
            bool inLane = false;
        };

        void layoutTiles();
        int tileAt (juce::Point<float> p) const;
        juce::Rectangle<float> dotBounds (const Tile& t) const;
        juce::Rectangle<float> modeBounds (const Tile& t) const;
        juce::Rectangle<float> laneArea() const;
        juce::Rectangle<float> rowArea() const;
        int insertIndexForX (float x) const;
        void showHint (const juce::String& text);
        bool isOn (dsp::ModuleId id) const;
        void drawTile (juce::Graphics&, const Tile&, juce::Rectangle<float> where, bool ghost) const;

        VelvoxAudioProcessor& proc;
        dsp::Routing routing;
        std::vector<Tile> tiles;
        juce::Rectangle<float> inBox, outBox;

        std::array<float, dsp::numModules> activity {};
        std::array<bool, dsp::numModules> onState {}, macroEngaged {};
        int selected = 0;
        int hovered = -2;

        // dragging
        int dragTile = -1;
        juce::Point<float> dragOffset, dragPos;
        bool dragging = false;

        juce::String hint;
        juce::uint32 hintUntil = 0;
    };
}
