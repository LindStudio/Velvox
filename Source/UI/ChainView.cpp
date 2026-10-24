#include "ChainView.h"
#include "../PluginProcessor.h"
#include "../StateKeys.h"
#include "UserSettings.h"
#include "Icons.h"
#include <set>

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        constexpr float corner = radius::tile;
        constexpr float arrowW = 36.0f;
        constexpr float edgeFade = 44.0f;
        constexpr float plusR = 11.0f;
        constexpr int hitLeftArrow = -3, hitRightArrow = -4, hitAddCard = -5, hitOutput = -100, hitInput = -101, hitNothing = -2;

        bool canBeSend (ModuleId id) { return dsp::Routing::canBeSend (id); }

        void drawPowerGlyph (juce::Graphics& g, juce::Rectangle<float> r, bool on, float hover, juce::Colour hue = colours::cyan)
        {
            const float size = std::min (r.getWidth(), r.getHeight());
            r = r.withSizeKeepingCentre (size, size);
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
        return getLocalBounds().toFloat().reduced (endW + 6.0f, 0.0f).withHeight (cardH);
    }

    // Send zone at the end of the row (behind the add column and a divider); at least one
    // column wide, so a column can always be dropped there
    juce::Rectangle<float> ChainView::laneArea() const
    {
        const auto row = rowArea();
        const float x = row.getX() + (float) numSerial * (serialW + gap) + addCardW + sendGap * 0.5f - scroll.current;
        return { x, row.getY(), sendGap * 0.5f + (float) std::max (1, numSends) * (serialW + gap), row.getHeight() };
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
        // Serial columns and the send zone (at least one column) share the row equally
        const int columns = n + std::max (1, numSends);
        const float vw = rowArea().getWidth() - addCardW - sendGap;
        const float fit = vw / (float) std::max (1, columns);
        return juce::jlimit (minCardW, maxCardW, fit);
    }

    float ChainView::contentWidth() const
    {
        return (float) numSerial * (serialW + gap) + addCardW + sendGap + (float) std::max (1, numSends) * (serialW + gap);
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

        std::vector<const dsp::ChainSlot*> serial, sends;
        for (const auto& slot : routing)
            (! slot.insert ? sends : serial).push_back (&slot);

        numSerial = (int) serial.size();
        numSends = (int) sends.size();
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

        // Sends: columns in the send zone behind the divider
        float x = laneArea().getX() + sendGap * 0.5f;
        for (auto* s : sends)
        {
            tiles.push_back ({ s->id, s->instance, { x, row.getY(), serialW, row.getHeight() }, true, 0 });
            x += serialW + gap;
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

    // Column geometry: power + name row, icon, two knob cells, footer
    juce::Rectangle<float> ChainView::dotBounds (const Tile& t) const
    {
        return juce::Rectangle<float> (24.0f, 24.0f).withCentre ({ t.bounds.getX() + 20.0f, t.bounds.getY() + nameH * 0.5f });
    }

    juce::Rectangle<float> ChainView::modeBounds (const Tile& t) const
    {
        // SEND / INSERT pill of delay and reverb, top right of the icon area
        return { t.bounds.getRight() - 8.0f - 48.0f, t.bounds.getY() + nameH + 4.0f, 48.0f, 18.0f };
    }

    juce::Rectangle<float> ChainView::footerButton (const Tile& t, int slot) const
    {
        const auto footer = t.bounds.withTrimmedTop (t.bounds.getHeight() - footerH).reduced (6.0f, 5.0f);
        const float w = footer.getWidth() / 4.0f;
        return juce::Rectangle<float> (w, footer.getHeight()).withX (footer.getX() + w * (float) slot).withY (footer.getY())
                   .withSizeKeepingCentre (std::min (w, 24.0f), 22.0f);
    }

    juce::Rectangle<float> ChainView::removeBounds (const Tile& t) const    { return footerButton (t, 2); }
    juce::Rectangle<float> ChainView::duplicateBounds (const Tile& t) const { return footerButton (t, 1); }

    juce::Rectangle<float> ChainView::getMoveLeftBounds (ModuleId id, int instance)
    {
        layoutTiles();
        if (auto* t = findTile (id, instance))
            return footerButton (*t, 0);
        return {};
    }

    juce::Rectangle<float> ChainView::getMoveRightBounds (ModuleId id, int instance)
    {
        layoutTiles();
        if (auto* t = findTile (id, instance))
            return footerButton (*t, 3);
        return {};
    }

    std::pair<const char*, const char*> ChainView::columnParams (ModuleId id)
    {
        switch (id)
        {
            case ModuleId::ride:    return { pid::rideTarget, pid::rideResponse };
            case ModuleId::smooth:  return { pid::smoothAmount, pid::smoothSensitivity };
            case ModuleId::smash:   return { pid::smashAmount, pid::smashBlend };
            case ModuleId::choir:   return { pid::choirMix, pid::choirWidth };
            case ModuleId::pulse:   return { pid::pulseDepth, pid::pulseRate };
            case ModuleId::chop:    return { pid::chopDepth, pid::chopRate };
            case ModuleId::delay:   return { pid::delayFeedback, pid::delayMix };
            case ModuleId::reverb:  return { pid::reverbDecay, pid::reverbMix };
            case ModuleId::eq:      return { pid::hpfFreq, pid::highGain };
            case ModuleId::comp:    return { pid::compThreshold, pid::compRatio };
            case ModuleId::deess:   return { pid::deessThreshold, pid::deessRange };
            case ModuleId::sat:     return { pid::satDrive, pid::satMix };
            case ModuleId::doubler: return { pid::doublerWidth, pid::doublerMix };
            case ModuleId::pitch:   return { pid::pitchSemitones, pid::pitchMix };
            case ModuleId::check:   return { nullptr, nullptr };
        }
        return { nullptr, nullptr };
    }

    std::pair<juce::String, juce::String> ChainView::columnLabels (ModuleId id)
    {
        switch (id)
        {
            case ModuleId::ride:    return { "Target", "Speed" };
            case ModuleId::smooth:  return { "Amount", "Sharpness" };
            case ModuleId::smash:   return { "Smash", "Mix" };
            case ModuleId::choir:   return { "Blend", "Width" };
            case ModuleId::pulse:   return { "Depth", "Rate" };
            case ModuleId::chop:    return { "Gate", "Rate" };
            case ModuleId::delay:   return { "Feedback", "Mix" };
            case ModuleId::reverb:  return { "Decay", "Mix" };
            case ModuleId::eq:      return { "Low Cut", "Highs" };
            case ModuleId::comp:    return { "Threshold", "Ratio" };
            case ModuleId::deess:   return { "Threshold", "Range" };
            case ModuleId::sat:     return { "Drive", "Mix" };
            case ModuleId::doubler: return { "Width", "Mix" };
            case ModuleId::pitch:   return { "Pitch", "Mix" };
            case ModuleId::check:   return {};
        }
        return {};
    }

    juce::Rectangle<float> ChainView::getArrowBounds (bool right)
    {
        layoutTiles();
        const float maxScroll = std::max (0.0f, contentWidth() - rowArea().getWidth());
        if (right)
            return scroll.target < maxScroll - 0.5f ? rightArrowArea() : juce::Rectangle<float>();
        return scroll.target > 0.5f ? leftArrowArea() : juce::Rectangle<float>();
    }

    juce::String ChainView::checkColumnNote()
    {
        return juce::String::fromUTF8 ("Diagnose\nohne Klang-\nver\xc3\xa4nderung");
    }

    Knob* ChainView::getColumnKnob (ModuleId id, int instance, int which)
    {
        updateColumnKnobs();
        const auto it = columnKnobs.find (dsp::selectionCode (id, instance));
        return it != columnKnobs.end() && juce::isPositiveAndBelow (which, 2) ? it->second[(size_t) which].get() : nullptr;
    }

    void ChainView::updateColumnKnobs()
    {
        layoutTiles();
        const auto row = rowArea();
        const float maxScroll = std::max (0.0f, contentWidth() - row.getWidth());
        const float visibleLeft = row.getX() + (scroll.current > 0.5f ? arrowW : 0.0f);
        const float visibleRight = row.getRight() - (scroll.current < maxScroll - 0.5f ? arrowW : 0.0f);

        // Knobs for every column in the chain, none for columns that left it
        std::set<int> present;
        for (const auto& t : tiles)
        {
            present.insert (t.code());
            auto& pair = columnKnobs[t.code()];
            if (pair[0] == nullptr && columnParams (t.id).first != nullptr)
            {
                const auto params = columnParams (t.id);
                const auto labels = columnLabels (t.id);
                const auto hue = moduleColour (t.id);
                pair[0] = std::make_unique<Knob> (proc.getState(), dsp::instanceParamId (params.first, t.instance).toRawUTF8(), labels.first, hue);
                pair[1] = std::make_unique<Knob> (proc.getState(), dsp::instanceParamId (params.second, t.instance).toRawUTF8(), labels.second, hue);
                for (auto& k : pair)
                {
                    k->setDialSize (48);
                    // The wheel scrolls the chain (the slider passes it on to its parents)
                    k->slider.setScrollWheelEnabled (false);
                    addChildComponent (*k);
                }
            }

            const auto onIt = onState.find (t.code());
            const bool on = (onIt == onState.end() ? isOn (t.id, t.instance) : onIt->second)
                            || (t.instance == 0 && macroEngaged[(size_t) t.id]);
            // Only fully visible columns show their knobs, and never under a scroll arrow
            // (the arrows are drawn by this component, the knobs would cover and catch them)
            const bool shown = visibleLeft - 0.5f <= t.bounds.getX() && t.bounds.getRight() <= visibleRight + 0.5f
                               && ! (dragging && dragTile >= 0 && tiles[(size_t) dragTile].code() == t.code());
            for (int k = 0; k < 2; ++k)
                if (auto& knob = pair[(size_t) k])
                {
                    const auto cell = juce::Rectangle<float> (t.bounds.getX() + 8.0f, t.bounds.getY() + nameH + iconH + knobCellH * (float) k,
                                                              t.bounds.getWidth() - 16.0f, knobCellH);
                    knob->setBounds (cell.toNearestInt());
                    knob->setVisible (shown);
                    knob->setAlpha (on ? 1.0f : 0.3f);
                }
        }
        for (auto it = columnKnobs.begin(); it != columnKnobs.end();)
            it = present.count (it->first) == 0 ? columnKnobs.erase (it) : std::next (it);

        // Undo / redo / chain menu under the round "+" (they scroll with the add column)
        const auto add = addCardArea();
        const bool addShown = row.getX() - 0.5f <= add.getX() && add.getRight() <= row.getRight() + 0.5f;
        auto stack = juce::Rectangle<float> (add.getCentreX() - 28.0f, add.getY() + nameH + iconH + 30.0f, 56.0f, 24.0f).toNearestInt();
        for (auto* b : { &undoButton, &redoButton, &chainMenuButton })
        {
            b->setBounds (stack);
            b->setVisible (addShown);
            stack.translate (0, 30);
        }
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
            if (std::abs (p.x - c.x) <= 6.0f && std::abs (p.y - c.y) <= plusR + 6.0f)
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

        if (changed || columnKnobs.size() != routing.size())
        {
            updateColumnKnobs();
            repaint();
        }
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
        {
            updateColumnKnobs();
            repaint();
        }
    }

    //==========================================================================
    // Drawing

    void ChainView::drawPort (juce::Graphics& g, juce::Rectangle<float> box, bool isOutput) const
    {
        // Input and output: slim end caps of the signal path with a direction arrow
        const bool isSel = isOutput && selected == outputTile;
        const bool hov = isOutput && hovered == hitOutput;
        g.setColour (isSel ? colours::teal.withAlpha (0.10f) : colours::panelInset);
        g.fillRoundedRectangle (box, 8.0f);
        g.setColour (isSel ? colours::teal.withAlpha (0.7f) : (hov ? colours::border.brighter (0.5f) : colours::border));
        g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);

        const auto c = juce::Point<float> (box.getCentreX(), box.getY() + 22.0f);
        juce::Path arrow;
        arrow.startNewSubPath (c.x - 6.0f, c.y);
        arrow.lineTo (c.x + 5.0f, c.y);
        arrow.startNewSubPath (c.x + 1.0f, c.y - 4.0f);
        arrow.lineTo (c.x + 5.0f, c.y);
        arrow.lineTo (c.x + 1.0f, c.y + 4.0f);
        g.setColour (isSel ? colours::teal : colours::textMuted);
        g.strokePath (arrow, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Vertical caption
        juce::Graphics::ScopedSaveState s (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, box.getCentreX(), box.getCentreY()));
        g.setColour (isSel ? colours::textStrong : colours::textMuted);
        g.setFont (semiBold (10.0f, 0.3f));
        g.drawText (isOutput ? "OUTPUT" : "INPUT", juce::Rectangle<float> (box.getHeight() - 80.0f, box.getWidth()).withCentre (box.getCentre()),
                    juce::Justification::centred, false);
    }

    void ChainView::drawAddCard (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        // Round dashed "+" (opens the module browser with all modules)
        const auto it = hoverGlow.find (-77);
        const float hov = it != hoverGlow.end() ? it->second.current : 0.0f;
        const auto c = juce::Point<float> (r.getCentreX(), r.getY() + nameH + iconH * 0.5f);
        const auto circle = juce::Rectangle<float> (44.0f, 44.0f).withCentre (c);
        g.setColour (colours::teal.withAlpha (0.04f + 0.06f * hov));
        g.fillEllipse (circle);
        juce::Path outline;
        outline.addEllipse (circle.reduced (0.75f));
        juce::Path dashed;
        const float dashes[] { 4.0f, 4.0f };
        juce::PathStrokeType (1.3f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour ((hov > 0.5f ? colours::teal : colours::textDim).withAlpha (0.6f + 0.4f * hov));
        g.fillPath (dashed);
        drawPlus (g, c, 16.0f, hov > 0.5f ? colours::teal : colours::textSoft, 2.0f);
        g.setColour (colours::textFaint);
        g.setFont (semiBold (9.0f, 0.2f));
        g.drawText ("ADD", juce::Rectangle<float> (r.getWidth(), 14.0f).withCentre ({ c.x, circle.getBottom() + 12.0f }), juce::Justification::centred, false);
    }

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
        // Neon: a soft halo in the module colour, then the line art
        drawGlow (g, area.getCentre(), area.getWidth() * 0.62f, 0.20f * alpha, moduleColour (id));
        d->drawWithin (g, area, juce::RectanglePlacement::centred, alpha);
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
        const auto hue = moduleColour (t.id);
        const float content = on ? 1.0f : 0.3f;   // bypassed: content at 30 %

        if (ghost)
        {
            drawGlow (g, r.getCentre().translated (0.0f, 10.0f), r.getWidth() * 0.7f, 0.6f, juce::Colours::black);
            g.setColour (colours::raisedHi);
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (hue.withAlpha (0.6f));
            g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        }

        // Selected: wash of the module colour, 14 % at the top -> 2 % at the bottom
        if (isSelected)
        {
            g.setGradientFill (juce::ColourGradient (hue.withAlpha (0.14f), r.getX(), r.getY(), hue.withAlpha (0.02f), r.getX(), r.getBottom(), false));
            g.fillRect (r);
            g.setColour (hue);
            g.fillRect (r.withHeight (2.0f).reduced (10.0f, 0.0f));
        }
        if (hov > 0.01f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.025f * hov));
            g.fillRect (r);
        }

        // Divider to the next column
        if (! ghost)
        {
            g.setColour (colours::borderSoft);
            g.fillRect (juce::Rectangle<float> (r.getRight() - 1.0f, r.getY() + 10.0f, 1.0f, r.getHeight() - 20.0f));
        }

        Tile shifted = t;
        shifted.bounds = r;

        // Power + name (44)
        drawPowerGlyph (g, dotBounds (shifted), switchedOn, hov, hue);
        const auto nameArea = juce::Rectangle<float> (dotBounds (shifted).getRight() + 6.0f, r.getY(), r.getRight() - 8.0f - dotBounds (shifted).getRight() - 6.0f, nameH);
        g.setColour ((on ? colours::textStrong : colours::textMuted));
        g.setFont (semiBold (11.5f, 0.15f));
        g.drawFittedText (cardName (routing, t.id, t.instance).toUpperCase(), nameArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);

        // Neon icon (78)
        const auto iconArea = juce::Rectangle<float> (56.0f, 56.0f).withCentre ({ r.getCentreX(), r.getY() + nameH + iconH * 0.5f });
        drawIcon (g, t.id, iconArea, content);

        // Status under the icon: bypass / macro / auto-mix
        juce::String status;
        if (switchedOn && globalBypass)  status = "BYPASS";
        else if (engagedOnly)            status = "MACRO";
        else if (! switchedOn)           status = "OFF";
        else if (autoKeys.contains (dsp::moduleKey (t.id))) status = "AUTO";
        if (status.isNotEmpty())
        {
            g.setColour (status == "BYPASS" ? colours::bypass : (status == "AUTO" ? hue : colours::textFaint));
            g.setFont (semiBold (8.5f, 0.25f));
            g.drawText (status, juce::Rectangle<float> (r.getWidth(), 12.0f).withPosition (r.getX(), r.getY() + nameH + iconH - 12.0f),
                        juce::Justification::centred, false);
        }

        // VOCAL CHECK has no knobs: its place shows what it does
        if (columnParams (t.id).first == nullptr)
        {
            g.setColour (colours::textFaint.withAlpha (content));
            g.setFont (font (10.5f));
            g.drawFittedText ("Diagnose\nohne Klang-\nveränderung", juce::Rectangle<float> (r.getX() + 8.0f, r.getY() + nameH + iconH + 20.0f,
                                                                                       r.getWidth() - 16.0f, knobCellH).toNearestInt(),
                              juce::Justification::centredTop, 3, 0.9f);
        }

        // Send / insert pill for delay and reverb
        if (canBeSend (t.id))
        {
            const auto m = modeBounds (shifted);
            const bool send = ! routing.isInsert (t.id, t.instance);
            g.setColour (send ? hue.withAlpha (0.16f) : colours::control);
            g.fillRoundedRectangle (m, m.getHeight() * 0.5f);
            g.setColour (send ? hue.withAlpha (0.7f) : colours::border.brighter (0.4f));
            g.drawRoundedRectangle (m.reduced (0.5f), m.getHeight() * 0.5f, 1.0f);
            g.setColour (send ? colours::textStrong : colours::textMuted);
            g.setFont (semiBold (9.0f, 0.15f));
            g.drawText (send ? "SEND" : "INSERT", m, juce::Justification::centred, false);
        }

        // Activity line above the footer (gain reduction, ducking, movement)
        const float act = activityOf (t);
        const auto footerTop = r.getBottom() - footerH;
        if (switchedOn && act > 0.01f)
        {
            const auto bar = juce::Rectangle<float> (r.getX() + 12.0f, footerTop - 3.0f, r.getWidth() - 24.0f, 2.0f);
            g.setColour (hue.withAlpha (0.9f));
            g.fillRoundedRectangle (bar.withWidth (std::max (3.0f, bar.getWidth() * act)), 1.0f);
        }

        // Footer: < duplicate remove >
        g.setColour (colours::borderSoft);
        g.fillRect (juce::Rectangle<float> (r.getX() + 8.0f, footerTop, r.getWidth() - 16.0f, 1.0f));
        const bool canMoveLeft = ! t.inLane && t.position > 1;
        const bool canMoveRight = ! t.inLane && t.position < numSerial;
        const bool canDuplicate = routing.nextFreeInstance (t.id) >= 0;
        for (int slot = 0; slot < 4; ++slot)
        {
            const auto b = footerButton (shifted, slot);
            const bool enabled = slot == 0 ? canMoveLeft : (slot == 3 ? canMoveRight : (slot == 1 ? canDuplicate : true));
            const bool hot = hov > 0.3f && b.contains (lastMouse);
            const auto colour = ! enabled ? colours::textFaint.withAlpha (0.35f)
                                          : (hot ? (slot == 2 ? colours::warning : colours::textStrong) : colours::textMuted);
            if (hot && enabled)
            {
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.fillRoundedRectangle (b, 5.0f);
            }
            const auto c = b.getCentre();
            if (slot == 0 || slot == 3)
            {
                juce::Path p;
                const float s = slot == 3 ? 1.0f : -1.0f;
                p.startNewSubPath (c.x - 2.5f * s, c.y - 5.0f);
                p.lineTo (c.x + 2.5f * s, c.y);
                p.lineTo (c.x - 2.5f * s, c.y + 5.0f);
                g.setColour (colour);
                g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else if (slot == 1)
            {
                g.setColour (colour);
                g.drawRoundedRectangle (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c.translated (-1.5f, -1.5f)), 1.5f, 1.2f);
                g.drawRoundedRectangle (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c.translated (1.8f, 1.8f)), 1.5f, 1.2f);
            }
            else
            {
                juce::Path x;
                x.startNewSubPath (c.x - 3.5f, c.y - 3.5f);
                x.lineTo (c.x + 3.5f, c.y + 3.5f);
                x.startNewSubPath (c.x + 3.5f, c.y - 3.5f);
                x.lineTo (c.x - 3.5f, c.y + 3.5f);
                g.setColour (colour);
                g.strokePath (x, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
    }

    void ChainView::paint (juce::Graphics& g)
    {
        layoutTiles();
        const auto row = rowArea();
        const auto lane = laneArea();
        const float maxScroll = std::max (0.0f, contentWidth() - row.getWidth());

        // Panel behind the columns
        g.setColour (colours::panel);
        g.fillRoundedRectangle (row.expanded (0.0f, 0.0f), 12.0f);
        g.setColour (colours::border);
        g.drawRoundedRectangle (row.reduced (0.5f), 12.0f, 1.0f);

        drawPort (g, inBox, false);
        drawPort (g, outBox, true);

        const bool draggingSendable = dragging && dragTile >= 0 && canBeSend (tiles[(size_t) dragTile].id);

        {
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clip;
            clip.addRoundedRectangle (row, 12.0f);
            g.reduceClipRegion (clip);

            for (int i = 0; i < (int) tiles.size(); ++i)
                if (! (dragging && i == dragTile))
                    drawTile (g, tiles[(size_t) i], tiles[(size_t) i].bounds, false);
            drawAddCard (g, addCardArea());

            // Send zone: divider and caption
            {
                const float x = lane.getX();
                g.setColour (colours::border.brighter (0.2f));
                const float dashes[] { 3.0f, 4.0f };
                juce::Path line, dashed;
                line.startNewSubPath (x, row.getY() + 12.0f);
                line.lineTo (x, row.getBottom() - 12.0f);
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
                g.fillPath (dashed);
                if (numSends == 0)
                {
                    g.setColour (colours::textFaint);
                    g.setFont (semiBold (9.0f, 0.25f));
                    g.drawFittedText ("SENDS\nDelay / Reverb\nhierher ziehen", lane.withTrimmedLeft (sendGap * 0.5f).reduced (8.0f, 0.0f).toNearestInt(),
                                      juce::Justification::centred, 3, 0.85f);
                }
            }

            // Drop zone while Delay or Reverb is dragged
            if (draggingSendable)
            {
                const bool over = lane.contains (dragPos);
                g.setColour (colours::teal.withAlpha (over ? 0.10f : 0.04f));
                g.fillRect (lane.withTrimmedLeft (sendGap * 0.5f));
            }

            // "+" on a divider: insert a module exactly there
            if (hoveredGap >= 0 && gapGlow.current > 0.01f)
            {
                const auto c = gapCentre (hoveredGap);
                drawGlow (g, c, 20.0f, 0.35f * gapGlow.current, colours::teal);
                g.setColour (colours::raisedHi.withAlpha (gapGlow.current));
                g.fillEllipse (juce::Rectangle<float> (2.0f * plusR, 2.0f * plusR).withCentre (c));
                g.setColour (colours::teal.withAlpha (0.9f * gapGlow.current));
                g.drawEllipse (juce::Rectangle<float> (2.0f * plusR, 2.0f * plusR).withCentre (c), 1.3f);
                drawPlus (g, c, 10.0f, colours::text.withAlpha (gapGlow.current));
            }

            // Insertion marker while dragging
            if (dragging && dragTile >= 0 && ! lane.contains (dragPos))
            {
                const auto& t = tiles[(size_t) dragTile];
                const int idx = insertIndexForX (dragPos.x);
                float markerX = row.getX() - scroll.current + 1.5f;
                int count = 0;
                for (auto& other : tiles)
                {
                    if (other.inLane || (other.id == t.id && other.instance == t.instance))
                        continue;
                    if (count++ < idx)
                        markerX = other.bounds.getRight();
                }
                markerX = juce::jlimit (row.getX() + 2.0f, row.getRight() - 2.0f, markerX);
                drawGlow (g, { markerX, row.getCentreY() }, 22.0f, 0.35f, colours::teal);
                g.setColour (colours::teal);
                g.fillRoundedRectangle (markerX - 1.5f, row.getY() + 8.0f, 3.0f, row.getHeight() - 16.0f, 1.5f);
            }

            // More columns beyond the edges: fade and arrows
            if (scroll.current > 0.5f)
            {
                const auto a = leftArrowArea();
                g.setGradientFill (juce::ColourGradient (colours::panel, a.getX(), 0.0f,
                                                         colours::panel.withAlpha (0.0f), a.getX() + edgeFade + arrowW * 0.5f, 0.0f, false));
                g.fillRect (a.withWidth (edgeFade + arrowW * 0.5f));
                const bool hov = hovered == hitLeftArrow;
                g.setColour (colours::raisedHi.withAlpha (hov ? 1.0f : 0.9f));
                g.fillEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (a.getCentre()));
                g.setColour (colours::border.brighter (hov ? 0.7f : 0.3f));
                g.drawEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (a.getCentre()), 1.0f);
                drawChevron (g, a.getCentre(), false, hov ? colours::text : colours::textDim);
            }
            if (scroll.current < maxScroll - 0.5f)
            {
                const auto a = rightArrowArea();
                g.setGradientFill (juce::ColourGradient (colours::panel.withAlpha (0.0f), a.getRight() - edgeFade - arrowW * 0.5f, 0.0f,
                                                         colours::panel, a.getRight(), 0.0f, false));
                g.fillRect (a.withLeft (a.getRight() - edgeFade - arrowW * 0.5f));
                const bool hov = hovered == hitRightArrow;
                g.setColour (colours::raisedHi.withAlpha (hov ? 1.0f : 0.9f));
                g.fillEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (a.getCentre()));
                g.setColour (colours::border.brighter (hov ? 0.7f : 0.3f));
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
            g.setColour (draggingThumb ? colours::teal.withAlpha (0.8f) : colours::accent.withAlpha (0.28f));
            g.fillRoundedRectangle (scrollThumb(), 2.0f);
        }

        // Dragged column follows the mouse
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
            const auto area = row.withTrimmedTop (row.getHeight() - 74.0f).reduced (40.0f, 18.0f);
            g.setColour (colours::raisedHi.withAlpha (0.96f));
            g.fillRoundedRectangle (area, 8.0f);
            g.setColour (colours::warning.withAlpha (0.6f));
            g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);
            g.setColour (colours::text);
            g.setFont (medium (13.0f));
            g.drawFittedText (hint, area.reduced (12.0f, 2.0f).toNearestInt(), juce::Justification::centred, 2, 0.9f);
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

        if (dotBounds (t).expanded (3.0f).contains (e.position))
        {
            toggleOn (t.id, t.instance);
            return;
        }
        // Footer: < duplicate remove >
        if (removeBounds (t).expanded (2.0f).contains (e.position))
        {
            removeInstance (t.id, t.instance);
            return;
        }
        if (duplicateBounds (t).expanded (2.0f).contains (e.position))
        {
            if (routing.nextFreeInstance (t.id) >= 0)
                duplicateInstance (t.id, t.instance);
            return;
        }
        if (footerButton (t, 0).expanded (2.0f).contains (e.position) || footerButton (t, 3).expanded (2.0f).contains (e.position))
        {
            if (! t.inLane)
                moveBy (t.id, t.instance, footerButton (t, 0).expanded (2.0f).contains (e.position) ? -1 : 1);
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
            UndoHistory::ScopedTransaction undoStep (proc.getUndo(), juce::String::fromUTF8 ("Kette bearbeiten"));
            applyRouting (r);
        }
        scrollToShow (t.id, t.instance);
        repaint();
    }

    void ChainView::mouseMove (const juce::MouseEvent& e)
    {
        layoutTiles();
        if (lastMouse != e.position)
        {
            lastMouse = e.position;
            repaint();   // footer button highlight
        }
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
        addButton.setVisible (false);   // the round "+" column replaces it (kept for its callback)
        pendingReveal = false;
        if (selected >= 0)
            scrollToShow (dsp::selectionModule (selected), dsp::selectionInstance (selected), false);
        updateColumnKnobs();
    }

    void ChainView::moveBy (ModuleId id, int instance, int steps)
    {
        // One serial position earlier / later (sends are not counted)
        auto r = routing;
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
            return;
        const int routingIndex = serialIndexToRoutingIndex (r, target, id, instance);
        const int from = r.indexOf (id, instance);
        r.moveTo (id, instance, from < routingIndex ? routingIndex - 1 : routingIndex);
        if (r != routing)
        {
            UndoHistory::ScopedTransaction undoStep (proc.getUndo(), juce::String::fromUTF8 ("Kette bearbeiten"));
            applyRouting (r);
            scrollToShow (id, instance);
        }
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
        UndoHistory::ScopedTransaction undoStep (proc.getUndo(), juce::String::fromUTF8 ("Kette bearbeiten"));

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
        UndoHistory::ScopedTransaction undoStep (proc.getUndo(), juce::String::fromUTF8 ("Kette bearbeiten"));

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
        UndoHistory::ScopedTransaction undoStep (proc.getUndo(), juce::String::fromUTF8 ("Kette bearbeiten"));
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
        UndoHistory::ScopedTransaction undoStep (proc.getUndo(), juce::String::fromUTF8 ("Kette bearbeiten"));
        applyRouting (r);
        return true;
    }

    bool ChainView::setSendMode (ModuleId id, int instance, bool send)
    {
        auto r = proc.getRouting();
        if (! r.setInsert (id, instance, ! send))
            return false;
        UndoHistory::ScopedTransaction undoStep (proc.getUndo(), juce::String::fromUTF8 ("Kette bearbeiten"));
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
            UndoHistory::ScopedTransaction undoStep (proc.getUndo(), juce::String::fromUTF8 ("Kette bearbeiten"));
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
            UndoHistory::ScopedTransaction undoStep (proc.getUndo(), juce::String::fromUTF8 ("Kette bearbeiten"));
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
