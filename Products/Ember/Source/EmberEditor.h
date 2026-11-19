#pragma once

#include "EmberProcessor.h"
#include "EmberMascot.h"
#include "EmberLiveMascot.h"
#include "EmberPresets.h"
#include "UI/VocalypseLookAndFeel.h"
#include "UI/Widgets.h"
#include "UI/Segments.h"
#include "UI/Spectrum.h"
#include "UI/LicenceUI.h"

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
        // pink seal (Jakob 2026-11-19; was Glutrot 0xffff3b1f / glow 0xffffb21f)
        inline const juce::Colour accent { 0xffff3a6e };
        inline const juce::Colour accentSoft { 0xffff7aa0 };
        inline const juce::Colour glow { 0xffffbed7 };
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

    // EMBER's own look on top of the shared one: the "fire" action button (AUTO) burns pink like the seal
    class EmberLookAndFeel final : public vocalypse::ui::VocalypseLookAndFeel
    {
    public:
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                                   bool highlighted, bool down) override;
    };

    // The window's part of the seal, on top of the controls (clicks pass through): runes running round the edge,
    // a wave of pink light through the panels when Ember swallows an S, a small seal turning on the Amount knob.
    // Repaints only those strips, so the controls underneath are not redrawn every frame.
    class SealOverlay final : public juce::Component
    {
    public:
        SealOverlay() { setInterceptsMouseClicks (false, false); }
        void update (const seal::Scene& scene, bool simple, juce::Rectangle<float> knob, std::vector<juce::Rectangle<float>> plates);
        void paint (juce::Graphics&) override;
    private:
        void repaintEdges (juce::Rectangle<float>, float width);
        seal::Scene scene;
        seal::RuneAtlas borderRunes { seal::pink }, sealRunes { seal::pinkLight };
        bool simple = true;
        int ticks = 0;
        juce::Rectangle<float> knob;
        std::vector<juce::Rectangle<float>> plates;
    };

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
        EmberLookAndFeel lnf;
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
        LiveMascotView& getMascot() noexcept { return mascot; }

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

        LiveMascotView mascot;      // Ember as a live rig (2026-11-17); MascotView (frames) stays for its tests until step 6
        SealOverlay sealOverlay;
        juce::Image chamber;        // the back wall, drawn once per size
        double sealClock = 0.0, lastTickS = 0.0;
        std::vector<juce::Rectangle<float>> plates() const;
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
        vocalypse::ui::LicenceLock licenceLock;   // on top of everything while locked
        std::vector<std::unique_ptr<vocalypse::ui::HoverInfo>> infos;
        std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;
        std::unique_ptr<juce::FileChooser> chooser;
        bool advanced = false;
        EmberAudioProcessor::AutoPhase lastPhase = EmberAudioProcessor::AutoPhase::idle;
        double messageUntil = 0.0;
        float shownGr = 0.0f;
    };
}
