#include "ChainView.h"
#include "../PluginProcessor.h"
#include "../StateKeys.h"
#include "UserSettings.h"
#include "Icons.h"
#include "SoloButton.h"
#include "../DSP/Solo.h"
#include "../Text/I18n.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        constexpr float corner = 10.0f;
        constexpr float plusR = 9.0f;
        constexpr float tileGap = 4.0f;
        constexpr int hitAddCard = -5, hitOutput = -100, hitNothing = -2;
        constexpr float scrollBarH = 8.0f;   // strip under the tiles for the thumb while the row scrolls

        // Vertical layout of a tile (designed for the full 88 px row), scaled to a lower tile
        float tileScale (juce::Rectangle<float> tile) { return tile.getHeight() / (float) ChainView::preferredHeight; }

        bool canBeSend (ModuleId id) { return dsp::Routing::canBeSend (id); }

        void drawPowerGlyph (juce::Graphics& g, juce::Rectangle<float> r, bool on, float hover, juce::Colour hue)
        {
            const float size = std::min (r.getWidth(), r.getHeight());
            r = r.withSizeKeepingCentre (size, size);
            if (on)
                drawGlow (g, r.getCentre(), size * 0.9f, 0.3f, hue);
            g.setColour (colours::background.withAlpha (0.9f));
            g.fillEllipse (r);
            g.setColour (on ? hue.withAlpha (0.75f) : colours::line.brighter (0.35f + 0.4f * hover));
            g.drawEllipse (r.reduced (0.6f), 1.2f);

            const auto c = r.getCentre();
            const float rad = size * 0.22f;
            juce::Path p;
            p.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
            p.startNewSubPath (c.x, c.y - rad * 1.3f);
            p.lineTo (c.x, c.y - rad * 0.2f);
            g.setColour (on ? hue : colours::textFaint.brighter (0.3f * hover));
            g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void drawPlus (juce::Graphics& g, juce::Point<float> c, float size, juce::Colour colour, float thickness = 1.8f)
        {
            g.setColour (colour);
            g.fillRoundedRectangle (juce::Rectangle<float> (size, thickness).withCentre (c), thickness * 0.5f);
            g.fillRoundedRectangle (juce::Rectangle<float> (thickness, size).withCentre (c), thickness * 0.5f);
        }

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

        juce::String chainEdit() { return i18n::tr ("Kette bearbeiten", "Edit chain"); }
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

    juce::String ChainView::cardName (const dsp::Routing& r, ModuleId id, int instance)
    {
        const juce::String name (dsp::moduleDisplayName (id));
        return instance > 0 || r.countOf (id) > 1 ? name + " " + juce::String (instance + 1) : name;
    }

    ChainView::ChainView (VelvoxAudioProcessor& p) : proc (p)
    {
        routing = proc.getRouting();
    }

    bool ChainView::isOn (ModuleId id, int instance) const
    {
        auto* v = proc.getState().getRawParameterValue (onParamId (id, instance));
        return v != nullptr && v->load() > 0.5f;
    }

    //==========================================================================
    // Geometry: [ serial tiles | + ] ¦ [ send tiles ]  ...  [ OUTPUT ]

    juce::Rectangle<float> ChainView::outputArea() const
    {
        return getLocalBounds().toFloat().removeFromRight (outputW);
    }

    juce::Rectangle<float> ChainView::rowArea() const
    {
        return getLocalBounds().toFloat().withTrimmedRight (outputW + tileGap * 2.0f);
    }

    juce::Rectangle<float> ChainView::addCardArea() const
    {
        const auto row = rowArea();
        return { row.getX() + (float) numSerial * (serialW + tileGap) - scroll.current, row.getY(), addTileW, tileHeight };
    }

    // Send zone behind the add tile and a divider; at least one tile wide, so a tile can always be dropped there
    juce::Rectangle<float> ChainView::laneArea() const
    {
        const auto add = addCardArea();
        return { add.getRight() + tileGap, add.getY(), sendGap + (float) std::max (1, numSends) * (serialW + tileGap), add.getHeight() };
    }

    float ChainView::contentWidth() const
    {
        return (float) numSerial * (serialW + tileGap) + addTileW + tileGap + sendGap + (float) std::max (1, numSends) * (serialW + tileGap);
    }

    float ChainView::getMaxScroll()
    {
        layoutTiles();
        return std::max (0.0f, contentWidth() - rowArea().getWidth());
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

        std::vector<const dsp::ChainSlot*> serial, sends;
        for (const auto& slot : routing)
            (! slot.insert ? sends : serial).push_back (&slot);

        numSerial = (int) serial.size();
        numSends = (int) sends.size();
        // Tiles shrink a little before the row starts to scroll
        const int columns = numSerial + std::max (1, numSends);
        const float fit = (row.getWidth() - addTileW - sendGap - tileGap * (float) (columns + 1)) / (float) std::max (1, columns);
        serialW = juce::jlimit (minTileW, tileW, fit);

        const float maxScroll = std::max (0.0f, contentWidth() - row.getWidth());
        if (scroll.target > maxScroll || scroll.current > maxScroll)
        {
            scroll.target = std::min (scroll.target, maxScroll);
            scroll.current = std::min (scroll.current, maxScroll);
        }
        // While the row scrolls, the tiles leave a strip at the bottom for the scroll thumb
        tileHeight = row.getHeight() - (maxScroll > 0.0f ? scrollBarH : 0.0f);

        for (int i = 0; i < numSerial; ++i)
            tiles.push_back ({ serial[(size_t) i]->id, serial[(size_t) i]->instance,
                               { row.getX() + (float) i * (serialW + tileGap) - scroll.current, row.getY(), serialW, tileHeight },
                               false, i + 1 });

        float x = laneArea().getX() + sendGap;
        for (auto* s : sends)
        {
            tiles.push_back ({ s->id, s->instance, { x, row.getY(), serialW, tileHeight }, true, 0 });
            x += serialW + tileGap;
        }
    }

    const ChainView::Tile* ChainView::findTile (ModuleId id, int instance) const
    {
        for (auto& t : tiles)
            if (t.id == id && t.instance == instance)
                return &t;
        return nullptr;
    }

    // Tile: name (top), icon (middle), on/off (bottom) - in this order
    juce::Rectangle<float> ChainView::nameBounds (const Tile& t) const
    {
        const float k = tileScale (t.bounds);
        return t.bounds.withHeight (nameH * k).reduced (6.0f, 0.0f).translated (0.0f, 4.0f * k);
    }

    juce::Rectangle<float> ChainView::iconBounds (const Tile& t) const
    {
        const float k = tileScale (t.bounds);
        const float size = (iconH - 6.0f) * k;
        return juce::Rectangle<float> (size, size).withCentre ({ t.bounds.getCentreX(), t.bounds.getY() + (4.0f + nameH + iconH * 0.5f) * k });
    }

    juce::Rectangle<float> ChainView::powerBounds (const Tile& t) const
    {
        const float k = tileScale (t.bounds);
        return juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ t.bounds.getCentreX(), t.bounds.getY() + (4.0f + nameH + iconH + powerH * 0.5f) * k });
    }

    // Solo "S": bottom right, on the line of the power switch
    juce::Rectangle<float> ChainView::soloBounds (const Tile& t) const
    {
        if (! dsp::canSolo (t.id))
            return {};
        return juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ t.bounds.getRight() - 15.0f, powerBounds (t).getCentreY() });
    }

    juce::Rectangle<float> ChainView::getSoloBounds (ModuleId id, int instance)
    {
        layoutTiles();
        if (auto* t = findTile (id, instance))
            return soloBounds (*t);
        return {};
    }

    juce::Rectangle<float> ChainView::getTileBounds (ModuleId id, int instance)
    {
        layoutTiles();
        if (auto* t = findTile (id, instance))
            return t->bounds;
        return {};
    }

    juce::Rectangle<float> ChainView::getNameBounds (ModuleId id, int instance)
    {
        layoutTiles();
        if (auto* t = findTile (id, instance))
            return nameBounds (*t);
        return {};
    }

    juce::Rectangle<float> ChainView::getIconBounds (ModuleId id, int instance)
    {
        layoutTiles();
        if (auto* t = findTile (id, instance))
            return iconBounds (*t);
        return {};
    }

    juce::Rectangle<float> ChainView::getPowerBounds (ModuleId id, int instance)
    {
        layoutTiles();
        if (auto* t = findTile (id, instance))
            return powerBounds (*t);
        return {};
    }

    juce::Point<float> ChainView::gapCentre (int insertIndex) const
    {
        const auto row = rowArea();
        const float x = row.getX() + (float) insertIndex * (serialW + tileGap) - tileGap * 0.5f - scroll.current;
        return { x, row.getCentreY() };
    }

    int ChainView::gapAt (juce::Point<float> p) const
    {
        const auto row = rowArea();
        if (! row.contains (p) || dragging)
            return -1;
        for (int i = 1; i < numSerial; ++i)   // between two tiles (the ends have the + tile)
        {
            const auto c = gapCentre (i);
            if (std::abs (p.x - c.x) <= 5.0f && std::abs (p.y - c.y) <= plusR + 4.0f)
                return i;
        }
        return -1;
    }

    int ChainView::tileAt (juce::Point<float> p) const
    {
        const auto row = rowArea();
        if (outputArea().contains (p))
            return hitOutput;
        if (! row.contains (p))
            return hitNothing;
        for (int i = 0; i < (int) tiles.size(); ++i)
            if (tiles[(size_t) i].bounds.contains (p))
                return i;
        if (addCardArea().contains (p))
            return hitAddCard;
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
        if (t == nullptr)
            return;
        const auto row = rowArea();
        const float x = t->bounds.getX() + scroll.current - row.getX();
        // Clear of the edge arrows (they stay where more tiles follow; at the very ends they vanish)
        constexpr float pad = 28.0f;
        float target = scroll.target;
        if (x - pad < target)
            target = x - pad;
        else if (x + serialW + pad > target + row.getWidth())
            target = x + serialW + pad - row.getWidth();
        target = juce::jlimit (0.0f, maxScrollNow(), target);
        if (std::abs (target - scroll.target) > 0.5f)
            scrollTo (target, animate);
    }

    bool ChainView::isTileFullyVisible (ModuleId id, int instance)
    {
        const auto b = getTileBounds (id, instance);
        // The edge arrows count as covering
        auto visible = rowArea().expanded (0.5f);
        if (const auto left = arrowArea (false); ! left.isEmpty())
            visible.setLeft (left.getRight());
        if (const auto right = arrowArea (true); ! right.isEmpty())
            visible.setRight (right.getX());
        return ! b.isEmpty() && visible.contains (b);
    }

    void ChainView::setSelected (int code)
    {
        selected = code;
        if (code >= 0)
        {
            if (getWidth() > 0)
                scrollToShow (dsp::selectionModule (code), dsp::selectionInstance (code), isShowing());
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
        // Vertical wheel, Shift + wheel, tilt wheel (WM_MOUSEHWHEEL) and two-finger swipes all move the row;
        // the larger axis wins
        const float delta = (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY)
                            * (wheel.isReversed ? -1.0f : 1.0f);
        if (wheel.isSmooth)
        {
            // Trackpad / Magic Mouse: JUCE gives 0.5 / 256 per pixel - follow the fingers 1:1, no glide lag
            scrollTo (scroll.target - delta * 512.0f, false);
            return;
        }
       #if JUCE_MAC
        constexpr float pixelsPerUnit = 2400.0f;   // a mouse notch on macOS is ~0.04
       #else
        constexpr float pixelsPerUnit = 200.0f;    // a notch on Windows is 120 / 256: about one tile
       #endif
        // Small steps (precision touchpads send many) follow directly, wheel notches glide
        scrollTo (scroll.target - delta * pixelsPerUnit, std::abs (delta) >= 0.2f);
    }

    float ChainView::scrollStep() const noexcept { return serialW + tileGap; }

    float ChainView::maxScrollNow() const
    {
        return std::max (0.0f, contentWidth() - rowArea().getWidth());
    }

    // Thumb track: a thin line along the bottom edge of the row (over the tiles' lower rim)
    juce::Rectangle<float> ChainView::trackArea() const
    {
        if (maxScrollNow() <= 0.0f)
            return {};
        const auto row = rowArea();
        return { row.getX() + 6.0f, row.getBottom() - 5.0f, row.getWidth() - 12.0f, 3.0f };
    }

    juce::Rectangle<float> ChainView::thumbArea() const
    {
        const auto track = trackArea();
        if (track.isEmpty())
            return {};
        const float content = contentWidth();
        const float w = juce::jlimit (32.0f, track.getWidth(), track.getWidth() * rowArea().getWidth() / std::max (1.0f, content));
        const float maxScroll = maxScrollNow();
        const float x = track.getX() + (track.getWidth() - w) * juce::jlimit (0.0f, 1.0f, scroll.current / std::max (1.0f, maxScroll));
        return { x, track.getY(), w, track.getHeight() };
    }

    // Edge arrows at the icon line (name and power of the tile below stay clickable)
    juce::Rectangle<float> ChainView::arrowArea (bool right) const
    {
        const float maxScroll = maxScrollNow();
        if (maxScroll <= 0.0f || (right ? scroll.target >= maxScroll - 0.5f : scroll.target <= 0.5f))
            return {};
        const auto row = rowArea();
        const float cy = row.getY() + 4.0f + nameH + iconH * 0.5f;
        return juce::Rectangle<float> (22.0f, 30.0f).withCentre ({ right ? row.getRight() - 13.0f : row.getX() + 13.0f, cy });
    }

    juce::Rectangle<float> ChainView::getScrollTrackBounds()  { layoutTiles(); return trackArea(); }
    juce::Rectangle<float> ChainView::getScrollThumbBounds()  { layoutTiles(); return thumbArea(); }
    juce::Rectangle<float> ChainView::getScrollArrowBounds (bool right) { layoutTiles(); return arrowArea (right); }

    void ChainView::drawScrollParts (juce::Graphics& g) const
    {
        const auto track = trackArea();
        if (track.isEmpty())
            return;
        // Thumb: always faintly there when the row scrolls, clearer on hover / while dragging
        const float glow = juce::jmax (barGlow.current, pan == Pan::thumb ? 1.0f : 0.0f);
        g.setColour (colours::border.brighter (0.2f).withAlpha (0.5f + 0.4f * glow));
        g.fillRoundedRectangle (track, 1.5f);
        g.setColour (colours::textMuted.withAlpha (0.45f + 0.5f * glow));
        g.fillRoundedRectangle (thumbArea(), 1.5f);

        for (bool right : { false, true })
        {
            const auto a = arrowArea (right);
            if (a.isEmpty())
                continue;
            const bool hov = hoveredArrow == (right ? 1 : -1);
            g.setColour (colours::raisedHi.withAlpha (hov ? 0.98f : 0.85f));
            g.fillRoundedRectangle (a, 7.0f);
            g.setColour (hov ? colours::teal.withAlpha (0.7f) : colours::border.brighter (0.4f));
            g.drawRoundedRectangle (a.reduced (0.5f), 7.0f, 1.0f);
            const auto c = a.getCentre();
            const float d = right ? 1.0f : -1.0f;
            juce::Path chevron;
            chevron.startNewSubPath (c.x - 2.5f * d, c.y - 5.0f);
            chevron.lineTo (c.x + 2.5f * d, c.y);
            chevron.lineTo (c.x - 2.5f * d, c.y + 5.0f);
            g.setColour (hov ? colours::textStrong : colours::textSoft);
            g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    void ChainView::showHint (const juce::String& text)
    {
        hint = text;
        hintUntil = juce::Time::getMillisecondCounter() + 3500;
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
        bool changed = bypassedNow != globalBypass;
        globalBypass = bypassedNow;
        if (proc.getSolo() != shownSolo)
        {
            shownSolo = proc.getSolo();
            changed = true;
        }

        const auto r = proc.getRouting();
        if (r != routing)
        {
            routing = r;
            changed = true;
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

        // Dragging a tile close to an edge of the row scrolls it along
        if (dragging && dragTile >= 0)
        {
            const auto row = rowArea();
            constexpr float zone = 48.0f, maxSpeed = 12.0f;
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
        barGlow.target = hoveredBar || pan == Pan::thumb ? 1.0f : (isMouseOver() ? 0.35f : 0.0f);
        moved = barGlow.tick (0.2f) || moved;

        if (moved)
            repaint();
    }

    //==========================================================================
    // Drawing

    void ChainView::drawIcon (juce::Graphics& g, ModuleId id, juce::Rectangle<float> area, float alpha) const
    {
        auto& d = iconCache[(int) id];
        if (d == nullptr)
        {
            const auto svg = moduleIconSvg (id).replace ("#FFFFFF", "#" + moduleColour (id).toDisplayString (false));
            const auto utf8 = svg.toUTF8();
            d = juce::Drawable::createFromImageData (utf8.getAddress(), utf8.sizeInBytes() - 1);
        }
        if (d == nullptr)
            return;
        drawGlow (g, area.getCentre(), area.getWidth() * 0.62f, 0.18f * alpha, moduleColour (id));
        d->drawWithin (g, area, juce::RectanglePlacement::centred, alpha);
    }

    void ChainView::drawTile (juce::Graphics& g, const Tile& t, juce::Rectangle<float> r, bool ghost) const
    {
        const auto code = t.code();
        const auto onIt = onState.find (code);
        const bool switchedOn = onIt != onState.end() ? onIt->second : isOn (t.id, t.instance);
        const bool on = switchedOn;   // 0.6.1: the Simple sliders switch modules on for real - no "engaged only" state
        const bool isSelected = code == selected && ! ghost;
        const auto glowIt = hoverGlow.find (code);
        const float hov = ghost ? 0.0f : (glowIt != hoverGlow.end() ? glowIt->second.current : 0.0f);
        const auto hue = moduleColour (t.id);
        const float content = on ? 1.0f : 0.35f;   // bypassed: content dimmed, the switch stays clear

        // Body
        g.setColour (ghost ? colours::raisedHi : colours::panel);
        g.fillRoundedRectangle (r, corner);
        if (isSelected)
        {
            g.setGradientFill (juce::ColourGradient (hue.withAlpha (0.18f), r.getX(), r.getY(), hue.withAlpha (0.03f), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, corner);
        }
        if (hov > 0.01f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.03f * hov));
            g.fillRoundedRectangle (r, corner);
        }
        g.setColour (isSelected ? hue.withAlpha (0.8f) : (ghost ? hue.withAlpha (0.6f) : colours::border.brighter (0.15f * hov)));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, isSelected ? 1.3f : 1.0f);

        Tile shifted = t;
        shifted.bounds = r;

        // 1) Name
        const auto nameArea = nameBounds (shifted);
        g.setColour ((on ? colours::textStrong : colours::textMuted));
        g.setFont (semiBold (9.5f, 0.12f));
        g.drawFittedText (cardName (routing, t.id, t.instance).toUpperCase(), nameArea.toNearestInt(), juce::Justification::centred, 1, 0.7f);

        // 2) Icon (with a SEND badge on sends)
        const auto iconArea = iconBounds (shifted);
        drawIcon (g, t.id, iconArea, content);
        if (t.inLane)
        {
            const auto badge = juce::Rectangle<float> (30.0f, 12.0f).withPosition (r.getRight() - 33.0f, iconArea.getY() - 1.0f);
            g.setColour (hue.withAlpha (0.18f));
            g.fillRoundedRectangle (badge, 6.0f);
            g.setColour (colours::textStrong.withAlpha (content));
            g.setFont (semiBold (7.5f, 0.15f));
            g.drawText ("SEND", badge, juce::Justification::centred, false);
        }

        // 3) On / off
        drawPowerGlyph (g, powerBounds (shifted), switchedOn, hov, hue);

        // Activity line along the bottom edge (gain reduction, ducking, movement)
        const float act = activityOf (t);
        if (switchedOn && act > 0.01f && ! ghost)
        {
            const auto bar = juce::Rectangle<float> (r.getX() + 10.0f, r.getBottom() - 3.0f, r.getWidth() - 20.0f, 1.5f);
            g.setColour (hue.withAlpha (0.9f));
            g.fillRoundedRectangle (bar.withWidth (std::max (3.0f, bar.getWidth() * act)), 0.75f);
        }
        if (switchedOn && globalBypass && ! ghost)
        {
            g.setColour (colours::bypass.withAlpha (0.6f));
            g.drawRoundedRectangle (r.reduced (2.0f), corner - 2.0f, 1.0f);
        }

        // Solo: the soloed tile framed in solo yellow, every other module dimmed (it is bypassed now)
        const int solo = proc.getSolo();
        const bool soloed = solo >= 0 && solo == code;
        if (solo >= 0 && ! soloed && dsp::canSolo (t.id) && ! ghost)
        {
            g.setColour (colours::windowBg.withAlpha (0.62f));
            g.fillRoundedRectangle (r, corner);
        }
        if (soloed)
        {
            g.setColour (soloColour.withAlpha (0.85f));
            g.drawRoundedRectangle (r.reduced (0.7f), corner, 1.4f);
        }
        // The S: on hover (fades with it) and always while soloed
        const auto s = soloBounds (shifted);
        if (! s.isEmpty() && (soloed || hov > 0.01f))
            drawSoloGlyph (g, s, soloed, hoveredSolo && hovered >= 0 && hovered < (int) tiles.size()
                                                 && tiles[(size_t) hovered].code() == code ? 1.0f : 0.0f,
                           soloed ? 1.0f : hov);
    }

    void ChainView::drawAddCard (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto it = hoverGlow.find (-77);
        const float hov = it != hoverGlow.end() ? it->second.current : 0.0f;
        juce::Path outline;
        outline.addRoundedRectangle (r.reduced (1.0f), corner);
        juce::Path dashed;
        const float dashes[] { 4.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (colours::teal.withAlpha (0.03f + 0.06f * hov));
        g.fillRoundedRectangle (r.reduced (1.0f), corner);
        g.setColour ((hov > 0.5f ? colours::teal : colours::textDim).withAlpha (0.5f + 0.4f * hov));
        g.fillPath (dashed);
        drawPlus (g, r.getCentre(), 16.0f, hov > 0.5f ? colours::teal : colours::textSoft, 2.0f);
    }

    void ChainView::drawOutput (juce::Graphics& g) const
    {
        const auto r = outputArea();
        const bool isSel = selected == outputTile;
        const bool hov = hovered == hitOutput;
        g.setColour (isSel ? colours::teal.withAlpha (0.10f) : colours::panel);
        g.fillRoundedRectangle (r, corner);
        g.setColour (isSel ? colours::teal.withAlpha (0.7f) : (hov ? colours::border.brighter (0.5f) : colours::border));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);

        g.setColour (isSel ? colours::textStrong : colours::textMuted);
        g.setFont (semiBold (9.0f, 0.12f));
        g.drawText ("OUTPUT", r.withHeight (nameH).translated (0.0f, 4.0f), juce::Justification::centred, false);
        const auto c = juce::Point<float> (r.getCentreX(), r.getY() + 4.0f + nameH + iconH * 0.5f);
        juce::Path arrow;
        arrow.startNewSubPath (c.x - 9.0f, c.y);
        arrow.lineTo (c.x + 7.0f, c.y);
        arrow.startNewSubPath (c.x + 2.0f, c.y - 5.0f);
        arrow.lineTo (c.x + 7.0f, c.y);
        arrow.lineTo (c.x + 2.0f, c.y + 5.0f);
        g.setColour (isSel ? colours::teal : colours::textDim);
        g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // What leaves the plugin while a module is soloed: a SOLO tag
        if (proc.isSoloActive())
        {
            const auto tag = juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ r.getCentreX(), r.getY() + 4.0f + nameH + iconH + powerH * 0.5f });
            g.setColour (soloColour);
            g.fillRoundedRectangle (tag, 7.0f);
            g.setColour (colours::windowBg);
            g.setFont (semiBold (8.0f, 0.15f));
            g.drawText ("SOLO", tag, juce::Justification::centred, false);
        }
    }

    void ChainView::paint (juce::Graphics& g)
    {
        layoutTiles();
        const auto row = rowArea();
        const auto lane = laneArea();
        const bool draggingSendable = dragging && dragTile >= 0 && canBeSend (tiles[(size_t) dragTile].id);

        drawOutput (g);
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (row.toNearestInt());

            for (int i = 0; i < (int) tiles.size(); ++i)
                if (! (dragging && i == dragTile))
                    drawTile (g, tiles[(size_t) i], tiles[(size_t) i].bounds, false);
            drawAddCard (g, addCardArea());

            // Send zone: dashed divider; empty: a faint caption only while a sendable tile is dragged
            {
                const float x = lane.getX() + sendGap * 0.5f;
                juce::Path line, dashed;
                line.startNewSubPath (x, row.getY() + 10.0f);
                line.lineTo (x, row.getBottom() - 10.0f);
                const float dashes[] { 3.0f, 4.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
                g.setColour (colours::border.brighter (0.25f));
                g.fillPath (dashed);
                if (draggingSendable)
                {
                    const bool over = lane.contains (dragPos);
                    g.setColour (colours::teal.withAlpha (over ? 0.10f : 0.04f));
                    g.fillRoundedRectangle (lane.withTrimmedLeft (sendGap), corner);
                    g.setColour (colours::textMuted);
                    g.setFont (semiBold (9.0f, 0.2f));
                    g.drawText ("SEND", lane.withTrimmedLeft (sendGap), juce::Justification::centred, false);
                }
            }

            // "+" on a gap: insert a module exactly there
            if (hoveredGap >= 0 && gapGlow.current > 0.01f)
            {
                const auto c = gapCentre (hoveredGap);
                drawGlow (g, c, 18.0f, 0.35f * gapGlow.current, colours::teal);
                g.setColour (colours::raisedHi.withAlpha (gapGlow.current));
                g.fillEllipse (juce::Rectangle<float> (2.0f * plusR, 2.0f * plusR).withCentre (c));
                g.setColour (colours::teal.withAlpha (0.9f * gapGlow.current));
                g.drawEllipse (juce::Rectangle<float> (2.0f * plusR, 2.0f * plusR).withCentre (c), 1.2f);
                drawPlus (g, c, 9.0f, colours::text.withAlpha (gapGlow.current));
            }

            // Insertion marker while dragging
            if (dragging && dragTile >= 0 && ! lane.contains (dragPos))
            {
                const auto& t = tiles[(size_t) dragTile];
                const int idx = insertIndexForX (dragPos.x);
                float markerX = row.getX() - scroll.current + 1.0f;
                int count = 0;
                for (auto& other : tiles)
                {
                    if (other.inLane || (other.id == t.id && other.instance == t.instance))
                        continue;
                    if (count++ < idx)
                        markerX = other.bounds.getRight() + tileGap * 0.5f;
                }
                markerX = juce::jlimit (row.getX() + 1.5f, row.getRight() - 1.5f, markerX);
                drawGlow (g, { markerX, row.getCentreY() }, 18.0f, 0.35f, colours::teal);
                g.setColour (colours::teal);
                g.fillRoundedRectangle (markerX - 1.5f, row.getY() + 6.0f, 3.0f, row.getHeight() - 12.0f, 1.5f);
            }

            // More tiles beyond the edges: soft fades
            const float maxScroll = std::max (0.0f, contentWidth() - row.getWidth());
            if (scroll.current > 0.5f)
            {
                g.setGradientFill (juce::ColourGradient (colours::windowBg, row.getX(), 0.0f, colours::windowBg.withAlpha (0.0f), row.getX() + 28.0f, 0.0f, false));
                g.fillRect (row.withWidth (28.0f));
            }
            if (scroll.current < maxScroll - 0.5f)
            {
                g.setGradientFill (juce::ColourGradient (colours::windowBg.withAlpha (0.0f), row.getRight() - 28.0f, 0.0f, colours::windowBg, row.getRight(), 0.0f, false));
                g.fillRect (row.withLeft (row.getRight() - 28.0f));
            }

            // Thumb and edge arrows (only while the row does not fit)
            if (! dragging)
                drawScrollParts (g);
        }

        // Dragged tile follows the mouse
        if (dragging && dragTile >= 0)
        {
            const auto& t = tiles[(size_t) dragTile];
            const auto r = t.bounds.withPosition (dragPos - dragOffset);
            if (lane.contains (dragPos) && ! canBeSend (t.id))
            {
                g.setColour (colours::warning.withAlpha (0.10f));
                g.fillRect (lane);
            }
            g.setOpacity (0.95f);
            drawTile (g, t, r, true);
        }

        if (hint.isNotEmpty())
        {
            const auto area = row.withSizeKeepingCentre (std::min (row.getWidth() - 40.0f, 560.0f), 30.0f);
            g.setColour (colours::raisedHi.withAlpha (0.97f));
            g.fillRoundedRectangle (area, 8.0f);
            g.setColour (colours::cyan.withAlpha (0.5f));
            g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);
            g.setColour (colours::text);
            g.setFont (medium (12.0f));
            g.drawFittedText (hint, area.reduced (12.0f, 2.0f).toNearestInt(), juce::Justification::centred, 1, 0.85f);
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

        if (e.mods.isPopupMenu())
        {
            if (i >= 0)
                showTileMenu (tiles[(size_t) i]);
            else
                showChainMenu();
            return;
        }

        // Scrolling: edge arrows (a page), the thumb / its track, a drag of the strip itself
        pan = Pan::none;
        for (bool right : { false, true })
            if (arrowArea (right).contains (e.position))
            {
                const float page = std::max (scrollStep(), rowArea().getWidth() - scrollStep());
                scrollTo (scroll.target + (right ? page : -page));
                return;
            }
        if (const auto track = trackArea(); ! track.isEmpty() && e.position.y >= rowArea().getY() + tileHeight
                                            && e.position.x >= track.getX() - 4.0f && e.position.x <= track.getRight() + 4.0f)
        {
            const auto thumb = thumbArea();
            if (e.position.x < thumb.getX() - 4.0f || e.position.x > thumb.getRight() + 4.0f)
            {
                // On the track: the thumb jumps there (centred under the mouse), then follows the drag
                const float travel = std::max (1.0f, track.getWidth() - thumb.getWidth());
                scrollTo ((e.position.x - thumb.getWidth() * 0.5f - track.getX()) / travel * maxScrollNow(), false);
            }
            pan = Pan::thumb;
            panStartScroll = scroll.target;
            panStartX = e.position.x;
            repaint();
            return;
        }
        if (e.mods.isMiddleButtonDown() || e.mods.isShiftDown() || (i == hitNothing && rowArea().contains (e.position) && gapAt (e.position) < 0))
        {
            pan = Pan::strip;
            panStartScroll = scroll.target;
            panStartX = e.position.x;
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            return;
        }

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
            // The SOLO tag on OUTPUT ends the solo
            const auto out = outputArea();
            if (proc.isSoloActive() && e.position.y > out.getY() + 4.0f + nameH + iconH)
            {
                proc.setSolo (-1);
                shownSolo = -1;
                repaint();
                return;
            }
            selected = outputTile;
            if (onSelect)
                onSelect (outputTile);
            repaint();
            return;
        }
        if (i < 0)
            return;

        const auto t = tiles[(size_t) i];
        if (dsp::canSolo (t.id) && (soloBounds (t).expanded (3.0f).contains (e.position) || e.mods.isAltDown()))
        {
            proc.toggleSolo (t.code());
            shownSolo = proc.getSolo();
            repaint();
            return;
        }
        if (powerBounds (t).expanded (4.0f).contains (e.position))
        {
            toggleOn (t.id, t.instance);
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
        if (pan == Pan::strip)
        {
            scrollTo (panStartScroll - (e.position.x - panStartX), false);
            return;
        }
        if (pan == Pan::thumb)
        {
            const auto track = trackArea();
            const float travel = std::max (1.0f, track.getWidth() - thumbArea().getWidth());
            scrollTo (panStartScroll + (e.position.x - panStartX) / travel * maxScrollNow(), false);
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
        if (pan != Pan::none)
        {
            pan = Pan::none;
            repaint();
            return;
        }
        if (! dragging || dragTile < 0)
        {
            // A click on a tile: it slides fully into view (a half-hidden one at an edge)
            if (dragTile >= 0 && dragTile < (int) tiles.size())
                scrollToShow (tiles[(size_t) dragTile].id, tiles[(size_t) dragTile].instance);
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
                showHint (i18n::format (i18n::tr ("{0} bleibt in der Kette - nur Delay und Reverb laufen als Send.",
                                                  "{0} stays in the chain - only Delay and Reverb run as a send."),
                                        { cardName (routing, t.id, t.instance) }));
        }
        else
        {
            // Serial position among the visible serial tiles -> routing index
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
            UndoHistory::ScopedTransaction undoStep (proc.getUndo(), chainEdit());
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
        const bool overSolo = i >= 0 && soloBounds (tiles[(size_t) i]).expanded (3.0f).contains (e.position);
        const int arrow = arrowArea (false).contains (e.position) ? -1 : (arrowArea (true).contains (e.position) ? 1 : 0);
        const auto track = trackArea();
        const bool bar = ! track.isEmpty() && e.position.y >= rowArea().getY() + tileHeight && e.position.x >= track.getX() - 4.0f
                         && e.position.x <= track.getRight() + 4.0f;
        if (arrow != hoveredArrow || bar != hoveredBar)
        {
            hoveredArrow = arrow;
            hoveredBar = bar;
            hovered = -99;   // forces the info below to refresh
        }
        if (arrow != 0 || bar)
        {
            if (hovered != hitNothing || hoveredGap >= 0 || hoveredSolo)
            {
                hovered = hitNothing;
                hoveredGap = -1;
                hoveredSolo = false;
                setMouseCursor (juce::MouseCursor::PointingHandCursor);
                setTooltip (arrow != 0 ? i18n::tr ("Weitere Module - Kette scrollen (auch Mausrad, Shift+Ziehen)",
                                                  "More modules - scroll the chain (also mouse wheel, Shift+drag)")
                                       : i18n::tr ("Kette scrollen: ziehen", "Scroll the chain: drag"));
                repaint();
            }
            return;
        }
        if (i != hovered || g != hoveredGap || overSolo != hoveredSolo)
            updateHoverInfo (i, g, overSolo);
    }

    void ChainView::updateHoverInfo (int i, int g, bool overSolo)
    {
        hovered = i;
        hoveredGap = g;
        hoveredSolo = overSolo;
        const bool clickable = g >= 0 || i >= 0 || i == hitOutput || i == hitAddCard;
        setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        if (overSolo)
            setTooltip (i18n::tr ("Solo: nur dieses Modul hören, alle anderen umgangen (nochmal klicken: aus)",
                                  "Solo: hear only this module, all others bypassed (click again: off)"));
        else if (i >= 0)
            setTooltip (juce::String (dsp::moduleDisplayName (tiles[(size_t) i].id))
                        + (dsp::canSolo (tiles[(size_t) i].id) ? i18n::tr (" - Klick öffnet, Ziehen ordnet um, S oder Alt+Klick: Solo, Rechtsklick: Menü",
                                                                          " - click opens, drag reorders, S or Alt+click: solo, right-click: menu")
                                                               : i18n::tr (" - Klick öffnet, Ziehen ordnet um, Rechtsklick: Menü",
                                                                          " - click opens, drag reorders, right-click: menu")));
        else if (i == hitAddCard || g >= 0)
            setTooltip (i18n::tr ("Modul hinzufügen", "Add module"));
        else if (i == hitOutput)
            setTooltip (proc.isSoloActive() ? i18n::tr ("Ausgang - SOLO aktiv: Klick auf SOLO hebt es auf", "Output - SOLO active: click SOLO to end it")
                                            : i18n::tr ("Ausgang: Eingangs-Gain, Stereo, Level Match, Clip Guard", "Output: input gain, stereo, level match, clip guard"));
        else
            setTooltip ({});
        repaint();
    }

    void ChainView::mouseExit (const juce::MouseEvent&)
    {
        hovered = hitNothing;
        hoveredGap = -1;
        hoveredSolo = false;
        hoveredArrow = 0;
        hoveredBar = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }

    //==========================================================================
    // Edits

    void ChainView::resized()
    {
        pendingReveal = false;
        if (selected >= 0)
            scrollToShow (dsp::selectionModule (selected), dsp::selectionInstance (selected), false);
    }

    bool ChainView::moveBy (ModuleId id, int instance, int steps)
    {
        // One serial position earlier / later (sends are not counted)
        auto r = proc.getRouting();
        int serialPos = -1, count = 0;
        for (const auto& s : r)
            if (s.insert)
            {
                if (s.sameInstance (id, instance))
                    serialPos = count;
                ++count;
            }
        const int target = serialPos + steps;
        if (serialPos < 0 || target < 0 || target >= count)
            return false;
        const int routingIndex = serialIndexToRoutingIndex (r, target, id, instance);
        const int from = r.indexOf (id, instance);
        r.moveTo (id, instance, from < routingIndex ? routingIndex - 1 : routingIndex);
        if (r == routing)
            return false;
        UndoHistory::ScopedTransaction undoStep (proc.getUndo(), chainEdit());
        applyRouting (r);
        scrollToShow (id, instance);
        return true;
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

    void ChainView::applyRouting (const dsp::Routing& r, const juce::String& message)
    {
        proc.setRouting (r);
        routing = proc.getRouting();
        if (message.isNotEmpty())
            showHint (message);
        repaint();
    }

    void ChainView::resetInstance (VelvoxAudioProcessor& p, ModuleId id, int instance)
    {
        auto& apvts = p.getState();
        const auto onBase = juce::String (onParamFor (id));
        UndoHistory::ScopedTransaction undoStep (p.getUndo(), i18n::format (i18n::tr ("{0} zurücksetzen", "Reset {0}"), { dsp::moduleDisplayName (id) }));
        for (const auto& base : moduleBaseIds (id))
        {
            if (base == onBase)
                continue;
            const auto pidK = dsp::instanceParamId (base, instance);
            if (auto* param = apvts.getParameter (pidK))
                setValue (apvts, pidK, param->convertFrom0to1 (param->getDefaultValue()));
        }
        // Reset = like a module fresh from the browser
        for (const auto& [base, value] : freshInstanceValues (id))
            setValue (apvts, dsp::instanceParamId (base, instance), value);
    }

    std::vector<std::pair<juce::String, float>> ChainView::freshInstanceValues (ModuleId id)
    {
        // De-esser: relative detection (Pro-DS "Single Vocal") for new instances only; the parameter default
        // stays Absolute, so loaded projects and the default chain of a new plugin keep the 0.5 behaviour
        if (id == ModuleId::deess)
            return { { pid::deessDetection, 1.0f } };
        return {};
    }

    int ChainView::insertModule (ModuleId id, int index, bool asSend)
    {
        routing = proc.getRouting();
        const int k = routing.nextFreeInstance (id);
        if (k < 0)
        {
            showHint (i18n::format (i18n::tr ("Alle {0} {1} sind schon in der Kette.", "All {0} {1} are already in the chain."),
                                   { juce::String (dsp::maxInstances (id)), dsp::moduleDisplayName (id) }));
            return -1;
        }
        UndoHistory::ScopedTransaction undoStep (proc.getUndo(), i18n::format (i18n::tr ("{0} hinzufügen", "Add {0}"), { dsp::moduleDisplayName (id) }));

        // A fresh instance: default settings, still off while it joins the chain
        auto& apvts = proc.getState();
        const auto onId = onParamId (id, k);
        for (const auto& base : moduleBaseIds (id))
        {
            const auto pidK = dsp::instanceParamId (base, k);
            if (auto* p = apvts.getParameter (pidK))
                setValue (apvts, pidK, p->convertFrom0to1 (p->getDefaultValue()));
        }
        for (const auto& [base, value] : freshInstanceValues (id))
            setValue (apvts, dsp::instanceParamId (base, k), value);
        setValue (apvts, onId, 0.0f);

        auto r = routing;
        const int at = index < 0 ? r.size() : serialIndexToRoutingIndex (r, index, id, k);
        r.insertAt (at, id, k, ! (asSend && canBeSend (id)));
        applyRouting (r);

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
            showHint (i18n::format (i18n::tr ("Alle {0} {1} sind schon in der Kette.", "All {0} {1} are already in the chain."),
                                   { juce::String (dsp::maxInstances (id)), dsp::moduleDisplayName (id) }));
            return -1;
        }
        UndoHistory::ScopedTransaction undoStep (proc.getUndo(), i18n::format (i18n::tr ("{0} duplizieren", "Duplicate {0}"), { dsp::moduleDisplayName (id) }));

        auto& apvts = proc.getState();
        const auto onBase = juce::String (onParamFor (id));
        for (const auto& base : moduleBaseIds (id))
            if (base != onBase)
                setValue (apvts, dsp::instanceParamId (base, k), getValue (apvts, dsp::instanceParamId (base, instance)));
        setValue (apvts, onParamId (id, k), 0.0f);

        auto r = routing;
        r.insertAt (r.indexOf (id, instance) + 1, id, k, r.isInsert (id, instance));
        applyRouting (r);
        proc.setLegacyMixLaw (id, k, proc.usesLegacyMixLaw (id, instance));   // the copy sounds like the original
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
        UndoHistory::ScopedTransaction undoStep (proc.getUndo(), i18n::format (i18n::tr ("{0} entfernen", "Remove {0}"), { dsp::moduleDisplayName (id) }));

        // Off first (automation of an instance outside the chain would add it back)
        setValue (proc.getState(), onParamId (id, instance), 0.0f);
        auto r = routing;
        const int at = r.indexOf (id, instance);
        r.remove (id, instance);
        applyRouting (r);

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
        UndoHistory::ScopedTransaction undoStep (proc.getUndo(), chainEdit());
        applyRouting (r);
        return true;
    }

    bool ChainView::setSendMode (ModuleId id, int instance, bool send)
    {
        auto r = proc.getRouting();
        if (! r.setInsert (id, instance, ! send))
            return false;
        UndoHistory::ScopedTransaction undoStep (proc.getUndo(), send ? i18n::tr ("Als Send", "As send") : i18n::tr ("Als Insert", "As insert"));
        applyRouting (r);
        return true;
    }

    bool ChainView::undo()
    {
        const bool done = proc.getUndo().undo();
        if (done)
            applyRouting (proc.getRouting());
        return done;
    }

    bool ChainView::redo()
    {
        const bool done = proc.getUndo().redo();
        if (done)
            applyRouting (proc.getRouting());
        return done;
    }

    bool ChainView::canUndo() const { return proc.getUndo().canUndo(); }
    bool ChainView::canRedo() const { return proc.getUndo().canRedo(); }

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
            UndoHistory::ScopedTransaction undoStep (proc.getUndo(), chainEdit());
            applyRouting (sorted);
        }
    }

    void ChainView::saveLayoutAsDefault()
    {
        settings::set (settings::myLayoutOrder, proc.getRouting().toString());
        showHint (i18n::tr ("Kette als dein Standard gespeichert.", "Chain saved as your default."));
    }

    bool ChainView::loadDefaultLayout()
    {
        const auto order = settings::getString (settings::myLayoutOrder);
        if (order.isEmpty())
            return false;
        const auto r = dsp::Routing::fromString (dsp::Routing::fromString (order).toString());
        if (r != proc.getRouting())
        {
            UndoHistory::ScopedTransaction undoStep (proc.getUndo(), chainEdit());
            applyRouting (r);
        }
        return true;
    }

    //==========================================================================
    // Menus

    bool ChainView::performTileAction (int code, TileAction action)
    {
        routing = proc.getRouting();
        if (code < 0)
            return false;
        const auto id = dsp::selectionModule (code);
        const int k = dsp::selectionInstance (code);
        if (! routing.contains (id, k) && action != TileAction::undo && action != TileAction::redo)
            return false;
        switch (action)
        {
            case TileAction::toggle:    toggleOn (id, k); return true;
            case TileAction::duplicate: return duplicateInstance (id, k) >= 0;
            case TileAction::remove:    return removeInstance (id, k);
            case TileAction::moveLeft:  return moveBy (id, k, -1);
            case TileAction::moveRight: return moveBy (id, k, 1);
            case TileAction::reset:     resetInstance (proc, id, k); return true;
            case TileAction::asSend:    return canBeSend (id) && setSendMode (id, k, true);
            case TileAction::asInsert:  return canBeSend (id) && setSendMode (id, k, false);
            case TileAction::undo:      return undo();
            case TileAction::redo:      return redo();
            case TileAction::help:      if (onHelp) onHelp (code); return true;
            case TileAction::solo:
                if (! dsp::canSolo (id))
                    return false;
                proc.toggleSolo (code);
                shownSolo = proc.getSolo();
                repaint();
                return true;
        }
        return false;
    }

    juce::PopupMenu ChainView::buildTileMenu (int code)
    {
        routing = proc.getRouting();
        const auto id = dsp::selectionModule (code);
        const int k = dsp::selectionInstance (code);
        const bool on = isOn (id, k);
        const bool insert = routing.isInsert (id, k);
        int serialPos = -1, serialCount = 0;
        for (const auto& s : routing)
            if (s.insert)
            {
                if (s.sameInstance (id, k))
                    serialPos = serialCount;
                ++serialCount;
            }

        juce::Component::SafePointer<ChainView> safe (this);
        const auto act = [safe, code] (TileAction a) { return [safe, code, a] { if (safe != nullptr) safe->performTileAction (code, a); }; };

        juce::PopupMenu menu;
        menu.addSectionHeader (cardName (routing, id, k));
        menu.addItem (on ? juce::String ("Bypass") : i18n::tr ("Einschalten", "Turn on"), act (TileAction::toggle));
        if (dsp::canSolo (id))
            menu.addItem (i18n::tr ("Solo (Alt+Klick)", "Solo (Alt+click)"), true, proc.isSoloed (code), act (TileAction::solo));
        menu.addItem (i18n::tr ("Duplizieren", "Duplicate"), routing.nextFreeInstance (id) >= 0, false, act (TileAction::duplicate));
        menu.addItem (i18n::tr ("Entfernen", "Remove"), act (TileAction::remove));
        menu.addSeparator();
        menu.addItem (i18n::tr ("Nach links", "Move left"), insert && serialPos > 0, false, act (TileAction::moveLeft));
        menu.addItem (i18n::tr ("Nach rechts", "Move right"), insert && serialPos >= 0 && serialPos < serialCount - 1, false, act (TileAction::moveRight));
        menu.addItem (i18n::tr ("Zurücksetzen", "Reset"), act (TileAction::reset));
        if (canBeSend (id))
        {
            menu.addSeparator();
            menu.addItem (i18n::tr ("Als Send", "As send"), true, ! insert, act (TileAction::asSend));
            menu.addItem (i18n::tr ("Als Insert", "As insert"), true, insert, act (TileAction::asInsert));
        }
        menu.addSeparator();
        menu.addItem (i18n::tr ("Rückgängig", "Undo") + (canUndo() ? "  (" + proc.getUndo().getUndoName() + ")" : juce::String()), canUndo(), false, act (TileAction::undo));
        menu.addItem (i18n::tr ("Wiederholen", "Redo"), canRedo(), false, act (TileAction::redo));
        menu.addSeparator();
        menu.addItem (i18n::format (i18n::tr ("Hilfe: {0}", "Help: {0}"), { dsp::moduleDisplayName (id) }), act (TileAction::help));
        return menu;
    }

    void ChainView::showTileMenu (const Tile& t)
    {
        buildTileMenu (t.code()).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (200)
                                                     .withTargetScreenArea (localAreaToGlobal (t.bounds.toNearestInt())));
    }

    void ChainView::showChainMenu()
    {
        juce::Component::SafePointer<ChainView> safe (this);
        juce::PopupMenu menu;
        menu.addSectionHeader (i18n::tr ("Kette", "Chain"));
        menu.addItem (i18n::tr ("Modul hinzufügen …", "Add module …"), [safe] { if (safe != nullptr && safe->onAddModule) safe->onAddModule (safe->numSerial, true); });
        menu.addItem (i18n::tr ("Rückgängig", "Undo"), canUndo(), false, [safe] { if (safe != nullptr) safe->undo(); });
        menu.addItem (i18n::tr ("Wiederholen", "Redo"), canRedo(), false, [safe] { if (safe != nullptr) safe->redo(); });
        menu.addSeparator();
        menu.addItem (i18n::tr ("In die übliche Reihenfolge sortieren", "Sort into the usual order"), [safe] { if (safe != nullptr) safe->restoreDefaultChain(); });
        menu.addItem (i18n::tr ("Diese Kette als meinen Standard speichern", "Save this chain as my default"), [safe] { if (safe != nullptr) safe->saveLayoutAsDefault(); });
        menu.addItem (i18n::tr ("Meine Standard-Kette laden", "Load my default chain"), settings::getString (settings::myLayoutOrder).isNotEmpty(), false,
                      [safe] { if (safe != nullptr) safe->loadDefaultLayout(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (220));
    }
}
