#pragma once

#include "DetailPanels.h"
#include "../DSP/ReverbDepth.h"

class VelvoxAudioProcessor;

// The 0.6 Reverb panel (spec 5.12, "Reverb 3 · Raumtiefe"): a room drawn in perspective with the vocal as a
// glowing dot. Dragging the dot back adds space - Mix, Pre-Delay and Size move together along the curated
// path of dsp::reverbDepthPath; manual edits of the three move the dot (dsp::reverbDepthFromSettings).
// Main row: Room (dropdown), Size, Decay, Mix (SEND while a send); everything else in Mehr.
namespace velvox::ui
{
    //==========================================================================
    // Depth operations (message thread). `instance` 0..1. Each complete operation is ONE undo step
    // and one host gesture group (mix, pre-delay, size).
    namespace reverbedit
    {
        // The depth the current Mix / Pre-Delay / Size stand for (0 = in front, 1 = far back)
        float depth (VelvoxAudioProcessor& proc, int instance);
        // The path point of the instance's current room type
        dsp::ReverbDepthPoint pointFor (VelvoxAudioProcessor& proc, int instance, float depth);

        // A drag: begin once, write as often as needed, end once (= one step)
        void beginDepthGesture (VelvoxAudioProcessor& proc, int instance);
        void writeDepth (VelvoxAudioProcessor& proc, int instance, float depth);
        void endDepthGesture (VelvoxAudioProcessor& proc, int instance);

        // Complete gestures
        void setDepth (VelvoxAudioProcessor& proc, int instance, float depth);
        void resetDepth (VelvoxAudioProcessor& proc, int instance);   // the three parameters' defaults
    }

    std::unique_ptr<DetailPanel> createReverbPanel (VelvoxAudioProcessor& proc);
}
