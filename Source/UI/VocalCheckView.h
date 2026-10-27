#pragma once

#include "Widgets.h"
#include "Segments.h"
#include "../Check/CheckTypes.h"
#include "../Check/CheckTargets.h"
#include "../Text/I18n.h"

class VelvoxAudioProcessor;

// VOCAL CHECK tab of Headliner 0.6 (spec 2.11 / 5.15, the user's pick "Check 1 · score + tiles"):
//
//   [score ring]  ⏻ VOCAL CHECK                                   GENRE  Party | Pop | Akustik
//                 Laufend · 12,4 s Stimme                         [ Alles anwenden (3) ]
//                 Laufend | Capture   Start   Reset
//   ─────────────────────────────────────────────────────────────────────────────────────────
//   ▌ Mulm um 300 Hz (+4 dB)                                         [Ignorieren] [Anwenden]
//   ▌ → EQ −3 dB bei 300 Hz
//   ...
//   2 ignoriert ▸                                                    (Alle wiederherstellen)
//
// Everything comes from VelvoxAudioProcessor::getVocalCheck() (the check engine's controller): the
// texts of a tile are the finding's `problem` and `fix`, "Anwenden" shows only where canApply(), its
// tooltip is the preview of what it writes. "Anwenden" / "Alles anwenden" are one undo step each
// (the controller writes through the global UndoHistory). Message thread only.
namespace velvox::ui
{
    juce::Colour checkScoreColour (int score);              // -1 = no score
    juce::Colour checkSeverityColour (check::Severity level);
    // What a fix writes, one line per parameter ("EQ Band 1 Gain: −3.0 dB") plus "EQ einfügen" for modules it inserts
    juce::String checkPreviewText (VelvoxAudioProcessor& proc, const check::ResolvedFix& fix);
    // Score ring (track + arc in the score colour + number); also used by the module panel's summary
    void drawScoreRing (juce::Graphics& g, juce::Rectangle<float> area, int score, float thickness, bool withCaption);

    // Pill button of the check view; painted here, tooltip computed on demand
    class CheckButton : public juce::Button
    {
    public:
        // filled = the action buttons (Anwenden / Alles anwenden / Einschalten): the normal button of the
        // design with brighter text, deliberately not louder than the findings
        enum class Style { filled, ghost, segment, link };
        CheckButton (const juce::String& text, Style style);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        juce::String getTooltip() override { return tooltipFn ? tooltipFn() : juce::Button::getTooltip(); }
        std::function<juce::String()> tooltipFn;
        Style style;
        juce::Colour accent;   // the Vocal Check colour
        int chevron = 0;       // link style: 0 none, 1 right (collapsed), 2 down (expanded)
    };

    class VocalCheckView final : public juce::Component
    {
    public:
        // One finding: a tile in the list, or a compact row in the expanded "x ignoriert" section
        class Tile final : public juce::Component, public juce::SettableTooltipClient
        {
        public:
            Tile (const juce::String& id, bool ignoredRow);
            void paint (juce::Graphics&) override;
            void resized() override;

            const juce::String& getId() const noexcept { return id; }
            bool isIgnoredRow() const noexcept { return ignoredRow; }
            bool isApplied() const noexcept { return applied; }
            const juce::String& getProblemText() const noexcept { return problem; }
            const juce::String& getFixText() const noexcept { return fix; }
            juce::Rectangle<int> getTextArea() const noexcept { return textArea; }
            CheckButton& getApplyButton() noexcept { return applyButton; }
            CheckButton& getIgnoreButton() noexcept { return ignoreButton; }
            CheckButton& getRestoreButton() noexcept { return restoreButton; }

            // Content; returns true when something visible changed
            bool set (const juce::String& problem, const juce::String& fix, juce::Colour edge,
                      bool canApply, bool applied, const juce::String& details);

            static constexpr int height = 56, ignoredHeight = 38;

        private:
            juce::String id, problem, fix, details;
            juce::Colour edge;
            bool ignoredRow, canApply = false, applied = false;
            juce::Rectangle<int> textArea;
            CheckButton applyButton { i18n::tr ("Anwenden", "Apply"), CheckButton::Style::filled };
            CheckButton ignoreButton { i18n::tr ("Ignorieren", "Ignore"), CheckButton::Style::ghost };
            CheckButton restoreButton { i18n::tr ("Wiederherstellen", "Restore"), CheckButton::Style::ghost };
            friend class VocalCheckView;
        };

        explicit VocalCheckView (VelvoxAudioProcessor& proc);
        ~VocalCheckView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // Called by the editor's frame timer: header at once, list at most every 6th call and only on change
        void refresh();
        // Rebuild now (after a click, tests)
        void refreshNow();
        void setAnimationsEnabled (bool shouldAnimate) noexcept { animate = shouldAnimate; }

        // ---- State (tests, snapshot renderer)
        int getNumTiles() const noexcept { return (int) tiles.size(); }
        Tile* getTileAt (int index) const noexcept;
        Tile* getTile (const juce::String& id) const noexcept;
        int getNumIgnoredRows() const noexcept { return (int) ignoredRows.size(); }
        Tile* getIgnoredRow (const juce::String& id) const noexcept;
        bool isIgnoredExpanded() const noexcept { return ignoredExpanded; }
        void setIgnoredExpanded (bool expanded);
        juce::String getScoreText() const;
        const juce::String& getStatusText() const noexcept { return status; }
        CheckButton& getApplyAllButton() noexcept { return applyAll; }
        CheckButton& getIgnoredToggle() noexcept { return ignoredToggle; }
        CheckButton& getRestoreAllButton() noexcept { return restoreAll; }
        CheckButton& getEnableButton() noexcept { return enable; }
        CheckButton& getGenreButton (check::Genre genre) noexcept { return *genres[(size_t) juce::jlimit (0, 2, (int) genre)]; }

    private:
        class List;
        enum class Empty { none, off, collecting, clean };
        void updateHeader();
        void rebuildList();
        void layoutList();
        void afterClick();
        juce::String listKey() const;

        VelvoxAudioProcessor& proc;
        PowerButton power;
        Segments mode;
        CheckButton startStop { "Start", CheckButton::Style::ghost }, reset { "Reset", CheckButton::Style::ghost };
        std::array<std::unique_ptr<CheckButton>, 3> genres;
        CheckButton applyAll { i18n::tr ("Alles anwenden", "Apply all"), CheckButton::Style::filled };
        CheckButton ignoredToggle { i18n::tr ("0 ignoriert", "0 ignored"), CheckButton::Style::link };
        CheckButton restoreAll { i18n::tr ("Alle wiederherstellen", "Restore all"), CheckButton::Style::link };
        CheckButton enable { i18n::tr ("Einschalten", "Turn on"), CheckButton::Style::filled };

        std::unique_ptr<List> list;
        juce::Viewport viewport;
        std::vector<std::unique_ptr<Tile>> tiles, ignoredRows;

        int score = -1;
        juce::String status;
        Empty empty = Empty::none;
        float collected = 0.0f;   // voice seconds while collecting
        bool ignoredExpanded = false, animate = true;
        juce::String lastKey;
        int tick = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalCheckView)
    };

    // Compact VOCAL CHECK summary for the Check module panel in the chain: small ring, the number of
    // findings and a link to the VOCAL CHECK tab (where they are shown and applied)
    class CheckSummary final : public juce::Component
    {
    public:
        explicit CheckSummary (VelvoxAudioProcessor& proc);
        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();

        std::function<void()> onOpenTab;
        juce::String getScoreText() const;
        const juce::String& getCountText() const noexcept { return count; }
        CheckButton& getOpenButton() noexcept { return open; }

    private:
        VelvoxAudioProcessor& proc;
        int score = -1;
        juce::String count;
        CheckButton open { i18n::tr ("Zum VOCAL CHECK \xe2\x86\x92", "Open VOCAL CHECK \xe2\x86\x92"), CheckButton::Style::ghost };
    };
}
