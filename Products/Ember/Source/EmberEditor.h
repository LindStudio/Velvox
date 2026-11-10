#pragma once

#include "EmberProcessor.h"
#include "EmberMascot.h"
#include "EmberPresets.h"
#include "UI/VocalypseLookAndFeel.h"
#include "UI/Widgets.h"
#include "UI/Segments.h"
#include "UI/Spectrum.h"

// EMBER De-Esser editor. Same controls, knobs and buttons as VOCALYPSE; only the colour (Glutrot) and the
// mascot are Ember's own.
//   top:      brand · SIMPLE / ADVANCED · AUTO · BYPASS · menu
//   preset:   ‹ preset name › · SAVE · A | B | A→B
//   SIMPLE:   Ember large (click = AUTO), GR meter, Amount, voice, Delta
//   ADVANCED: spectrum with the draggable S band, S-level meter with the draggable threshold, every parameter
// Every control explains itself on hover (info card). The language (German / English / system) is a user setting;
// switching it rebuilds the panel.
namespace ember
{
    namespace look
    {
        inline const juce::Colour accent { 0xffff3b1f };      // Glutrot (plan 2026-11-10)
        inline const juce::Colour accentSoft { 0xffff7a4a };
        inline const juce::Colour glow { 0xffffb21f };
        inline constexpr int baseW = 900, baseH = 600;
    }

    // Vertical gain-reduction meter (dB from the top) with a numeric read-out
    class ReductionMeter final : public juce::Component
    {
    public:
        void setValue (float db) { value = db; repaint(); }
        void paint (juce::Graphics&) override;
        static constexpr float maxDb = 12.0f;
    private:
        float value = 0.0f, peakHold = 0.0f;
        int holdTicks = 0;
    };

    // ADVANCED: detector level of the S band with the threshold as a draggable line
    class DetectorMeter final : public juce::Component
    {
    public:
        explicit DetectorMeter (EmberAudioProcessor&);
        void setLevel (float db, float smartWeight);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        static constexpr float minDb = -60.0f, maxDb = 0.0f;
    private:
        juce::Rectangle<float> barArea() const;
        float yFor (float db) const;
        float dbFor (float y) const;
        EmberAudioProcessor& processor;
        float level = -100.0f, hold = -100.0f, weight = 0.0f;
        bool dragging = false;
    };

    // ADVANCED display: input / output spectrum, the S band (drag its edge = frequency), reduction history
    class BandDisplay final : public juce::Component
    {
    public:
        BandDisplay (EmberAudioProcessor&, vocalypse::ui::SpectrumSource&);
        void paint (juce::Graphics&) override;
        void resized() override { spectrum.setBounds (getLocalBounds()); }
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void pushReduction (float db);
        void refresh() { spectrum.refresh(); repaint(); }
        float xForHz (float hz) const;
        float hzForX (float x) const;

    private:
        EmberAudioProcessor& processor;
        vocalypse::ui::SpectrumView spectrum;
        std::array<float, 240> history {};
        int head = 0;
        bool dragging = false;
    };

    class EmberPanel;

    class EmberEditor final : public juce::AudioProcessorEditor
    {
    public:
        explicit EmberEditor (EmberAudioProcessor&);
        ~EmberEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // Tests / snapshots
        bool isAdvanced() const;
        void setAdvanced (bool);
        void tickForTest();
        EmberPanel& getPanel() noexcept { return *panel; }
        void setLanguage (const juce::String& preference);   // "system" / "de" / "en"; rebuilds the panel

    private:
        void rebuildPanel();
        static void applyLanguage (const juce::String& preference);

        EmberAudioProcessor& processor;
        vocalypse::ui::VocalypseLookAndFeel lnf;
        juce::SharedResourcePointer<UserSettings> settings;
        std::unique_ptr<EmberPanel> panel;
    };

    class EmberPanel final : public juce::Component, public vocalypse::ui::InfoPopupHost, private juce::Timer
    {
    public:
        EmberPanel (EmberAudioProcessor&, UserSettings&, std::function<void (juce::String)> onLanguage);
        ~EmberPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        void showInfo (juce::Component& anchor, const juce::String& title, const juce::String& value, const juce::String& text) override;
        void hideInfo (juce::Component& anchor) override;

        bool isAdvanced() const noexcept { return advanced; }
        void setAdvanced (bool);
        void tick() { timerCallback(); }
        MascotView& getMascot() noexcept { return mascot; }

    private:
        class AboutOverlay;
        void timerCallback() override;
        void layout();
        void autoClicked();
        void setVoice (float hz);
        void refreshButtons();
        void showPresetMenu();
        void savePresetAs();
        void showMainMenu();
        void addInfo (juce::Component&, const juce::String& title, const juce::String& text);

        EmberAudioProcessor& processor;
        UserSettings& settings;
        std::function<void (juce::String)> onLanguage;
        vocalypse::ui::SpectrumSource spectrumSource;

        juce::TextButton simpleBtn, advancedBtn, autoBtn, bypassBtn;
        juce::ShapeButton menuBtn { "menu", juce::Colours::white, juce::Colours::white, juce::Colours::white };
        juce::ShapeButton prevBtn { "prev", juce::Colours::white, juce::Colours::white, juce::Colours::white };
        juce::ShapeButton nextBtn { "next", juce::Colours::white, juce::Colours::white, juce::Colours::white };
        juce::TextButton presetBtn, saveBtn, slotA, slotB, slotCopy;

        MascotView mascot;
        ReductionMeter meter;
        BandDisplay display;
        DetectorMeter detector;

        // SIMPLE
        vocalypse::ui::Knob amountBig;
        juce::TextButton voiceLow, voiceMid, voiceHigh, listenBtn;
        juce::Label voiceLbl;
        juce::Label status;

        // ADVANCED
        vocalypse::ui::Knob freqK, threshK, rangeK, amountK, attackK, releaseK, lookK, mixK, outK;
        vocalypse::ui::Segments modeSeg, detectSeg, listenSeg;
        juce::Label modeLbl, detectLbl, listenLbl;

        vocalypse::ui::InfoPopup infoPopup;
        std::unique_ptr<AboutOverlay> about;
        std::vector<std::unique_ptr<vocalypse::ui::HoverInfo>> infos;
        std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;
        std::unique_ptr<juce::FileChooser> chooser;
        bool advanced = false;
        EmberAudioProcessor::AutoPhase lastPhase = EmberAudioProcessor::AutoPhase::idle;
        double messageUntil = 0.0;
        float shownGr = 0.0f;
    };
}
