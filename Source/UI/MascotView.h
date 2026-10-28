#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "MascotBrain.h"

namespace velvox::ui
{
    // Memphy as a comic sticker (SIMPEL, bottom left). The editor feeds it once per frame; it shows the
    // sticker of the mood the Brain picks, with its pop, breathing, talking bounce and hop, and cross-fades
    // between two stickers. The sticker fills the stand area (his place in the layout) and scales with the
    // window (stickers are 384 px, sharp up to about 3x); parts of the figure pop out over the disc, so
    // the component is larger than the stand area and only takes clicks on the sticker itself.
    // A click on him opens the Vocal Check (approved assistant concept: no extra button for it).
    class MascotView final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        MascotView();

        void tick (const mascot::Inputs& in, double dt);

        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool hitTest (int x, int y) override;

        // Where the sticker goes, in local coordinates (default: the whole component)
        void setStandArea (juce::Rectangle<float> area) { standArea = area; repaint(); }
        juce::Rectangle<float> getStandArea() const;

        std::function<void()> onClick;
        void simulateClick() { if (onClick) onClick(); }

        const mascot::Brain& getBrain() const noexcept { return brain; }
        void playIntro() { brain.playIntro(); repaint(); }
        void playBow() { brain.playBow(); repaint(); }

        // The sticker of a mood (embedded PNG, decoded once)
        static juce::Image sticker (mascot::Mood mood);

    private:
        juce::Rectangle<float> stickerArea() const;   // the sticker image at rest, in local coordinates
        juce::AffineTransform motion() const;          // pop, breathing, bounce, hop around his feet

        mascot::Brain brain;
        juce::Rectangle<float> standArea;
    };
}
