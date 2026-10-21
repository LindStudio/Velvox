#include "HelpOverlay.h"

namespace velvox::ui
{
    namespace
    {
        constexpr int cardW = 1080, cardH = 692;
        constexpr int headerH = 72;
        constexpr int navW = 256;
        constexpr int rowH = 30, groupH = 34;
        constexpr int pagePad = space::xxl;

        void styleViewport (juce::Viewport& v)
        {
            v.setScrollBarsShown (true, false);
            v.setScrollBarThickness (8);
            v.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, colours::accent.withAlpha (0.16f));
            v.getVerticalScrollBar().setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
        }
    }

    //==========================================================================
    std::vector<HelpOverlay::Nav::Row> HelpOverlay::Nav::rows() const
    {
        std::vector<Row> out;
        juce::String group;
        int y = space::s;
        for (const auto& p : help::pages())
        {
            if (p.group != group)
            {
                group = p.group;
                out.push_back ({ true, group, {}, { 0, y, getWidth(), groupH } });
                y += groupH;
            }
            out.push_back ({ false, p.title, p.key, { 0, y, getWidth(), rowH } });
            y += rowH;
        }
        return out;
    }

    int HelpOverlay::Nav::preferredHeight() const
    {
        const auto r = rows();
        return r.empty() ? 0 : r.back().bounds.getBottom() + space::l;
    }

    juce::Rectangle<int> HelpOverlay::Nav::rowFor (const juce::String& key) const
    {
        for (const auto& r : rows())
            if (! r.header && r.key == key)
                return r.bounds;
        return {};
    }

    void HelpOverlay::Nav::paint (juce::Graphics& g)
    {
        for (const auto& r : rows())
        {
            const auto b = r.bounds.toFloat();
            if (r.header)
            {
                drawCaption (g, r.text, b.reduced ((float) space::l, 0.0f).withTrimmedTop (10.0f), juce::Justification::centredLeft,
                             colours::textFaint, typesize::caption);
                continue;
            }

            const bool selected = r.key == owner.currentKey;
            const bool hovered = r.key == hoveredKey;
            const auto pill = b.reduced ((float) space::s, 2.0f);
            if (selected || hovered)
            {
                g.setColour (colours::accent.withAlpha (selected ? 0.06f : 0.03f));
                g.fillRoundedRectangle (pill, radius::control);
            }
            if (selected)
            {
                g.setGradientFill (auroraGradient (pill.getTopLeft(), pill.getBottomLeft()));
                g.fillRoundedRectangle (pill.getX(), pill.getY() + 6.0f, 2.5f, pill.getHeight() - 12.0f, 1.25f);
            }
            g.setColour (selected ? colours::text : (hovered ? colours::text.withAlpha (0.85f) : colours::textDim));
            g.setFont (medium (typesize::body));
            g.drawText (r.text, pill.withTrimmedLeft (14.0f), juce::Justification::centredLeft, true);
        }
    }

    void HelpOverlay::Nav::mouseDown (const juce::MouseEvent& e)
    {
        for (const auto& r : rows())
            if (! r.header && r.bounds.contains (e.getPosition()))
                owner.showPage (r.key);
    }

    void HelpOverlay::Nav::mouseMove (const juce::MouseEvent& e)
    {
        juce::String key;
        for (const auto& r : rows())
            if (! r.header && r.bounds.contains (e.getPosition()))
                key = r.key;
        if (key != hoveredKey)
        {
            hoveredKey = key;
            setMouseCursor (key.isNotEmpty() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void HelpOverlay::Nav::mouseExit (const juce::MouseEvent&)
    {
        hoveredKey.clear();
        repaint();
    }

    //==========================================================================
    void HelpOverlay::PageView::setPage (const help::Page* p, int width)
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::topLeft);
        s.setLineSpacing (3.0f);
        s.setWordWrap (juce::AttributedString::byWord);

        if (p != nullptr)
        {
            s.append (p->title + "\n", display (typesize::hero, true), colours::text);
            s.append ("\n", font (8.0f), colours::text);

            for (const auto& rawLine : juce::StringArray::fromLines (p->body))
            {
                const auto line = rawLine.trimEnd();
                if (line.startsWith ("## "))
                {
                    s.append ("\n" + line.substring (3) + "\n", font (typesize::lead + 1.0f, true), colours::text);
                    s.append ("\n", font (4.0f), colours::text);
                }
                else if (line.startsWith ("- "))
                {
                    s.append (juce::String::fromUTF8 ("\xe2\x80\xa2   ") + line.substring (2) + "\n", font (typesize::lead),
                              colours::text.withAlpha (0.78f));
                }
                else if (line.isEmpty())
                {
                    s.append ("\n", font (6.0f), colours::text);
                }
                else
                {
                    s.append (line + "\n", font (typesize::lead), colours::text.withAlpha (0.78f));
                }
            }
        }

        const int textW = std::max (100, width - 2 * pagePad);
        layout.createLayout (s, (float) textW);
        setSize (width, (int) std::ceil (layout.getHeight()) + 2 * pagePad);
        repaint();
    }

    void HelpOverlay::PageView::paint (juce::Graphics& g)
    {
        layout.draw (g, getLocalBounds().reduced (pagePad).toFloat());
    }

    //==========================================================================
    HelpOverlay::HelpOverlay()
    {
        setWantsKeyboardFocus (true);
        setVisible (false);

        navViewport.setViewedComponent (&nav, false);
        pageViewport.setViewedComponent (&page, false);
        styleViewport (navViewport);
        styleViewport (pageViewport);
        addAndMakeVisible (navViewport);
        addAndMakeVisible (pageViewport);

        addAndMakeVisible (closeButton);
        addAndMakeVisible (tutorialButton);
        closeButton.setTooltip ("Close the help (Esc)");
        closeButton.onClick = [this] { close(); };
        tutorialButton.setTooltip ("Walk through the interface step by step");
        tutorialButton.onClick = [this]
        {
            close();
            if (onStartTutorial)
                onStartTutorial();
        };
    }

    juce::Rectangle<int> HelpOverlay::cardBounds() const
    {
        return getLocalBounds().withSizeKeepingCentre (std::min (cardW, getWidth() - 2 * space::xl),
                                                       std::min (cardH, getHeight() - 2 * space::xl));
    }

    void HelpOverlay::open (const juce::String& key)
    {
        showPage (help::find (key) != nullptr ? key : help::pages().front().key);
        if (! isVisible())
        {
            setAlpha (0.0f);
            setVisible (true);
            juce::Desktop::getInstance().getAnimator().fadeIn (this, 180);
        }
        toFront (true);
        if (isShowing())
            grabKeyboardFocus();
    }

    void HelpOverlay::close()
    {
        juce::Desktop::getInstance().getAnimator().cancelAnimation (this, false);
        setVisible (false);
    }

    void HelpOverlay::showPage (const juce::String& key)
    {
        currentKey = key;
        page.setPage (help::find (key), pageViewport.getMaximumVisibleWidth() > 0 ? pageViewport.getMaximumVisibleWidth()
                                                                                  : pageViewport.getWidth() - 8);
        pageViewport.setViewPosition (0, 0);

        // Keep the selected topic visible in the list
        const auto row = nav.rowFor (key);
        if (! row.isEmpty() && ! navViewport.getViewArea().contains (row))
            navViewport.setViewPosition (0, std::max (0, row.getY() - navViewport.getHeight() / 3));
        nav.repaint();
    }

    void HelpOverlay::paint (juce::Graphics& g)
    {
        g.fillAll (colours::scrim);

        const auto card = cardBounds().toFloat();
        drawGlow (g, { card.getX() + 160.0f, card.getY() }, 420.0f, 0.06f, colours::teal);
        drawGlow (g, { card.getRight() - 200.0f, card.getY() }, 420.0f, 0.05f, colours::blue);
        g.setColour (colours::panel);
        g.fillRoundedRectangle (card, radius::panel);
        drawGlass (g, card, radius::panel, 0.02f);

        auto header = cardBounds().removeFromTop (headerH).reduced (space::xl, 0);
        g.setColour (colours::text);
        g.setFont (font (typesize::title + 1.0f, true));
        g.drawText ("Help", header.removeFromTop (headerH / 2 + 8).withTrimmedTop (14), juce::Justification::bottomLeft, false);
        g.setColour (colours::textDim);
        g.setFont (font (typesize::body));
        g.drawText ("How Headliner works, every module explained. Works completely offline.", header.withTrimmedBottom (12),
                    juce::Justification::centredLeft, true);

        // Hairlines: under the header and between navigation and page
        const auto body = cardBounds().withTrimmedTop (headerH);
        g.setColour (colours::line);
        g.fillRect (juce::Rectangle<float> ((float) body.getX(), (float) body.getY(), (float) body.getWidth(), 1.0f));
        g.fillRect (juce::Rectangle<float> ((float) body.getX() + navW, (float) body.getY(), 1.0f, (float) body.getHeight()));
    }

    void HelpOverlay::resized()
    {
        const auto card = cardBounds();
        closeButton.setBounds (card.getRight() - space::xl - 84, card.getY() + (headerH - 32) / 2, 84, 32);

        auto body = card.withTrimmedTop (headerH + 1);
        auto left = body.removeFromLeft (navW);
        tutorialButton.setBounds (left.removeFromBottom (60).reduced (space::l, space::m));
        navViewport.setBounds (left);
        nav.setSize (navW - 10, nav.preferredHeight());

        pageViewport.setBounds (body.withTrimmedLeft (1).reduced (0, 2));
        if (currentKey.isNotEmpty())
            showPage (currentKey);
    }

    bool HelpOverlay::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            close();
            return true;
        }
        return false;
    }

    void HelpOverlay::mouseDown (const juce::MouseEvent& e)
    {
        if (! cardBounds().contains (e.getPosition()))
            close();
    }
}
