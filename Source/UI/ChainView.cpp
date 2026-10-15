#include "ChainView.h"
#include "../PluginProcessor.h"

namespace velvox::ui
{
    using dsp::ModuleId;

    namespace
    {
        constexpr float tileH = 40.0f;
        constexpr float laneTileH = 30.0f;
        constexpr float laneOffset = 12.0f;
        constexpr float endW = 54.0f;
        constexpr float gap = 10.0f;
        constexpr float corner = 10.0f;

        bool canBeSend (ModuleId id) { return id == ModuleId::delay || id == ModuleId::reverb; }
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
        }
        return pid::eqOn;
    }

    ChainView::ChainView (VelvoxAudioProcessor& p) : proc (p)
    {
        routing = proc.getRouting();
        setTooltip ("Drag modules to change their order. Drag Delay or Reverb onto the lower lane to run them in parallel.");
    }

    bool ChainView::isOn (ModuleId id) const
    {
        return proc.getState().getRawParameterValue (onParamFor (id))->load() > 0.5f;
    }

    juce::Rectangle<float> ChainView::rowArea() const
    {
        return getLocalBounds().toFloat().reduced (endW + 2.0f * gap, 0.0f).withTrimmedTop (4.0f).withHeight (tileH);
    }

    juce::Rectangle<float> ChainView::laneArea() const
    {
        const auto row = rowArea();
        return row.withY (row.getBottom() + laneOffset).withHeight (laneTileH);
    }

    void ChainView::layoutTiles()
    {
        tiles.clear();
        const auto row = rowArea();
        const auto lane = laneArea();

        std::vector<ModuleId> serial, sends;
        for (auto id : routing.order)
            (routing.isSend (id) ? sends : serial).push_back (id);

        const int n = (int) serial.size();
        const float w = n > 0 ? (row.getWidth() - gap * (float) (n - 1)) / (float) n : 0.0f;
        for (int i = 0; i < n; ++i)
            tiles.push_back ({ serial[(size_t) i], { row.getX() + (float) i * (w + gap), row.getY(), w, row.getHeight() }, false });

        // Sends sit on the right part of the lane, between the chain end and the output
        const float sendW = 132.0f;
        float x = lane.getRight() - sendW * (float) sends.size() - gap * (float) (sends.size() > 0 ? sends.size() - 1 : 0);
        for (auto id : sends)
        {
            tiles.push_back ({ id, { x, lane.getY(), sendW, lane.getHeight() }, true });
            x += sendW + gap;
        }

        inBox = { (float) gap, row.getY(), endW, row.getHeight() };
        outBox = { (float) getWidth() - gap - endW, row.getY(), endW, row.getHeight() };
    }

    juce::Rectangle<float> ChainView::getTileBounds (ModuleId id)
    {
        layoutTiles();
        for (auto& t : tiles)
            if (t.id == id)
                return t.bounds;
        return {};
    }

    juce::Rectangle<float> ChainView::dotBounds (const Tile& t) const
    {
        return juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ t.bounds.getX() + 15.0f, t.bounds.getCentreY() });
    }

    juce::Rectangle<float> ChainView::modeBounds (const Tile& t) const
    {
        return t.bounds.withTrimmedLeft (t.bounds.getWidth() - 46.0f).reduced (4.0f, t.inLane ? 7.0f : 11.0f);
    }

    int ChainView::tileAt (juce::Point<float> p) const
    {
        for (int i = 0; i < (int) tiles.size(); ++i)
            if (tiles[(size_t) i].bounds.contains (p))
                return i;
        if (outBox.contains (p))
            return -100; // output
        return -2;
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

    void ChainView::showHint (const juce::String& text)
    {
        hint = text;
        hintUntil = juce::Time::getMillisecondCounter() + 4000;
        repaint();
    }

    void ChainView::refresh()
    {
        bool changed = false;

        const auto r = proc.getRouting();
        if (r != routing)
        {
            routing = r;
            changed = true;
        }

        auto& chain = proc.getChain();
        const auto engaged = dsp::modulesEngagedByMacros (proc.readSettings(), proc.readMacros());
        std::array<float, dsp::numModules> act {};
        // Gain reduction drives the glow of the power dots (the editor pulls the meters once per frame)
        const float compGr = live != nullptr ? live->compGr.ago (0) : chain.getCompressor().gainReductionDb.peak.load();
        const float deessGr = live != nullptr ? live->deessGr.ago (0) : chain.getDeEsser().gainReductionDb.peak.load();
        act[(size_t) ModuleId::comp] = juce::jlimit (0.0f, 1.0f, compGr / 12.0f);
        act[(size_t) ModuleId::deess] = juce::jlimit (0.0f, 1.0f, deessGr / 8.0f);
        if (live != nullptr)
        {
            act[(size_t) ModuleId::delay] = juce::jlimit (0.0f, 1.0f, live->delayDuck.ago (0) / 12.0f);
            act[(size_t) ModuleId::reverb] = juce::jlimit (0.0f, 1.0f, live->reverbDuck.ago (0) / 12.0f);
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

        if (hint.isNotEmpty() && juce::Time::getMillisecondCounter() > hintUntil)
        {
            hint.clear();
            changed = true;
        }

        if (changed)
            repaint();
    }

    juce::Rectangle<float> ChainView::selectionTarget()
    {
        layoutTiles();
        if (selected == outputTile)
            return outBox;
        for (auto& t : tiles)
            if ((int) t.id == selected)
                return t.bounds;
        return {};
    }

    void ChainView::tick()
    {
        bool moved = false;
        const auto target = selectionTarget();
        if (! target.isEmpty())
        {
            const float values[] { target.getX(), target.getY(), target.getWidth(), target.getHeight() };
            for (size_t i = 0; i < 4; ++i)
            {
                sel[i].target = values[i];
                moved = sel[i].tick (0.22f) || moved;
            }
        }

        const int hoveredId = hovered >= 0 && hovered < (int) tiles.size() ? (int) tiles[(size_t) hovered].id : -1;
        for (int i = 0; i < dsp::numModules; ++i)
        {
            hoverGlow[(size_t) i].target = i == hoveredId ? 1.0f : 0.0f;
            moved = hoverGlow[(size_t) i].tick (0.18f) || moved;
        }

        if (moved)
            repaint();
    }

    void ChainView::drawTile (juce::Graphics& g, const Tile& t, juce::Rectangle<float> r, bool ghost) const
    {
        const auto idx = (size_t) t.id;
        const bool on = onState[idx] || macroEngaged[idx];
        const bool isSelected = (int) t.id == selected;
        const float hov = ghost ? 0.0f : hoverGlow[idx].current;

        // Opaque base so the spine does not run through the label
        g.setColour (ghost ? colours::raisedHi : colours::background);
        g.fillRoundedRectangle (r, corner);
        g.setColour (colours::accent.withAlpha ((isSelected ? 0.06f : (on ? 0.04f : 0.016f)) + 0.035f * hov));
        g.fillRoundedRectangle (r, corner);
        g.setColour (on ? colours::accent.withAlpha (0.12f + 0.1f * hov) : colours::line.brighter (0.4f * hov));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);

        // Power dot: lit when the module processes (pulses with its activity), hollow when off
        Tile shifted = t;
        shifted.bounds = r;
        const auto dot = dotBounds (shifted).reduced (4.5f);
        if (on)
        {
            const auto c = aurora (juce::jlimit (0.0f, 1.0f, r.getCentreX() / std::max (1.0f, (float) getWidth())));
            drawGlow (g, dot.getCentre(), 10.0f + 9.0f * activity[idx], 0.45f + 0.45f * activity[idx], c);
            g.setColour (c.interpolatedWith (juce::Colours::white, 0.45f));
            g.fillEllipse (dot);
        }
        else
        {
            g.setColour (colours::textFaint);
            g.drawEllipse (dot.reduced (0.5f), 1.2f);
        }

        // Name
        auto textArea = r.withTrimmedLeft (28.0f);
        if (canBeSend (t.id))
            textArea = textArea.withTrimmedRight (44.0f);
        g.setColour (on ? colours::text : colours::textDim);
        g.setFont (medium (t.inLane ? 12.5f : 13.0f));
        g.drawFittedText (dsp::moduleDisplayName (t.id), textArea.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

        if (macroEngaged[idx] && ! onState[idx])
        {
            drawCaption (g, "M", r.withTrimmedLeft (r.getWidth() - 16.0f).withHeight (14.0f).translated (-4.0f, 3.0f),
                         juce::Justification::centred, colours::textDim, 8.5f);
        }

        // Send / insert switch for delay and reverb
        if (canBeSend (t.id))
        {
            const auto m = modeBounds (shifted);
            const bool send = routing.isSend (t.id);
            g.setColour (colours::accent.withAlpha (send ? 0.10f : 0.03f));
            g.fillRoundedRectangle (m, m.getHeight() * 0.5f);
            g.setColour (colours::accent.withAlpha (send ? 0.30f : 0.14f));
            g.drawRoundedRectangle (m.reduced (0.5f), m.getHeight() * 0.5f, 1.0f);
            drawCaption (g, send ? "Send" : "Ins", m, juce::Justification::centred, send ? colours::text : colours::textDim, 8.0f);
        }
    }

    void ChainView::paint (juce::Graphics& g)
    {
        layoutTiles();
        const auto row = rowArea();
        const auto lane = laneArea();
        const float cy = row.getCentreY();

        // Spine: in -> serial chain -> out
        g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.05f), inBox.getRight(), cy,
                                                 colours::accent.withAlpha (0.14f), outBox.getX(), cy, false));
        g.fillRect (juce::Rectangle<float> (inBox.getRight(), cy - 0.5f, outBox.getX() - inBox.getRight(), 1.0f));

        // Parallel lane: splits after the serial chain, joins at the output
        bool anySend = false;
        for (auto& t : tiles)
            anySend = anySend || t.inLane;
        if (anySend)
        {
            float firstSendX = 1.0e9f;
            for (auto& t : tiles)
                if (t.inLane)
                    firstSendX = std::min (firstSendX, t.bounds.getX());
            juce::Path branch;
            branch.startNewSubPath (row.getRight() - 4.0f, row.getBottom());
            branch.quadraticTo (row.getRight() - 4.0f, lane.getCentreY(), firstSendX - 30.0f, lane.getCentreY());
            branch.lineTo (firstSendX, lane.getCentreY());
            juce::Path join;
            join.startNewSubPath (lane.getRight(), lane.getCentreY());
            join.quadraticTo (outBox.getCentreX(), lane.getCentreY(), outBox.getCentreX(), outBox.getBottom());

            g.setColour (colours::accent.withAlpha (0.16f));
            const float dashes[] { 3.0f, 3.0f };
            juce::Path dashedBranch, dashedJoin;
            juce::PathStrokeType stroke (1.0f);
            stroke.createDashedStroke (dashedBranch, branch, dashes, 2);
            stroke.createDashedStroke (dashedJoin, join, dashes, 2);
            g.fillPath (dashedBranch);
            g.fillPath (dashedJoin);

            drawCaption (g, "Parallel sends", juce::Rectangle<float> (firstSendX - 160.0f, lane.getY(), 150.0f, lane.getHeight()),
                         juce::Justification::centredRight, colours::textFaint, 8.5f);
        }

        // End points
        for (auto* box : { &inBox, &outBox })
        {
            const bool isOut = box == &outBox;
            const bool boxSelected = isOut && selected == outputTile;
            g.setColour (colours::background);
            g.fillRoundedRectangle (*box, corner);
            g.setColour (colours::accent.withAlpha (boxSelected ? 0.06f : 0.02f));
            g.fillRoundedRectangle (*box, corner);
            g.setColour (colours::line);
            g.drawRoundedRectangle (box->reduced (0.5f), corner, 1.0f);
            drawCaption (g, isOut ? "Out" : "In", *box, juce::Justification::centred, boxSelected ? colours::text : colours::textDim, 9.5f);
        }

        // Selection light (animated): glow pool underneath ...
        if (! selInitialised || ! isShowing())
        {
            const auto t = selectionTarget();
            sel[0].snap (t.getX()); sel[1].snap (t.getY()); sel[2].snap (t.getWidth()); sel[3].snap (t.getHeight());
            selInitialised = ! t.isEmpty();
        }
        const auto selRect = juce::Rectangle<float> (sel[0].current, sel[1].current, sel[2].current, sel[3].current);
        if (! selRect.isEmpty())
        {
            drawGlow (g, selRect.getCentre(), selRect.getWidth() * 0.7f, 0.10f,
                      aurora (juce::jlimit (0.0f, 1.0f, selRect.getCentreX() / std::max (1.0f, (float) getWidth()))));
        }

        for (int i = 0; i < (int) tiles.size(); ++i)
            if (! (dragging && i == dragTile))
                drawTile (g, tiles[(size_t) i], tiles[(size_t) i].bounds, false);

        // ... and an aurora outline on top
        if (! selRect.isEmpty() && ! dragging)
        {
            g.setGradientFill (auroraGradient (selRect.getTopLeft(), selRect.getBottomRight(), 0.9f));
            g.drawRoundedRectangle (selRect.reduced (0.5f), corner, 1.3f);
        }

        // Dragged tile and insertion marker
        if (dragging && dragTile >= 0)
        {
            const auto& t = tiles[(size_t) dragTile];
            const auto r = t.bounds.withPosition (dragPos - dragOffset);
            const bool overLane = laneArea().expanded (0.0f, 8.0f).contains (dragPos);
            if (! overLane)
            {
                const int idx = insertIndexForX (dragPos.x);
                float markerX = row.getX() - gap * 0.5f;
                int count = 0;
                for (auto& other : tiles)
                {
                    if (other.inLane || other.id == t.id)
                        continue;
                    if (count++ < idx)
                        markerX = other.bounds.getRight() + gap * 0.5f;
                }
                drawGlow (g, { markerX, row.getCentreY() }, 18.0f, 0.3f);
                g.setColour (colours::accent);
                g.fillRoundedRectangle (markerX - 1.0f, row.getY() - 2.0f, 2.0f, row.getHeight() + 4.0f, 1.0f);
            }
            else if (! canBeSend (t.id))
            {
                g.setColour (colours::warning.withAlpha (0.10f));
                g.fillRoundedRectangle (laneArea(), corner);
            }
            drawTile (g, t, r, true);
        }

        if (hint.isNotEmpty())
        {
            g.setColour (colours::warning);
            g.setFont (medium (11.5f));
            g.drawText (hint, juce::Rectangle<float> (row.getX(), lane.getY(), lane.getWidth() * 0.6f, lane.getHeight()),
                        juce::Justification::centredLeft, true);
        }
    }

    void ChainView::mouseDown (const juce::MouseEvent& e)
    {
        layoutTiles();
        const int i = tileAt (e.position);
        dragTile = -1;
        dragging = false;

        if (i == -100)
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
            if (auto* p = proc.getState().getParameter (onParamFor (t.id)))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
                p->endChangeGesture();
            }
            return;
        }

        if (canBeSend (t.id) && modeBounds (t).contains (e.position))
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
        if (! dragging || dragTile < 0)
        {
            dragTile = -1;
            dragging = false;
            return;
        }

        const auto id = tiles[(size_t) dragTile].id;
        const bool overLane = laneArea().expanded (0.0f, 8.0f).contains (e.position);
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

            std::vector<ModuleId> serial;
            for (auto m : r.order)
                if (m != id && r.isInsert (m))
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
        repaint();
    }

    void ChainView::mouseMove (const juce::MouseEvent& e)
    {
        layoutTiles();
        const int i = tileAt (e.position);
        if (i != hovered)
        {
            hovered = i;
            setMouseCursor (i >= 0 || i == -100 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }
    }

    void ChainView::mouseExit (const juce::MouseEvent&)
    {
        hovered = -2;
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }
}
