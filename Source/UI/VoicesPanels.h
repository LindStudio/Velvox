#pragma once

#include <optional>
#include "DetailPanels.h"
#include "../DSP/DoublerModule.h"

class VelvoxAudioProcessor;

// The voice panel of 0.6 (spec 5.8) on the common frame (Choir was removed in 0.7):
//   Doubler - "like Waves Doubler": voice mode Classic / 2 / 4, a stage strip with every voice as a dot
//             (x = pan, y = gain, draggable; live wobble from the engine) and one compact row of value
//             fields per voice (on, delay, detune, gain, pan)
// The views are public so the tests can drive them like a user.
namespace velvox::ui
{
    // Visual / panel for the doubler; nullptr for every other module
    std::unique_ptr<ModuleView> createVoicesModuleView (VelvoxAudioProcessor& p, dsp::ModuleId id, int instance);
    std::unique_ptr<DetailPanel> createVoicesModulePanel (VelvoxAudioProcessor& p, dsp::ModuleId id);

    //==========================================================================
    // Doubler voice edits (message thread). `instance` 0..1, `voice` 0..3. Every complete operation is ONE
    // undo step. Classic mode has no per-voice parameters: its two voices follow Delay / Detune / Width.
    // Editing a voice there first copies the Classic voices into V1 / V2 (delays D and 1.37 D, 0 ct, 0 dB,
    // pans -100 / +100 %, Mod Depth = the Classic Detune) and switches to "2" - in the same step, so the
    // sound only changes by the edited value.
    namespace doubleredit
    {
        enum class Field { on, delay, detune, gain, pan };

        // What a voice is set to, in parameter units (pan in %)
        struct VoiceValues
        {
            bool on = true;
            float delayMs = 14.0f, detuneCents = 0.0f, gainDb = 0.0f, panPercent = 0.0f;
        };

        int mode (VelvoxAudioProcessor&, int instance);         // 0 Classic, 1 "2", 2 "4"
        int voiceCount (VelvoxAudioProcessor&, int instance);   // voices of the mode: 2 or 4
        VoiceValues voice (VelvoxAudioProcessor&, int instance, int voice);   // Classic: the linked values
        float value (const VoiceValues&, Field) noexcept;
        juce::String paramId (int instance, int voice, Field);
        juce::RangedAudioParameter* param (VelvoxAudioProcessor&, int instance, int voice, Field);
        juce::String valueText (Field, float value);              // "14.0 ms", "+5.0 ct", "-3.0 dB", "L50" / "C" / "R100"
        // A typed value in real units (units accepted; pan also "L50" / "C" / "R30"); nullopt = no value
        std::optional<float> parseValue (VelvoxAudioProcessor&, int instance, int voice, Field, const juce::String& text);

        // Copies the Classic voices into V1 / V2 and switches to "2" (call inside a transaction)
        void leaveClassic (VelvoxAudioProcessor&, int instance);

        // An edit of one voice: begin once (transaction + host gestures on `fields`, Classic -> 2),
        // set as often as needed, end with the destructor (= one undo step)
        class Edit
        {
        public:
            Edit (VelvoxAudioProcessor&, int instance, int voice, std::initializer_list<Field> fields, const juce::String& name);
            ~Edit();
            void set (Field, float realValue);

        private:
            VelvoxAudioProcessor& proc;
            const int instance, voiceIndex;
            std::vector<juce::RangedAudioParameter*> gestures;
            JUCE_DECLARE_NON_COPYABLE (Edit)
        };

        // Complete operations
        void setValue (VelvoxAudioProcessor&, int instance, int voice, Field, float realValue);
        void toggleOn (VelvoxAudioProcessor&, int instance, int voice);
        void resetVoice (VelvoxAudioProcessor&, int instance, int voice);      // all five values
        void resetPosition (VelvoxAudioProcessor&, int instance, int voice);   // pan + gain
    }

    //==========================================================================
    // DOUBLER: the stage strip (lead in the centre at the Dry level, every voice at its pan (x) and gain (y),
    // Width shown as the band the voices are heard in, the live wobble as a faint ghost) and under it one
    // row of value fields per voice.
    // Voice gestures: drag (x = pan, y = gain; Shift / Ctrl fine 1/10) · double-click or Alt+click = pan and
    // gain default · wheel = detune · right-click = on/off, reset voice + panel items · hover 600 ms = card.
    class DoublerStage final : public ModuleView
    {
    public:
        DoublerStage (VelvoxAudioProcessor& p, int instance);
        ~DoublerStage() override;

        static constexpr float wheelCents = 1.0f;   // detune per wheel notch (Shift / Ctrl: 1/10)

        void update (const LiveData&) override;
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        // ---- State (also for tests)
        void readParameters();                                  // what update() does with the parameters
        int numShownVoices() const noexcept { return shownVoices; }
        bool isClassicShown() const noexcept { return classic; }
        juce::Point<float> voicePoint (int voice) const noexcept;
        juce::Point<float> leadPoint() const noexcept;
        juce::Rectangle<float> getPlotArea() const noexcept { return plot; }
        float xForPan (float panPercent) const noexcept;
        float yForGain (float gainDb) const noexcept;
        int voiceAt (juce::Point<float>) const noexcept;
        const doubleredit::VoiceValues& getVoice (int v) const noexcept { return values[(size_t) juce::jlimit (0, 3, v)]; }
        juce::Component& getVoiceHandle (int v) noexcept;
        juce::Component& getRow (int v) noexcept;
        juce::Component* getField (int v, doubleredit::Field f) noexcept;
        juce::String cardValue (int voice) const;               // "14.0 ms · +0.0 ct · 0.0 dB · L100"
        juce::PopupMenu buildMenu (int voice);
        bool isDragging() const noexcept { return edit != nullptr; }
        float getLivePan (int v) const noexcept { return livePan[(size_t) juce::jlimit (0, 3, v)]; }

        static juce::Colour voiceColour (int voice) noexcept;

    private:
        class Handle;
        class ValueField;
        class Row;
        void layoutRows();
        void endDrag();

        VelvoxAudioProcessor& proc;
        const int instance;
        std::array<std::unique_ptr<Handle>, 4> handles;
        std::array<std::unique_ptr<Row>, 4> rows;

        // Parameter state
        std::array<doubleredit::VoiceValues, 4> values {};
        int shownVoices = 0;   // set by the first readParameters()
        bool classic = true, on = false;
        float width = 0.8f, dryDb = 0.0f;

        // Live (engine feed)
        std::array<float, 4> livePan {}, liveCents {}, liveDelay {}, liveLevel {};
        float leadLevel = 0.0f;
        double lastPaint = -1.0;

        // Drag
        int hovered = -1, dragVoice = -1;
        bool ignoreDrag = false;
        juce::Point<float> lastPos;
        float dragPan = 0.0f, dragGain = 0.0f;
        std::unique_ptr<doubleredit::Edit> edit;

        juce::Rectangle<float> plot;
        juce::Rectangle<int> rowsArea;
        juce::String readout, widthText { "80 %" };
        std::array<juce::String, 4> rowCaptions;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DoublerStage)
    };
}
