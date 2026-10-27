#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MascotBrain.h"
#include "MascotClips.h"

namespace velvox::ui
{
    // The animated devil (SIMPEL, bottom left). The editor feeds it once per frame; it plays the clip
    // the Brain picks, swaps in the mouth patch for the level and cross-fades between clips.
    // The standing figure fills the stand area (his place in the layout) and scales with the window
    // (frames are rendered for twice the design size); wider poses reach beyond it, so the component
    // is larger than the stand area and only takes clicks on the figure itself.
    // A click on him opens the Vocal Check (approved assistant concept: no extra button for it).
    class MascotView final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        MascotView();

        void tick (const mascot::Inputs& in, double dt);

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool hitTest (int x, int y) override;

        // Where the standing figure goes, in local coordinates (default: the whole component)
        void setStandArea (juce::Rectangle<float> area) { standArea = area; repaint(); }
        juce::Rectangle<float> getStandArea() const;

        std::function<void()> onClick;
        void simulateClick() { if (onClick) onClick(); }

        const mascot::Brain& getBrain() const noexcept { return brain; }

    private:
        struct Cached
        {
            mascot::Clip clip = mascot::Clip::wakeUp;
            int frame = -1;
            juce::Image image;
        };

        const juce::Image& frameImage (Cached& slot, mascot::Clip clip, int frame);
        juce::Rectangle<float> frameArea() const;   // the whole frame image, in local coordinates

        const mascot::ClipLibrary& library;
        mascot::Brain brain;
        Cached current, fading;
        juce::Rectangle<float> standArea;
        mascot::Patch mouthPatch;
        mascot::Clip mouthClip = mascot::Clip::wakeUp;
        int mouthFrame = -1;
        mascot::Mouth mouthKind = mascot::Mouth::closed;
        int lastFrame = -1;
        mascot::Clip lastClip = mascot::Clip::wakeUp;
        mascot::Mouth lastMouth = mascot::Mouth::closed;
    };
}
