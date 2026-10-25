#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>

// Shared value handling of every control (spec 2.6): typed values with units, reset, copy / paste,
// and the right-click menu. Knob, HSlider and Segments all go through here, so every control of the
// plugin behaves the same and every change is one gesture (= one undo step, recorded by the host).
namespace velvox::ui
{
    // Parses what a user types into a value field, in REAL units of the parameter, clamped to its range.
    //   numbers with "." or ",", optional sign; suffix k (x1000); units dB, Hz / kHz, ms / s (converted
    //   to the parameter's unit), %, st, ct, x, ":1"; "off" = bottom of the range (Hz filters);
    //   bools: on/off, an/aus, ein, true/false, 1/0; choices: by name ("1/8", "Plate"), case-insensitive.
    // Returns nullopt for text that is no value (the control then keeps its value).
    std::optional<float> parseValueText (const juce::String& text, const juce::RangedAudioParameter& parameter);

    namespace gestures
    {
        // Implemented by the module panel: adds "Modul zurücksetzen", "Modul-Preset speichern ..." to the
        // right-click menu of every control inside it
        struct PanelMenuHost
        {
            virtual ~PanelMenuHost() = default;
            virtual void addPanelItems (juce::PopupMenu& menu) = 0;
        };

        // Complete gestures (begin / set / end)
        void setReal (juce::RangedAudioParameter&, float realValue);
        void resetToDefault (juce::RangedAudioParameter&);
        void stepBy (juce::RangedAudioParameter&, int steps);   // choices / bools: next / previous value

        // Values are copied as text with their unit ("-40.0 dB"), so they paste into any parameter that
        // understands it. Also mirrored to the system clipboard.
        bool copyValue (juce::RangedAudioParameter&);
        bool pasteValue (juce::RangedAudioParameter&);
        bool canPaste (const juce::RangedAudioParameter&);

        // Right-click menu of a control: Standardwert, Wert eingeben ..., Wert kopieren, Wert einfügen
        // (+ the panel's items). `enterValue` opens the control's value field.
        juce::PopupMenu buildMenu (juce::RangedAudioParameter&, PanelMenuHost* panel, std::function<void()> enterValue);
        void showMenu (juce::Component& control, juce::RangedAudioParameter&, std::function<void()> enterValue);
    }
}
