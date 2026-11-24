#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

// The pink seal ("Siegel", Jakob 2026-11-19, mockup https://claude.ai/artifact/2DbiyZf8V7SpKonaiNKYNf): Ember stands on a
// glowing summoning circle, the seal's letters float in the air above it and circle round him, the whole window takes
// part (runes round the edge, a wave of light through the panels on every S, a small seal on the Amount knob).
//
// Everything here is plain drawing into a Graphics, with no state of its own: the time and the last S come in as
// a Scene. So the stage parts can be drawn on Ember's drawing thread (LiveMascotView) and the window parts on the
// message thread (SealOverlay). The runes are drawn as strokes (Elder Futhark is straight lines), so no font is needed.
namespace ember::seal
{
    inline const juce::Colour pink { 0xffff3a6e };       // the seal, accents
    inline const juce::Colour pinkLight { 0xffffbed7 };  // highlights, letters
    inline const juce::Colour pinkDeep { 0xff7a0a2e };
    inline const juce::Colour wall { 0xff0b0409 };       // window background

    struct Scene
    {
        double t = 0;          // seconds, running
        float sinceS = 99.0f;  // seconds since Ember swallowed the last S
        float heat = 0;        // 0..1.2: how hard the de-esser works (reduction / 9 dB, smoothed)
        bool on = true;        // false in bypass: the seal goes dark
        bool calm = false;     // animation "reduced" / "off" (menu): the circle, its letters and the border stand still
        float lit() const noexcept { return on ? 0.65f + 0.35f * juce::jmin (1.0f, heat) : 0.12f; }
        float wave() const noexcept { return juce::jmax (0.0f, 1.0f - sinceS * 1.1f); }
        float jump() const noexcept { return juce::jmax (0.0f, 1.0f - sinceS * 1.6f); }
        float rotation() const noexcept { return calm ? 0.0f : (float) (t * 0.11); }
    };

    // One of the 24 runes as strokes in a box of 0.6 x 1 (top left 0,0)
    const juce::Path& rune (int index);
    // A rune of this height, centred on c, glowing
    void drawRune (juce::Graphics&, int index, juce::Point<float> c, float height, juce::Colour, float alpha);

    // The glowing runes drawn once into small images and then only copied (drawing ~160 of them as glowing strokes
    // every frame cost most of the seal's time). One atlas per thread that draws; sizes are rounded to 1 px.
    // forScreen: the atlas draws straight onto the window (message thread). Its sprites are then kept as the screen's
    // own images (Direct2D): a memory image is uploaded to the graphics card anew on every single draw, which made
    // the ~200 border runes the dearest part of every repaint (2026-11-23). Same pixels either way.
    class RuneAtlas
    {
    public:
        explicit RuneAtlas (juce::Colour c, bool forScreen = false) : colour (c), screen (forScreen) {}
        void setScale (float physicalScale);
        void draw (juce::Graphics&, int index, juce::Point<float> c, float height, float alpha);
        // the area draw() covers (to skip runes outside the part of the window being repainted)
        juce::Rectangle<float> bounds (juce::Point<float> c, float height) const;
    private:
        juce::Colour colour;
        bool screen = false;
        float scale = 1.0f;
        std::map<int, juce::Image> sprites;
    };

    // The circle lying on the floor: centre, radii of the ellipse (perspective). drawFloor = the calm part (rings,
    // star, seals, light), renewed only every few frames; drawFloorLive = what moves fast (running dashes, the wave
    // after an S), drawn every frame on top.
    void drawFloor (juce::Graphics&, juce::Point<float> centre, float rx, float ry, const Scene&);
    void drawFloorLive (juce::Graphics&, juce::Point<float> centre, float rx, float ry, const Scene&);
    // The letters above the floor circle; back = the half behind Ember, front = the half in front of him
    void drawFloatingRunes (juce::Graphics&, RuneAtlas&, juce::Point<float> centre, float rx, float ry, const Scene&, bool front);

    // Window parts
    void drawRuneBorder (juce::Graphics&, RuneAtlas&, juce::Rectangle<float> window, const Scene&);
    // The small seal on the Amount knob; it turns with the knob only (angle in radians, clockwise from 12 o'clock)
    void drawMiniSeal (juce::Graphics&, RuneAtlas&, juce::Point<float> centre, float radius, float angle, const Scene&);

    // The static back wall of the chamber (bricks, pillars, chains, floor), drawn once per size
    void drawChamber (juce::Graphics&, juce::Rectangle<float> window, juce::Rectangle<float> stage);
}
