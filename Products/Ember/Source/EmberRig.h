#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <map>
#include <vector>

// Ember as a live rig (2026-11-17, "Animations-Engine statt Video"): the figure is drawn every frame from a few parts
// of Jakob's drawing, moved by a pose. Same data and the same drawing as the browser prototype
// (tools/mascot/ember_live_prototype_template.html, Products/Ember/Mascot/live_v17); how it is made:
// tools/mascot/FIGUR_ANIMIEREN.md. The parts and rig.json come from tools/mascot/export_plugin_rig.py.
//
// Everything is in the 1024 px space of the picture. RigRenderer draws a pose into an area of a Graphics.
namespace ember
{
    // One pose: what the brain decides each frame. Zero = the drawing at rest.
    struct RigPose
    {
        float head = 0, headX = 0, headY = 0;          // degrees around the neck, px
        float earL = 0, earR = 0, tail = 0;            // degrees (meshes bend)
        float armL = 0, armR = 0;                      // degrees, negative left / positive right = out
        float lean = 0, bob = 0, squash = 0;           // whole body from the feet
        float lookX = 0, lookY = 0, pupil = 1;         // -1..1, pupil scale
        float lid = 0, lowLid = 0;                     // 0 open .. 1 shut / squint
        float mouth = 0, mouthW = 1;                   // 0 closed .. ~1.2, width 0.55..1
        float browY = 0, browTilt = 0;                 // px, degrees (positive = cross)
    };

    class MascotRig
    {
    public:
        static const MascotRig& get();                 // loaded once, shared by every window

        struct Part { juce::Image image; juce::Rectangle<float> box; };
        struct Mesh
        {
            float x = 0, y = 0, step = 14; int cols = 0, rows = 0;
            std::vector<float> weight;                 // per grid point
            std::vector<uint8_t> quad;                 // per cell: has pixels
            juce::Point<float> pivot;
        };
        struct Eye
        {
            std::vector<float> x, top, bot, up, low;   // sampled per column
            float pupil[5] {}, glint[4] {};
            std::vector<juce::Point<float>> browUp, browLow, tail;
            float cx = 0;
        };

        bool isValid() const noexcept { return valid; }
        const Part& part (const juce::String& name) const;
        const Mesh* mesh (const juce::String& name) const;

        std::map<juce::String, Part> parts;
        std::map<juce::String, Mesh> meshes;
        juce::Point<float> neck, tailJoint, earL, earR, shoulderL, shoulderR, feet;
        float smileX0 = 0, smileStep = 3; std::vector<float> smile;
        std::vector<Eye> eyes;
        juce::Colour line, pupil, pupilRim, yellowHi, yellow, yellowLo, shade, glint;

    private:
        MascotRig();
        bool valid = false;
        Part empty;
    };

    class RigRenderer
    {
    public:
        // Draws the figure for this pose into area (the figure is fitted to the area's height, feet at the bottom).
        void render (juce::Graphics&, juce::Rectangle<float> area, const RigPose&, float displayScale = 1.0f);

        // The same picture, only into an image of area x displayScale pixels (figure + outline, premultiplied), to be
        // drawn into area later. Touches no Graphics of the screen, so it may run on a thread of its own (one
        // renderer per thread). The rig must be loaded first (MascotRig::get() on the message thread).
        void renderInto (juce::Image& target, juce::Rectangle<float> area, const RigPose&, float displayScale);

        // picture -> area: where the figure sits in an area (the same as getRigToArea() after a render)
        static juce::AffineTransform fitTransform (juce::Rectangle<float> area);

        static constexpr float outlineWidth = 6.0f;                 // px in the 1024 space (the drawing's own: 5-6)
        static constexpr juce::uint32 outlineColour = 0xff340616;   // the drawing's outline maroon

        // picture -> picture: where a point of the head (face, ear, mouth) is in this pose; and of the body
        static juce::AffineTransform headTransform (const RigPose&);
        static juce::AffineTransform bodyTransform (const RigPose&);

        // the picture -> area mapping of the last render (for effects that follow the figure)
        juce::AffineTransform getRigToArea() const noexcept { return rigToArea; }

        // where the time of the last frame went (tests): drawing the figure, the outline, putting it on screen
        double lastDrawMs = 0, lastOutlineMs = 0, lastCompositeMs = 0, lastFaceMs = 0;

    private:
        void drawFigure (juce::Graphics&, const RigPose&);
        // a whole part (rows yFrom..yTo of the picture), into the figure's pixels like the meshes
        void drawPart (const juce::String& name, const juce::AffineTransform& space, float yFrom = -1.0e6f, float yTo = 1.0e6f);
        void drawMesh (juce::Graphics&, const juce::String& name, float degrees, const juce::AffineTransform& space,
                       std::function<juce::Point<float> (juce::Point<float> p, float restY)> post = {});
        void drawNeckBand (juce::Graphics&, const juce::String& name, float y0, float y1, const RigPose&);
        // one textured triangle, drawn straight into the figure's pixels (the Graphics clip per triangle was too slow
        // on 2x screens): bilinear sampling, top-left fill rule so neighbours tile without seams or overlap
        void raster (const juce::Image& src, const juce::AffineTransform& space,
                     juce::Point<float> s0, juce::Point<float> s1, juce::Point<float> s2,
                     juce::Point<float> d0, juce::Point<float> d1, juce::Point<float> d2);
        juce::AffineTransform base;                        // picture -> figure image pixels
        juce::Image::BitmapData* pixels = nullptr;         // the figure's pixels while it is drawn
        void drawEyes (juce::Graphics&, const RigPose&);
        void drawMouth (juce::Graphics&, const RigPose&);
        void outline (const juce::Image& figure, juce::Image& target, float radiusPx, juce::Colour);

        // the face (eyes, brows, open mouth) as vectors is the dearest part of a frame; it is drawn into its own image
        // only when one of its values changes, and laid onto the head like a part (2026-11-19)
        void drawFace (const RigPose&);
        struct FaceKey { float v[9] {}; float scale = 0; bool valid = false; };
        static FaceKey faceKey (const RigPose&, float scale);
        static bool same (const FaceKey&, const FaceKey&);
        static juce::Rectangle<float> faceBox();
        juce::Image faceImage;
        FaceKey faceFor;

        juce::Image figure, ring;
        std::vector<float> dist, buf, z;     // the outline's distance transform
        std::vector<int> v;
        // the parts pre-scaled to the current size (scaling the 1024 px parts every frame cost most of the time)
        const juce::Image& scaled (const juce::String& name);
        std::map<juce::String, juce::Image> scaledParts;
        float scaledFor = 0.0f;
        juce::AffineTransform rigToArea;
    };
}
