#pragma once

#include <juce_graphics/juce_graphics.h>
#include "MascotBrain.h"

// The mascot's sprite clips, packed by tools/mascot/pack_mascot.py into Resources/Mascot/<clip>.vms
// and embedded as binary data. Frames stay compressed until they are drawn.
//
// .vms layout (little endian):
//   "VMS3"  int16 fps  int16 width  int16 height  int32 frames  int8 loops
//           int16 left, top, right, bottom   box of the standing figure (idle clip) in the frame
//   per frame:  int32 size + JPEG colour, int32 size + PNG alpha   the whole figure with the mouth closed
//               2 x (int16 x, int16 y, int32 size + PNG)           mouth half / open: a small patch at x, y (size 0 = none)
namespace velvox::ui::mascot
{
    struct Patch
    {
        juce::Image image;
        juce::Point<int> position;
    };

    class ClipData
    {
    public:
        bool parse (const void* data, size_t size);

        const ClipInfo& info() const noexcept { return clipInfo; }
        int frameCount() const noexcept { return (int) frames.size(); }
        juce::Point<int> size() const noexcept { return frameSize; }
        juce::Rectangle<int> standingBox() const noexcept { return standing; }

        juce::Image decodeBase (int frame) const;
        Patch patch (int frame, Mouth mouth) const;   // invalid image for Mouth::closed

    private:
        struct Blob { const char* data = nullptr; int size = 0; };
        struct Frame
        {
            Blob colour, alpha;
            std::array<Blob, 2> mouths;
            std::array<juce::Point<int>, 2> mouthPos;
        };

        static juce::Image decode (Blob b);
        static juce::Image withAlpha (const juce::Image& colour, const juce::Image& alpha);

        ClipInfo clipInfo;
        juce::Point<int> frameSize;
        juce::Rectangle<int> standing;
        std::vector<Frame> frames;
    };

    class ClipLibrary
    {
    public:
        static const ClipLibrary& get();

        const ClipData& clip (Clip c) const noexcept { return clips[(size_t) c]; }
        std::array<ClipInfo, numClips> infos() const;

    private:
        ClipLibrary();
        std::array<ClipData, numClips> clips;
    };
}
