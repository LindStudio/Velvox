#include "ChainView.h"
#include "../PluginProcessor.h"
#include "../StateKeys.h"
#include "UserSettings.h"
#include "Icons.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        constexpr float corner = radius::tile;
        constexpr float sendW = 184.0f;
        constexpr float arrowW = 36.0f;
        constexpr float edgeFade = 44.0f;
        constexpr int hitLeftArrow = -3, hitRightArrow = -4, hitOutput = -100, hitInput = -101, hitNothing = -2;

        bool canBeSend (ModuleId id) { return id == ModuleId::delay || id == ModuleId::reverb; }

        void drawPowerGlyph (juce::Graphics& g, juce::Rectangle<float> r, bool on, float hover)
        {
            const float size = std::min (r.getWidth(), r.getHeight());
            r = r.withSizeKeepingCentre (size, size);
            g.setColour (colours::background.withAlpha (0.9f));
            g.fillEllipse (r);
            g.setColour (on ? colours::cyan.withAlpha (0.65f) : colours::line.brighter (0.35f + 0.4f * hover));
            g.drawEllipse (r.reduced (0.6f), 1.2f);

            const auto c = r.getCentre();
            const float rad = size * 0.22f;
            juce::Path p;
            p.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
            p.startNewSubPath (c.x, c.y - rad * 1.3f);
            p.lineTo (c.x, c.y - rad * 0.2f);
            g.setColour (on ? colours::cyan : colours::textFaint.brighter (0.3f * hover));
            g.strokePath (p, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void drawChevron (juce::Graphics& g, juce::Point<float> c, bool pointsRight, juce::Colour colour)
        {
            juce::Path p;
            const float s = pointsRight ? 1.0f : -1.0f;
            p.startNewSubPath (c.x - 3.5f * s, c.y - 7.0f);
            p.lineTo (c.x + 3.5f * s, c.y);
            p.lineTo (c.x - 3.5f * s, c.y + 7.0f);
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    const char* ChainView::onParamFor (ModuleId id)
    {
        switch (id)
        {
            case ModuleId::eq:      return pid::eqOn;
            case ModuleId::comp:    return pid::compOn;
            case ModuleId::deess:   return pid::deessOn;
            case ModuleId::sat:     return pid::satOn;
            case ModuleId::doubler: return pid::doublerOn;
            case ModuleId::pitch:   return pid::pitchOn;
            case ModuleId::chop:    return pid::chopOn;
            case ModuleId::delay:   return pid::delayOn;
            case ModuleId::reverb:  return pid::reverbOn;
            case ModuleId::pulse:   return pid::pulseOn;
            case ModuleId::smash:   return pid::smashOn;
            case ModuleId::choir:   return pid::choirOn;
            case ModuleId::smooth:  return pid::smoothOn;
            case ModuleId::check:   return pid::checkOn;
            case ModuleId::ride:    return pid::rideOn;
        }
        return pid::eqOn;
    }

    ChainView::ChainView (VelvoxAudioProcessor& p) : proc (p)
    {
        routing = proc.getRouting();
        setTooltip ("Click a module to open it. Drag to change the order - the row scrolls along at the edges. "
                    "Drag Delay or Reverb onto the lower lane to run them in parallel. Right-click for more options.");

        addAndMakeVisible (modulesButton);
        modulesButton.setTooltip ("Every module, including hidden ones: open, switch on or off, show or hide - and save your layout");
        modulesButton.onClick = [this]
        {
            juce::Component::SafePointer<ChainView> safe (this);
            createModulesMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&modulesButton).withMinimumWidth (260),
                                               [safe] (int result) { if (safe != nullptr) safe->handleMenuResult (result); });
        };
        readHidden();
        updateModulesButton();
    }

    bool ChainView::isOn (ModuleId id) const
    {
        return proc.getState().getRawParameterValue (onParamFor (id))->load() > 0.5f;
    }

    //==========================================================================
    // Geometry

    juce::Rectangle<float> ChainView::rowArea() const
    {
        return getLocalBounds().toFloat().reduced (endW + gap, 0.0f).withTrimmedTop (2.0f).withHeight (cardH);
    }

    juce::Rectangle<float> ChainView::laneArea() const
    {
        const auto row = rowArea();
        return row.withY (row.getBottom() + laneOffset).withHeight (laneH);
    }

    juce::Rectangle<float> ChainView::leftArrowArea() const  { return rowArea().withWidth (arrowW); }
    juce::Rectangle<float> ChainView::rightArrowArea() const { return rowArea().withTrimmedLeft (rowArea().getWidth() - arrowW); }

    juce::Rectangle<float> ChainView::scrollTrack() const
    {
        const auto row = rowArea();
        return { row.getX(), row.getBottom() + 5.0f, row.getWidth(), 4.0f };
    }

    float ChainView::cardWidth (int n) const
    {
        const float vw = rowArea().getWidth();
        if (n <= 0)
            return minCardW;
        const float fit = (vw - gap * (float) (n - 1)) / (float) n;
        if (fit >= minCardW)
            return std::min (fit, maxCardW);
        // Not everything fits: as many whole cards as possible, never narrower than minCardW
        const int k = std::max (1, (int) std::floor ((vw + gap) / (minCardW + gap)));
        return (vw - gap * (float) (k - 1)) / (float) k;
    }

    float ChainView::contentWidth() const
    {
        return numSerial > 0 ? (float) numSerial * serialW + gap * (float) (numSerial - 1) : 0.0f;
    }

    float ChainView::getMaxScroll()
    {
        layoutTiles();
        return std::max (0.0f, contentWidth() - rowArea().getWidth());
    }

    juce::Rectangle<float> ChainView::scrollThumb()
    {
        const auto track = scrollTrack();
        const float content = std::max (1.0f, contentWidth());
        const float maxScroll = std::max (0.0f, content - rowArea().getWidth());
        const float w = std::max (48.0f, track.getWidth() * rowArea().getWidth() / content);
        const float x = maxScroll > 0.0f ? track.getX() + (track.getWidth() - w) * scroll.current / maxScroll : track.getX();
        return track.withX (x).withWidth (w);
    }

    void ChainView::layoutTiles()
    {
        tiles.clear();
        const auto row = rowArea();
        const auto lane = laneArea();

        std::vector<ModuleId> serial, sends;
        for (auto id : routing.order)
            if (! hidden[(size_t) id])
                (routing.isSend (id) ? sends : serial).push_back (id);

        numSerial = (int) serial.size();
        serialW = cardWidth (numSerial);
        const float maxScroll = std::max (0.0f, contentWidth() - row.getWidth());
        if (scroll.target > maxScroll || scroll.current > maxScroll)
        {
            scroll.target = std::min (scroll.target, maxScroll);
            scroll.current = std::min (scroll.current, maxScroll);
        }

        for (int i = 0; i < numSerial; ++i)
            tiles.push_back ({ serial[(size_t) i], { row.getX() + (float) i * (serialW + gap) - scroll.current, row.getY(), serialW, row.getHeight() },
                               false, i + 1 });

        // Sends sit on the right part of the lane, between the chain end and the output
        float x = lane.getRight() - sendW * (float) sends.size() - gap * (float) (sends.size() > 0 ? sends.size() - 1 : 0);
        for (auto id : sends)
        {
            tiles.push_back ({ id, { x, lane.getY(), sendW, lane.getHeight() }, true, 0 });
            x += sendW + gap;
        }

        inBox = { 0.0f, row.getY(), endW, row.getHeight() };
        outBox = { (float) getWidth() - endW, row.getY(), endW, row.getHeight() };
    }

    const ChainView::Tile* ChainView::findTile (ModuleId id) const
    {
        for (auto& t : tiles)
            if (t.id == id)
                return &t;
        return nullptr;
    }

    juce::Rectangle<float> ChainView::getTileBounds (ModuleId id)
    {
        layoutTiles();
        if (auto* t = findTile (id))
            return t->bounds;
        return {};
    }

    juce::Rectangle<float> ChainView::getPowerBounds (ModuleId id)
    {
        layoutTiles();
        if (auto* t = findTile (id))
            return dotBounds (*t);
        return {};
    }

    juce::Rectangle<float> ChainView::getModeBounds (ModuleId id)
    {
        layoutTiles();
        if (auto* t = findTile (id); t != nullptr && canBeSend (id))
            return modeBounds (*t);
        return {};
    }

    juce::Rectangle<float> ChainView::dotBounds (const Tile& t) const
    {
        if (t.inLane)
            return juce::Rectangle<float> (26.0f, 26.0f).withCentre ({ t.bounds.getX() + 22.0f, t.bounds.getCentreY() });
        return juce::Rectangle<float> (28.0f, 28.0f).withCentre ({ t.bounds.getX() + 26.0f, t.bounds.getY() + 26.0f });
    }

    juce::Rectangle<float> ChainView::modeBounds (const Tile& t) const
    {
        if (t.inLane)
            return { t.bounds.getRight() - 10.0f - 58.0f, t.bounds.getCentreY() - 12.0f, 58.0f, 24.0f };
        return { t.bounds.getRight() - 12.0f - 58.0f, t.bounds.getBottom() - 12.0f - 24.0f, 58.0f, 24.0f };
    }

    int ChainView::tileAt (juce::Point<float> p) const
    {
        const auto row = rowArea();
        const float maxScroll = std::max (0.0f, contentWidth() - row.getWidth());

        if (row.contains (p))
        {
            if (scroll.target > 0.5f && leftArrowArea().contains (p))
                return hitLeftArrow;
            if (scroll.target < maxScroll - 0.5f && rightArrowArea().contains (p))
                return hitRightArrow;
        }

        for (int i = 0; i < (int) tiles.size(); ++i)
        {
            const auto& t = tiles[(size_t) i];
            if (t.bounds.contains (p) && (t.inLane || row.contains (p)))
                return i;
        }
        if (outBox.contains (p))
            return hitOutput;
        if (inBox.contains (p))
            return hitInput;
        return hitNothing;
    }

    int ChainView::insertIndexForX (float x) const
    {
        // Position among the serial tiles (excluding the dragged one)
        int index = 0;
        for (auto& t : tiles)
        {
            if (t.inLane || (dragTile >= 0 && t.id == tiles[(size_t) dragTile].id))
                continue;
            if (x > t.bounds.getCentreX())
                ++index;
        }
        return index;
    }

    //==========================================================================
    // Scrolling

    void ChainView::scrollTo (float x, bool animate)
    {
        const float maxScroll = getMaxScroll();
        scroll.target = juce::jlimit (0.0f, maxScroll, x);
        if (! animate)
            scroll.snap (scroll.target);
        repaint();
    }

    void ChainView::scrollToShow (ModuleId id, bool animate)
    {
        layoutTiles();
        const auto* t = findTile (id);
        if (t == nullptr || t->inLane)
            return;

        // Content position of the card; keep a margin so it does not sit under the arrows
        const auto row = rowArea();
        const float x = t->bounds.getX() + scroll.current - row.getX();
        const float margin = getMaxScroll() > 0.0f ? arrowW : 0.0f;
        float target = scroll.target;
        if (x - margin < target)
            target = x - margin;
        else if (x + serialW + margin > target + row.getWidth())
            target = x + serialW + margin - row.getWidth();
        if (std::abs (target - scroll.target) > 0.5f)
            scrollTo (target, animate);
    }

    bool ChainView::isTileFullyVisible (ModuleId id)
    {
        const auto b = getTileBounds (id);
        return ! b.isEmpty() && rowArea().expanded (0.5f).contains (b);
    }

    void ChainView::setSelected (int id)
    {
        selected = id;
        if (id >= 0 && id < dsp::numModules)
        {
            if (getWidth() > 0)
                scrollToShow ((ModuleId) id, isShowing());   // glide when visible, jump otherwise
            else
                pendingReveal = true;
        }
        repaint();
    }

    void ChainView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (getMaxScroll() <= 0.0f)
        {
            Component::mouseWheelMove (e, wheel);
            return;
        }
        const float delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        scrollTo (scroll.target - delta * 420.0f * (wheel.isReversed ? -1.0f : 1.0f));
    }

    void ChainView::showHint (const juce::String& text)
    {
        hint = text;
        hintUntil = juce::Time::getMillisecondCounter() + 4000;
        repaint();
    }

    void ChainView::refresh()
    {
        const bool bypassedNow = proc.getState().getRawParameterValue (pid::bypass)->load() > 0.5f;
        if (bypassedNow != globalBypass)
        {
            globalBypass = bypassedNow;
            repaint();
        }

        bool changed = false;
        readHidden();

        const auto r = proc.getRouting();
        if (r != routing)
        {
            routing = r;
            changed = true;
        }

        auto& chain = proc.getChain();
        const auto engaged = dsp::modulesEngagedByMacros (proc.readSettings(), proc.readMacros());
        std::array<float, dsp::numModules> act {};
        // Gain reduction drives the activity bars of the cards (the editor pulls the meters once per frame)
        const float compGr = live != nullptr ? live->compGr.ago (0) : chain.getCompressor().gainReductionDb.peak.load();
        const float deessGr = live != nullptr ? live->deessGr.ago (0) : chain.getDeEsser().gainReductionDb.peak.load();
        act[(size_t) ModuleId::comp] = juce::jlimit (0.0f, 1.0f, compGr / 12.0f);
        act[(size_t) ModuleId::deess] = juce::jlimit (0.0f, 1.0f, deessGr / 8.0f);
        if (live != nullptr)
        {
            act[(size_t) ModuleId::delay] = juce::jlimit (0.0f, 1.0f, live->delayDuck.ago (0) / 12.0f);
            act[(size_t) ModuleId::reverb] = juce::jlimit (0.0f, 1.0f, live->reverbDuck.ago (0) / 12.0f);
            act[(size_t) ModuleId::smash] = juce::jlimit (0.0f, 1.0f, live->smashGr.ago (0) / 15.0f);
            act[(size_t) ModuleId::ride] = juce::jlimit (0.0f, 1.0f, std::abs (live->rideGain.ago (0)) / 8.0f);
            act[(size_t) ModuleId::smooth] = juce::jlimit (0.0f, 1.0f, chain.getSmooth().getMaxCutDb() / 8.0f);
            act[(size_t) ModuleId::pulse] = juce::jlimit (0.0f, 1.0f, 1.0f - chain.getPulse().displayGain.load());
        }

        for (int i = 0; i < dsp::numModules; ++i)
        {
            const auto id = (ModuleId) i;
            const bool on = isOn (id);
            const float a = activity[(size_t) i] * 0.75f + act[(size_t) i] * 0.25f;
            if (on != onState[(size_t) i] || engaged[(size_t) i] != macroEngaged[(size_t) i] || std::abs (a - activity[(size_t) i]) > 0.01f)
                changed = true;
            onState[(size_t) i] = on;
            macroEngaged[(size_t) i] = engaged[(size_t) i];
            activity[(size_t) i] = a;
        }

        if (changed)
            updateModulesButton();

        if (hint.isNotEmpty() && juce::Time::getMillisecondCounter() > hintUntil)
        {
            hint.clear();
            changed = true;
        }

        if (changed)
            repaint();
    }

    void ChainView::tick()
    {
        bool moved = false;

        if (pendingReveal && getWidth() > 0)
        {
            pendingReveal = false;
            if (selected >= 0 && selected < dsp::numModules)
                scrollToShow ((ModuleId) selected, false);
        }

        // Dragging a card close to an edge of the row scrolls it along
        if (dragging && dragTile >= 0 && ! laneArea().expanded (0.0f, 10.0f).contains (dragPos))
        {
            const auto row = rowArea();
            constexpr float zone = 64.0f, maxSpeed = 16.0f;
            float speed = 0.0f;
            if (dragPos.x < row.getX() + zone)
                speed = -maxSpeed * juce::jlimit (0.0f, 1.0f, (row.getX() + zone - dragPos.x) / zone);
            else if (dragPos.x > row.getRight() - zone)
                speed = maxSpeed * juce::jlimit (0.0f, 1.0f, (dragPos.x - (row.getRight() - zone)) / zone);
            if (speed != 0.0f)
            {
                const float before = scroll.current;
                scroll.snap (juce::jlimit (0.0f, getMaxScroll(), scroll.current + speed));
                moved = moved || scroll.current != before;
            }
        }

        moved = scroll.tick (0.25f) || moved;

        const int hoveredId = hovered >= 0 && hovered < (int) tiles.size() ? (int) tiles[(size_t) hovered].id : -1;
        for (int i = 0; i < dsp::numModules; ++i)
        {
            hoverGlow[(size_t) i].target = i == hoveredId ? 1.0f : 0.0f;
            moved = hoverGlow[(size_t) i].tick (0.18f) || moved;
        }

        if (moved)
            repaint();
    }

    //==========================================================================
    // Drawing

    void ChainView::drawPort (juce::Graphics& g, juce::Rectangle<float> box, bool isOutput) const
    {
        // Input and output are fixed ports of the signal path, not effect modules:
        // recessed, without power button, with a direction arrow
        const bool isSel = isOutput && selected == outputTile;
        const bool hov = isOutput && hovered == hitOutput;
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (box, corner);
        if (isSel)
        {
            g.setColour (colours::cyan.withAlpha (0.08f));
            g.fillRoundedRectangle (box, corner);
        }
        g.setColour (isSel ? colours::cyan.withAlpha (0.8f) : colours::line.brighter (hov ? 0.5f : 0.1f));
        g.drawRoundedRectangle (box.reduced (0.75f), corner, isSel ? 1.6f : 1.0f);

        // Arrow: into the chain / out of it
        const auto c = juce::Point<float> (box.getCentreX(), box.getY() + 30.0f);
        juce::Path arrow;
        arrow.startNewSubPath (c.x - 13.0f, c.y);
        arrow.lineTo (c.x + 11.0f, c.y);
        arrow.startNewSubPath (c.x + 4.0f, c.y - 7.0f);
        arrow.lineTo (c.x + 11.0f, c.y);
        arrow.lineTo (c.x + 4.0f, c.y + 7.0f);
        g.setColour (isSel ? colours::cyan : colours::textDim);
        g.strokePath (arrow, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (isSel ? colours::text : colours::textDim);
        g.setFont (medium (14.0f));
        g.drawText (isOutput ? "Output" : "Input", box.withTrimmedTop (44.0f).withHeight (20.0f), juce::Justification::centred, false);
    }

    void ChainView::drawTile (juce::Graphics& g, const Tile& t, juce::Rectangle<float> r, bool ghost) const
    {
        const auto idx = (size_t) t.id;
        const bool engagedOnly = macroEngaged[idx] && ! onState[idx];
        const bool on = onState[idx] || macroEngaged[idx];
        const bool isSelected = (int) t.id == selected && ! ghost;
        const float hov = ghost ? 0.0f : hoverGlow[idx].current;

        // Card body: raised when on, flat and darker when off
        if (ghost)
        {
            drawGlow (g, r.getCentre().translated (0.0f, 10.0f), r.getWidth() * 0.6f, 0.5f, juce::Colours::black);
        }
        g.setColour (ghost ? colours::raisedHi : (on ? colours::raised : colours::panel));
        g.fillRoundedRectangle (r, corner);
        if (on)
        {
            // Lit glass: a white sheen plus a hint of the aurora colour for its place in the chain
            const float place = juce::jlimit (0.0f, 1.0f, (float) (t.position - 1) / (float) std::max (1, dsp::numModules - 1));
            g.setGradientFill (juce::ColourGradient (aurora (place, 0.10f), r.getX(), r.getY(),
                                                     aurora (place, 0.0f), r.getRight(), r.getBottom(), false));
            g.fillRoundedRectangle (r, corner);
            g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.05f), r.getX(), r.getY(),
                                                     colours::accent.withAlpha (0.0f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, corner);
        }
        if (isSelected)
        {
            g.setColour (colours::cyan.withAlpha (0.075f));
            g.fillRoundedRectangle (r, corner);
        }
        if (hov > 0.01f)
        {
            g.setColour (colours::accent.withAlpha (0.035f * hov));
            g.fillRoundedRectangle (r, corner);
        }

        // Frame: cyan when selected, brighter when on
        if (isSelected)
        {
            g.setColour (colours::cyan.withAlpha (0.9f));
            g.drawRoundedRectangle (r.reduced (0.8f), corner, 1.6f);
            // Accent bar along the top edge
            g.fillRoundedRectangle (r.getX() + 16.0f, r.getY() + 1.5f, r.getWidth() - 32.0f, 2.5f, 1.25f);
        }
        else
        {
            g.setColour (ghost ? colours::cyan.withAlpha (0.6f)
                               : (on ? colours::line.brighter (0.45f + 0.4f * hov) : colours::line.brighter (0.1f + 0.4f * hov)));
            g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
        }

        Tile shifted = t;
        shifted.bounds = r;
        drawPowerGlyph (g, dotBounds (shifted), onState[idx], hov);

        const auto nameColour = on ? colours::text : colours::textDim;
        const juce::String name (dsp::moduleDisplayName (t.id));

        if (t.inLane)
        {
            // Parallel send: compact card on the lane
            const auto dot = dotBounds (shifted);
            auto text = r.withLeft (dot.getRight() + 10.0f).withRight (modeBounds (shifted).getX() - 6.0f);
            g.setColour (nameColour);
            g.setFont (font (14.5f, true));
            g.drawFittedText (name, text.toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
        }
        else
        {
            const auto dot = dotBounds (shifted);
            const float textX = dot.getRight() + 10.0f;

            // Name: full width of the card, readable
            g.setColour (nameColour);
            g.setFont (font (15.5f, true));
            g.drawFittedText (name, juce::Rectangle<float> (textX, r.getY() + 15.0f, r.getRight() - 12.0f - textX, 22.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.92f);

            // Status line with the module icon
            const auto iconArea = juce::Rectangle<float> (r.getX() + 16.0f, r.getY() + 46.0f, 20.0f, 20.0f);
            drawModuleIcon (g, (int) t.id, iconArea, on ? colours::cyan.interpolatedWith (colours::text, 0.25f) : colours::textFaint);
            juce::String status = onState[idx] ? (globalBypass ? "Bypass" : "On") : (engagedOnly ? "On via macro" : "Off");
            g.setColour (onState[idx] && globalBypass ? colours::bypass : (on ? colours::text.withAlpha (0.82f) : colours::textFaint));
            g.setFont (medium (13.0f));
            auto statusArea = juce::Rectangle<float> (textX, iconArea.getY(), r.getRight() - 12.0f - textX, iconArea.getHeight());
            if (canBeSend (t.id))
                statusArea.setRight (modeBounds (shifted).getX() - 6.0f);
            g.drawFittedText (status, statusArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);

            // Position in the chain (bottom right, behind the drag handle)
            if (! canBeSend (t.id) && (ghost ? 1.0f : hov) < 0.5f)
            {
                g.setColour (colours::textFaint);
                g.setFont (medium (12.0f));
                g.drawText (juce::String (t.position).paddedLeft ('0', 2), juce::Rectangle<float> (r.getRight() - 42.0f, iconArea.getY(), 28.0f, iconArea.getHeight()),
                            juce::Justification::centredRight, false);
            }

            // Set up by the automatic mix
            if (autoKeys.contains (dsp::moduleKey (t.id)) && onState[idx])
            {
                const auto badge = juce::Rectangle<float> (r.getRight() - 50.0f, r.getY() + 9.0f, 40.0f, 17.0f);
                g.setGradientFill (auroraGradient (badge.getTopLeft(), badge.getBottomRight(), 0.30f));
                g.fillRoundedRectangle (badge, 8.5f);
                g.setColour (colours::text);
                g.setFont (medium (11.0f));
                g.drawText ("Auto", badge, juce::Justification::centred, false);
            }

            // Activity bar along the bottom edge (gain reduction, ducking, movement)
            if (onState[idx] && activity[idx] > 0.01f)
            {
                const auto bar = juce::Rectangle<float> (r.getX() + 14.0f, r.getBottom() - 6.0f, r.getWidth() - 28.0f, 2.5f);
                g.setColour (colours::cyan.withAlpha (0.85f));
                g.fillRoundedRectangle (bar.withWidth (std::max (3.0f, bar.getWidth() * activity[idx])), 1.25f);
            }

            // Drag handle: appears on hover (and while dragging) so the card reads as movable
            const float grip = ghost ? 1.0f : hov;
            if (grip > 0.01f && ! canBeSend (t.id))
            {
                g.setColour (colours::textDim.withAlpha (0.85f * grip));
                for (int gx = 0; gx < 2; ++gx)
                    for (int gy = -1; gy <= 1; ++gy)
                        g.fillEllipse (juce::Rectangle<float> (2.6f, 2.6f).withCentre ({ r.getRight() - 22.0f + 5.0f * (float) gx,
                                                                                          iconArea.getCentreY() + 5.0f * (float) gy }));
            }
        }

        // Send / insert switch for delay and reverb
        if (canBeSend (t.id))
        {
            const auto m = modeBounds (shifted);
            const bool send = routing.isSend (t.id);
            g.setColour (send ? colours::cyan.withAlpha (0.14f) : colours::accent.withAlpha (0.04f));
            g.fillRoundedRectangle (m, m.getHeight() * 0.5f);
            g.setColour (send ? colours::cyan.withAlpha (0.55f) : colours::line.brighter (0.4f));
            g.drawRoundedRectangle (m.reduced (0.5f), m.getHeight() * 0.5f, 1.0f);
            g.setColour (send ? colours::text : colours::textDim);
            g.setFont (medium (12.5f));
            g.drawText (send ? "Send" : "Insert", m, juce::Justification::centred, false);
        }
    }

    void ChainView::paint (juce::Graphics& g)
    {
        layoutTiles();
        const auto row = rowArea();
        const auto lane = laneArea();
        const float cy = row.getCentreY();
        const float maxScroll = std::max (0.0f, contentWidth() - row.getWidth());

        // Spine: in -> serial chain -> out
        g.setColour (colours::accent.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (inBox.getRight(), cy - 0.75f, outBox.getX() - inBox.getRight(), 1.5f));

        // Parallel lane: splits after the serial chain, joins at the output
        const bool draggingSendable = dragging && dragTile >= 0 && canBeSend (tiles[(size_t) dragTile].id);
        float firstSendX = 1.0e9f;
        for (auto& t : tiles)
            if (t.inLane)
                firstSendX = std::min (firstSendX, t.bounds.getX());
        if (firstSendX < 1.0e8f)
        {
            juce::Path branch;
            branch.startNewSubPath (row.getRight() - 6.0f, row.getBottom());
            branch.quadraticTo (row.getRight() - 6.0f, lane.getCentreY(), firstSendX - 30.0f, lane.getCentreY());
            branch.lineTo (firstSendX, lane.getCentreY());
            juce::Path join;
            join.startNewSubPath (lane.getRight(), lane.getCentreY());
            join.quadraticTo (outBox.getCentreX(), lane.getCentreY(), outBox.getCentreX(), outBox.getBottom());

            g.setColour (colours::accent.withAlpha (0.22f));
            const float dashes[] { 4.0f, 4.0f };
            juce::Path dashedBranch, dashedJoin;
            juce::PathStrokeType stroke (1.2f);
            stroke.createDashedStroke (dashedBranch, branch, dashes, 2);
            stroke.createDashedStroke (dashedJoin, join, dashes, 2);
            g.fillPath (dashedBranch);
            g.fillPath (dashedJoin);

            drawCaption (g, "Parallel sends", juce::Rectangle<float> (firstSendX - 196.0f, lane.getY(), 150.0f, lane.getHeight()),
                         juce::Justification::centredRight, colours::textDim, 10.5f);
        }

        // Drop zone while Delay or Reverb is dragged
        if (draggingSendable)
        {
            const bool over = lane.expanded (0.0f, 10.0f).contains (dragPos);
            const auto zone = lane.withTrimmedLeft ((float) modulesButton.getWidth() + 16.0f);
            g.setColour (colours::cyan.withAlpha (over ? 0.12f : 0.05f));
            g.fillRoundedRectangle (zone, corner);
            juce::Path outline;
            outline.addRoundedRectangle (zone.reduced (0.75f), corner);
            juce::Path dashed;
            const float dashes[] { 5.0f, 4.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (colours::cyan.withAlpha (over ? 0.8f : 0.4f));
            g.fillPath (dashed);
            g.setColour (colours::text.withAlpha (over ? 1.0f : 0.7f));
            g.setFont (medium (13.5f));
            g.drawText ("Drop here to run it in parallel", zone.withTrimmedLeft (16.0f), juce::Justification::centredLeft, false);
        }

        drawPort (g, inBox, false);
        drawPort (g, outBox, true);

        // Serial cards, clipped to the scrolling row
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (row.expanded (0.0f, 2.0f).toNearestInt());
            for (int i = 0; i < (int) tiles.size(); ++i)
                if (! tiles[(size_t) i].inLane && ! (dragging && i == dragTile))
                    drawTile (g, tiles[(size_t) i], tiles[(size_t) i].bounds, false);

            // Insertion marker
            if (dragging && dragTile >= 0 && ! lane.expanded (0.0f, 10.0f).contains (dragPos))
            {
                const auto& t = tiles[(size_t) dragTile];
                const int idx = insertIndexForX (dragPos.x);
                float markerX = row.getX() - scroll.current - gap * 0.5f + 1.5f;
                int count = 0;
                for (auto& other : tiles)
                {
                    if (other.inLane || other.id == t.id)
                        continue;
                    if (count++ < idx)
                        markerX = other.bounds.getRight() + gap * 0.5f;
                }
                markerX = juce::jlimit (row.getX() + 2.0f, row.getRight() - 2.0f, markerX);
                drawGlow (g, { markerX, row.getCentreY() }, 22.0f, 0.35f, colours::cyan);
                g.setColour (colours::cyan);
                g.fillRoundedRectangle (markerX - 1.5f, row.getY() + 2.0f, 3.0f, row.getHeight() - 4.0f, 1.5f);
            }

            // More cards beyond the edges: fade and arrows
            if (scroll.current > 0.5f)
            {
                const auto a = leftArrowArea();
                g.setGradientFill (juce::ColourGradient (colours::background, a.getX(), 0.0f,
                                                         colours::background.withAlpha (0.0f), a.getX() + edgeFade + arrowW * 0.5f, 0.0f, false));
                g.fillRect (a.withWidth (edgeFade + arrowW * 0.5f).expanded (0.0f, 2.0f));
                const bool hov = hovered == hitLeftArrow;
                g.setColour (colours::raisedHi.withAlpha (hov ? 1.0f : 0.9f));
                g.fillEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (a.getCentre()));
                g.setColour (colours::line.brighter (hov ? 0.7f : 0.3f));
                g.drawEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (a.getCentre()), 1.0f);
                drawChevron (g, a.getCentre(), false, hov ? colours::text : colours::textDim);
            }
            if (scroll.current < maxScroll - 0.5f)
            {
                const auto a = rightArrowArea();
                g.setGradientFill (juce::ColourGradient (colours::background.withAlpha (0.0f), a.getRight() - edgeFade - arrowW * 0.5f, 0.0f,
                                                         colours::background, a.getRight(), 0.0f, false));
                g.fillRect (a.withLeft (a.getRight() - edgeFade - arrowW * 0.5f).expanded (0.0f, 2.0f));
                const bool hov = hovered == hitRightArrow;
                g.setColour (colours::raisedHi.withAlpha (hov ? 1.0f : 0.9f));
                g.fillEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (a.getCentre()));
                g.setColour (colours::line.brighter (hov ? 0.7f : 0.3f));
                g.drawEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (a.getCentre()), 1.0f);
                drawChevron (g, a.getCentre(), true, hov ? colours::text : colours::textDim);
            }
        }

        // Scroll bar (only when the row scrolls)
        if (maxScroll > 0.5f)
        {
            const auto track = scrollTrack();
            g.setColour (colours::accent.withAlpha (0.05f));
            g.fillRoundedRectangle (track, 2.0f);
            g.setColour (draggingThumb ? colours::cyan.withAlpha (0.8f) : colours::accent.withAlpha (0.28f));
            g.fillRoundedRectangle (scrollThumb(), 2.0f);
        }

        // Parallel sends
        for (int i = 0; i < (int) tiles.size(); ++i)
            if (tiles[(size_t) i].inLane && ! (dragging && i == dragTile))
                drawTile (g, tiles[(size_t) i], tiles[(size_t) i].bounds, false);

        // Dragged card follows the mouse
        if (dragging && dragTile >= 0)
        {
            const auto& t = tiles[(size_t) dragTile];
            const auto r = t.bounds.withPosition (dragPos - dragOffset);
            if (lane.expanded (0.0f, 10.0f).contains (dragPos) && ! canBeSend (t.id))
            {
                g.setColour (colours::warning.withAlpha (0.10f));
                g.fillRoundedRectangle (lane, corner);
            }
            g.setOpacity (0.95f);
            drawTile (g, t, r, true);
        }

        if (hint.isNotEmpty())
        {
            const float left = (float) modulesButton.getRight() + 16.0f;
            const float right = firstSendX < 1.0e8f ? firstSendX - 210.0f : lane.getRight();
            g.setColour (colours::warning);
            g.setFont (medium (13.0f));
            g.drawFittedText (hint, juce::Rectangle<float> (left, lane.getY(), std::max (200.0f, right - left), lane.getHeight()).toNearestInt(),
                              juce::Justification::centredLeft, 2, 0.9f);
        }
    }

    //==========================================================================
    // Mouse

    void ChainView::mouseDown (const juce::MouseEvent& e)
    {
        layoutTiles();
        const int i = tileAt (e.position);
        dragTile = -1;
        dragging = false;
        draggingThumb = false;

        if (e.mods.isPopupMenu())
        {
            if (i >= 0)
            {
                showTileMenu (tiles[(size_t) i].id);
            }
            else
            {
                juce::Component::SafePointer<ChainView> safe (this);
                createModulesMenu().showMenuAsync (juce::PopupMenu::Options().withMinimumWidth (260),
                                                   [safe] (int result) { if (safe != nullptr) safe->handleMenuResult (result); });
            }
            return;
        }

        // Arrows: one page of cards
        if (i == hitLeftArrow || i == hitRightArrow)
        {
            const float page = (serialW + gap) * (float) std::max (1, (int) (rowArea().getWidth() / (serialW + gap)) - 1);
            scrollTo (scroll.target + (i == hitLeftArrow ? -page : page));
            return;
        }

        // Scroll bar: grab the thumb or jump to the clicked position
        if (getMaxScroll() > 0.0f && scrollTrack().expanded (0.0f, 5.0f).contains (e.position))
        {
            const auto thumb = scrollThumb();
            draggingThumb = true;
            thumbGrabOffset = thumb.contains (e.position.withY (thumb.getCentreY())) ? e.position.x - thumb.getX() : thumb.getWidth() * 0.5f;
            mouseDrag (e);
            return;
        }

        if (i == hitOutput)
        {
            selected = outputTile;
            if (onSelect)
                onSelect (outputTile);
            repaint();
            return;
        }
        if (i < 0)
            return;

        const auto& t = tiles[(size_t) i];

        if (dotBounds (t).expanded (3.0f).contains (e.position))
        {
            toggleOn (t.id);
            return;
        }

        if (canBeSend (t.id) && modeBounds (t).expanded (2.0f).contains (e.position))
        {
            auto r = routing;
            if (t.id == ModuleId::delay) r.delayInsert = ! r.delayInsert;
            else r.reverbInsert = ! r.reverbInsert;
            proc.setRouting (r);
            routing = r;
            repaint();
            return;
        }

        dragTile = i;
        dragOffset = e.position - t.bounds.getPosition();
        dragPos = e.position;
        selected = (int) t.id;
        if (onSelect)
            onSelect ((int) t.id);
        repaint();
    }

    void ChainView::mouseDrag (const juce::MouseEvent& e)
    {
        if (draggingThumb)
        {
            const auto track = scrollTrack();
            const auto thumb = scrollThumb();
            const float travel = std::max (1.0f, track.getWidth() - thumb.getWidth());
            const float maxScroll = getMaxScroll();
            scrollTo ((e.position.x - thumbGrabOffset - track.getX()) / travel * maxScroll, false);
            return;
        }

        if (dragTile < 0)
            return;
        if (! dragging && e.getDistanceFromDragStart() < 6)
            return;
        dragging = true;
        dragPos = e.position;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        repaint();
    }

    void ChainView::mouseUp (const juce::MouseEvent& e)
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        if (draggingThumb)
        {
            draggingThumb = false;
            repaint();
            return;
        }
        if (! dragging || dragTile < 0)
        {
            dragTile = -1;
            dragging = false;
            return;
        }

        layoutTiles();
        const auto id = tiles[(size_t) dragTile].id;
        const bool overLane = laneArea().expanded (0.0f, 10.0f).contains (e.position);
        auto r = routing;

        if (overLane)
        {
            if (canBeSend (id))
            {
                if (id == ModuleId::delay) r.delayInsert = false;
                else r.reverbInsert = false;
            }
            else
            {
                showHint (juce::String (dsp::moduleDisplayName (id)) + " shapes the vocal itself, so it stays in the series chain. Only Delay and Reverb can run in parallel.");
            }
        }
        else
        {
            // Serial position: index among serial tiles -> position in the full order
            const int serialIndex = insertIndexForX (e.position.x);
            if (id == ModuleId::delay) r.delayInsert = true;
            if (id == ModuleId::reverb) r.reverbInsert = true;

            // Visible serial tiles only: the index came from what is on screen
            std::vector<ModuleId> serial;
            for (auto m : r.order)
                if (m != id && r.isInsert (m) && ! hidden[(size_t) m])
                    serial.push_back (m);

            // Place before the serial module currently at serialIndex (or at the end)
            if (serialIndex < (int) serial.size())
                r.placeBefore (id, serial[(size_t) serialIndex]);
            else
                r.placeAtEnd (id);
        }

        if (r != routing)
        {
            proc.setRouting (r);
            routing = r;
        }

        dragTile = -1;
        dragging = false;
        scrollToShow (id);
        repaint();
    }

    void ChainView::mouseMove (const juce::MouseEvent& e)
    {
        layoutTiles();
        const int i = tileAt (e.position);
        if (i != hovered)
        {
            hovered = i;
            setMouseCursor (i >= 0 || i == hitOutput || i == hitLeftArrow || i == hitRightArrow ? juce::MouseCursor::PointingHandCursor
                                                                                                  : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void ChainView::mouseExit (const juce::MouseEvent&)
    {
        hovered = hitNothing;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }

    //==========================================================================
    // Module organisation (view only - never changes the audio)

    void ChainView::resized()
    {
        const auto lane = laneArea();
        modulesButton.setBounds (juce::Rectangle<float> (lane.getX(), lane.getY(), 212.0f, lane.getHeight()).toNearestInt());
        pendingReveal = false;
        if (selected >= 0 && selected < dsp::numModules)
            scrollToShow ((ModuleId) selected, false);
    }

    void ChainView::readHidden()
    {
        const auto keys = proc.getState().state.getProperty (statekeys::uiHidden).toString();
        if (keys == hiddenKeys)
            return;

        hiddenKeys = keys;
        hidden.fill (false);
        for (const auto& k : juce::StringArray::fromTokens (keys, ",", ""))
            if (const auto id = dsp::moduleFromKey (k))
                hidden[(size_t) *id] = true;
        updateModulesButton();
        repaint();
    }

    void ChainView::writeHidden()
    {
        juce::StringArray keys;
        for (auto id : routing.order)
            if (hidden[(size_t) id])
                keys.add (dsp::moduleKey (id));
        hiddenKeys = keys.joinIntoString (",");
        proc.getState().state.setProperty (statekeys::uiHidden, hiddenKeys, nullptr);
        updateModulesButton();
        repaint();
    }

    void ChainView::setHidden (ModuleId id, bool shouldHide)
    {
        readHidden();
        if (hidden[(size_t) id] == shouldHide)
            return;
        hidden[(size_t) id] = shouldHide;
        writeHidden();
        if (shouldHide)
            showHint (juce::String (dsp::moduleDisplayName (id)) + " is hidden - it keeps its settings"
                      + (onState[(size_t) id] ? " and keeps processing." : ".") + " Show it again from Modules.");
    }

    void ChainView::showAllModules()
    {
        hidden.fill (false);
        writeHidden();
    }

    int ChainView::getNumHidden() const noexcept
    {
        return (int) std::count (hidden.begin(), hidden.end(), true);
    }

    void ChainView::updateModulesButton()
    {
        int n = 0, active = 0;
        for (int i = 0; i < dsp::numModules; ++i)
        {
            if (! hidden[(size_t) i])
                continue;
            ++n;
            if (onState[(size_t) i] || macroEngaged[(size_t) i])
                ++active;
        }

        juce::String text ("Modules");
        if (n > 0)
            text << juce::String::fromUTF8 ("  \xc2\xb7  ") << n << " hidden" << (active > 0 ? ", " + juce::String (active) + " on" : juce::String());
        if (text != modulesButtonText)
        {
            modulesButtonText = text;
            modulesButton.setButtonText (text);
        }
    }

    void ChainView::toggleOn (ModuleId id)
    {
        if (auto* p = proc.getState().getParameter (onParamFor (id)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
            p->endChangeGesture();
        }
    }

    void ChainView::restoreDefaultOrder()
    {
        auto r = proc.getRouting();
        r.order = dsp::Routing::defaults().order;
        if (r != proc.getRouting())
            proc.setRouting (r);
        routing = r;
        repaint();
    }

    void ChainView::saveLayoutAsDefault()
    {
        settings::set (settings::myLayoutOrder, proc.getRouting().toString());
        settings::set (settings::myLayoutHidden, hiddenKeys);
        showHint ("Layout saved as your default. Load it in any project from Modules.");
    }

    bool ChainView::loadDefaultLayout()
    {
        const auto order = settings::getString (settings::myLayoutOrder);
        if (order.isEmpty())
            return false;
        const auto r = dsp::Routing::fromString (order);
        if (r != proc.getRouting())
            proc.setRouting (r);
        routing = r;
        proc.getState().state.setProperty (statekeys::uiHidden, settings::getString (settings::myLayoutHidden), nullptr);
        readHidden();
        repaint();
        return true;
    }

    namespace
    {
        enum MenuAction { actOpen = 1, actToggle, actHide, actShow, actToStart, actToEnd, actHelp };
        enum GlobalAction { actShowAll = 9001, actDefaultOrder, actSaveLayout, actLoadLayout };
        int menuId (MenuAction a, ModuleId id) { return (int) a * 1000 + (int) id; }
    }

    juce::PopupMenu ChainView::createModulesMenu()
    {
        readHidden();
        juce::PopupMenu menu;
        menu.addSectionHeader ("Modules in processing order");

        for (auto id : routing.order)
        {
            const auto idx = (size_t) id;
            const bool on = onState[idx] || macroEngaged[idx];

            juce::PopupMenu sub;
            sub.addItem (menuId (actOpen, id), "Open");
            sub.addItem (menuId (actToggle, id), onState[idx] ? "Switch off" : "Switch on");
            if (hidden[idx])
                sub.addItem (menuId (actShow, id), "Show in chain view");
            else
                sub.addItem (menuId (actHide, id), "Hide from chain view");
            sub.addSeparator();
            sub.addItem (menuId (actHelp, id), "Help");

            juce::String label (dsp::moduleDisplayName (id));
            if (routing.isSend (id))
                label << " (send)";
            label << "   " << (on ? "on" : "off");
            if (hidden[idx])
                label << ", hidden";
            menu.addSubMenu (label, sub);
        }

        menu.addSeparator();
        menu.addItem (actShowAll, "Show all modules", getNumHidden() > 0);
        menu.addItem (actDefaultOrder, "Restore default order", routing.order != dsp::Routing::defaults().order);
        menu.addSeparator();
        menu.addItem (actSaveLayout, "Save as my default layout");
        menu.addItem (actLoadLayout, "Load my default layout", settings::getString (settings::myLayoutOrder).isNotEmpty());
        return menu;
    }

    void ChainView::showTileMenu (ModuleId id)
    {
        const auto idx = (size_t) id;
        juce::PopupMenu menu;
        menu.addSectionHeader (dsp::moduleDisplayName (id));
        menu.addItem (menuId (actOpen, id), "Open");
        menu.addItem (menuId (actToggle, id), onState[idx] ? "Switch off" : "Switch on");
        menu.addItem (menuId (actHide, id), "Hide from chain view");
        if (! routing.isSend (id))
        {
            menu.addSeparator();
            menu.addItem (menuId (actToStart, id), "Move to the start of the chain", routing.positionOf (id) > 0);
            menu.addItem (menuId (actToEnd, id), "Move to the end of the chain", routing.positionOf (id) < dsp::numModules - 1);
        }
        menu.addSeparator();
        menu.addItem (menuId (actHelp, id), "Help for " + juce::String (dsp::moduleDisplayName (id)));
        menu.addItem (actDefaultOrder, "Restore default order", routing.order != dsp::Routing::defaults().order);

        juce::Component::SafePointer<ChainView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withMinimumWidth (220),
                            [safe] (int result) { if (safe != nullptr) safe->handleMenuResult (result); });
    }

    void ChainView::handleMenuResult (int result)
    {
        if (result <= 0)
            return;

        switch (result)
        {
            case actShowAll:      showAllModules(); return;
            case actDefaultOrder: restoreDefaultOrder(); return;
            case actSaveLayout:   saveLayoutAsDefault(); return;
            case actLoadLayout:   loadDefaultLayout(); return;
            default: break;
        }

        const int action = result / 1000;
        const int raw = result % 1000;
        if (raw < 0 || raw >= dsp::numModules)
            return;
        const auto id = (ModuleId) raw;

        switch (action)
        {
            case actOpen:
                selected = (int) id;
                if (onSelect)
                    onSelect ((int) id);
                break;
            case actToggle: toggleOn (id); break;
            case actHide:   setHidden (id, true); break;
            case actShow:   setHidden (id, false); break;
            case actToStart:
            case actToEnd:
            {
                auto r = proc.getRouting();
                if (action == actToStart) r.move (id, 0);
                else r.placeAtEnd (id);
                proc.setRouting (r);
                routing = r;
                break;
            }
            case actHelp:
                if (onHelp)
                    onHelp ((int) id);
                break;
            default: break;
        }
        repaint();
    }
}
