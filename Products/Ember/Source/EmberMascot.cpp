#include "EmberMascot.h"
#include "EmberMascotData.h"
#include "UI/Theme.h"
#include "Text/I18n.h"
#include <list>

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
            case MascotState::dancing:    return "dancing";
            case MascotState::listening:  return "listening";
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

        // Too much with hysteresis: in after hotAfterS above hotDb, out after coolAfterS below coolDb
        const float gr = in.bypass ? 0.0f : in.reductionDb;
        hotFor = gr > hotDb ? hotFor + dt : 0.0;
        coolFor = gr < coolDb ? coolFor + dt : 0.0;
        if (! hot && hotFor >= hotAfterS)
            hot = true;
        if (hot && coolFor >= coolAfterS)
            hot = false;

        // One accent per S: rises above accentDb, re-armed once the reduction fell below releaseDb
        sinceAccent += dt;
        accent = std::max (0.0f, accent - (float) (dt / accentDecayS));
        if (gr < releaseDb)
            armed = true;
        if (armed && gr > accentDb && sinceAccent >= minAccentGapS)
        {
            armed = false;
            sinceAccent = 0.0;
            accent = 1.0f;
            strength = juce::jlimit (0.35f, 1.0f, 0.35f + gr / 8.0f);
            ++accents;
        }
        else if (! armed && accent > 0.0f)
            strength = std::max (strength, juce::jlimit (0.35f, 1.0f, 0.35f + gr / 8.0f));   // the S grows

        const float target = juce::jlimit (0.0f, 1.0f, gr / 12.0f);
        intensity += (target - intensity) * (target > intensity ? 0.6f : 0.15f);

        MascotState wanted;
        if (in.bypass)                   wanted = MascotState::sleeping;
        else if (in.autoListening)       wanted = MascotState::listening;
        else if (in.hasSignal || gr > 0.0f) wanted = MascotState::dancing;
        else                             wanted = MascotState::idle;

        const bool immediate = wanted == MascotState::sleeping || wanted == MascotState::listening
                            || state == MascotState::sleeping || state == MascotState::listening;
        if (wanted == state)
        {
            pendingFor = 0.0;
            return;
        }
        if (wanted != pending)
        {
            pending = wanted;
            pendingFor = 0.0;
        }
        pendingFor += dt;
        if (immediate || (pendingFor >= debounceS && timeInState >= minHoldS))
        {
            setState (wanted);
            pendingFor = 0.0;
        }
    }

    // ----------------------------------------------------------------- frames
    // Decoded pictures, shared by every MascotFrames in the process and freed with the last one (never a
    // static juce::Image: releasing Direct2D images at DLL unload hung FL Studio, VOCALYPSE 2026-11-03).
    // Least recently shown pictures are dropped first. Message thread only.
    struct FrameCache
    {
        std::list<std::pair<int, juce::Image>> lru;   // front = most recently used

        juce::Image get (int resource)
        {
            for (auto it = lru.begin(); it != lru.end(); ++it)
                if (it->first == resource)
                {
                    lru.splice (lru.begin(), lru, it);
                    return lru.front().second;
                }
            int size = 0;
            const char* data = EmberMascotData::getNamedResource (EmberMascotData::namedResourceList[resource], size);
            auto img = data != nullptr ? juce::ImageFileFormat::loadFrom (data, (size_t) size) : juce::Image();
            lru.emplace_front (resource, img);
            while ((int) lru.size() > MascotFrames::cacheCapacity)
                lru.pop_back();
            return img;
        }
    };

    namespace
    {
        std::weak_ptr<FrameCache>& sharedCache()
        {
            static std::weak_ptr<FrameCache> cache;   // holds no pictures itself
            return cache;
        }

        std::shared_ptr<FrameCache> acquireCache()
        {
            auto p = sharedCache().lock();
            if (p == nullptr)
            {
                p = std::make_shared<FrameCache>();
                sharedCache() = p;
            }
            return p;
        }
    }

    int MascotFrames::decodedPicturesForTest()
    {
        const auto p = sharedCache().lock();
        return p == nullptr ? 0 : (int) p->lru.size();
    }

    MascotFrames::~MascotFrames() = default;

    MascotFrames::MascotFrames() : cache (acquireCache())
    {
        // File names: <state>_<number>.png, e.g. dancing_001.png ... (Products/Ember/Mascot/frames)
        for (int i = 0; i < EmberMascotData::namedResourceListSize; ++i)
        {
            const auto* resource = EmberMascotData::namedResourceList[i];
            const juce::String file = EmberMascotData::getNamedResourceOriginalFilename (resource);
            if (! file.endsWithIgnoreCase (".png"))
                continue;
            const auto stem = file.upToLastOccurrenceOf (".", false, false);
            auto prefix = stem.upToLastOccurrenceOf ("_", false, false).toLowerCase();
            auto clip = MascotClip::loop;
            if (prefix.endsWith ("_in"))       { clip = MascotClip::in;  prefix = prefix.dropLastCharacters (3); }
            else if (prefix.endsWith ("_out")) { clip = MascotClip::out; prefix = prefix.dropLastCharacters (4); }
            for (int s = 0; s < numMascotStates; ++s)
                if (prefix == mascotStateName ((MascotState) s))
                    frames[key ((MascotState) s, clip)].add ({ i, {} });   // resources come sorted by name: 001, 002 ...
        }

        // Mascot/mascot.json: {"fps": 12, "size": 600, "hands": [[[lx, ly], [rx, ry]] per dancing frame],
        // optional "states": {"dancing": 24}}
        int size = 0;
        if (const char* json = EmberMascotData::getNamedResource ("mascot_json", size))
        {
            const auto v = juce::JSON::parse (juce::String::fromUTF8 (json, size));
            if (v.hasProperty ("fps"))
                defaultFps = (float) (double) v["fps"];
            const double frameSize = v.hasProperty ("size") ? (double) v["size"] : 0.0;
            if (auto* list = v["hands"].getArray(); list != nullptr && frameSize > 0.0)
                for (const auto& f : *list)
                    if (auto* pair = f.getArray(); pair != nullptr && pair->size() == 2)
                    {
                        std::array<juce::Point<float>, 2> h;
                        for (int s = 0; s < 2; ++s)
                            if (auto* xy = (*pair)[s].getArray(); xy != nullptr && xy->size() == 2)
                                h[(size_t) s] = { (float) ((double) (*xy)[0] / frameSize), (float) ((double) (*xy)[1] / frameSize) };
                        hands.push_back (h);
                    }
            if (auto* states = v["states"].getDynamicObject())
                for (const auto& p : states->getProperties())
                    for (int s = 0; s < numMascotStates; ++s)
                        if (p.name.toString() == mascotStateName ((MascotState) s))
                            rates[s] = (float) (double) p.value;
        }
    }

    bool MascotFrames::has (MascotState s, MascotClip c) const noexcept { return count (s, c) > 0; }

    juce::Point<float> MascotFrames::hand (int index, int side) const noexcept
    {
        if (hands.empty())
            return { -1.0f, -1.0f };
        return hands[(size_t) (((index % (int) hands.size()) + (int) hands.size()) % (int) hands.size())][(size_t) juce::jlimit (0, 1, side)];
    }

    int MascotFrames::count (MascotState s, MascotClip c) const noexcept
    {
        const auto it = frames.find (key (s, c));
        return it == frames.end() ? 0 : it->second.size();
    }

    juce::Image MascotFrames::frame (MascotState s, MascotClip c, int index) const
    {
        const auto& list = frames.at (key (s, c));
        const auto& src = list.getReference (juce::jlimit (0, list.size() - 1, index));
        return src.resource >= 0 ? cache->get (src.resource) : src.image;
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
        brain.update (input, dt);
        advanceClips (dt);

        if (brain.getAccentCount() != lastAccents)
        {
            lastAccents = brain.getAccentCount();
            if (brain.getState() == MascotState::dancing)
                spawnSparks();
        }
        const float scale = getHeight() / 300.0f;
        if (! frames.has (brain.getState()))
        {
            const auto st = brain.getState();
            if ((st == MascotState::idle || st == MascotState::sleeping) && random.nextFloat() < 0.06f)
                particles.push_back ({ mouth.translated (-20.0f * scale, -30.0f * scale), { 0.3f * scale, -0.9f * scale }, 1.0f, 5.0f * scale, Kind::smoke });
        }

        for (auto& p : particles)
        {
            p.pos += p.vel;
            switch (p.kind)
            {
                case Kind::smoke:  p.vel.y -= 0.01f;                          p.life -= 0.012f; break;
                case Kind::spark:  p.vel *= 0.9f; p.vel.y += 0.12f * scale;    p.life -= 0.07f;  break;
                case Kind::letter: p.vel *= 0.92f;                            p.life -= 0.05f;  break;
            }
        }
        particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& p) { return p.life <= 0.0f; }),
                         particles.end());
        if (particles.size() > 200)
            particles.erase (particles.begin(), particles.begin() + (long) (particles.size() - 200));
        repaint();
    }

    juce::Rectangle<float> MascotView::frameBox() const
    {
        // the box paintFrame draws the (square) picture into, anchored at the bottom centre
        const auto area = getLocalBounds().toFloat();
        const float side = std::min (area.getWidth(), area.getHeight() * 1.1f);
        return juce::Rectangle<float> (side, side).withCentre (area.getCentre()).withBottom (area.getBottom() + side * 0.04f);
    }

    float MascotView::accentTilt() const noexcept
    {
        // the snap: a quick nod towards the hand that took the S
        return (accentSide == 0 ? -1.0f : 1.0f) * 0.03f * brain.getAccent() * brain.getAccentStrength();
    }

    void MascotView::spawnSparks()
    {
        // From the hand that is further out in the frame on screen (the one he swings); the placeholder: its mouth
        const float k = brain.getAccentStrength() > 0.0f ? brain.getAccentStrength() : 0.6f;
        const auto box = frameBox();
        juce::Point<float> at = mouth;
        float dir = 1.0f;
        if (frames.has (MascotState::dancing) && frames.hand (0, 0).x >= 0.0f)
        {
            const int index = (int) (clipTime * frames.fps (MascotState::dancing));
            const auto l = frames.hand (index, 0), r = frames.hand (index, 1);
            accentSide = std::abs (l.x - 0.5f) > std::abs (r.x - 0.5f) ? 0 : 1;
            const auto h = accentSide == 0 ? l : r;
            at = { box.getX() + h.x * box.getWidth(), box.getY() + h.y * box.getHeight() };
            dir = accentSide == 0 ? -1.0f : 1.0f;
        }
        else
            accentSide = 1 - accentSide;
        const float scale = box.getWidth() / 600.0f;
        const int n = 4 + (int) (k * 8.0f);
        for (int i = 0; i < n; ++i)
        {
            const float a = juce::MathConstants<float>::pi * (-0.5f + dir * (0.15f + random.nextFloat() * 0.6f));
            const float v = (4.0f + random.nextFloat() * 6.0f) * (0.6f + 0.6f * k) * scale;
            particles.push_back ({ at, { std::cos (a) * v, std::sin (a) * v - 2.0f * scale }, 1.0f,
                                   (3.5f + random.nextFloat() * 3.5f) * (0.7f + 0.5f * k) * scale, Kind::spark });
        }
        // the S itself, flicked away and gone
        particles.push_back ({ at.translated (dir * 8.0f * scale, -10.0f * scale), { dir * 3.0f * scale, -3.5f * scale }, 1.0f,
                               (38.0f + 20.0f * k) * scale, Kind::letter });
    }

    void MascotView::advanceClips (double dt)
    {
        clipTime += dt;
        fadeTime += dt;
        const auto desired = brain.getState();

        const auto switchTo = [this] (MascotState s, bool viaClips)
        {
            previousState = shownState;
            previousClip = shownClip;
            previousTime = clipTime;
            // Clips meet in the neutral pose, but AI clips never match exactly: a short blend hides the seam.
            // Moods without clips get a longer cross-fade.
            fading = s != shownState;
            fadeLength = viaClips ? 0.15 : 0.35;
            fadeTime = 0.0;
            shownState = s;
            shownClip = frames.has (s, MascotClip::in) ? MascotClip::in : MascotClip::loop;
            clipTime = 0.0;
        };

        if (shownClip == MascotClip::out)
        {
            if (clipTime >= frames.duration (shownState, MascotClip::out))
                switchTo (desired, true);
        }
        else if (shownClip == MascotClip::in)
        {
            if (clipTime >= frames.duration (shownState, MascotClip::in))
            {
                shownClip = MascotClip::loop;
                clipTime = 0.0;
            }
        }
        else if (desired != shownState)
        {
            if (frames.has (shownState, MascotClip::out))
            {
                shownClip = MascotClip::out;
                clipTime = 0.0;
            }
            else
            {
                switchTo (desired, frames.has (desired, MascotClip::in));
            }
        }
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

        if (frames.has (shownState))
        {
            // Moods without in / out clips cross-fade
            const float fade = fading ? (float) juce::jlimit (0.0, 1.0, fadeTime / fadeLength) : 1.0f;
            if (fade < 1.0f && frames.has (previousState) && previousState != shownState)
                paintFrame (g, area, previousState, previousClip, previousTime + fadeTime, 1.0f - fade);
            paintFrame (g, area, shownState, shownClip, clipTime, fade);
            paintSparks (g);
        }
        else
        {
            paintPlaceholder (g, area);
            paintSparks (g);
        }

        if (bubble.isNotEmpty() || (brain.isHot() && st == MascotState::dancing))
            paintBubble (g, area);
    }

    void MascotView::paintFrame (juce::Graphics& g, juce::Rectangle<float> area, MascotState st, MascotClip clip, double time, float alpha)
    {
        if (! frames.has (st, clip))
            clip = MascotClip::loop;
        const int n = frames.count (st, clip);
        const int raw = (int) (time * frames.fps (st));
        const int frame = clip == MascotClip::loop ? raw % n : std::min (raw, n - 1);   // in / out play once
        const auto img = frames.frame (st, clip, frame);
        const bool still = n == 1 && clip == MascotClip::loop;   // one picture per mood: the motion comes from here

        // Fit the (square) picture into the area, anchored at its bottom centre
        juce::ignoreUnused (area);
        const auto box = frameBox();
        const float side = box.getWidth();

        const float tt = (float) time;
        float dx = 0.0f, dy = 0.0f, rot = 0.0f, sx = 1.0f, sy = 1.0f;
        if (still)
        {
            switch (st)
            {
                // Small, slow motion only: breathing and a gentle sway (calm, Jakob 2026-11-10)
                case MascotState::idle:       sy = 1.0f + 0.008f * std::sin (tt * 1.8f); sx = 2.0f - sy; break;
                case MascotState::dancing:    rot = 0.015f * std::sin (tt * 2.6f); sy = 1.0f + 0.006f * std::sin (tt * 5.2f); break;
                case MascotState::sleeping:   sy = 1.0f + 0.012f * std::sin (tt * 1.1f); break;
                case MascotState::listening:  rot = -0.02f + 0.008f * std::sin (tt * 1.4f); break;
            }
        }
        if (st == MascotState::dancing)
        {
            // the S accent: a quick snap - he dips a little and nods towards the hand
            const float a = brain.getAccent() * brain.getAccentStrength();
            sy *= 1.0f - 0.03f * a;
            sx *= 1.0f + 0.015f * a;
            rot += accentTilt();
            dy += 0.01f * a * side;
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
        const bool moving = st == MascotState::dancing;
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
        const float twitch = st == MascotState::dancing ? brain.getAccent() * 6.0f : 0.0f;
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
            const bool squint = brain.isHot();
            const bool blink = std::fmod (t, 4.0) < 0.12;
            const float eyeH = blink ? 2.0f : (squint ? 8.0f : 14.0f);
            const float look = std::sin ((float) t * 0.7f) * 3.0f;
            for (float side : { -1.0f, 1.0f })
            {
                g.setColour (eye);
                g.fillEllipse (juce::Rectangle<float> (30 * s, eyeH * 2 * s).withCentre (P (side * 28, -36)));
                g.setColour (ink);
                g.fillEllipse (juce::Rectangle<float> (10 * s, std::max (2.0f, eyeH * 1.3f) * s).withCentre (P (side * 28 + look, -36)));
            }
            // Brows
            juce::Path brows;
            const float angry = 6.0f * brain.getAccent();
            brows.startNewSubPath (P (-46, -60 + (brain.isHot() ? -6.0f : 0.0f)));
            brows.lineTo (P (-14, -52 + angry));
            brows.startNewSubPath (P (46, -60 + (brain.isHot() ? -6.0f : 0.0f)));
            brows.lineTo (P (14, -52 + angry));
            g.setColour (juce::Colour (0xff3a0d08));
            g.strokePath (brows, juce::PathStrokeType (5.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Mouth: a grin, open while he dances
        const float open = st == MascotState::dancing ? 0.25f + 0.15f * std::sin ((float) t * 8.0f) : 0.0f;
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

        // AUTO: hand at the ear
        if (st == MascotState::listening)
        {
            g.setGradientFill (skin);
            const auto hand = P (-104, -36);
            g.fillEllipse (juce::Rectangle<float> (30 * s, 26 * s).withCentre (hand));
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

    }

    void MascotView::paintSparks (juce::Graphics& g)
    {
        for (const auto& p : particles)
        {
            const float a = juce::jlimit (0.0f, 1.0f, p.life);
            switch (p.kind)
            {
                case Kind::smoke:
                {
                    g.setColour (juce::Colour (0xffa09696).withAlpha (a * 0.45f));
                    const float r = p.size + (1.0f - a) * 14.0f;
                    g.fillEllipse (p.pos.x - r, p.pos.y - r, r * 2, r * 2);
                    break;
                }
                case Kind::spark:
                {
                    const float r = p.size * (0.5f + 0.5f * a);
                    juce::ColourGradient glow (juce::Colour (0xfffff3c4).withAlpha (a), p.pos,
                                               juce::Colour (0xffff6a1e).withAlpha (0.0f), p.pos.translated (r * 2.2f, 0), true);
                    g.setGradientFill (glow);
                    g.fillEllipse (p.pos.x - r * 2.2f, p.pos.y - r * 2.2f, r * 4.4f, r * 4.4f);
                    g.setColour (juce::Colour (0xfffffbe8).withAlpha (a));
                    g.fillEllipse (p.pos.x - r * 0.6f, p.pos.y - r * 0.6f, r * 1.2f, r * 1.2f);
                    break;
                }
                case Kind::letter:
                {
                    // pops up, then breaks into nothing: the S he took away
                    const float pop = a > 0.8f ? (1.0f - a) / 0.2f : 1.0f;
                    g.setFont (vocalypse::ui::display (p.size * (0.7f + 0.3f * pop), true));
                    g.setColour (juce::Colour (0xffffd27a).withAlpha (a * a));
                    g.drawText ("S", juce::Rectangle<float> (p.size * 2.0f, p.size * 2.0f).withCentre (p.pos), juce::Justification::centred, false);
                    break;
                }
            }
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
