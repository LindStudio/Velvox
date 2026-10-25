#pragma once

#include "DetailPanels.h"
#include "AuditionButton.h"

namespace velvox::ui
{
    // "Link" of the Saturation bands (satLink, panel behaviour - the DSP never reads it): while Link is
    // on and the user edits one band's Drive (any control: a gesture is open on it), the other two
    // bands follow by the same amount, relative to where they were when the gesture began and
    // limited at the range ends (dsp::linkedBandDrives). Their changes run inside gestures of their
    // own, which the undo history folds into the user's step. Host automation (no gesture) and the
    // single-band Drive never link. One object per panel, for its instance; message thread only.
    class SatLink final : private juce::AudioProcessorParameter::Listener
    {
    public:
        SatLink (APVTS& state, int instance);
        ~SatLink() override;

    private:
        void parameterValueChanged (int parameterIndex, float newValue) override;
        void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override;
        int bandOf (int parameterIndex) const noexcept;

        std::array<juce::RangedAudioParameter*, 3> drives {};
        juce::RangedAudioParameter* link = nullptr;
        std::array<float, 3> start {};
        int driving = -1;      // band whose user gesture is open (-1: none)
        bool writing = false;  // our own begin / set / end calls

        JUCE_DECLARE_NON_COPYABLE (SatLink)
    };

    // The character set of the Saturation module (0 Warm, 1 Tape, 2 Tube, 3 Hard): colour and icon
    namespace satlook
    {
        juce::Colour characterColour (int character);
        juce::String characterName (int character);
        // Icon stroked into `area` (cached paths, no allocation)
        void drawCharacterIcon (juce::Graphics& g, int character, juce::Rectangle<float> area, juce::Colour colour, float thickness = 1.6f);
        // One character tile (the Segments tile painter of the panel)
        void drawCharacterTile (juce::Graphics& g, int character, juce::Rectangle<float> r, bool selected, bool hover);
    }

    // The Saturation visual (spec 5.6): a frequency strip 20 Hz - 20 kHz with the Low / Mid / High bands
    // (character colour + icon, drive, live drive meter, on/off switch) and the two crossover handles;
    // below it a small transfer curve and the harmonics H2..H8 of the selected band. With multiband off
    // the strip shows one band over the whole range (the single-band character and drive).
    //
    // Gestures (spec 2.6): crossover handle - drag = frequency (Shift / Ctrl fine), double-click or
    // Alt+click = default, wheel = 1/24 octave, right-click = value menu, hover = info card. Band -
    // click = select, vertical drag = its drive, double-click = default drive, wheel = 0.5 dB,
    // right-click = drive menu, hover = info card. Every edit is one gesture (one undo step).
    class SatView final : public ModuleView
    {
    public:
        SatView (VelvoxAudioProcessor& p, int instance);
        ~SatView() override;

        void update (const LiveData&) override;
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

        bool isMultiband() const noexcept { return multiband; }
        // Selected band 0 / 1 / 2 (Low / Mid / High); -1 while multiband is off
        int getSelectedBand() const noexcept { return multiband ? selected : -1; }
        int getChosenBand() const noexcept { return selected; }   // kept while multiband is off
        void setSelectedBand (int band);
        std::function<void (int band)> onSelectBand;   // the user picked a band (click or drag)

        // Geometry (local coordinates)
        juce::Rectangle<float> getStripArea() const noexcept { return strip; }
        juce::Rectangle<float> getBandBounds (int band) const;
        float xForFrequency (float hz) const noexcept;
        float frequencyForX (float x) const noexcept;

        // Child controls (for the panel and tests)
        juce::Component& getCrossoverHandle (int which);   // 0 = Low / Mid, 1 = Mid / High
        juce::Component& getBandArea (int band);
        Toggle& getBandSwitch (int band);
        // Band solo (0.6.1): headphone at the top right of each band, multiband only. Monitoring state in
        // the DSP (SaturationModule::setSoloBand), one band at a time, ended with the view / multiband off
        AuditionButton& getSoloButton (int band);

        class Handle;   // shared gesture code of the crossover handles and the band areas

    private:
        class Crossover;
        class BandArea;
        friend class Crossover;
        friend class BandArea;

        float raw (const juce::String& id) const;
        void layoutChildren();
        void rebuildCurve();
        int shownCharacter (int band) const noexcept;
        float shownDrive (int band) const noexcept;

        VelvoxAudioProcessor& proc;
        const int instance;
        juce::String multibandId, xoverIds[2], onIds[3], driveIds[3], charIds[3], singleDriveId, singleCharId, enabledId;
        std::array<std::unique_ptr<Crossover>, 2> crossovers;
        std::array<std::unique_ptr<BandArea>, 3> bands;
        std::array<std::unique_ptr<Toggle>, 3> switches;
        std::array<std::unique_ptr<AuditionButton>, 3> solos;
        int soloShown = -1;
        void syncSolo();

        dsp::SatSettings shown;   // effective settings (macros applied), from the live data
        bool haveLive = false, multiband = false, enabled = false;
        int selected = 1;
        float xover[2] { 250.0f, 4000.0f };
        std::array<float, 3> meterDb { -100.0f, -100.0f, -100.0f };   // band input + drive (dBFS), smoothed
        int frame = 0;

        juce::Rectangle<float> strip, curveBox, harmonicsBox;
        // Selected band's picture, rebuilt only when its character / drive change
        int curveCharacter = -1;
        float curveDrive = -1000.0f;
        juce::Path curvePath;
        std::array<float, 8> harmonics {};
        float thdPercent = 0.0f;
    };

    std::unique_ptr<ModuleView> createSatView (VelvoxAudioProcessor& p, int instance);

    // The Saturation panel (spec 5.6 "Sat 2" + Low / Mid / High): main row = character tiles of the selected
    // band, Multiband, Link; the big Drive of the selected band beside the strip; Mehr: Mix, Output and the
    // two crossovers. Multiband off: the single-band character and drive. Instance from
    // DetailPanel::constructionInstance (createDetailPanel sets it).
    std::unique_ptr<DetailPanel> createSatPanel (VelvoxAudioProcessor& p);
}
