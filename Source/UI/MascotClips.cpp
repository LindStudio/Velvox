#include "MascotClips.h"
#include "VelvoxBinaryData.h"

namespace velvox::ui::mascot
{
    namespace
    {
        // Binary data names of the packed clips, in Clip order (juce_add_binary_data turns "idle.vms" into "idle_vms")
        constexpr const char* resourceNames[numClips] {
            "wakeUp_vms", "idle_vms", "bad_vms", "problem_vms", "okay_vms", "good_vms",
            "super_vms", "spin_vms", "working_vms", "walk_vms", "call_vms"
        };
    }

    bool ClipData::parse (const void* data, size_t size)
    {
        frames.clear();
        if (data == nullptr || size < 25 || std::memcmp (data, "VMS3", 4) != 0)
            return false;

        const auto* bytes = static_cast<const char*> (data);
        juce::MemoryInputStream in (data, size, false);
        in.skipNextBytes (4);
        clipInfo.fps = in.readShort();
        const int w = in.readShort(), h = in.readShort();
        frameSize = { w, h };
        clipInfo.frames = in.readInt();
        clipInfo.loops = in.readByte() != 0;
        const int l = in.readShort(), t = in.readShort(), r = in.readShort(), b = in.readShort();
        standing = juce::Rectangle<int>::leftTopRightBottom (l, t, r, b);

        const auto blob = [&] () -> Blob
        {
            const int n = in.readInt();
            const auto at = (int) in.getPosition();
            if (n < 0 || at + (juce::int64) n > (juce::int64) size)
                return {};
            in.skipNextBytes (n);
            return { bytes + at, n };
        };

        for (int i = 0; i < clipInfo.frames && ! in.isExhausted(); ++i)
        {
            Frame f;
            f.colour = blob();
            f.alpha = blob();
            for (size_t m = 0; m < 2; ++m)
            {
                const int x = in.readShort(), y = in.readShort();
                f.mouthPos[m] = { x, y };
                f.mouths[m] = blob();
            }
            if (f.colour.data == nullptr || f.alpha.data == nullptr)
                break;
            frames.push_back (f);
        }
        clipInfo.frames = std::max (1, (int) frames.size());
        return ! frames.empty();
    }

    juce::Image ClipData::decode (Blob b)
    {
        if (b.data == nullptr || b.size == 0)
            return {};
        return juce::ImageFileFormat::loadFrom (b.data, (size_t) b.size);
    }

    juce::Image ClipData::withAlpha (const juce::Image& colour, const juce::Image& alpha)
    {
        if (! colour.isValid() || ! alpha.isValid() || colour.getBounds() != alpha.getBounds())
            return {};
        juce::Image out (juce::Image::ARGB, colour.getWidth(), colour.getHeight(), false);
        const juce::Image::BitmapData src (colour, juce::Image::BitmapData::readOnly);
        const juce::Image::BitmapData mask (alpha, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData dst (out, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < out.getHeight(); ++y)
            for (int x = 0; x < out.getWidth(); ++x)
            {
                const auto c = src.getPixelColour (x, y);
                // The alpha PNG is grey: any channel holds the coverage
                dst.setPixelColour (x, y, c.withAlpha (mask.getPixelColour (x, y).getRed()));
            }
        return out;
    }

    juce::Image ClipData::decodeBase (int frame) const
    {
        if (frames.empty())
            return {};
        const auto& f = frames[(size_t) juce::jlimit (0, (int) frames.size() - 1, frame)];
        return withAlpha (decode (f.colour), decode (f.alpha));
    }

    Patch ClipData::patch (int frame, Mouth mouth) const
    {
        if (frames.empty() || mouth == Mouth::closed)
            return {};
        const auto& f = frames[(size_t) juce::jlimit (0, (int) frames.size() - 1, frame)];
        const size_t m = mouth == Mouth::half ? 0 : 1;
        return { decode (f.mouths[m]), f.mouthPos[m] };
    }

    const ClipLibrary& ClipLibrary::get()
    {
        static const ClipLibrary library;
        return library;
    }

    ClipLibrary::ClipLibrary()
    {
        for (int i = 0; i < numClips; ++i)
        {
            int size = 0;
            const char* data = VelvoxBinaryData::getNamedResource (resourceNames[i], size);
            clips[(size_t) i].parse (data, (size_t) size);
        }
    }

    std::array<ClipInfo, numClips> ClipLibrary::infos() const
    {
        std::array<ClipInfo, numClips> out {};
        for (int i = 0; i < numClips; ++i)
            out[(size_t) i] = clips[(size_t) i].info();
        return out;
    }
}
