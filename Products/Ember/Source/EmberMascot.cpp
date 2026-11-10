#include "EmberMascot.h"
#include "EmberMascotData.h"
#include "UI/Theme.h"
#include "Text/I18n.h"

namespace ember
{
    namespace
    {
        const juce::Colour skinTop { 0xffff6a3a }, skinBottom { 0xffa8140c }, horn { 0xff2a1512 },
                           eye { 0xffffd23f }, ink { 0xff1a0806 }, sweat { 0xff8fd8ff };
    }

    const char* mascotStateName (MascotState s) noexcept
    {
        switch (s)
        {
            case MascotState::sleeping:   return "sleeping";
            case MascotState::idle:       return "idle";
            case MascotState::singing:    return "singing";
            case MascotState::snapping:   return "snapping";
            case MascotState::fire:       return "fire";
            case MascotState::overheated: return "overheated";
            case MascotState::listening:  return "listening";
            case MascotState::proud:      return "proud";
        }
        return "idle";
    }

    // ------------------------------------------------------------------ brain
    void MascotBrain::setState (MascotState s) noexcept
    {
        if (s != state)
        {
            state = s;
            timeInState = 0.0;
        }
    }

    void MascotBrain::update (const MascotInput& in, double dt) noexcept
    {
        timeInState += dt;
        if (in.autoJustFinished)
            proudLeft = proudForS;
        proudLeft = std::max (0.0, proudLeft - dt);

        // Overheat with hysteresis: in after hotAfterS above hotDb, out after coolAfterS below coolDb
        const float gr = in.bypass ? 0.0f : in.reductionDb;
        hotFor = gr > hotDb ? hotFor + dt : 0.0;
        coolFor = gr < coolDb ? coolFor + dt : 0.0;
        if (! hot && hotFor >= hotAfterS)
            hot = true;
        if (hot && coolFor >= coolAfterS)
            hot = false;

        // The flame stays a moment after the S, so a short S still shows a puff of fire
        fireLeft = gr > fireDb ? fireHoldS : std::max (0.0, fireLeft - dt);
        const float target = juce::jlimit (0.0f, 1.0f, gr / 12.0f);
        intensity += (target - intensity) * (target > intensity ? 0.6f : 0.15f);

        if (in.bypass)                   setState (MascotState::sleeping);
        else if (in.autoListening)       setState (MascotState::listening);
        else if (proudLeft > 0.0)        setState (MascotState::proud);
        else if (hot)                    setState (MascotState::overheated);
        else if (fireLeft > 0.0)         setState (MascotState::fire);
        else if (gr > snapDb)            setState (MascotState::snapping);
        else if (in.hasSignal)           setState (MascotState::singing);
        else                             setState (MascotState::idle);
    }

    // ----------------------------------------------------------------- frames
    MascotFrames::MascotFrames()
    {
        // File names: <state>_<number>.png, e.g. fire_001.png ... (Products/Ember/Mascot/frames)
        for (int i = 0; i < EmberMascotData::namedResourceListSize; ++i)
        {
            const auto* resource = EmberMascotData::namedResourceList[i];
            const juce::String file = EmberMascotData::getNamedResourceOriginalFilename (resource);
            if (! file.endsWithIgnoreCase (".png"))
                continue;
            const auto stem = file.upToLastOccurrenceOf (".", false, false);
            const auto prefix = stem.upToLastOccurrenceOf ("_", false, false).toLowerCase();
            for (int s = 0; s < numMascotStates; ++s)
                if (prefix == mascotStateName ((MascotState) s))
                {
                    int size = 0;
                    const char* data = EmberMascotData::getNamedResource (resource, size);
                    auto img = juce::ImageFileFormat::loadFrom (data, (size_t) size);
                    if (img.isValid())
                        frames[s].add (img);
                }
        }

        // Optional frame rates: Mascot/mascot.json {"fps": 24, "states": {"fire": 30}}
        int size = 0;
        if (const char* json = EmberMascotData::getNamedResource ("mascot_json", size))
        {
            const auto v = juce::JSON::parse (juce::String::fromUTF8 (json, size));
            if (v.hasProperty ("fps"))
                defaultFps = (float) (double) v["fps"];
            if (auto* states = v["states"].getDynamicObject())
                for (const auto& p : states->getProperties())
                    for (int s = 0; s < numMascotStates; ++s)
                        if (p.name.toString() == mascotStateName ((MascotState) s))
                            rates[s] = (float) (double) p.value;
        }
    }

    bool MascotFrames::has (MascotState s) const noexcept { return count (s) > 0; }

    int MascotFrames::count (MascotState s) const noexcept
    {
        const auto it = frames.find ((int) s);
        return it == frames.end() ? 0 : it->second.size();
    }

    const juce::Image& MascotFrames::frame (MascotState s, int index) const
    {
        const auto& list = frames.at ((int) s);
        return list.getReference (juce::jlimit (0, list.size() - 1, index));
    }

    float MascotFrames::fps (MascotState s) const noexcept
    {
        const auto it = rates.find ((int) s);
        return juce::jmax (1.0f, it == rates.end() ? defaultFps : it->second);
    }

    // ------------------------------------------------------------------- view
    MascotView::MascotView()
    {
        setRepaintsOnMouseActivity (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        startTimerHz (30);
    }

    MascotView::~MascotView()
    {
        stopTimer();
    }

    void MascotView::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mouseWasClicked() && onClick)
            onClick();
    }

    void MascotView::timerCallback()
    {
        constexpr double dt = 1.0 / 30.0;
        t += dt;
        const auto before = brain.getState();
        const double beforeTime = brain.getTimeInState();
        brain.update (input, dt);
        if (brain.getState() != before)
        {
            previousState = before;
            previousTime = beforeTime;
        }
        input.autoJustFinished = false;

        const auto st = brain.getState();
        const float k = brain.getIntensity();
        const bool fireNow = st == MascotState::fire || st == MascotState::overheated;
        const float scale = getHeight() / 300.0f;

        if (! frames.has (st))
        {
            if (fireNow)
            {
                const int n = 1 + (int) (k * 10.0f);
                for (int i = 0; i < n; ++i)
                    particles.push_back ({ mouth, { (3.0f + random.nextFloat() * 3.0f + k * 6.0f) * scale,
                                                    (random.nextFloat() - 0.5f) * (1.5f + k * 4.0f) * scale },
                                           1.0f, (5.0f + random.nextFloat() * 5.0f + k * 9.0f) * scale, false });
            }
            const bool smoky = st == MascotState::idle || st == MascotState::sleeping || st == MascotState::overheated;
            if (smoky && random.nextFloat() < 0.06f)
                particles.push_back ({ mouth.translated (-20.0f * scale, -30.0f * scale), { 0.3f * scale, -0.9f * scale }, 1.0f, 5.0f * scale, true });
        }

        for (auto& p : particles)
        {
            p.pos += p.vel;
            p.vel.y -= p.smoke ? 0.01f : 0.05f * scale;
            p.life -= p.smoke ? 0.012f : 0.045f;
        }
        particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& p) { return p.life <= 0.0f; }),
                         particles.end());
        if (particles.size() > 600)
            particles.erase (particles.begin(), particles.begin() + (long) (particles.size() - 600));
        repaint();
    }

    void MascotView::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds().toFloat();
        const auto st = brain.getState();

        // Stage light in the plugin colour
        const auto centre = area.getCentre();
        const float glow = 0.18f + 0.25f * brain.getIntensity() + (hover ? 0.05f : 0.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff3b1f).withAlpha (glow), centre,
                                                 juce::Colour (0xffff3b1f).withAlpha (0.0f), centre.translated (area.getWidth() * 0.5f, 0), true));
        g.fillEllipse (area.reduced (area.getWidth() * 0.02f));

        if (frames.has (st))
        {
            // Short cross-fade from the previous mood
            const float fade = (float) juce::jlimit (0.0, 1.0, brain.getTimeInState() / 0.15);
            if (fade < 1.0f && frames.has (previousState) && previousState != st)
                paintFrame (g, area, previousState, previousTime + brain.getTimeInState(), 1.0f - fade);
            paintFrame (g, area, st, brain.getTimeInState(), fade);
        }
        else
        {
            paintPlaceholder (g, area);
            paintFire (g);
        }

        if (bubble.isNotEmpty() || st == MascotState::overheated)
            paintBubble (g, area);
    }

    void MascotView::paintFrame (juce::Graphics& g, juce::Rectangle<float> area, MascotState st, double time, float alpha)
    {
        const int frame = (int) (time * frames.fps (st)) % frames.count (st);
        const auto& img = frames.frame (st, frame);
        const bool still = frames.count (st) == 1;   // one picture per mood: the motion comes from here

        // Fit the (square) picture into the area, anchored at its bottom centre
        const float side = std::min (area.getWidth(), area.getHeight() * 1.1f);
        auto box = juce::Rectangle<float> (side, side).withCentre (area.getCentre()).withBottom (area.getBottom() + side * 0.04f);

        const float tt = (float) time, k = brain.getIntensity();
        float dx = 0.0f, dy = 0.0f, rot = 0.0f, sx = 1.0f, sy = 1.0f;
        if (still)
        {
            switch (st)
            {
                case MascotState::idle:       sy = 1.0f + 0.012f * std::sin (tt * 2.0f); sx = 2.0f - sy; break;
                case MascotState::singing:    rot = 0.05f * std::sin (tt * 5.0f); dy = -std::abs (std::sin (tt * 5.0f)) * side * 0.015f; break;
                case MascotState::snapping:   dx = std::sin (tt * 38.0f) * side * 0.006f; sy = 1.0f + 0.02f * std::abs (std::sin (tt * 9.0f)); break;
                case MascotState::fire:
                {
                    // the stronger the reduction, the harder he shakes and the bigger he gets
                    const float shake = (0.004f + 0.012f * k) * side;
                    dx = std::sin (tt * 47.0f) * shake;
                    dy = std::cos (tt * 39.0f) * shake * 0.6f;
                    sx = sy = 1.0f + 0.05f * k;
                    break;
                }
                case MascotState::overheated: dx = std::sin (tt * 61.0f) * side * 0.01f; rot = 0.03f * std::sin (tt * 23.0f); break;
                case MascotState::sleeping:   sy = 1.0f + 0.02f * std::sin (tt * 1.1f); break;
                case MascotState::listening:  rot = -0.04f + 0.02f * std::sin (tt * 1.6f); break;
                case MascotState::proud:      dy = -std::abs (std::sin (tt * 4.0f)) * side * 0.03f * (float) std::max (0.0, 1.0 - time / 2.0); break;
            }
        }

        const auto anchor = box.getCentre().withY (box.getBottom() - side * 0.12f);
        const auto transform = juce::AffineTransform::translation (-anchor.x, -anchor.y)
                                    .scaled (sx, sy)
                                    .rotated (rot)
                                    .translated (anchor.x + dx, anchor.y + dy);

        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (transform);
        g.setOpacity (juce::jlimit (0.0f, 1.0f, alpha));
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (img, box, juce::RectanglePlacement::centred);
    }

    void MascotView::paintPlaceholder (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto st = brain.getState();
        const float s = std::min (area.getWidth() * 0.9f, area.getHeight()) / 300.0f;   // design units: 300 high
        const float cx = area.getX() + area.getWidth() * (compact ? 0.42f : 0.38f);
        const bool asleep = st == MascotState::sleeping;
        const bool moving = st == MascotState::singing || st == MascotState::fire || st == MascotState::snapping;
        const float bob = asleep ? std::sin ((float) t * 1.2f) * 1.5f : std::sin ((float) t * (moving ? 6.0f : 2.0f)) * (moving ? 5.0f : 3.0f);
        const float cy = area.getCentreY() + 10.0f * s + bob * s;
        const auto P = [&] (float x, float y) { return juce::Point<float> (cx + x * s, cy + y * s); };

        // Tail
        {
            juce::Path tail;
            const float wag = std::sin ((float) t * 3.0f) * 10.0f;
            tail.startNewSubPath (P (-50, 80));
            tail.cubicTo (P (-120, 100), P (-140, 30), P (-110, 0 + wag));
            g.setColour (skinBottom.brighter (0.2f));
            g.strokePath (tail, juce::PathStrokeType (8.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path tip;
            const auto tp = P (-110, 0 + wag);
            tip.addTriangle (tp.translated (0, -14 * s), tp.translated (12 * s, 4 * s), tp.translated (-12 * s, 4 * s));
            g.fillPath (tip);
        }

        juce::ColourGradient skin (skinTop, P (-20, -60), skinBottom, P (30, 90), true);

        // Body
        g.setGradientFill (skin);
        g.fillEllipse (juce::Rectangle<float> (120 * s, 90 * s).withCentre (P (0, 70)));

        // Horns
        g.setColour (horn);
        for (float side : { -1.0f, 1.0f })
        {
            juce::Path h;
            h.startNewSubPath (P (side * 52, -48));
            h.quadraticTo (P (side * 80, -105), P (side * 48, -122));
            h.quadraticTo (P (side * 58, -88), P (side * 22, -66));
            h.closeSubPath();
            g.fillPath (h);
        }

        // Head + ears
        g.setGradientFill (skin);
        g.fillEllipse (juce::Rectangle<float> (150 * s, 135 * s).withCentre (P (0, -18)));
        const float twitch = st == MascotState::snapping ? std::sin ((float) t * 40.0f) * 4.0f : 0.0f;
        const bool earUp = st == MascotState::listening;
        for (float side : { -1.0f, 1.0f })
        {
            juce::Path ear;
            ear.addTriangle (P (side * 68, -30), P (side * 110, (earUp ? -70 : -50) + twitch * side), P (side * 66, -4));
            g.fillPath (ear);
        }

        // Eyes
        if (asleep)
        {
            g.setColour (ink);
            for (float side : { -1.0f, 1.0f })
            {
                juce::Path lid;
                lid.addCentredArc (P (side * 28, -36).x, P (side * 28, -36).y, 12 * s, 6 * s, 0.0f,
                                   juce::MathConstants<float>::pi * 0.6f, juce::MathConstants<float>::pi * 1.4f, true);
                g.strokePath (lid, juce::PathStrokeType (3.5f * s));
            }
            g.setFont (vocalypse::ui::display (20.0f * s, true));
            g.setColour (vocalypse::ui::colours::textMuted.withAlpha (0.5f + 0.5f * std::sin ((float) t * 2.0f)));
            g.drawText ("z z Z", juce::Rectangle<float> (90 * s, 30 * s).withCentre (P (90, -110)), juce::Justification::centred);
        }
        else
        {
            const bool squint = st == MascotState::overheated;
            const bool blink = std::fmod (t, 4.0) < 0.12;
            const float eyeH = blink ? 2.0f : (squint ? 8.0f : 14.0f);
            const float look = st == MascotState::fire || st == MascotState::snapping ? 4.0f : std::sin ((float) t * 0.7f) * 3.0f;
            for (float side : { -1.0f, 1.0f })
            {
                g.setColour (eye);
                g.fillEllipse (juce::Rectangle<float> (30 * s, eyeH * 2 * s).withCentre (P (side * 28, -36)));
                g.setColour (ink);
                g.fillEllipse (juce::Rectangle<float> (10 * s, std::max (2.0f, eyeH * 1.3f) * s).withCentre (P (side * 28 + look, -36)));
            }
            // Brows
            juce::Path brows;
            const float angry = st == MascotState::fire ? 6.0f : 0.0f;
            brows.startNewSubPath (P (-46, -60 + (st == MascotState::overheated ? -6.0f : 0.0f)));
            brows.lineTo (P (-14, -52 + angry));
            brows.startNewSubPath (P (46, -60 + (st == MascotState::overheated ? -6.0f : 0.0f)));
            brows.lineTo (P (14, -52 + angry));
            g.setColour (juce::Colour (0xff3a0d08));
            g.strokePath (brows, juce::PathStrokeType (5.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Mouth: open with the flame, a grin otherwise
        const float open = st == MascotState::fire || st == MascotState::overheated ? 0.4f + 0.6f * brain.getIntensity()
                         : st == MascotState::snapping ? 0.35f : st == MascotState::singing ? 0.25f + 0.15f * std::sin ((float) t * 8.0f) : 0.0f;
        juce::Path m;
        m.startNewSubPath (P (10, 8));
        m.quadraticTo (P (60, 4), P (78, 2 - open * 14));
        m.quadraticTo (P (60, 22 + open * 26), P (10, 20 + open * 8));
        m.closeSubPath();
        g.setColour (juce::Colour (0xff2a0503));
        g.fillPath (m);
        juce::Path fang;
        fang.addTriangle (P (30, 7), P (35, 15), P (40, 6));
        g.setColour (juce::Colours::white);
        g.fillPath (fang);
        mouth = P (78, 8);

        // AUTO: hand at the ear / proud: thumbs up
        if (st == MascotState::listening || st == MascotState::proud)
        {
            g.setGradientFill (skin);
            const auto hand = st == MascotState::listening ? P (-104, -36) : P (92, 40 - std::abs (std::sin ((float) t * 5.0f)) * 8.0f);
            g.fillEllipse (juce::Rectangle<float> (30 * s, 26 * s).withCentre (hand));
            if (st == MascotState::proud)
                g.fillRoundedRectangle (juce::Rectangle<float> (10 * s, 22 * s).withCentre (hand.translated (0, -20 * s)), 5 * s);
            else
            {
                g.setColour (vocalypse::ui::colours::textSoft.withAlpha (0.7f));
                for (int i = 0; i < 3; ++i)
                {
                    const float r = (float) std::fmod (t * 30.0 + i * 12.0, 36.0) * s;
                    juce::Path wave;
                    wave.addCentredArc (hand.x - 30 * s, hand.y, r, r, 0.0f, -2.2f, -0.9f, true);
                    g.strokePath (wave, juce::PathStrokeType (2.0f * s));
                }
            }
        }

        // Sweat drops
        if (st == MascotState::overheated)
        {
            g.setColour (sweat);
            for (int i = 0; i < 2; ++i)
            {
                const float y = -70.0f + (float) std::fmod (t * 60.0 + i * 40.0, 60.0);
                g.fillEllipse (juce::Rectangle<float> (8 * s, 14 * s).withCentre (P (i == 0 ? -70.0f : 62.0f, y)));
            }
        }
    }

    void MascotView::paintFire (juce::Graphics& g)
    {
        for (const auto& p : particles)
        {
            const float a = juce::jlimit (0.0f, 1.0f, p.life);
            if (p.smoke)
            {
                g.setColour (juce::Colour (0xffa09696).withAlpha (a * 0.45f));
                const float r = p.size + (1.0f - a) * 14.0f;
                g.fillEllipse (p.pos.x - r, p.pos.y - r, r * 2, r * 2);
                continue;
            }
            const float r = p.size * (0.6f + a * 0.6f);
            juce::ColourGradient fire (juce::Colour (0xffffe678).withAlpha (a), p.pos,
                                       juce::Colour (0xffff280a).withAlpha (0.0f), p.pos.translated (r, 0), true);
            fire.addColour (0.5, juce::Colour (0xffff6e1e).withAlpha (a * 0.7f));
            g.setGradientFill (fire);
            g.fillEllipse (p.pos.x - r, p.pos.y - r, r * 2, r * 2);
        }
    }

    void MascotView::paintBubble (juce::Graphics& g, juce::Rectangle<float> area)
    {
        using vocalypse::i18n::tr;
        const auto text = bubble.isNotEmpty() ? bubble : tr ("Klingt gelispelt!", "Sounds lispy!");
        const float h = compact ? 26.0f : 34.0f;
        auto font = vocalypse::ui::semiBold (compact ? 12.0f : 14.5f);
        const float w = std::min (area.getWidth() - 12.0f,
                                  juce::GlyphArrangement::getStringWidth (font, text) + 24.0f);
        const auto box = juce::Rectangle<float> (w, h).withPosition (area.getRight() - w - 6.0f, area.getY() + 6.0f);
        g.setColour (juce::Colour (0xffefe9ea));
        g.fillRoundedRectangle (box, 9.0f);
        juce::Path tail;
        tail.addTriangle (box.getBottomLeft().translated (18, -1), box.getBottomLeft().translated (10, 12), box.getBottomLeft().translated (34, -1));
        g.fillPath (tail);
        g.setColour (juce::Colour (0xff1a0d0d));
        g.setFont (font);
        g.drawFittedText (text, box.toNearestInt().reduced (8, 2), juce::Justification::centred, 2);
    }
}
