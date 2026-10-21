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
        constexpr float plusR = 11.0f;
        constexpr int hitLeftArrow = -3, hitRightArrow = -4, hitAddCard = -5, hitOutput = -100, hitInput = -101, hitNothing = -2;

        bool canBeSend (ModuleId id) { return dsp::Routing::canBeSend (id); }

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

        void drawPlus (juce::Graphics& g, juce::Point<float> c, float size, juce::Colour colour, float thickness = 1.8f)
        {
            g.setColour (colour);
            g.fillRoundedRectangle (juce::Rectangle<float> (size, thickness).withCentre (c), thickness * 0.5f);
            g.fillRoundedRectangle (juce::Rectangle<float> (thickness, size).withCentre (c), thickness * 0.5f);
        }

        // Small round hover action on a card (duplicate / remove)
        void drawCardAction (juce::Graphics& g, juce::Rectangle<float> r, bool isRemove, float alpha)
        {
            g.setColour (colours::background.withAlpha (0.85f * alpha));
            g.fillEllipse (r);
            g.setColour ((isRemove ? colours::warning : colours::line.brighter (0.6f)).withAlpha (0.8f * alpha));
            g.drawEllipse (r.reduced (0.5f), 1.0f);
            const auto c = r.getCentre();
            if (isRemove)
            {
                juce::Path x;
                x.startNewSubPath (c.x - 3.5f, c.y - 3.5f);
                x.lineTo (c.x + 3.5f, c.y + 3.5f);
                x.startNewSubPath (c.x + 3.5f, c.y - 3.5f);
                x.lineTo (c.x - 3.5f, c.y + 3.5f);
                g.setColour (colours::text.withAlpha (alpha));
                g.strokePath (x, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else
            {
                // Two overlapping squares
                g.setColour (colours::text.withAlpha (alpha));
                g.drawRoundedRectangle (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c.translated (-1.5f, -1.5f)), 1.5f, 1.2f);
                g.drawRoundedRectangle (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c.translated (1.8f, 1.8f)), 1.5f, 1.2f);
            }
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

    juce::String ChainView::cardName (const dsp::Routing& routing, ModuleId id, int instance)
    {
        const juce::String name (dsp::moduleDisplayName (id));
        return instance > 0 || routing.countOf (id) > 1 ? name + " " + juce::String (instance + 1) : name;
    }

    ChainView::ChainView (VelvoxAudioProcessor& p) : proc (p)
    {
        routing = proc.getRouting();
        setTooltip ("Click a module to open it, drag it to change the order. Hover between two modules and click + to insert one there. "
                    "Drag Delay or Reverb onto the lower lane to run them in parallel. Right-click for more options.");

        addButton.setButtonText ("+  Add module");
        addButton.setTooltip ("Open the module browser and add a module at the end of the chain. "
                              "The same module can be used several times - each instance has its own settings.");
        addButton.onClick = [this] { if (onAddModule) onAddModule (numSerial, true); };
        addAndMakeVisible (addButton);

        undoButton.setButtonText ("Undo");
        undoButton.setTooltip ("Undo the last chain change (add, remove, duplicate, move)");
        undoButton.onClick = [this] { undo(); };
        redoButton.setButtonText ("Redo");
        redoButton.setTooltip ("Redo");
        redoButton.onClick = [this] { redo(); };
        chainMenuButton.setButtonText (juce::String::fromUTF8 ("\xe2\x8b\xaf"));   // midline ellipsis
        chainMenuButton.setTooltip ("Chain options: usual order, personal default chain");
        chainMenuButton.onClick = [this] { showChainMenu(); };
        for (auto* b : { &undoButton, &redoButton, &chainMenuButton })
            addAndMakeVisible (*b);
    }

    bool ChainView::isOn (ModuleId id, int instance) const
    {
        auto* v = proc.getState().getRawParameterValue (onParamId (id, instance));
        return v != nullptr && v->load() > 0.5f;
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
        const float vw = rowArea().getWidth() - addCardW - gap;
        if (n <= 0)
            return minCardW;
        const float fit = (vw - gap * (float) (n - 1)) / (float) n;
        if (fit >= minCardW)
            return std::min (fit, maxCardW);
        // Not everything fits: as many whole cards as possible, never narrower than minCardW
        const int k = std::max (1, (int) std::floor ((vw + gap) / (minCardW + gap)));
        return std::max (minCardW, (vw - gap * (float) (k - 1)) / (float) k);
    }

    float ChainView::contentWidth() const
    {
        return (float) numSerial * (serialW + gap) + addCardW;
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

    juce::Rectangle<float> ChainView::addCardArea() const
    {
        const auto row = rowArea();
        return { row.getX() + (float) numSerial * (serialW + gap) - scroll.current, row.getY(), addCardW, row.getHeight() };
    }

    juce::Rectangle<float> ChainView::getAddCardBounds()
    {
        layoutTiles();
        return addCardArea();
    }

    void ChainView::layoutTiles()
    {
        tiles.clear();
        const auto row = rowArea();
        const auto lane = laneArea();

        std::vector<const dsp::ChainSlot*> serial, sends;
        for (const auto& slot : routing)
            (! slot.insert ? sends : serial).push_back (&slot);

        numSerial = (int) serial.size();
        serialW = cardWidth (numSerial);
        const float maxScroll = std::max (0.0f, contentWidth() - row.getWidth());
        if (scroll.target > maxScroll || scroll.current > maxScroll)
        {
            scroll.target = std::min (scroll.target, maxScroll);
            scroll.current = std::min (scroll.current, maxScroll);
        }

        for (int i = 0; i < numSerial; ++i)
            tiles.push_back ({ serial[(size_t) i]->id, serial[(size_t) i]->instance,
                               { row.getX() + (float) i * (serialW + gap) - scroll.current, row.getY(), serialW, row.getHeight() },
                               false, i + 1 });

        // Sends sit on the right part of the lane, between the chain end and the output
        float x = lane.getRight() - sendW * (float) sends.size() - gap * (float) (sends.size() > 0 ? sends.size() - 1 : 0);
        for (auto* s : sends)
        {
            tiles.push_back ({ s->id, s->instance, { x, lane.getY(), sendW, lane.getHeight() }, true, 0 });
            x += sendW + gap;
        }

        inBox = { 0.0f, row.getY(), endW, row.getHeight() };
        outBox = { (float) getWidth() - endW, row.getY(), endW, row.getHeight() };
    }

    const ChainView::Tile* ChainView::findTile (ModuleId id, int instance) const
    {
        for (auto& t : tiles)
            if (t.id == id && t.instance == instance)
                return &t;
        return nullptr;
    }

    juce::Rectangle<float> ChainView::getTileBounds (ModuleId id, int instance)
    {
        layoutTiles();
        if (auto* t = findTile (id, instance))
            return t->bounds;
        return {};
    }

    juce::Rectangle<float> ChainView::getPowerBounds (ModuleId id, int instance)
    {
        layoutTiles();
        if (auto* t = findTile (id, instance))
            return dotBounds (*t);
        return {};
    }

    juce::Rectangle<float> ChainView::getModeBounds (ModuleId id, int instance)
    {
        layoutTiles();
        if (auto* t = findTile (id, instance); t != nullptr && canBeSend (id))
            return modeBounds (*t);
        return {};
    }

    juce::Rectangle<float> ChainView::getRemoveBounds (ModuleId id, int instance)
    {
        layoutTiles();
        if (auto* t = findTile (id, instance))
            return removeBounds (*t);
        return {};
    }

    juce::Rectangle<float> ChainView::getDuplicateBounds (ModuleId id, int instance)
    {
        layoutTiles();
        if (auto* t = findTile (id, instance))
            return duplicateBounds (*t);
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

    juce::Rectangle<float> ChainView::removeBounds (const Tile& t) const
    {
        if (t.inLane)
            return juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ modeBounds (t).getX() - 16.0f, t.bounds.getCentreY() });
        return juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ t.bounds.getRight() - 16.0f, t.bounds.getY() + 16.0f });
    }

    juce::Rectangle<float> ChainView::duplicateBounds (const Tile& t) const
    {
        return removeBounds (t).translated (-26.0f, 0.0f);
    }

    juce::Point<float> ChainView::gapCentre (int insertIndex) const
    {
        const auto row = rowArea();
        const float x = row.getX() + (float) insertIndex * (serialW + gap) - gap * 0.5f - scroll.current;
        return { x, row.getCentreY() };
    }

    int ChainView::gapAt (juce::Point<float> p) const
    {
        const auto row = rowArea();
        if (! row.contains (p) || dragging)
            return -1;
        for (int i = 0; i <= numSerial; ++i)
        {
            const auto c = gapCentre (i);
            if (c.x < row.getX() - 1.0f || c.x > row.getRight() + 1.0f)
                continue;
            if (std::abs (p.x - c.x) <= gap * 0.5f + 3.0f && std::abs (p.y - c.y) <= plusR + 6.0f)
                return i;
        }
        return -1;
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
        if (row.contains (p) && addCardArea().contains (p))
            return hitAddCard;
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
            if (t.inLane || (dragTile >= 0 && t.id == tiles[(size_t) dragTile].id && t.instance == tiles[(size_t) dragTile].instance))
                continue;
            if (x > t.bounds.getCentreX())
                ++index;
        }
        return index;
    }

    int ChainView::serialIndexToRoutingIndex (const dsp::Routing& r, int serialIndex, ModuleId exceptId, int exceptInstance) const
    {
        // Routing index in front of the serial entry number `serialIndex` (sends and the
        // excluded entry are skipped); past the last serial entry = behind it
        int serialSeen = 0, lastSerial = -1;
        for (int i = 0; i < r.size(); ++i)
        {
            const auto& s = r[i];
            if (! s.insert || s.sameInstance (exceptId, exceptInstance))
                continue;
            if (serialSeen == serialIndex)
                return i;
            ++serialSeen;
            lastSerial = i;
        }
        return lastSerial + 1;
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

    void ChainView::scrollToShow (ModuleId id, int instance, bool animate)
    {
        layoutTiles();
        const auto* t = findTile (id, instance);
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

    bool ChainView::isTileFullyVisible (ModuleId id, int instance)
    {
        const auto b = getTileBounds (id, instance);
        return ! b.isEmpty() && rowArea().expanded (0.5f).contains (b);
    }

    void ChainView::setSelected (int code)
    {
        selected = code;
        if (code >= 0)
        {
            if (getWidth() > 0)
                scrollToShow (dsp::selectionModule (code), dsp::selectionInstance (code), isShowing());   // glide when visible
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
        hintUntil = juce::Time::getMillisecondCounter() + 4500;
        repaint();
    }

    float ChainView::activityOf (const Tile& t) const
    {
        const auto it = activity.find (t.code());
        return it != activity.end() ? it->second : 0.0f;
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
        const auto r = proc.getRouting();
        if (r != routing)
        {
            routing = r;
            changed = true;
        }

        undoButton.setEnabled (canUndo());
        redoButton.setEnabled (canRedo());

        const auto engaged = dsp::modulesEngagedByMacros (proc.readSettings(), proc.readMacros());
        for (int i = 0; i < dsp::numModules; ++i)
        {
            if (engaged[(size_t) i] != macroEngaged[(size_t) i])
                changed = true;
            macroEngaged[(size_t) i] = engaged[(size_t) i];
        }

        for (const auto& slot : routing)
        {
            const int code = dsp::selectionCode (slot.id, slot.instance);
            const bool on = isOn (slot.id, slot.instance);
            const float target = live != nullptr ? live->activity[(size_t) slot.id][(size_t) slot.instance] : 0.0f;
            auto& a = activity[code];
            const float next = a * 0.75f + target * 0.25f;
            auto it = onState.find (code);
            if (it == onState.end() || it->second != on || std::abs (next - a) > 0.01f)
                changed = true;
            onState[code] = on;
            a = next;
        }

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
            if (selected >= 0)
                scrollToShow (dsp::selectionModule (selected), dsp::selectionInstance (selected), false);
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

        const int hoveredCode = hovered >= 0 && hovered < (int) tiles.size() ? tiles[(size_t) hovered].code() : (hovered == hitAddCard ? -77 : -1);
        for (auto& t : tiles)
        {
            auto& glow = hoverGlow[t.code()];
            glow.target = t.code() == hoveredCode ? 1.0f : 0.0f;
            moved = glow.tick (0.18f) || moved;
        }
        auto& addGlow = hoverGlow[-77];
        addGlow.target = hoveredCode == -77 ? 1.0f : 0.0f;
        moved = addGlow.tick (0.18f) || moved;
        gapGlow.target = hoveredGap >= 0 ? 1.0f : 0.0f;
        moved = gapGlow.tick (0.22f) || moved;

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

    void ChainView::drawAddCard (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto it = hoverGlow.find (-77);
        const float hov = it != hoverGlow.end() ? it->second.current : 0.0f;
        g.setColour (colours::accent.withAlpha (0.02f + 0.04f * hov));
        g.fillRoundedRectangle (r, corner);
        juce::Path outline;
        outline.addRoundedRectangle (r.reduced (0.75f), corner);
        juce::Path dashed;
        const float dashes[] { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour ((hov > 0.5f ? colours::cyan : colours::line.brighter (0.5f)).withAlpha (0.55f + 0.4f * hov));
        g.fillPath (dashed);
        const auto c = juce::Point<float> (r.getCentreX(), r.getY() + 30.0f);
        drawGlow (g, c, 18.0f, 0.15f + 0.25f * hov, colours::cyan);
        drawPlus (g, c, 16.0f, hov > 0.5f ? colours::cyan : colours::textDim, 2.0f);
        g.setColour (hov > 0.5f ? colours::text : colours::textDim);
        g.setFont (medium (13.0f));
        g.drawText ("Add module", r.withTrimmedTop (44.0f).withHeight (20.0f), juce::Justification::centred, false);
    }

    void ChainView::drawTile (juce::Graphics& g, const Tile& t, juce::Rectangle<float> r, bool ghost) const
    {
        const auto code = t.code();
        const auto onIt = onState.find (code);
        const bool switchedOn = onIt != onState.end() && onIt->second;
        const bool engagedOnly = t.instance == 0 && macroEngaged[(size_t) t.id] && ! switchedOn;
        const bool on = switchedOn || engagedOnly;
        const bool isSelected = code == selected && ! ghost;
        const auto glowIt = hoverGlow.find (code);
        const float hov = ghost ? 0.0f : (glowIt != hoverGlow.end() ? glowIt->second.current : 0.0f);
        const bool repeated = routing.countOf (t.id) > 1;

        // Card body: raised when on, flat and darker when off
        if (ghost)
            drawGlow (g, r.getCentre().translated (0.0f, 10.0f), r.getWidth() * 0.6f, 0.5f, juce::Colours::black);
        g.setColour (ghost ? colours::raisedHi : (on ? colours::raised : colours::panel));
        g.fillRoundedRectangle (r, corner);
        if (on)
        {
            // Lit glass: a white sheen plus a hint of the aurora colour for its place in the chain
            const float place = juce::jlimit (0.0f, 1.0f, (float) (t.position - 1) / (float) std::max (1, numSerial - 1));
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
        drawPowerGlyph (g, dotBounds (shifted), switchedOn, hov);

        const auto nameColour = on ? colours::text : colours::textDim;
        const juce::String baseName (dsp::moduleDisplayName (t.id));

        // Instance number chip ("2") after the name of a repeated module: two EQs are
        // visibly two separate instances, not one module shown twice
        const auto drawInstanceChip = [&] (float x, float centreY)
        {
            const auto chip = juce::Rectangle<float> (20.0f, 18.0f).withPosition (x, centreY - 9.0f);
            g.setColour (colours::cyan.withAlpha (on ? 0.16f : 0.08f));
            g.fillRoundedRectangle (chip, 5.0f);
            g.setColour (colours::cyan.withAlpha (on ? 0.7f : 0.35f));
            g.drawRoundedRectangle (chip.reduced (0.5f), 5.0f, 1.0f);
            g.setColour (on ? colours::text : colours::textDim);
            g.setFont (medium (11.5f));
            g.drawText (juce::String (t.instance + 1), chip, juce::Justification::centred, false);
        };

        const float actions = ghost ? 0.0f : hov;

        if (t.inLane)
        {
            // Parallel send: compact card on the lane
            const auto dot = dotBounds (shifted);
            auto text = r.withLeft (dot.getRight() + 10.0f).withRight (modeBounds (shifted).getX() - 6.0f - (actions > 0.05f ? 26.0f : 0.0f));
            g.setColour (nameColour);
            g.setFont (font (14.5f, true));
            const float nameW = std::min (text.getWidth() - (repeated ? 26.0f : 0.0f),
                                          (float) juce::GlyphArrangement::getStringWidthInt (font (14.5f, true), baseName) + 4.0f);
            g.drawFittedText (baseName, text.withWidth (nameW).toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);
            if (repeated)
                drawInstanceChip (text.getX() + nameW + 4.0f, text.getCentreY());
        }
        else
        {
            const auto dot = dotBounds (shifted);
            const float textX = dot.getRight() + 10.0f;
            const float nameRight = r.getRight() - 12.0f - (actions > 0.05f ? 54.0f : 0.0f);

            g.setColour (nameColour);
            g.setFont (font (15.5f, true));
            const float nameW = std::min (nameRight - textX - (repeated ? 26.0f : 0.0f),
                                          (float) juce::GlyphArrangement::getStringWidthInt (font (15.5f, true), baseName) + 4.0f);
            g.drawFittedText (baseName, juce::Rectangle<float> (textX, r.getY() + 15.0f, nameW, 22.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.92f);
            if (repeated)
                drawInstanceChip (textX + nameW + 4.0f, r.getY() + 26.0f);

            // Status line with the module icon
            const auto iconArea = juce::Rectangle<float> (r.getX() + 16.0f, r.getY() + 46.0f, 20.0f, 20.0f);
            drawModuleIcon (g, (int) t.id, iconArea, on ? colours::cyan.interpolatedWith (colours::text, 0.25f) : colours::textFaint);
            juce::String status = switchedOn ? (globalBypass ? "Bypass" : "On") : (engagedOnly ? "On via macro" : "Off");
            g.setColour (switchedOn && globalBypass ? colours::bypass : (on ? colours::text.withAlpha (0.82f) : colours::textFaint));
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
            if (autoKeys.contains (dsp::moduleKey (t.id)) && switchedOn && actions < 0.05f)
            {
                const auto badge = juce::Rectangle<float> (r.getRight() - 50.0f, r.getY() + 9.0f, 40.0f, 17.0f);
                g.setGradientFill (auroraGradient (badge.getTopLeft(), badge.getBottomRight(), 0.30f));
                g.fillRoundedRectangle (badge, 8.5f);
                g.setColour (colours::text);
                g.setFont (medium (11.0f));
                g.drawText ("Auto", badge, juce::Justification::centred, false);
            }

            // Activity bar along the bottom edge (gain reduction, ducking, movement)
            const float act = activityOf (t);
            if (switchedOn && act > 0.01f)
            {
                const auto bar = juce::Rectangle<float> (r.getX() + 14.0f, r.getBottom() - 6.0f, r.getWidth() - 28.0f, 2.5f);
                g.setColour (colours::cyan.withAlpha (0.85f));
                g.fillRoundedRectangle (bar.withWidth (std::max (3.0f, bar.getWidth() * act)), 1.25f);
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

        // Hover actions: duplicate and remove
        if (actions > 0.05f)
        {
            drawCardAction (g, removeBounds (shifted), true, actions);
            if (routing.nextFreeInstance (t.id) >= 0)
                drawCardAction (g, duplicateBounds (shifted), false, actions);
        }

        // Send / insert switch for delay and reverb
        if (canBeSend (t.id))
        {
            const auto m = modeBounds (shifted);
            const bool send = ! routing.isInsert (t.id, t.instance);
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
            const auto zone = lane.withTrimmedLeft ((float) chainMenuButton.getRight() - lane.getX() + 16.0f);
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
            drawAddCard (g, addCardArea());

            // "+" between two cards: insert a module exactly there
            if (hoveredGap >= 0 && gapGlow.current > 0.01f)
            {
                const auto c = gapCentre (hoveredGap);
                drawGlow (g, c, 20.0f, 0.35f * gapGlow.current, colours::cyan);
                g.setColour (colours::raisedHi.withAlpha (gapGlow.current));
                g.fillEllipse (juce::Rectangle<float> (2.0f * plusR, 2.0f * plusR).withCentre (c));
                g.setColour (colours::cyan.withAlpha (0.9f * gapGlow.current));
                g.drawEllipse (juce::Rectangle<float> (2.0f * plusR, 2.0f * plusR).withCentre (c), 1.3f);
                drawPlus (g, c, 10.0f, colours::text.withAlpha (gapGlow.current));
            }

            // Insertion marker
            if (dragging && dragTile >= 0 && ! lane.expanded (0.0f, 10.0f).contains (dragPos))
            {
                const auto& t = tiles[(size_t) dragTile];
                const int idx = insertIndexForX (dragPos.x);
                float markerX = row.getX() - scroll.current - gap * 0.5f + 1.5f;
                int count = 0;
                for (auto& other : tiles)
                {
                    if (other.inLane || (other.id == t.id && other.instance == t.instance))
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
            const float left = (float) chainMenuButton.getRight() + 16.0f;
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
                showTileMenu (tiles[(size_t) i]);
            else
                showChainMenu();
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

        // "+" between two cards, and the add card at the end
        if (const int g = gapAt (e.position); g >= 0)
        {
            if (onAddModule)
                onAddModule (g, false);
            return;
        }
        if (i == hitAddCard)
        {
            if (onAddModule)
                onAddModule (numSerial, true);
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

        const auto t = tiles[(size_t) i];
        const auto glowIt = hoverGlow.find (t.code());
        const bool actionsVisible = glowIt != hoverGlow.end() && glowIt->second.current > 0.3f;

        if (dotBounds (t).expanded (3.0f).contains (e.position))
        {
            toggleOn (t.id, t.instance);
            return;
        }
        if (removeBounds (t).expanded (2.0f).contains (e.position) && (actionsVisible || hovered == i))
        {
            removeInstance (t.id, t.instance);
            return;
        }
        if (duplicateBounds (t).expanded (2.0f).contains (e.position) && (actionsVisible || hovered == i) && routing.nextFreeInstance (t.id) >= 0)
        {
            duplicateInstance (t.id, t.instance);
            return;
        }
        if (canBeSend (t.id) && modeBounds (t).expanded (2.0f).contains (e.position))
        {
            setSendMode (t.id, t.instance, routing.isInsert (t.id, t.instance));
            return;
        }

        dragTile = i;
        dragOffset = e.position - t.bounds.getPosition();
        dragPos = e.position;
        selected = t.code();
        if (onSelect)
            onSelect (t.code());
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
        hoveredGap = -1;
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
        const auto t = tiles[(size_t) dragTile];
        const bool overLane = laneArea().expanded (0.0f, 10.0f).contains (e.position);
        auto r = routing;

        if (overLane)
        {
            if (canBeSend (t.id))
                r.setInsert (t.id, t.instance, false);
            else
                showHint (cardName (routing, t.id, t.instance) + " shapes the vocal itself, so it stays in the series chain. Only Delay and Reverb can run in parallel.");
        }
        else
        {
            // Serial position among the visible serial cards -> routing index
            const int serialIndex = insertIndexForX (e.position.x);
            r.setInsert (t.id, t.instance, true);
            const int target = serialIndexToRoutingIndex (r, serialIndex, t.id, t.instance);
            const int from = r.indexOf (t.id, t.instance);
            r.moveTo (t.id, t.instance, from < target ? target - 1 : target);
        }

        dragTile = -1;
        dragging = false;
        if (r != routing)
        {
            pushUndo();
            applyRouting (r);
        }
        scrollToShow (t.id, t.instance);
        repaint();
    }

    void ChainView::mouseMove (const juce::MouseEvent& e)
    {
        layoutTiles();
        const int i = tileAt (e.position);
        const int g = gapAt (e.position);
        if (i != hovered || g != hoveredGap)
        {
            hovered = i;
            hoveredGap = g;
            const bool clickable = g >= 0 || i >= 0 || i == hitOutput || i == hitLeftArrow || i == hitRightArrow || i == hitAddCard;
            setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void ChainView::mouseExit (const juce::MouseEvent&)
    {
        hovered = hitNothing;
        hoveredGap = -1;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }

    //==========================================================================
    // Edits

    void ChainView::resized()
    {
        const auto lane = laneArea();
        auto strip = juce::Rectangle<float> (lane.getX(), lane.getY(), 420.0f, lane.getHeight()).toNearestInt();
        addButton.setBounds (strip.removeFromLeft (172));
        strip.removeFromLeft (8);
        undoButton.setBounds (strip.removeFromLeft (70));
        strip.removeFromLeft (4);
        redoButton.setBounds (strip.removeFromLeft (70));
        strip.removeFromLeft (4);
        chainMenuButton.setBounds (strip.removeFromLeft ((int) lane.getHeight()));
        pendingReveal = false;
        if (selected >= 0)
            scrollToShow (dsp::selectionModule (selected), dsp::selectionInstance (selected), false);
    }

    void ChainView::toggleOn (ModuleId id, int instance)
    {
        if (auto* p = proc.getState().getParameter (onParamId (id, instance)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
            p->endChangeGesture();
        }
    }

    void ChainView::pushUndo()
    {
        undoStack.push_back (ParameterSnapshot::capture (proc.getState(), "chain edit"));
        if (undoStack.size() > maxUndo)
            undoStack.erase (undoStack.begin());
        redoStack.clear();
    }

    void ChainView::applyRouting (const dsp::Routing& r, const juce::String& message)
    {
        proc.setRouting (r);
        routing = proc.getRouting();
        undoButton.setEnabled (canUndo());
        redoButton.setEnabled (canRedo());
        if (message.isNotEmpty())
            showHint (message);
        repaint();
    }

    namespace
    {
        void setValue (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
        {
            if (auto* p = apvts.getParameter (id))
            {
                const float n = p->convertTo0to1 (value);
                if (std::abs (n - p->getValue()) < 1.0e-7f)
                    return;
                p->beginChangeGesture();
                p->setValueNotifyingHost (n);
                p->endChangeGesture();
            }
        }

        float getValue (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
        {
            auto* v = apvts.getRawParameterValue (id);
            return v != nullptr ? v->load() : 0.0f;
        }

        // Instance-1 IDs of every parameter of a module
        juce::StringArray moduleBaseIds (ModuleId id)
        {
            juce::StringArray ids;
            for (const auto& spec : getParameterSpecs())
                if (moduleOfParameter (spec) == id && dsp::splitInstanceParamId (spec.id).second == 0)
                    ids.add (spec.id);
            return ids;
        }
    }

    int ChainView::insertModule (ModuleId id, int index, bool asSend)
    {
        routing = proc.getRouting();
        const int k = routing.nextFreeInstance (id);
        if (k < 0)
        {
            showHint ("All " + juce::String (dsp::maxInstances (id)) + " " + dsp::moduleDisplayName (id) + " instances are already in the chain.");
            return -1;
        }
        pushUndo();

        // A fresh instance: default settings, still off while it joins the chain
        auto& apvts = proc.getState();
        const auto onId = onParamId (id, k);
        for (const auto& base : moduleBaseIds (id))
        {
            const auto pidK = dsp::instanceParamId (base, k);
            if (auto* p = apvts.getParameter (pidK))
                setValue (apvts, pidK, p->convertFrom0to1 (p->getDefaultValue()));
        }
        setValue (apvts, onId, 0.0f);

        auto r = routing;
        const int at = index < 0 ? r.size() : serialIndexToRoutingIndex (r, index, id, k);
        r.insertAt (at, id, k, ! (asSend && canBeSend (id)));
        applyRouting (r, cardName (r, id, k) + " added - it has its own settings. Undo removes it again.");

        // ...then switched on: it fades in (no click)
        setValue (apvts, onId, 1.0f);
        const int code = dsp::selectionCode (id, k);
        selected = code;
        if (onSelect)
            onSelect (code);
        scrollToShow (id, k);
        return code;
    }

    int ChainView::duplicateInstance (ModuleId id, int instance)
    {
        routing = proc.getRouting();
        const int k = routing.nextFreeInstance (id);
        if (k < 0 || ! routing.contains (id, instance))
        {
            showHint ("All " + juce::String (dsp::maxInstances (id)) + " " + dsp::moduleDisplayName (id) + " instances are already in the chain.");
            return -1;
        }
        pushUndo();

        auto& apvts = proc.getState();
        const auto onBase = juce::String (onParamFor (id));
        for (const auto& base : moduleBaseIds (id))
            if (base != onBase)
                setValue (apvts, dsp::instanceParamId (base, k), getValue (apvts, dsp::instanceParamId (base, instance)));
        setValue (apvts, onParamId (id, k), 0.0f);

        auto r = routing;
        r.insertAt (r.indexOf (id, instance) + 1, id, k, r.isInsert (id, instance));
        applyRouting (r, cardName (r, id, k) + " is a copy of " + cardName (r, id, instance) + " - change it independently.");
        setValue (apvts, onParamId (id, k), getValue (apvts, onParamId (id, instance)));

        const int code = dsp::selectionCode (id, k);
        selected = code;
        if (onSelect)
            onSelect (code);
        scrollToShow (id, k);
        return code;
    }

    bool ChainView::removeInstance (ModuleId id, int instance)
    {
        routing = proc.getRouting();
        if (! routing.contains (id, instance))
            return false;
        pushUndo();
        const auto name = cardName (routing, id, instance);

        // Off first (automation of an instance outside the chain would add it back)
        setValue (proc.getState(), onParamId (id, instance), 0.0f);
        auto r = routing;
        const int at = r.indexOf (id, instance);
        r.remove (id, instance);
        applyRouting (r, name + " removed. Undo brings it back with its settings.");

        if (selected == dsp::selectionCode (id, instance))
        {
            const int next = r.size() == 0 ? outputTile : dsp::selectionCode (r[std::min (at, r.size() - 1)].id, r[std::min (at, r.size() - 1)].instance);
            selected = next;
            if (onSelect)
                onSelect (next);
        }
        return true;
    }

    bool ChainView::moveInstance (ModuleId id, int instance, int newIndex)
    {
        auto r = proc.getRouting();
        if (! r.moveTo (id, instance, newIndex))
            return false;
        pushUndo();
        applyRouting (r);
        return true;
    }

    bool ChainView::setSendMode (ModuleId id, int instance, bool send)
    {
        auto r = proc.getRouting();
        if (! r.setInsert (id, instance, ! send))
            return false;
        pushUndo();
        applyRouting (r);
        return true;
    }

    bool ChainView::undo()
    {
        if (undoStack.empty())
            return false;
        redoStack.push_back (ParameterSnapshot::capture (proc.getState(), "redo"));
        const auto snapshot = undoStack.back();
        undoStack.pop_back();
        snapshot.applyTo (proc.getState(), true);
        applyRouting (proc.getRouting(), "Undone.");
        return true;
    }

    bool ChainView::redo()
    {
        if (redoStack.empty())
            return false;
        undoStack.push_back (ParameterSnapshot::capture (proc.getState(), "undo"));
        const auto snapshot = redoStack.back();
        redoStack.pop_back();
        snapshot.applyTo (proc.getState(), true);
        applyRouting (proc.getRouting(), "Redone.");
        return true;
    }

    void ChainView::restoreDefaultChain()
    {
        // The usual order: every instance keeps its settings, modules are sorted into the
        // order of a fresh chain (instances of one module stay together)
        const auto current = proc.getRouting();
        dsp::Routing sorted;
        for (const auto& ref : dsp::Routing::legacyFull())
            for (const auto& slot : current)
                if (slot.id == ref.id)
                    sorted.insertAt (sorted.size(), slot.id, slot.instance, slot.insert);
        if (sorted != current)
        {
            pushUndo();
            applyRouting (sorted, "Modules sorted into the usual order.");
        }
    }

    void ChainView::saveLayoutAsDefault()
    {
        settings::set (settings::myLayoutOrder, proc.getRouting().toString());
        showHint ("Chain saved as your default. Load it in any project from the chain menu.");
    }

    bool ChainView::loadDefaultLayout()
    {
        const auto order = settings::getString (settings::myLayoutOrder);
        if (order.isEmpty())
            return false;
        const auto r = dsp::Routing::fromString (dsp::Routing::fromString (order).toString());
        if (r != proc.getRouting())
        {
            pushUndo();
            applyRouting (r);
        }
        return true;
    }

    namespace
    {
        enum MenuAction { actOpen = 1, actToggle, actDuplicate, actRemove, actToStart, actToEnd, actHelp, actSend, actInsert };
        enum GlobalAction { actAdd = 90001, actSort, actUndo, actRedo, actSaveLayout, actLoadLayout };
        int menuId (MenuAction a, int code) { return (int) a * 10000 + code; }
    }

    void ChainView::showTileMenu (const Tile& t)
    {
        const int code = t.code();
        const bool on = isOn (t.id, t.instance);
        juce::PopupMenu menu;
        menu.addSectionHeader (cardName (routing, t.id, t.instance));
        menu.addItem (menuId (actOpen, code), "Open");
        menu.addItem (menuId (actToggle, code), on ? "Bypass (switch off)" : "Switch on");
        menu.addItem (menuId (actDuplicate, code), "Duplicate (new instance, same settings)", routing.nextFreeInstance (t.id) >= 0);
        menu.addItem (menuId (actRemove, code), "Remove from chain");
        if (canBeSend (t.id))
        {
            menu.addSeparator();
            const bool send = ! routing.isInsert (t.id, t.instance);
            menu.addItem (menuId (actSend, code), "Run as parallel send", true, send);
            menu.addItem (menuId (actInsert, code), "Run as insert in the chain", true, ! send);
        }
        if (routing.isInsert (t.id, t.instance))
        {
            menu.addSeparator();
            menu.addItem (menuId (actToStart, code), "Move to the start of the chain", routing.indexOf (t.id, t.instance) > 0);
            menu.addItem (menuId (actToEnd, code), "Move to the end of the chain", routing.indexOf (t.id, t.instance) < routing.size() - 1);
        }
        menu.addSeparator();
        menu.addItem (menuId (actHelp, code), "Help for " + juce::String (dsp::moduleDisplayName (t.id)));

        juce::Component::SafePointer<ChainView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withMinimumWidth (240),
                            [safe] (int result)
                            {
                                if (safe == nullptr || result <= 0)
                                    return;
                                const int action = result / 10000, c = result % 10000;
                                const auto id = dsp::selectionModule (c);
                                const int k = dsp::selectionInstance (c);
                                switch (action)
                                {
                                    case actOpen:      safe->selected = c; if (safe->onSelect) safe->onSelect (c); break;
                                    case actToggle:    safe->toggleOn (id, k); break;
                                    case actDuplicate: safe->duplicateInstance (id, k); break;
                                    case actRemove:    safe->removeInstance (id, k); break;
                                    case actSend:      safe->setSendMode (id, k, true); break;
                                    case actInsert:    safe->setSendMode (id, k, false); break;
                                    case actToStart:   safe->moveInstance (id, k, 0); break;
                                    case actToEnd:     safe->moveInstance (id, k, safe->routing.size() - 1); break;
                                    case actHelp:      if (safe->onHelp) safe->onHelp (c); break;
                                    default: break;
                                }
                                safe->repaint();
                            });
    }

    void ChainView::showChainMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Chain");
        menu.addItem (actAdd, "Add module...");
        menu.addItem (actUndo, "Undo", canUndo());
        menu.addItem (actRedo, "Redo", canRedo());
        menu.addSeparator();
        menu.addItem (actSort, "Sort into the usual order");
        menu.addItem (actSaveLayout, "Save this chain as my default");
        menu.addItem (actLoadLayout, "Load my default chain", settings::getString (settings::myLayoutOrder).isNotEmpty());

        juce::Component::SafePointer<ChainView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chainMenuButton).withMinimumWidth (240),
                            [safe] (int result)
                            {
                                if (safe == nullptr)
                                    return;
                                switch (result)
                                {
                                    case actAdd:        if (safe->onAddModule) safe->onAddModule (safe->numSerial, true); break;
                                    case actUndo:       safe->undo(); break;
                                    case actRedo:       safe->redo(); break;
                                    case actSort:       safe->restoreDefaultChain(); break;
                                    case actSaveLayout: safe->saveLayoutAsDefault(); break;
                                    case actLoadLayout: safe->loadDefaultLayout(); break;
                                    default: break;
                                }
                            });
    }
}
