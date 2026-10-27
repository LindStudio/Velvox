#pragma once

#include "Widgets.h"
#include "HelpContent.h"
#include "../Text/I18n.h"

namespace velvox::ui
{
    // Offline help: navigation on the left (general topics, then one page per
    // module), the selected page on the right. Lives inside the editor canvas
    // like the other dialogs, so it never opens a separate OS window.
    class HelpOverlay final : public juce::Component
    {
    public:
        HelpOverlay();

        // Opens (or switches to) a page; unknown keys fall back to the first page
        void open (const juce::String& pageKey);
        void close();
        bool isOpen() const noexcept { return isVisible(); }
        juce::String getCurrentKey() const { return currentKey; }

        std::function<void()> onStartTutorial;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        // Topic list grouped like help::pages()
        class Nav final : public juce::Component
        {
        public:
            explicit Nav (HelpOverlay& o) : owner (o) {}
            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent&) override;
            void mouseMove (const juce::MouseEvent&) override;
            void mouseExit (const juce::MouseEvent&) override;
            int preferredHeight() const;
            juce::Rectangle<int> rowFor (const juce::String& key) const;

        private:
            struct Row { bool header; juce::String text, key; juce::Rectangle<int> bounds; };
            std::vector<Row> rows() const;
            HelpOverlay& owner;
            juce::String hoveredKey;
        };

        // The page text, laid out once per page / width
        class PageView final : public juce::Component
        {
        public:
            void setPage (const help::Page* page, int width);
            void paint (juce::Graphics&) override;

        private:
            juce::TextLayout layout;
        };

        juce::Rectangle<int> cardBounds() const;
        void showPage (const juce::String& key);

        juce::String currentKey;
        Nav nav { *this };
        PageView page;
        juce::Viewport navViewport, pageViewport;
        juce::TextButton closeButton, tutorialButton;
    };
}
