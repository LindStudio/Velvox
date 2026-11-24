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

    // Vertical gain-reduction meter (dB from the top) with a numeric read-out ("ABSENKUNG" / "REDUCTION")
    class ReductionMeter final : public juce::Component
    {
    public:
        // From the editor's timer; the peak holds for a second and then falls 4.5 dB/s (by the clock, not per frame)
        void setValue (float db);
        void paint (juce::Graphics&) override;
        static constexpr float maxDb = 12.0f;
    private:
        float value = 0.0f, peakHold = 0.0f, holdLeft = 0.0f;
        double lastUpdate = 0.0;
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
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        static constexpr float minDb = thresholdMinDb, maxDb = thresholdMaxDb;
    private:
        juce::Rectangle<float> barArea() const;
        float yFor (float db) const;
        float dbFor (float y) const;
        bool nearHandle (juce::Point<float>) const;   // only the threshold line is grabbed (a click elsewhere does nothing)
        EmberAudioProcessor& processor;
        float level = -100.0f, hold = -100.0f, weight = 0.0f;
        double lastUpdate = 0.0;
        float lastDragY = 0.0f, dragDb = 0.0f;   // the value follows unrounded while dragging
        bool dragging = false, hover = false;
    };

    // ADVANCED display: input / output spectrum (zoomed to minHz..maxHz, where the frequency is set), the S range
    // (drag its edge = frequency), reduction history
    class BandDisplay final : public juce::Component
    {
    public:
        BandDisplay (EmberAudioProcessor&, vocalypse::ui::SpectrumSource&);
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void pushReduction (float db);
        void setHasSignal (bool);   // no audio for a while: a hint instead of an empty black box
        void refresh() { spectrum.refresh(); repaint(); }
        float xForHz (float hz) const;
        float hzForX (float x) const;
        bool nearHandle (juce::Point<float>) const;   // only the edge is grabbed (a click elsewhere does nothing)
        static constexpr float minHz = 1000.0f, maxHz = 20000.0f;

    private:
        EmberAudioProcessor& processor;
        vocalypse::ui::SpectrumView spectrum;
        std::array<float, 240> history {};
        int head = 0;
        float lastDragX = 0.0f, dragHz = 0.0f;
        bool dragging = false, hasSignal = true;
    };

    class EmberPanel;

    // EMBER's own look on top of the shared one (the shared VOCALYPSE look stays as it is): the "fire" action button
    // (AUTO) burns pink like the seal, and everything that opens on top of the plugin is in its style (2026-11-24,
    // Jakob: "die Popups sind noch im blauen Stil von damals"): menus, dialogs, tooltips, the info card, text fields,
    // scroll bars, the spectrum's light. No blue, teal or violet anywhere.
    class EmberLookAndFeel final : public vocalypse::ui::VocalypseLookAndFeel
    {
    public:
        EmberLookAndFeel();
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                                   bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

        // menus: a dark rounded panel with a thin pink edge, a pink highlight, pink ticks
        juce::Font getPopupMenuFont() override;
        void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
        void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
        void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                const juce::String& shortcutKeyText, const juce::Drawable* icon, const juce::Colour* textColour) override;
        void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                        int& idealWidth, int& idealHeight) override;
        int getPopupMenuBorderSize() override { return 6; }

        // dialogs (rename / delete preset, licence, messages)
        void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
        juce::Font getAlertWindowTitleFont() override;
        juce::Font getAlertWindowMessageFont() override;
        juce::Font getAlertWindowFont() override;
        int getAlertWindowButtonHeight() override { return 32; }

        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
        void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
        void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    };

    // While an EMBER window is open, its look is also the default one of this plugin binary: windows that open on
    // their own (a message box without a parent, a menu) then never fall back to JUCE's grey / blue default.
    // Each editor holds one; the last to close restores JUCE's default.
    struct EmberDefaultLook
    {
        EmberDefaultLook() { juce::LookAndFeel::setDefaultLookAndFeel (&look); }
        ~EmberDefaultLook()
        {
            if (&juce::LookAndFeel::getDefaultLookAndFeel() == &look)
                juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        }
        EmberLookAndFeel look;
    };

    // The window's part of the seal, on top of the controls (clicks pass through): runes running round the edge,
    // a wave of pink light through the panels when Ember swallows an S, a small seal on the Amount knob that turns
    // with the knob only (2026-11-24). Repaints only those strips, so the controls underneath are not redrawn every frame.
    class SealOverlay final : public juce::Component
    {
    public:
        SealOverlay() { setInterceptsMouseClicks (false, false); }
        void update (const seal::Scene& scene, bool simple, juce::Rectangle<float> knob, std::vector<juce::Rectangle<float>> plates);
        void paint (juce::Graphics&) override;
        std::function<float()> knobAngle;   // the Amount knob's shown angle (clockwise from 12 o'clock), read when painting
    private:
        void repaintEdges (juce::Rectangle<float>, float width);
        seal::Scene scene;
        seal::RuneAtlas borderRunes { seal::pink, true }, sealRunes { seal::pinkLight, true };
        bool simple = true;
        int ticks = 0;
        float paintedLit = -1.0f;      // calm (animation reduced / off): the border is repainted only when the light changes
        float paintedKnobLit = -1.0f, paintedAngle = -100.0f;   // the knob's seal: only when its light or the knob changes
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
        void setScalePercent (int percent);                  // ≡ > Size (75 - 200 %); remembered like a dragged size
        int getScalePercent() const;
        bool keyPressed (const juce::KeyPress&) override;     // undo / redo also when the editor itself has the focus

    private:
        void rebuildPanel();
        static void applyLanguage (const juce::String& preference);

        EmberAudioProcessor& processor;
        juce::SharedResourcePointer<EmberDefaultLook> defaultLook;   // before lnf: outlives the panel's popups
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
        SealOverlay& getSealOverlay() noexcept { return sealOverlay; }   // EmberTests --frames
        bool keyPressed (const juce::KeyPress&) override;   // Ctrl+Z undo, Ctrl+Shift+Z / Ctrl+Y redo
        // The preset menu and the ≡ menu, the preset dialogs (public for EmberTests --gui-shots)
       #define EMBER_MENU_BUILDERS 1
        juce::PopupMenu buildPresetMenu();
        juce::PopupMenu buildMainMenu();
        void showPresetMenu();
        void showMainMenu();
        void renamePreset (const juce::File&);
        void deletePreset (const juce::File&);

    private:
        class AboutOverlay;
        void timerCallback() override;
        void layout();
        void autoClicked();
        void setVoice (int voice);
        void refreshButtons();
        void showPresetInfo();   // the loaded factory preset's one-line purpose in the status line
        void savePresetAs();
        void showNotice (const juce::String&);   // a short note in the status line (a few seconds)
        void warn (const juce::String& title, const juce::String& message);
        void addInfo (juce::Component&, const juce::String& title, const juce::String& text);

        EmberAudioProcessor& processor;
        UserSettings& settings;
        std::function<void (juce::String)> onLanguage;
        vocalypse::ui::SpectrumSource spectrumSource;

        // AUTO: while listening it stays "cancel" and shows the progress as a bar along its bottom edge
        struct AutoButton final : juce::TextButton
        {
            float progress = -1.0f;   // < 0: no bar
            void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        };
        juce::TextButton simpleBtn, advancedBtn, bypassBtn;
        AutoButton autoBtn;
        juce::ShapeButton menuBtn { "menu", juce::Colours::white, juce::Colours::white, juce::Colours::white };
        juce::ShapeButton prevBtn { "prev", juce::Colours::white, juce::Colours::white, juce::Colours::white };
        juce::ShapeButton nextBtn { "next", juce::Colours::white, juce::Colours::white, juce::Colours::white };
        juce::TextButton presetBtn, saveBtn, slotA, slotB, slotCopy;

        LiveMascotView mascot;      // Ember as a live rig (2026-11-17); MascotView (frames) stays for its tests until step 6
        SealOverlay sealOverlay;
        juce::Image chamber;        // the back wall, drawn once per size
        juce::Point<int> chamberWanted;     // while resizing: the size it should have, since when (drawn when it stays)
        double chamberWantedSince = 0;
        double sealClock = 0.0, lastTickS = 0.0;
        std::vector<juce::Rectangle<float>> plates() const;
        ReductionMeter meter, advancedMeter;
        BandDisplay display;
        DetectorMeter detector;

        // SIMPLE
        vocalypse::ui::Knob amountBig;
        juce::TextButton voiceLow, voiceMid, voiceHigh, listenBtn;
        juce::Label voiceLbl;
        juce::Label status;

        // ADVANCED
        vocalypse::ui::Knob freqK, threshK, rangeK, amountK, attackK, releaseK, lookK, mixK, outK;
        vocalypse::ui::Knob widthK, linkK;   // 2026-11-23 (fix/ember-dsp): split band width, stereo link
        vocalypse::ui::Segments modeSeg, detectSeg, listenSeg, stereoSeg;
        juce::Label modeLbl, detectLbl, listenLbl, stereoLbl;

        vocalypse::ui::InfoPopup infoPopup;
        std::unique_ptr<AboutOverlay> about;
        vocalypse::ui::LicenceLock licenceLock;   // on top of everything while locked
        std::vector<std::unique_ptr<vocalypse::ui::HoverInfo>> infos;
        std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;
        std::unique_ptr<juce::FileChooser> chooser;
        std::unique_ptr<juce::AlertWindow> dialog;   // rename preset (owned here: it uses the editor's look)
        juce::String notice;
        double noticeUntil = 0.0;
        bool advanced = false;
        EmberAudioProcessor::AutoPhase lastPhase = EmberAudioProcessor::AutoPhase::idle;
        double messageUntil = 0.0, lastSignal = -1.0e9;
        float shownGr = 0.0f;
    };
}
