#pragma once

#include "DetailPanels.h"

class VelvoxAudioProcessor;

// The 0.6 Pitch panel (spec 5.10, "Pitch 2 · zwei große Regler"): a big semitone control (snaps, +-24 st, a tick
// per semitone around it) and a big Formant control side by side in the visual, between them the note that is
// sung and the note that comes out. Main row: Fine, Mix, Formanten erhalten.
namespace velvox::ui
{
    namespace pitchnotes
    {
        juce::String noteName (float hz);   // "A4" (C4 = middle C); "–" for <= 0
        float centsOff (float hz);          // deviation from the nearest equal-tempered note, -50..+50
    }

    // The visual: background for the two big knobs (the panel places them), the semitone tick ring and the
    // live read-out IN note -> OUT note
    class PitchStage final : public ModuleView
    {
    public:
        explicit PitchStage (VelvoxAudioProcessor& proc);

        static constexpr double holdSeconds = 0.25;   // the last voiced note stays this long

        void update (const LiveData&) override;
        void paint (juce::Graphics&) override;

        // Where the panel put the semitone dial (stage coordinates): the tick ring goes around it
        void setDialGeometry (juce::Point<float> centre, float radius);
        juce::Point<float> getDialCentre() const noexcept { return dialCentre; }
        // Space between the two knobs for the read-out (stage coordinates)
        void setReadoutArea (juce::Rectangle<float> r) { readoutArea = r; repaint(); }

        const juce::String& getInputText() const noexcept { return inText; }
        const juce::String& getOutputText() const noexcept { return outText; }
        bool isVoiced() const noexcept { return voiced; }

    private:
        VelvoxAudioProcessor& proc;
        bool on = false, voiced = false;
        float semitones = 0.0f, cents = 0.0f, mix = 1.0f, inHz = 0.0f, outHz = 0.0f;
        double lastVoiced = -10.0;
        juce::String inText, outText, inDetail, outDetail;
        juce::Point<float> dialCentre;
        float dialRadius = 0.0f;
        juce::Rectangle<float> readoutArea;
    };

    std::unique_ptr<ModuleView> createPitchStage (VelvoxAudioProcessor& proc);
    std::unique_ptr<DetailPanel> createPitchPanel (VelvoxAudioProcessor& proc);
}
