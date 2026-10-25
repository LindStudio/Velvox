#pragma once

#include "DetailPanels.h"
#include "AuditionButton.h"
#include "../Parameters/EqBands.h"

class VelvoxAudioProcessor;

// The 0.6 EQ panel (spec 5.1, "EQ 1", Pro-Q style): a large curve over the live spectrum with up to 16
// user points, the selected band's values under it, Mix and "Alte EQ-Kurve – umstellen" in Mehr.
namespace velvox::ui
{
    //==========================================================================
    // Band operations of the panel (message thread). Every function that changes something is ONE undo
    // step (proc.getUndo()) and sends host gestures. `instance` 0..3, `band` 0..15.
    namespace eqedit
    {
        struct Shown
        {
            dsp::EqBand band;         // what the DSP processes (effective settings, macro share included)
            bool used = false;        // not a free slot
            bool macroOnly = false;   // created by a macro in the effective settings, no parameters yet
            float gainOffset = 0.0f;  // effective gain - parameter gain (the macro share)
        };
        using ShownBands = std::array<Shown, dsp::maxEqBands>;

        ShownBands shownBands (VelvoxAudioProcessor& proc, int instance);
        dsp::EqBand parameterBand (VelvoxAudioProcessor& proc, int instance, int band);
        juce::String paramId (int instance, int band, const char* field);

        // A band a macro created only in the effective settings is written as parameters (gain = 0 dB:
        // the macro keeps adding its share, the sound does not change). No undo step of its own: call it
        // inside the step / gesture of the edit. Returns true when it wrote something.
        bool materialise (VelvoxAudioProcessor& proc, int instance, int band);

        // New Bell (Q 1, on, role none) in a free slot; returns it, or -1 when all 16 are in use (no step)
        int addBand (VelvoxAudioProcessor& proc, int instance, float freqHz, float gainDb);
        // Delete = all 7 band parameters back to their defaults (also role bands: a macro recreates its band)
        void deleteBand (VelvoxAudioProcessor& proc, int instance, int band);
        void setType (VelvoxAudioProcessor& proc, int instance, int band, dsp::EqBandType type);
        void setSlope (VelvoxAudioProcessor& proc, int instance, int band, int slopeIndex);
        void toggleOn (VelvoxAudioProcessor& proc, int instance, int band);
        // Alt+click: gain and Q back to their defaults (0 dB, Q 1; cuts: 24 dB/oct)
        void resetBand (VelvoxAudioProcessor& proc, int instance, int band);

        // Short names for menus, the hover card and the role tag
        juce::String typeName (int type);
        juce::String roleTag (int role);   // "" for user bands
        bool hasGain (int type) noexcept;  // bell, shelves, tilt
        bool hasQ (int type) noexcept;     // bell, shelves, notch, band pass
        juce::Colour bandColour (int band);
    }

    //==========================================================================
    // The graph: spectrum (before faint, after filled), each band's curve faint in its colour, the sum
    // curve bright in the EQ colour, the band points. Gestures (spec 2.6):
    //   double-click empty space = new Bell there · drag a point = freq / gain (Shift / Ctrl: fine) ·
    //   wheel over a point = Q (cuts: slope) · double-click a point = on / off · Alt+click a point = reset
    //   gain / Q · right-click a point = type, slope, on / off, delete · Delete key = delete the selected
    //   band · hover a point 600 ms = card with its values.
    class EqGraph final : public ModuleView, private juce::Timer
    {
    public:
        EqGraph (VelvoxAudioProcessor& p, const SpectrumSource* spectrum, int instance);
        ~EqGraph() override;

        void update (const LiveData&) override;
        void refreshCurves();   // recomputes the curves when the settings changed (update() calls it)
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;

        // ---- Geometry (also for tests)
        static constexpr float rangeDb = 18.0f;          // shown gain range +-18 dB
        juce::Rectangle<float> plotArea() const noexcept;
        float xForFreq (float hz) const noexcept;
        float freqForX (float x) const noexcept;
        float yForGain (float db) const noexcept;
        float gainForY (float y) const noexcept;
        juce::Point<float> pointFor (int band) const;   // where the band's point is drawn
        int bandAt (juce::Point<float> p) const;        // -1: no point there

        // ---- Selection
        int getSelected() const noexcept { return selected; }
        void setSelected (int band);
        std::function<void (int band)> onSelect;

        // ---- Right-click menu of a point (built and performed here, tests call performMenu)
        enum MenuId { menuTypeBase = 100, menuSlopeBase = 200, menuToggle = 300, menuDelete = 301 };
        juce::PopupMenu buildPointMenu (int band);
        void performMenu (int band, int menuId);

        bool isHoverCardShown() const noexcept { return cardBand >= 0; }
        int getHoverBand() const noexcept { return hovered; }
        const eqedit::ShownBands& getShown() const noexcept { return shown; }

        // ---- Band solo (0.6.1): the headphone hot spot beside the hovered (or soloed) point. Click =
        //      solo on / off, press longer than AuditionPress::holdMs = only while held. A monitoring
        //      state in the DSP (EqualizerModule::setAuditionBand), ended when the graph goes away.
        int getAuditionBand() const;            // -1: none
        void setAuditionBand (int band);        // -1: off
        juce::Rectangle<float> auditionIconArea (int band) const;
        bool isAuditionIconShown (int band) const;
        std::function<juce::uint32()> clock = [] { return juce::Time::getMillisecondCounter(); };

    private:
        int auditionIconAt (juce::Point<float> p) const;   // band whose shown hot spot is at p, -1: none
        void timerCallback() override;
        void endWheelGesture();
        void beginDrag (int band);
        void endDrag();
        float responseAt (int band, float hz) const;

        VelvoxAudioProcessor& proc;
        const int instance;
        SpectrumView spectrumView;

        eqedit::ShownBands shown {};
        bool eqOn = true;
        float mix = 1.0f;
        dsp::EqSettings lastSettings;
        bool haveCurves = false;
        juce::Rectangle<int> lastSize;

        static constexpr int numPoints = 200;
        std::array<float, numPoints> freqs {};
        std::array<std::array<float, numPoints>, dsp::maxEqBands> bandDb {};
        std::array<float, numPoints> sumDb {};
        std::array<juce::Path, dsp::maxEqBands> bandStroke, bandFill;
        juce::Path sumStroke, sumFill;

        int selected = -1, hovered = -1, dragged = -1, wheelBand = -1, cardBand = -1;
        juce::uint32 hoverSince = 0;
        // Drag state: start values, so Shift / Ctrl fine is relative and never jumps
        juce::Point<float> dragLast;
        float dragFreq = 1000.0f, dragGain = 0.0f, dragOffset = 0.0f;
        bool materialised = true;
        float wheelAccum = 0.0f;   // cut slope steps from the wheel
        // Hover card text, built in update() (no allocation while painting)
        juce::String cardTitle, cardValues;
        float cardWidth = 0.0f;
        // Band solo
        AuditionPress auditionPress;
        int auditionPressBand = -1;   // hot spot being pressed
        int shownAudition = -1;       // last state read from the DSP (repaints when it changes)
    };

    std::unique_ptr<ModuleView> createEqGraph (VelvoxAudioProcessor& p, const SpectrumSource* spectrum, int instance);

    //==========================================================================
    // The panel: graph as the visual; main row = the selected band (On, Type, Freq, Gain, Q - Slope for
    // cuts); Mehr: Mix and "Alte EQ-Kurve – umstellen" (only while eqLegacyResponse is set).
    class EqPanel final : public DetailPanel
    {
    public:
        explicit EqPanel (VelvoxAudioProcessor& p);
        ~EqPanel() override;

        void refresh() override;
        void resized() override;
        void paintOverChildren (juce::Graphics&) override;

        void selectBand (int band);   // -1: none
        int getSelectedBand() const noexcept { return selectedBand; }
        EqGraph* getGraph() noexcept;
        juce::Button& getLegacyResponseButton() noexcept { return *legacyResponse; }
        // Headphone of the selected band (0.6.1 band solo); hidden while no band is selected
        AuditionButton& getAuditionButton() noexcept { return soloButton; }

        // One slot of the selected-band row; holds the control bound to the band's parameter
        class Slot;

    protected:
        void layoutVisual (juce::Rectangle<int> area) override;

    private:
        void bindSlots();
        void connectGraph();
        void layoutBandRow();

        std::array<Slot*, 5> slots {};
        juce::TextButton* legacyResponse = nullptr;
        void syncAudition();
        juce::Label noBand;
        AuditionButton soloButton;
        juce::Rectangle<int> badgeArea;
        juce::String badgeText, badgeTag;   // built when the selection changes, not in paint
        int selectedBand = -1, boundBand = -2, boundType = -1;
        bool graphConnected = false, lastLegacyShown = false;
        inline static std::array<int, 4> lastSelection { -1, -1, -1, -1 };
    };

    std::unique_ptr<DetailPanel> createEqPanel (VelvoxAudioProcessor& p);
}
