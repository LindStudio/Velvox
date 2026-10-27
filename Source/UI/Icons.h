#pragma once

#include "Theme.h"

namespace velvox::ui
{
    // Small line icon per module (and the output stage for id < 0), drawn into
    // `r` with a 1.5 px stroke - same visual language as the rest of the UI.
    inline void drawModuleIcon (juce::Graphics& g, int moduleIdOrOutput, juce::Rectangle<float> r, juce::Colour c)
    {
        using M = dsp::ModuleId;
        r = r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight()), std::min (r.getWidth(), r.getHeight()));
        const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
        const auto P = [&] (float px, float py) { return juce::Point<float> (x + px * w, y + py * h); };
        juce::Path p;

        switch (moduleIdOrOutput < 0 ? -1 : moduleIdOrOutput)
        {
            case (int) M::eq:        // bell curve
                p.startNewSubPath (P (0.05f, 0.65f));
                p.cubicTo (P (0.35f, 0.65f), P (0.38f, 0.15f), P (0.5f, 0.15f));
                p.cubicTo (P (0.62f, 0.15f), P (0.65f, 0.65f), P (0.95f, 0.65f));
                break;
            case (int) M::comp:      // knee
                p.startNewSubPath (P (0.1f, 0.9f));
                p.lineTo (P (0.55f, 0.4f));
                p.lineTo (P (0.9f, 0.28f));
                break;
            case (int) M::deess:     // S
                p.startNewSubPath (P (0.75f, 0.25f));
                p.cubicTo (P (0.6f, 0.05f), P (0.2f, 0.15f), P (0.32f, 0.38f));
                p.cubicTo (P (0.42f, 0.58f), P (0.82f, 0.5f), P (0.7f, 0.78f));
                p.cubicTo (P (0.6f, 0.98f), P (0.3f, 0.92f), P (0.25f, 0.75f));
                break;
            case (int) M::sat:       // soft clip
                p.startNewSubPath (P (0.05f, 0.85f));
                p.cubicTo (P (0.35f, 0.85f), P (0.4f, 0.15f), P (0.95f, 0.15f));
                break;
            case (int) M::doubler:   // two rings
                p.addEllipse (juce::Rectangle<float> (w * 0.5f, h * 0.5f).withCentre (P (0.38f, 0.5f)));
                p.addEllipse (juce::Rectangle<float> (w * 0.5f, h * 0.5f).withCentre (P (0.62f, 0.5f)));
                break;
            case (int) M::delay:     // fading repeats
                for (int i = 0; i < 4; ++i)
                {
                    const float px = 0.15f + 0.23f * (float) i, ph = 0.7f - 0.15f * (float) i;
                    p.startNewSubPath (P (px, 0.5f - ph * 0.5f));
                    p.lineTo (P (px, 0.5f + ph * 0.5f));
                }
                break;
            case (int) M::reverb:    // spreading arcs
                for (int i = 1; i <= 3; ++i)
                {
                    const float rad = 0.15f * (float) i;
                    p.addCentredArc (x + 0.25f * w, y + 0.5f * h, rad * w, rad * h, 0.0f, 0.5f, 2.64f, true);
                }
                break;
            case (int) M::smash:     // two layers: dynamic + flat
                p.startNewSubPath (P (0.05f, 0.4f));
                p.cubicTo (P (0.25f, 0.05f), P (0.35f, 0.75f), P (0.55f, 0.3f));
                p.cubicTo (P (0.7f, 0.0f), P (0.8f, 0.55f), P (0.95f, 0.35f));
                p.startNewSubPath (P (0.05f, 0.75f)); p.lineTo (P (0.95f, 0.75f));
                break;
            case (int) M::smooth:    // spectrum with a tamed peak
                p.startNewSubPath (P (0.05f, 0.7f));
                p.lineTo (P (0.35f, 0.6f)); p.lineTo (P (0.5f, 0.45f)); p.lineTo (P (0.65f, 0.6f)); p.lineTo (P (0.95f, 0.65f));
                p.startNewSubPath (P (0.5f, 0.12f)); p.lineTo (P (0.5f, 0.32f));
                p.startNewSubPath (P (0.42f, 0.24f)); p.lineTo (P (0.5f, 0.32f)); p.lineTo (P (0.58f, 0.24f));
                break;
            case (int) M::check:     // magnifier with tick
                p.addEllipse (juce::Rectangle<float> (w * 0.55f, h * 0.55f).withCentre (P (0.42f, 0.42f)));
                p.startNewSubPath (P (0.63f, 0.63f)); p.lineTo (P (0.9f, 0.9f));
                p.startNewSubPath (P (0.3f, 0.43f)); p.lineTo (P (0.4f, 0.53f)); p.lineTo (P (0.56f, 0.33f));
                break;
            case (int) M::ride:      // fader riding a wave
                p.startNewSubPath (P (0.05f, 0.6f));
                p.cubicTo (P (0.3f, 0.2f), P (0.45f, 0.9f), P (0.7f, 0.5f));
                p.lineTo (P (0.95f, 0.45f));
                p.addRoundedRectangle (juce::Rectangle<float> (w * 0.16f, h * 0.3f).withCentre (P (0.7f, 0.5f)), 1.5f);
                break;
            default:                 // output: speaker
                p.startNewSubPath (P (0.1f, 0.38f)); p.lineTo (P (0.3f, 0.38f)); p.lineTo (P (0.55f, 0.15f));
                p.lineTo (P (0.55f, 0.85f)); p.lineTo (P (0.3f, 0.62f)); p.lineTo (P (0.1f, 0.62f)); p.closeSubPath();
                p.addCentredArc (x + 0.6f * w, y + 0.5f * h, 0.22f * w, 0.22f * h, 0.0f, 0.6f, 2.54f, true);
                break;
        }

        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
