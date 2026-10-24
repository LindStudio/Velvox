#pragma once

#include <juce_graphics/juce_graphics.h>

//==============================================================================
// HEADLINER VOCAL – Design Tokens (1:1 aus dem Design-Canvas übernommen)
//==============================================================================
namespace hv
{
    namespace colours
    {
        // Flächen
        inline const juce::Colour windowBg    { 0xff05060a };   // Plugin-Hintergrund
        inline const juce::Colour panel       { 0xff07080d };   // Spalten, Modul-Panels
        inline const juce::Colour panelInset  { 0xff040509 };   // Visualizer-Boxen
        inline const juce::Colour control     { 0xff0d0f15 };   // Hintergrund Segment-Buttons
        inline const juce::Colour border      { 0xff161925 };   // Panel-Rahmen
        inline const juce::Colour borderSoft  { 0xff13151e };   // Trennlinien

        // Regler
        inline const juce::Colour tickOff     { 0xff30343d };   // inaktive Striche im Kranz
        inline const juce::Colour knobTop     { 0xff34373f };   // Knopf-Körper oben
        inline const juce::Colour knobBottom  { 0xff121418 };   // Knopf-Körper unten
        inline const juce::Colour capTop      { 0xff17191d };   // Kappe oben (vertieft)
        inline const juce::Colour capBottom   { 0xff2a2d34 };   // Kappe unten
        inline const juce::Colour switchOff   { 0xff2a3040 };

        // Text
        inline const juce::Colour textStrong  { 0xfff1f5f9 };
        inline const juce::Colour text        { 0xffe8ebf2 };
        inline const juce::Colour textSoft    { 0xffc9ced9 };
        inline const juce::Colour textMuted   { 0xff8a93a6 };
        inline const juce::Colour textDim     { 0xff5b6478 };
        inline const juce::Colour textFaint   { 0xff3a4052 };

        // Modul-Farben (Verlauf Cyan -> Blau -> Violett -> Pink)
        inline const juce::Colour levelRide   { 0xff00d1ff };
        inline const juce::Colour smooth      { 0xff22b8ff };
        inline const juce::Colour vocalSmash  { 0xff3b82f6 };
        inline const juce::Colour choir       { 0xff6366ff };
        inline const juce::Colour pulse       { 0xff8b5cf6 };
        inline const juce::Colour stutter     { 0xffa855f7 };
        inline const juce::Colour delay       { 0xffd946ef };
        inline const juce::Colour reverb      { 0xfff472b6 };

        // Vocal-Control-Makros
        inline const juce::Colour macroPresence { 0xff00d1ff };
        inline const juce::Colour macroSpace    { 0xff3b82f6 };
        inline const juce::Colour macroWidth    { 0xff6366ff };
        inline const juce::Colour macroPunch    { 0xff8b5cf6 };
        inline const juce::Colour macroWarmth   { 0xffd946ef };
        inline const juce::Colour macroLevel    { 0xfff472b6 };
    }

    enum class ModuleType { LevelRide, Smooth, VocalSmash, Choir, Pulse, Stutter, Delay, Reverb };

    inline juce::Colour moduleColour (ModuleType t)
    {
        switch (t)
        {
            case ModuleType::LevelRide:  return colours::levelRide;
            case ModuleType::Smooth:     return colours::smooth;
            case ModuleType::VocalSmash: return colours::vocalSmash;
            case ModuleType::Choir:      return colours::choir;
            case ModuleType::Pulse:      return colours::pulse;
            case ModuleType::Stutter:    return colours::stutter;
            case ModuleType::Delay:      return colours::delay;
            case ModuleType::Reverb:     return colours::reverb;
        }
        return colours::levelRide;
    }

    inline juce::String moduleName (ModuleType t)
    {
        switch (t)
        {
            case ModuleType::LevelRide:  return "LEVEL RIDE";
            case ModuleType::Smooth:     return "SMOOTH";
            case ModuleType::VocalSmash: return "VOCAL SMASH";
            case ModuleType::Choir:      return "CHOIR";
            case ModuleType::Pulse:      return "PULSE";
            case ModuleType::Stutter:    return "STUTTER";
            case ModuleType::Delay:      return "DELAY";
            case ModuleType::Reverb:     return "REVERB";
        }
        return {};
    }
}
