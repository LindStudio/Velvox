#pragma once

#include "MacroBake.h"
#include <array>

namespace velvox
{
    // 0.6.1 (feedback item 2): the Simple sliders SET the modules. The six macro parameters hold the
    // sliders' positions (0..100 %) and never reach the audio path; every change of a position is
    // turned into module-parameter writes here (message thread), so the Advanced panels show what is
    // heard and a module switched off stays silent.
    //
    //  - The move is relative (dsp::applyMacroMove) and ANCHORED: computed from the module state where
    //    the move of this slider began, as long as nobody else changed a parameter it writes. Back at
    //    the start position the modules are restored exactly.
    //  - A move while the slider's gesture is open (Simple / auto-mix view) writes host gestures, which
    //    the UndoHistory folds into the slider's step; host automation of a macro writes plain values
    //    (no undo step, no touch on the host's other lanes).
    //  - SPACE: a delay / reverb it switches on that a fresh chain holds as an unused insert on the true
    //    law becomes a send at that moment (spec Q2, the 0.6 rule).
    //  - The dry of additive inserts is kept like the 0.6.0 macros kept it (macrobake::keepDry).
    //  - Changes of the positions by loading, undo / redo, transactions (presets, auto-mix, Vocal Check)
    //    or the auto-mix compare are taken as positions only: nothing is mapped.
    class MacroMapper
    {
    public:
        struct Hooks
        {
            std::function<dsp::ChainSettings()> readSettings;          // the module parameters now
            std::function<dsp::Routing()> getRouting;
            std::function<void (const dsp::Routing&)> setRouting;
            std::function<dsp::MixLaws()> getLaws;
            std::function<void (const dsp::MixLaws&)> setLaws;
            std::function<bool()> isApplyingState;                     // load, undo / redo, transaction, compare
        };

        MacroMapper (juce::AudioProcessorValueTreeState& state, Hooks hooks);

        // Message thread: maps every position that changed since the last call
        void update();
        // The current values become the positions; nothing is mapped (after a state load)
        void syncPositions();

        // From the processor's gesture listener (message thread)
        void gestureBegan (int parameterIndex);
        void gestureEnded (int parameterIndex);

        static const std::array<const char*, dsp::numMacros>& paramIds();
        static bool isMacroParam (const juce::String& id);
        bool isMacroIndex (int parameterIndex) const;

    private:
        void move (dsp::MacroId id, float from, float to);
        std::vector<float> captureWritten() const;

        juce::AudioProcessorValueTreeState& apvts;
        Hooks hooks;
        std::array<float, dsp::numMacros> positions {};
        std::array<std::atomic<float>*, dsp::numMacros> raw {};
        std::vector<juce::RangedAudioParameter*> written;   // macrobake::touchedParamIds()
        std::vector<int> macroIndices;
        int openGestures = 0;
        bool busy = false;

        struct Anchor
        {
            bool valid = false;
            dsp::MacroId id = dsp::MacroId::presence;
            float from = 0.0f;
            dsp::ChainSettings base;
            dsp::MixLaws baseLaws;
            std::vector<float> written;
            juce::String routing, laws;
        } anchor;

        JUCE_DECLARE_NON_COPYABLE (MacroMapper)
    };
}
