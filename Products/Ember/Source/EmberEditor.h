#pragma once

#include "EmberProcessor.h"
#include "EmberMascot.h"
#include "UI/VocalypseLookAndFeel.h"
#include "UI/Widgets.h"
#include "UI/Segments.h"
#include "UI/Spectrum.h"

// EMBER De-Esser editor. Same controls, knobs and buttons as VOCALYPSE; only the colour (Glutrot) and
// the mascot are Ember's own. SIMPLE: Ember large, Amount, voice, Delta, AUTO. ADVANCED: spectrum with
// the draggable S band, every parameter, Ember small.
namespace ember
{
    namespace look
    {
        inline const juce::Colour accent { 0xffff3b1f };      // Glutrot (plan 2026-11-10)
        inline const juce::Colour accentSoft { 0xffff7a4a };
        inline const juce::Colour glow { 0xffffb21f };
        inline constexpr int baseW = 900, baseH = 560;
    }

    // Vertical gain-reduction meter (dB from the top)
    class ReductionMeter final : public juce::Component
    {
    public:
        void setValue (float db) { value = db; repaint(); }
        void paint (juce::Graphics&) override;
        static constexpr float maxDb = 18.0f;
    private:
        float value = 0.0f, peakHold = 0.0f;
        int holdTicks = 0;
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
        void mouseMove (const juce::MouseEvent&) override;
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

    class EmberEditor final : public juce::AudioProcessorEditor, private juce::Timer
    {
    public:
        explicit EmberEditor (EmberAudioProcessor&);
        ~EmberEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // Tests / snapshots
        bool isAdvanced() const noexcept { return advanced; }
        void setAdvanced (bool);
        MascotView& getMascot() noexcept { return mascot; }
        void tickForTest() { timerCallback(); }

    private:
        class Content;
        void timerCallback() override;
        void layoutContent();
        void autoClicked();
        void setVoice (float hz);
        void refreshButtons();

        EmberAudioProcessor& processor;
        vocalypse::ui::VocalypseLookAndFeel lnf;
        vocalypse::ui::SpectrumSource spectrumSource;
        std::unique_ptr<juce::Component> content;

        juce::TextButton simpleBtn, advancedBtn, autoBtn, bypassBtn;
        MascotView mascot;
        ReductionMeter meter;
        BandDisplay display;

        // SIMPLE
        vocalypse::ui::Knob amountBig;
        juce::TextButton voiceLow, voiceMid, voiceHigh, listenBtn;
        juce::Label status;

        // ADVANCED
        vocalypse::ui::Knob freqK, threshK, rangeK, amountK, attackK, releaseK, lookK, mixK, outK;
        vocalypse::ui::Segments modeSeg, detectSeg, listenSeg;
        juce::Label modeLbl, detectLbl, listenLbl;

        std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;
        bool advanced = false;
        EmberAudioProcessor::AutoPhase lastPhase = EmberAudioProcessor::AutoPhase::idle;
        double messageUntil = 0.0;
        float shownGr = 0.0f;
    };
}
