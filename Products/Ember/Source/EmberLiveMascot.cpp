#include "EmberLiveMascot.h"
#include "Text/I18n.h"
#include "UI/Theme.h"
#include <cmath>
#include <cstring>

// The behaviour follows the approved browser prototype (tools/mascot/ember_live_prototype_template.html, v17):
// brain(), triggerS / chomp, talkStep, the idle actions, blinking. Comments name the prototype's parts.
namespace ember
{
    using vocalypse::i18n::tr;

    namespace
    {
        constexpr float pi = juce::MathConstants<float>::pi;
        float clamp1 (float v) { return juce::jlimit (-1.0f, 1.0f, v); }
        const juce::Point<float> mouthRest { 517.0f, 466.0f };   // the middle of the smile, picture space
        const juce::Point<float> earTip { 736.0f, 338.0f };      // where AUTO's sound rings arrive (his right ear)

        // idle actions (prototype: ACTIONS): never the same loop
        enum Action { lookAround, earFlick, tailSwish, yawn, hop, smirk, stretch, numActions };
        const float actionLength[numActions] { 3.2f, 0.8f, 1.6f, 2.6f, 0.9f, 1.8f, 2.4f };
    }

    // ------------------------------------------------------------------ brain
    void LiveBrain::wake() noexcept
    {
        if (asleep)
        {
            asleep = false;
            lid.v = 1; bob.kick (-260); earL.kick (-200); earR.kick (200); squash.kick (-3);
        }
        silentFor = 0; sleepy = 0;
    }

    void LiveBrain::lookAt (juce::Point<float> target, float forSeconds)
    {
        look = target;
        lookFor = forSeconds;
    }

    void LiveBrain::say (const juce::String& text)
    {
        if (text.isEmpty())
            return;
        wake();
        talkText = text; talkShown = 0; talkT = 0; talkEnd = -1; syl = 0;
        gesture = rnd.nextBool() ? -1 : 1;
        gestureAt = 0.3f + rnd.nextFloat() * 0.6f;
    }

    void LiveBrain::chomp (const Eat& e)
    {
        chompT = t;
        const float k = 0.45f + e.strength;
        mouth.v = std::min (mouth.v, 0.15f); mouth.vel = -8;                 // snap shut
        headY.kick (90 * k); head.kick ((rnd.nextBool() ? -1.0f : 1.0f) * 14 * k); squash.kick (1.8f * k);
        earL.kick (-70 * k); earR.kick (70 * k);
        lowLid.v = std::max (lowLid.v, 0.55f);                                // pleased squint
        for (int i = 0, n = fewer ? 1 + juce::roundToInt (2 * e.strength) : 2 + juce::roundToInt (4 * e.strength); i < n; ++i)
            fx.push_back ({ Fx::crumb, e.at, { (rnd.nextFloat() - 0.5f) * 160, -60 - rnd.nextFloat() * 120 }, 0, 0.5f + rnd.nextFloat() * 0.3f, 3 + rnd.nextFloat() * 3 });
        if (e.db > 9)
            hot = std::min (1.0f, hot + 0.5f);
        ++eaten;
    }

    void LiveBrain::pickAction()
    {
        // weighted like the prototype; yawning gets likelier when he is sleepy
        std::vector<int> pool { lookAround, lookAround, earFlick, earFlick, tailSwish, smirk, hop, stretch, yawn };
        if (sleepy > 0.3f) { pool.push_back (yawn); pool.push_back (yawn); }
        action = pool[(size_t) rnd.nextInt ((int) pool.size())];
        actionT = 0; actionLen = actionLength[action]; actionDone = false;
    }

    // prototype: talkStep - types the text, the mouth forms syllables, a calm nod, one gesture per line
    void LiveBrain::talkStep (float dt)
    {
        talkT += dt;
        if (talkEnd < 0)
        {
            talkShown = std::min ((float) talkText.length(), talkShown + dt * 24.0f);
            const int n = (int) talkShown;
            const auto ch = n > 0 ? talkText[n - 1] : ' ';
            syl -= dt;
            if (juce::CharacterFunctions::isWhitespace (ch) || juce::String (",.!?:…").containsChar (ch))
            {
                mouth.target = 0.04f; mouthW.target = 1;
            }
            else
            {
                if (syl <= 0)
                {
                    syl = 0.085f + rnd.nextFloat() * 0.07f;
                    sylOpen = 0.6f + rnd.nextFloat() * 0.5f; sylWide = 0.62f + rnd.nextFloat() * 0.38f;
                }
                const float phase = 1.0f - std::max (0.0f, syl) / 0.12f;
                mouth.target = sylOpen * (1 - 0.3f * phase); mouthW.target = sylWide;
            }
            if (n >= talkText.length())
                talkEnd = talkT;
        }
        else
        {
            mouth.target = 0; mouthW.target = 1;
            if (talkT - talkEnd > 1.6f)
                talkText.clear();
        }
        headY.target += 2.2f * std::sin (talkT * 7.0f);
        head.target += 2.5f * std::sin (talkT * 2.3f) + 1.2f * std::sin (talkT * 5.1f);
        const float g = std::max (0.0f, std::sin (juce::jlimit (0.0f, 1.0f, (talkT - gestureAt) / 1.1f) * pi));
        if (gesture < 0) armL.target = std::min (armL.target, -26 * g); else armR.target = std::max (armR.target, 26 * g);
        lowLid.target = std::max (lowLid.target, 0.12f);
        browY.target = -1.5f - 2.5f * std::max (0.0f, std::sin (talkT * 4.3f));
    }

    void LiveBrain::update (const MascotInput& in, float dt, juce::Point<float> mouthAt)
    {
        dt = juce::jlimit (0.0f, 0.05f, dt);
        t += dt;
        const float bpm = in.bpm > 30.0f ? in.bpm : 120.0f;
        const bool playing = in.hasSignal && ! in.hostStopped && ! in.bypass;
        if (playing)
            beat = std::fmod (beat + dt * bpm / 60.0f, 1.0f);

        // S sounds: the reduction rising over 1 dB is an S; one flies in (prototype: triggerS), at most every 0.45 s
        if (in.reductionDb < 0.5f)
            armed = true;
        if (playing && armed && in.reductionDb > 1.0f && t - lastS > 0.45f && eating.size() < 2 && ! asleep)
        {
            armed = false; lastS = t;
            Eat e;
            e.strength = std::min (1.0f, in.reductionDb / 10.0f);
            e.db = in.reductionDb;
            e.dur = 0.55f + rnd.nextFloat() * 0.15f;
            e.side = rnd.nextBool() ? -1 : 1;
            e.y0 = 220 + rnd.nextFloat() * 220;
            e.size = 54 + 46 * e.strength;
            e.spin = (rnd.nextFloat() - 0.5f) * 2.4f;
            eating.push_back (e);
        }

        // AUTO: listening, then proud (the prototype's 'auto' and 'proud')
        if (wasListening && ! in.autoListening)
        {
            proudFor = 2.2f; bob.kick (-520); squash.kick (3);
        }
        wasListening = in.autoListening;
        proudFor = std::max (0.0f, proudFor - dt);
        hot = std::max (0.0f, hot - dt * 0.35f);
        if (in.reductionDb > 9.0f && playing)
            hot = std::min (1.0f, hot + dt * 2.5f);

        const bool active = playing || in.autoListening || proudFor > 0 || lookFor > 0 || isTalking();
        if (in.bypass) mode = Mode::sleep;
        else if (in.autoListening) mode = Mode::listen;
        else if (proudFor > 0) mode = Mode::proud;
        else if (hot > 0.55f && playing) mode = Mode::hot;
        else if (playing) mode = Mode::groove;
        else mode = asleep ? Mode::sleep : Mode::idle;

        if (! active && ! in.bypass)
        {
            silentFor += dt;
            sleepy = juce::jlimit (0.0f, 1.0f, (silentFor - 14.0f) / 14.0f);
            if (silentFor > 30.0f) asleep = true;
        }
        else if (active)
        {
            if (asleep && ! in.bypass) wake();
            silentFor = 0; sleepy = 0;
        }

        // defaults every frame; the modes and actions override
        const float breath = std::sin (t * (mode == Mode::sleep ? 1.3f : 2.1f));
        for (auto* s : { &head, &headX, &headY, &armL, &armR, &lean, &tail, &earL, &earR, &mouth, &lowLid })
            s->target = 0;
        lid.target = 0; pupil.target = 1; squash.target = -0.012f * breath; bob.target = 0;
        tail.target = std::sin (t * 1.1f) * 8;
        earL.target = std::sin (t * 0.9f) * 2; earR.target = -std::sin (t * 0.9f + 1) * 2;
        armL.target = 1.5f * breath; armR.target = -1.5f * breath;
        browY.target = 0; browTilt.target = 0; mouthW.target = 1;

        // where he looks: the cursor / a knob, or around
        lookFor = std::max (0.0f, lookFor - dt);
        if (lookFor > 0 && mode != Mode::sleep)
        {
            const float dx = (look.x - 512) / 420, dy = (look.y - 380) / 420;
            lookX.target = clamp1 (dx); lookY.target = clamp1 (dy);
            head.target += juce::jlimit (-9.0f, 9.0f, dx * 10); headX.target = juce::jlimit (-10.0f, 10.0f, dx * 12);
        }
        else { lookX.target = std::sin (t * 0.37f) * 0.25f; lookY.target = 0.05f; }

        if (mode == Mode::sleep)
        {
            browY.target = 3; browTilt.target = -3;
            lid.target = 1; head.target = 9; headY.target = 16 + 4 * breath; earL.target = 22; earR.target = -22;
            squash.target = -0.022f * breath; tail.target = 4 * std::sin (t * 0.5f); mouth.target = 0.12f + 0.08f * breath;
            if (rnd.nextFloat() < dt * 0.55f)
                fx.push_back ({ Fx::zed, { 640, 250 }, { 26 + rnd.nextFloat() * 18, -48 }, 0, 2.6f, 30 + rnd.nextFloat() * 16 });
        }
        else if (mode == Mode::listen)
        {
            head.target = 13; headX.target = 10; lean.target = 3; earR.target = -26; earL.target = 6;
            browY.target = -6; browTilt.target = -4;
            pupil.target = 1.12f; lookX.target = 0.7f; lookY.target = -0.35f; tail.target = std::sin (t * 6) * 6;
            armR.target = -8 + 4 * std::sin (t * 3);
            if (rnd.nextFloat() < dt * 2.2f)
                fx.push_back ({ Fx::ring, earTip, {}, 0, 0.9f, 10 });
        }
        else if (mode == Mode::proud)
        {
            const float e = std::min (1.0f, proudFor / 0.4f);
            armL.target = -34 * e; armR.target = 34 * e; mouth.target = 0.55f; lowLid.target = 0.35f; head.target = -4;
            squash.target = 0.02f; tail.target = 26 * std::sin (t * 9);
            browY.target = -4; browTilt.target = -2;
        }
        else if (mode == Mode::hot)
        {
            head.target = 9 * std::sin (t * 22); lowLid.target = 0.55f; lid.target = 0.45f; mouth.target = 0.35f + 0.15f * std::sin (t * 30);
            earL.target = 30; earR.target = -30; armL.target = -12; armR.target = 12; tail.target = 30 * std::sin (t * 14);
            browY.target = 4; browTilt.target = 9;
            if (rnd.nextFloat() < dt * 7)
            {
                const bool left = rnd.nextBool();
                fx.push_back ({ Fx::puff, { left ? 318.0f : 708.0f, 262 }, { (left ? -1.0f : 1.0f) * (20 + rnd.nextFloat() * 30), -90 - rnd.nextFloat() * 50 }, 0, 1.4f, 16 + rnd.nextFloat() * 8 });
            }
        }
        else if (mode == Mode::groove)
        {
            // on the beat: bounce down on every beat, nod, tail on the half beat, arms swing, sings along
            const float hit = std::pow (1 - beat, 3.0f);
            const float beats = t * bpm / 60.0f;
            const float two = std::sin ((beat + (float) ((int) beats % 2)) * pi);
            bob.target = -14 * (1 - hit); squash.target = 0.035f * hit - 0.01f;
            head.target += 7 * std::sin (beats * pi); headY.target = 6 * hit;
            tail.target = 20 * std::sin (beats * pi);
            armL.target = -6 - 8 * two; armR.target = 6 + 8 * two;
            earL.target = -6 * hit; earR.target = 6 * hit;
            mouth.target = juce::jlimit (0.0f, 0.55f, (in.reductionDb > 0 ? 0.3f : 0.0f) + 0.25f * std::abs (std::sin (t * 7.3f)));
            lowLid.target = 0.12f; browY.target = -2 * hit;
            if (rnd.nextFloat() < dt * 1.2f)
                fx.push_back ({ Fx::note, { 600 + rnd.nextFloat() * 60, 430 }, { 40 + rnd.nextFloat() * 40, -80 - rnd.nextFloat() * 40 }, 0, 1.6f, 34 });
        }
        if (mode == Mode::listen || mode == Mode::proud || mode == Mode::hot)
            if (talkText.isEmpty() && mode == Mode::hot && t - hotSaid > 12)
            {
                hotSaid = t;
                say (tr ("Uff, zu viel! Dreh Amount etwas runter.", "Whoa, too much! Turn Amount down a bit."));
            }

        if (mode == Mode::idle)
        {
            if (sleepy > 0)
            {
                lid.target = std::max (lid.target, 0.35f * sleepy + 0.25f * sleepy * (0.5f + 0.5f * std::sin (t * 0.7f)));
                headY.target += 8 * sleepy; head.target += 4 * sleepy; browY.target += 2 * sleepy;
            }
            if (! isTalking())
                nextAction -= dt;
            if (action < 0 && nextAction <= 0 && ! isTalking())
                pickAction();
            if (action >= 0)
            {
                actionT += dt;
                const float p = std::min (1.0f, actionT / actionLen), s = std::sin (p * pi);
                switch (action)
                {
                    case lookAround: head.target = std::sin (p * 2 * pi) * 7; lookX.target = std::sin (p * 2 * pi); lookY.target = -0.15f; break;
                    case earFlick: if (! actionDone && p > 0.1f) { actionDone = true; (rnd.nextBool() ? earL : earR).kick ((rnd.nextBool() ? -1.0f : 1.0f) * 160); } break;
                    case tailSwish: tail.target = std::sin (p * pi * 3) * 22; break;
                    case yawn: { const float e = std::sin (std::min (1.0f, p * 1.25f) * pi);
                                 mouth.target = e * 1.15f; lid.target = std::max (lid.target, e * 0.75f); head.target = -e * 6; headY.target = -e * 8;
                                 armL.target = -e * 18; armR.target = e * 18; lean.target = -e * 1.5f; break; }
                    case hop: if (! actionDone) { actionDone = true; squash.kick (1.4f); bob.kick (-300); } break;
                    case smirk: lookX.target = 0.9f; lookY.target = 0.2f; head.target = 5 * s; lowLid.target = 0.25f * s; break;
                    case stretch: armL.target = -38 * s; armR.target = 38 * s; squash.target = -0.05f * s; lid.target = std::max (lid.target, 0.4f * s); break;
                    default: break;
                }
                if (p >= 1) { action = -1; nextAction = 2 + rnd.nextFloat() * 4; }
            }
        }
        else
            action = -1;

        // the S: flies in, he watches it and opens wide just before it arrives, then chomps (prototype: brain + chomp)
        for (auto& e : eating)
            e.age += dt;
        for (size_t i = eating.size(); i-- > 0;)
            if (eating[i].age >= eating[i].dur)
            {
                eating[i].at = mouthAt;
                chomp (eating[i]);
                eating.erase (eating.begin() + (long) i);
            }
        if (! eating.empty() && mode != Mode::sleep)
        {
            auto& e = eating.front();
            const float left = e.dur - e.age;
            if (e.at != juce::Point<float>())
            {
                lookX.target = clamp1 ((e.at.x - 512) / 260); lookY.target = clamp1 ((e.at.y - 380) / 260);
            }
            if (left < 0.28f) { mouth.target = std::max (mouth.target, 1.1f); pupil.target = 1.1f; browY.target = -5; browTilt.target = -3; }
        }
        if (t - chompT < 0.16f) mouth.target = 0;
        if (t - chompT < 0.35f) { lowLid.target = std::max (lowLid.target, 0.5f); browY.target = 3; browTilt.target = 6; }

        // blinking (not while asleep); sometimes a double blink
        if (mode != Mode::sleep)
        {
            nextBlink -= dt;
            if (nextBlink <= 0 && blinkT < 0) { blinkT = 0; doubleBlink = rnd.nextFloat() < 0.25f; nextBlink = 1.6f + rnd.nextFloat() * 4.2f; }
            if (blinkT >= 0)
            {
                blinkT += dt;
                const float len = 0.16f;
                const float b = blinkT < len ? std::sin (blinkT / len * pi)
                                             : (doubleBlink && blinkT > 0.24f && blinkT < 0.24f + len ? std::sin ((blinkT - 0.24f) / len * pi) : 0.0f);
                lid.v = std::max (lid.v, b);
                if (blinkT > (doubleBlink ? 0.45f : 0.2f)) blinkT = -1;
            }
        }
        if (hover && mode == Mode::idle) { lowLid.target = std::max (lowLid.target, 0.18f); browY.target = -2.5f; }
        if (isTalking() && mode != Mode::sleep) { action = -1; talkStep (dt); }
        else if (isTalking())
        {
            // asleep (BYPASS): no mouth and no gesture, but the text still types in and goes after its time. Before
            // 2026-11-24 the typing stood still here and the text never ended: with BYPASS on, AUTO's "Ich hör zu …"
            // left the plate as an empty frame for good (Jakob's standalone test).
            talkT += dt;
            talkShown = std::min ((float) talkText.length(), talkShown + dt * 24.0f);
            if (talkEnd < 0 && (int) talkShown >= talkText.length())
                talkEnd = talkT;
            if (talkEnd >= 0 && talkT - talkEnd > 1.6f)
                talkText.clear();
        }

        for (auto* s : { &head, &headX, &headY, &earL, &earR, &tail, &armL, &armR, &lean, &bob, &squash, &lookX, &lookY,
                         &lid, &lowLid, &mouth, &mouthW, &pupil, &browY, &browTilt })
            s->step (dt);
        // secondary motion: head turns and bounces drag the ears and the tail
        earL.vel += -head.vel * 0.25f + bob.vel * 0.08f; earR.vel += -head.vel * 0.25f - bob.vel * 0.08f;
        tail.vel += lean.vel * 0.6f - bob.vel * 0.05f;

        for (auto& f : fx)
        {
            f.age += dt;
            if (f.kind == Fx::crumb) f.vel.y += 600 * dt;
            f.pos += f.vel * dt;
        }
        fx.erase (std::remove_if (fx.begin(), fx.end(), [] (const Fx& f) { return f.age >= f.life; }), fx.end());
    }

    RigPose LiveBrain::pose() const
    {
        RigPose p;
        p.head = head.v; p.headX = headX.v; p.headY = headY.v; p.earL = earL.v; p.earR = earR.v; p.tail = tail.v;
        p.armL = armL.v; p.armR = armR.v; p.lean = lean.v; p.bob = bob.v; p.squash = squash.v;
        p.lookX = lookX.v; p.lookY = lookY.v; p.pupil = pupil.v; p.lid = lid.v; p.lowLid = lowLid.v;
        p.mouth = mouth.v; p.mouthW = mouthW.v; p.browY = browY.v; p.browTilt = browTilt.v;
        return p;
    }

    // ------------------------------------------------------------------ view
    namespace
    {
        std::atomic<int> showingViews { 0 };   // Ember windows open in this process (all instances in a host)
    }

    bool emberHostWindowHidden (void* nativeHandle);

    bool LiveMascotView::hostWindowHidden() const
    {
        auto* peer = getPeer();
        return peer != nullptr && emberHostWindowHidden (peer->getNativeHandle());
    }

    AnimationLevel animationLevelFromString (const juce::String& s)
    {
        return s == "reduced" ? AnimationLevel::reduced : (s == "off" ? AnimationLevel::off : AnimationLevel::full);
    }

    juce::String toString (AnimationLevel l)
    {
        return l == AnimationLevel::reduced ? "reduced" : (l == AnimationLevel::off ? "off" : "full");
    }

    struct LiveMascotView::Drawer final : juce::Thread
    {
        explicit Drawer (LiveMascotView& v) : juce::Thread ("Ember drawing"), view (v) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                wake.wait (200);
                if (threadShouldExit())
                    break;
                Job j;
                {
                    const juce::ScopedLock sl (view.frameLock);
                    j = view.job;
                    view.job.pending = false;
                }
                if (j.pending)
                    view.renderFrame (j.pose, j.area, j.scale, j.scene);
            }
        }
        LiveMascotView& view;
        juce::WaitableEvent wake;
    };

    LiveMascotView::LiveMascotView()
    {
        setOpaque (false);
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (juce::FontOptions (100.0f, juce::Font::bold), "S", 0, 0);
            ga.createPath (sGlyph);
        }
        lastTick = juce::Time::getMillisecondCounterHiRes();
        drawer = std::make_unique<Drawer> (*this);
        drawer->startThread (juce::Thread::Priority::normal);
        startTimerHz (60);
    }

    LiveMascotView::~LiveMascotView()
    {
        stopTimer();
        drawer->signalThreadShouldExit();
        drawer->wake.signal();
        drawer->stopThread (2000);
        if (counted)
            --showingViews;
    }

    void LiveMascotView::renderFrame (const RigPose& pose, juce::Rectangle<float> bounds, float scale, const seal::Scene& sc)
    {
        const juce::ScopedLock rl (renderLock);
        int back;
        {
            const juce::ScopedLock fl (frameLock);
            back = front == 0 ? 1 : 0;       // never the one paint() may be showing
            if (backdropPending)
            {
                backdrop = pendingBackdrop;
                pendingBackdrop = {};
                backdropPending = false;
            }
        }
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        // the square stage inside the view holds the figure; the frame covers the whole view (the seal is wider)
        const auto area = bounds.withSizeKeepingCentre (std::min (bounds.getWidth(), bounds.getHeight()), bounds.getHeight());
        // the figure is drawn again only when it changed (animation off, or the same pose twice)
        if (! figure.isValid() || figureArea != area || figureScale != scale || std::memcmp (&figurePose, &pose, sizeof (RigPose)) != 0)
        {
            renderer.renderInto (figure, area, pose, scale);
            figurePose = pose; figureArea = area; figureScale = scale;
        }
        const auto tFig = juce::Time::getMillisecondCounterHiRes();
        double floorMs = 0, runesMs = 0;
        const int w = std::max (1, juce::roundToInt (bounds.getWidth() * scale)), h = std::max (1, juce::roundToInt (bounds.getHeight() * scale));
        auto& out = frames[back];
        const bool wallFills = backdrop.isValid() && backdrop.getWidth() == w && backdrop.getHeight() == h;
        if (out.getWidth() != w || out.getHeight() != h)
            out = juce::Image (juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
        else if (! wallFills)
            out.clear (out.getBounds());     // (the wall is opaque: copying it sets every pixel anyway)
        {
            juce::Graphics g (out);
            if (backdrop.isValid())
            {
                if (wallFills)
                    g.drawImageAt (backdrop, 0, 0);
                else
                    g.drawImage (backdrop, juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h));
            }
            juce::Graphics::ScopedSaveState scaled (g);
            g.addTransform (juce::AffineTransform::scale (scale));
            const auto toArea = RigRenderer::fitTransform (area, renderer.fitHeight, renderer.fitFeet);
            const auto feet = juce::Point<float> (512.0f, 859.0f).transformedBy (toArea);
            const float rx = bounds.getWidth() * 0.46f, ry = rx * 0.2f;
            const juce::Point<float> centre (bounds.getCentreX(), feet.y + 2.0f);
            if (sealStage)
            {
                runeAtlas.setScale (scale);
                const auto tf = juce::Time::getMillisecondCounterHiRes();
                // the calm floor in a layer of its own, only as big as what it draws (the light pool, the rings, the
                // light pillars rising 260 px from the star's points), placed on whole pixels: the same pixels as a
                // layer the size of the frame, with far less to clear and copy
                const auto floorBox = juce::Rectangle<float> (centre.x - rx * 1.08f, centre.y - ry * 0.873f - 260.0f,
                                                              rx * 2.16f, ry * 0.873f + 260.0f + ry * 1.15f).expanded (14.0f);
                const auto px = juce::Rectangle<float> (floorBox.getX() * scale, floorBox.getY() * scale, floorBox.getWidth() * scale, floorBox.getHeight() * scale)
                                    .getSmallestIntegerContainer().getIntersection ({ 0, 0, w, h });
                const bool fresh = floorLayer.getWidth() != px.getWidth() || floorLayer.getHeight() != px.getHeight() || floorAt != px.getPosition();
                if (! px.isEmpty() && (fresh || frameCount++ % 4 == 0))
                {
                    if (fresh)
                        floorLayer = juce::Image (juce::Image::ARGB, px.getWidth(), px.getHeight(), true, juce::SoftwareImageType());
                    else
                        floorLayer.clear (floorLayer.getBounds());
                    floorAt = px.getPosition();
                    juce::Graphics fg (floorLayer);
                    fg.addTransform (juce::AffineTransform::scale (scale).translated ((float) -px.getX(), (float) -px.getY()));
                    seal::drawFloor (fg, centre, rx, ry, sc);
                }
                if (floorLayer.isValid())
                {
                    juce::Graphics::ScopedSaveState pxs (g);
                    g.setOrigin (0, 0);
                    g.addTransform (juce::AffineTransform::scale (1.0f / scale));   // back to pixels: a plain copy
                    g.drawImageAt (floorLayer, floorAt.x, floorAt.y);
                }
                seal::drawFloorLive (g, centre, rx, ry, sc);
                floorMs += juce::Time::getMillisecondCounterHiRes() - tf;
                const auto tr = juce::Time::getMillisecondCounterHiRes();
                seal::drawFloatingRunes (g, runeAtlas, centre, rx, ry, sc, false);
                runesMs += juce::Time::getMillisecondCounterHiRes() - tr;
            }
            // the floor shadow under his feet
            const float up = -pose.bob, k = area.getHeight() * renderer.fitHeight / 700.0f;
            const float sw = 175.0f * k * (1.0f + juce::jlimit (-0.05f, 0.06f, pose.squash)) * (1.0f - std::min (0.3f, up / 500.0f));
            g.setColour (juce::Colours::black.withAlpha (0.28f - std::min (0.15f, up / 1000.0f)));
            g.fillEllipse (feet.x - sw, feet.y - 22.0f * k, 2.0f * sw, 44.0f * k);
            g.setOpacity (1.0f);   // images take the opacity of the last colour (the shadow's 28 % made him see-through)
            {
                // the figure was drawn at exactly this pixel size: copied, not resampled
                juce::Graphics::ScopedSaveState pxs (g);
                g.addTransform (juce::AffineTransform::scale (1.0f / scale));
                g.drawImageAt (figure, juce::roundToInt (area.getX() * scale), juce::roundToInt (area.getY() * scale));
            }
            if (sealStage)
            {
                const auto tr = juce::Time::getMillisecondCounterHiRes();
                seal::drawFloatingRunes (g, runeAtlas, centre, rx, ry, sc, true);
                runesMs += juce::Time::getMillisecondCounterHiRes() - tr;
            }
        }
        {
            const auto tEnd = juce::Time::getMillisecondCounterHiRes();
            benchFigureMs = benchFigureMs.load() + (tFig - t0);
            benchFloorMs = benchFloorMs.load() + floorMs;
            benchRunesMs = benchRunesMs.load() + runesMs;
            benchComposeMs = benchComposeMs.load() + (tEnd - tFig - floorMs - runesMs);
            ++benchFrames;
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        renderMs = renderMs.load() == 0 ? ms : renderMs.load() * 0.9 + ms * 0.1;
        const juce::ScopedLock fl (frameLock);
        front = back;
        frontArea = bounds;
        frontScale = scale;
        ++frameSerial;
    }

    void LiveMascotView::setBackdrop (const juce::Image& wall, float scale)
    {
        // handed over, not drawn here: the drawing thread may be in the middle of a frame (this never waits for it)
        const auto copy = wall.isValid() ? juce::SoftwareImageType().convert (wall) : juce::Image();
        {
            const juce::ScopedLock fl (frameLock);
            pendingBackdrop = copy;
            backdropPending = true;
        }
        paintBackdrop = wall;
        backdropScale = scale;
        stillKey.clear();
        setOpaque (copy.isValid());
        repaint();
    }

    void LiveMascotView::setSealStage (bool on)
    {
        const juce::ScopedLock rl (renderLock);
        sealStage = on;
        // on the seal he stands a little higher and smaller, so the circle fits under his feet
        // (0.74 since 2026-11-24: room for the text plate above his horns)
        renderer.fitHeight = on ? 0.74f : 0.86f;
        renderer.fitFeet = on ? 0.86f : 0.97f;
        figure = {};
        stillKey.clear();
        const juce::ScopedLock fl (frameLock);
        front = -1;
    }

    juce::AffineTransform LiveMascotView::toStage() const
    {
        return RigRenderer::fitTransform (stageArea(), sealStage ? 0.74f : 0.86f, sealStage ? 0.86f : 0.97f);
    }

    seal::Scene LiveMascotView::scene() const
    {
        seal::Scene sc;
        const auto level = getAnimationLevel();
        sc.on = ! input.bypass;
        sc.calm = level != AnimationLevel::full;
        if (level == AnimationLevel::off)
            return sc;          // a still seal: no time, no S, no heat
        sc.t = clock;
        sc.sinceS = sinceSwallow;
        sc.heat = heat;
        return sc;
    }

    // 60 frames a second while he is the one being looked at; 30 when the host's window is not in front, the cursor
    // has been away from the plugin for a few seconds, several Ember windows are open, or this machine needs more
    // than 14 ms a frame (Jakob 2026-11-19: ok). Animation reduced: 30; off: only a few looks a second for changes.
    void LiveMascotView::updateRate (AnimationLevel level)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        auto* top = getTopLevelComponent();
        const bool mouseIn = top != nullptr && top->getLocalBounds().contains (top->getMouseXYRelative());
        const bool focused = top != nullptr && top->getPeer() != nullptr && top->getPeer()->isFocused();
        if (alwaysInFront.load() || (juce::Process::isForegroundProcess() && (mouseIn || focused)))
            lastActive = now;
        const bool active = now - lastActive < 3000.0;
        const double ms = renderMs.load();
        int hz = ms > 14.0 ? 30 : (ms < 9.0 ? 60 : (currentHz >= 30 ? currentHz : 60));
        if (! active || showingViews.load() > 1 || level == AnimationLevel::reduced)
            hz = 30;
        if (level == AnimationLevel::off)
            hz = 4;
        else if (fixedHzForTest.load() > 0)
            hz = fixedHzForTest.load();
        if (hz != currentHz) { currentHz = hz; startTimerHz (hz); }
    }

    void LiveMascotView::setBubble (const juce::String& text)
    {
        if (text != bubble)
        {
            bubble = text;
            if (getAnimationLevel() == AnimationLevel::off)
            {
                plateAlpha = text.isNotEmpty() ? 1.0f : 0.0f;
                repaint();      // he does not speak: the text just shows
            }
            else
                brain.say (text);
        }
    }

    juce::Rectangle<float> LiveMascotView::stageArea() const
    {
        auto r = getLocalBounds().toFloat();
        // a square stage, as tall as the view, centred (the old view drew Ember in a box of the same height)
        return r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight()), r.getHeight());
    }

    void LiveMascotView::postFrame (const RigPose& pose, const seal::Scene& sc)
    {
        {
            const juce::ScopedLock fl (frameLock);
            job = { pose, getLocalBounds().toFloat(), renderScale > 0.0f ? renderScale : 1.0f, sc, true };
        }
        drawer->wake.signal();
        repaint();
    }

    void LiveMascotView::timerCallback()
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const float dt = (float) ((now - lastTick) * 0.001);
        lastTick = now;
        // hidden, minimised (also the host's whole window), or under the licence card: nothing is drawn, the timer
        // only looks a few times a second whether he can be seen again
        const bool showing = isShowing() && ! covered && ! hostWindowHidden();
        if (showing != counted)
        {
            counted = showing;
            showing ? ++showingViews : --showingViews;
        }
        if (! showing)
        {
            if (currentHz != 5) { currentHz = 5; startTimerHz (5); }
            return;
        }
        const auto level = getAnimationLevel();
        updateRate (level);
        // a new pixel density (another screen, the window being resized): frames keep the old one until it has
        // stayed the same for a moment; paint stretches them meanwhile, so resizing never waits for a new frame
        if (screenScale > 0.0f && screenScale != wantedScale) { wantedScale = screenScale; wantedSince = now; }
        if (wantedScale > 0.0f && renderScale != wantedScale && (renderScale == 0.0f || now - wantedSince > 200.0))
            renderScale = wantedScale;

        if (level == AnimationLevel::off)
        {
            // a still picture: drawn again only when something it shows changes
            const auto key = juce::String ((int) input.bypass) + "|" + getLocalBounds().toString() + "|" + juce::String (renderScale)
                             + "|" + juce::String ((int) sealStage) + "|" + juce::String (backdropScale);
            plateAlpha = bubble.isNotEmpty() ? 1.0f : 0.0f;   // still: the plate shows or not, no fade
            if (key != stillKey || shownLevel != level)
            {
                stillKey = key;
                shownLevel = level;
                postFrame (RigPose {}, scene());
            }
            return;
        }
        if (shownLevel == AnimationLevel::off)
            repaint();          // the text plate and the S come back
        shownLevel = level;
        stillKey.clear();
        brain.setFewerParticles (level == AnimationLevel::reduced);
        // he looks at the cursor (or the knob being turned) when it moves anywhere in the window
        const auto toArea = toStage();
        const auto mouse = getMouseXYRelative();
        if (mouse != lastMouse && getTopLevelComponent() != nullptr
            && getTopLevelComponent()->getLocalBounds().contains (getTopLevelComponent()->getLocalPoint (this, mouse)))
            brain.lookAt (juce::Point<float> ((float) mouse.x, (float) mouse.y).transformedBy (toArea.inverted()), 3.0f);
        lastMouse = mouse;
        tick (dt);
        postFrame (brain.pose(), scene());
    }

    void LiveMascotView::tick (float dt)
    {
        const auto mouth = mouthRest.transformedBy (RigRenderer::headTransform (brain.pose()));
        brain.update (input, dt, mouth);
        clock += dt;
        sinceSwallow += dt;
        if (brain.eatenCount() != lastEaten)
        {
            if (brain.eatenCount() > lastEaten)
                sinceSwallow = 0.0f;
            lastEaten = brain.eatenCount();
        }
        heat += ((input.bypass ? 0.0f : input.reductionDb / 9.0f) - heat) * std::min (1.0f, dt * 6.0f);
        // the text plate fades in and out (a quarter of a second) instead of popping
        // shown only while there is something to read: an empty frame is never left standing
        const bool talkingVisible = brain.isTalking() && getAnimationLevel() != AnimationLevel::off;
        const float plateTarget = (talkingVisible && brain.fullText().trim().isNotEmpty()) || bubble.trim().isNotEmpty() ? 1.0f : 0.0f;
        plateAlpha += (plateTarget - plateAlpha) * std::min (1.0f, dt * 8.0f);
        if (std::abs (plateTarget - plateAlpha) < 0.01f)
            plateAlpha = plateTarget;
    }

    void LiveMascotView::paint (juce::Graphics& g)
    {
        const auto area = stageArea();
        if (area.isEmpty() || ! rig->isValid())
            return;
        // drawn at the screen's real pixel density (Windows 125 / 150 / 200 %, editor zoom): he was drawn at 1x and
        // the screen scaled him up, which looked pixelated (Jakob 2026-11-17). Above 2x the difference cannot be seen.
        float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        if (! (scale > 0.25f)) scale = 1.0f;
        scale = juce::jlimit (1.0f, maxRenderScale, scale);
        const bool still = getAnimationLevel() == AnimationLevel::off;
        const auto pose = still ? RigPose {} : brain.pose();
        const auto bounds = getLocalBounds().toFloat();
        // On screen the drawing thread's newest frame is shown, stretched while the size has just changed: the message
        // thread never waits for the drawing thread (a frame takes 3-15 ms). Only the very first frame is drawn here,
        // and only if the drawing thread is free. Off screen (snapshots, tests): drawn here and now.
        const bool onScreen = getPeer() != nullptr;
        if (onScreen)
            screenScale = scale;
        else
            plateAlpha = brain.isTalking() || bubble.isNotEmpty() ? 1.0f : 0.0f;   // snapshots: no fade
        bool fits, any;
        {
            const juce::ScopedLock fl (frameLock);
            fits = front >= 0 && frontArea == bounds && frontScale == scale;
            any = front >= 0;
        }
        if (! onScreen && ! fits)
            renderFrame (pose, bounds, scale, scene());
        else if (onScreen && ! any)
        {
            const juce::ScopedTryLock rl (renderLock);
            if (rl.isLocked())
                renderFrame (pose, bounds, scale, scene());
        }
        {
            const juce::ScopedLock fl (frameLock);
            if (front >= 0 && frameSerial != shownSerial)
            {
                // into the screen's own image, in place: a memory image would be uploaded as a new texture every time
                const auto& src = frames[front];
                if (screen.getWidth() != src.getWidth() || screen.getHeight() != src.getHeight())
                    screen = juce::Image (juce::Image::ARGB, src.getWidth(), src.getHeight(), false, juce::NativeImageType());
                {
                    const juce::Image::BitmapData from (src, juce::Image::BitmapData::readOnly);
                    juce::Image::BitmapData to (screen, juce::Image::BitmapData::writeOnly);
                    for (int y = 0; y < src.getHeight(); ++y)
                        std::memcpy (to.getLinePointer (y), from.getLinePointer (y), (size_t) src.getWidth() * 4);
                }
                shownSerial = frameSerial;
            }
        }
        if (screen.isValid())
        {
            juce::Graphics::ScopedSaveState keep (g);
            g.setOpacity (1.0f);
            g.drawImage (screen, bounds, juce::RectanglePlacement::stretchToFit);
        }
        else if (paintBackdrop.isValid())
            g.drawImage (paintBackdrop, bounds, juce::RectanglePlacement::stretchToFit);
        if (! still)
            paintEffects (g);
        paintPlate (g);
    }

    // the S, notes, Z, steam, crumbs, sound rings (prototype: drawEats / drawFx / drawRings)
    void LiveMascotView::paintEffects (juce::Graphics& g)
    {
        const auto toArea = toStage();
        const float k = std::sqrt (std::abs (toArea.getDeterminant()));
        const auto pose = brain.pose();
        const auto mouth = mouthRest.transformedBy (RigRenderer::headTransform (pose));
        for (const auto& e : brain.eats())
        {
            const float p = std::min (1.0f, e.age / e.dur), q = std::pow (p, 1.35f);
            const juce::Point<float> a (e.side < 0 ? 70.0f : 954.0f, e.y0), b = mouth;
            const juce::Point<float> c ((a.x + b.x) / 2, std::min (a.y, b.y) - 140);
            const auto pos = a * ((1 - q) * (1 - q)) + c * (2 * (1 - q) * q) + b * (q * q);
            const float size = e.size * (1 - 0.65f * std::max (0.0f, (p - 0.78f) / 0.22f)) * std::min (1.0f, p * 6) * k;
            const auto at = pos.transformedBy (toArea);
            auto s = sGlyph;   // the glyph at 100 px, made once
            s.applyTransform (juce::AffineTransform::scale (size / 100.0f));
            const auto bb = s.getBounds();
            const auto tf = juce::AffineTransform::translation (-bb.getCentreX(), -bb.getCentreY()).rotated (e.spin * (1 - p)).translated (at);
            g.setColour (juce::Colour (0xff2a0a12));
            g.strokePath (s, juce::PathStrokeType (size * 0.14f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), tf);
            g.setGradientFill (sealStage ? juce::ColourGradient (seal::pinkLight.brighter (0.3f), at.x, at.y - size / 2, seal::pink, at.x, at.y + size / 2, false)
                                          : juce::ColourGradient (juce::Colour (0xffffe29a), at.x, at.y - size / 2, juce::Colour (0xffff7a2e), at.x, at.y + size / 2, false));
            g.fillPath (s, tf);
        }
        // he swallowed an S: a small pink glow at his mouth (Jakob 2026-11-19)
        if (sealStage && sinceSwallow < 0.5f && ! input.bypass)
        {
            const float u = sinceSwallow / 0.5f, r = (18.0f + 46.0f * u) * k * 1.6f;
            const auto m = mouth.transformedBy (toArea);
            g.setGradientFill (juce::ColourGradient (seal::pinkLight.withAlpha (0.85f * (1.0f - u)), m.x, m.y, seal::pink.withAlpha (0.0f), m.x + r, m.y, true));
            g.fillEllipse (m.x - r, m.y - r * 0.8f, 2.0f * r, 1.6f * r);
            g.setColour (seal::pinkLight.withAlpha (0.7f * (1.0f - u)));
            g.drawEllipse (m.x - r * 0.9f, m.y - r * 0.55f, 1.8f * r, 1.1f * r, 1.6f);
        }
        const auto headT = RigRenderer::headTransform (pose);
        for (const auto& f : brain.effects())
        {
            const float p = f.age / f.life;
            switch (f.kind)
            {
                case LiveBrain::Fx::crumb:
                {
                    const auto at = f.pos.transformedBy (toArea);
                    g.setColour (sealStage ? seal::pinkLight.interpolatedWith (seal::pink, p).withAlpha (1 - p) : juce::Colour::fromFloatRGBA (1.0f, (200.0f - 60.0f * p) / 255.0f, 120.0f / 255.0f, 1 - p));
                    g.fillEllipse (at.x - f.size * k, at.y - f.size * k, 2 * f.size * k, 2 * f.size * k);
                    break;
                }
                case LiveBrain::Fx::note:
                case LiveBrain::Fx::zed:
                {
                    const auto at = (f.pos + juce::Point<float> (std::sin (f.age * 4) * 8, 0)).transformedBy (toArea);
                    g.setColour (juce::Colour (0xffffc89a).withAlpha (std::min (1.0f, (1 - p) * 1.4f)));
                    g.setFont (juce::FontOptions (f.size * k, juce::Font::bold));
                    g.drawText (f.kind == LiveBrain::Fx::zed ? "Z" : juce::String::fromUTF8 ("\xe2\x99\xaa"),
                                juce::Rectangle<float> (at.x - f.size * k, at.y - f.size * k, 2 * f.size * k, 2 * f.size * k), juce::Justification::centred);
                    break;
                }
                case LiveBrain::Fx::puff:
                {
                    const auto at = f.pos.transformedBy (toArea);
                    const float r = f.size * (0.7f + p * 1.5f) * k, a = 0.32f * std::sin (std::min (1.0f, p * 1.6f) * pi * 0.5f) * (1 - p);
                    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff0ea).withAlpha (a), at.x, at.y, juce::Colour (0x00f0dcd6), at.x + r, at.y, true));
                    g.fillEllipse (at.x - r, at.y - r, 2 * r, 2 * r);
                    break;
                }
                case LiveBrain::Fx::ring:
                {
                    // arriving sound at the ear, drawn in head space so it follows the head
                    const auto c = (earTip + juce::Point<float> (-6.0f, 30.0f)).transformedBy (headT).transformedBy (toArea);
                    const float r = (10 + (1 - p) * 58) * k;
                    juce::Path arc;
                    arc.addCentredArc (c.x, c.y, r, r, 0, pi / 2 - 0.75f, pi / 2 + 0.75f, true);
                    g.setColour (juce::Colour (0xffffd6ba).withAlpha (std::sin (p * pi) * 0.75f));
                    g.strokePath (arc, juce::PathStrokeType ((5 - 2 * p) * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    break;
                }
            }
        }
    }

    // The text plate (2026-11-24, Jakob: the speech bubble did not fit the seal): a small dark plate hanging between
    // the chains above Ember, in the panels' style, at a fixed place; the text types in while he speaks.
    juce::Rectangle<float> LiveMascotView::plateArea() const
    {
        const auto r = getLocalBounds().toFloat();
        return juce::Rectangle<float> (std::min (r.getWidth() - 16.0f, 312.0f), 40.0f).withCentre ({ r.getCentreX(), r.getY() + 30.0f });
    }

    void LiveMascotView::paintPlate (juce::Graphics& g)
    {
        // while he says something it types in; otherwise the editor's text stays as it is
        const bool talking = brain.isTalking() && getAnimationLevel() != AnimationLevel::off;
        const auto full = talking ? brain.fullText() : bubble;
        if (full.isNotEmpty())
            plateText = full;
        if (plateAlpha <= 0.01f || plateText.isEmpty())
            return;
        const auto box = plateArea();
        auto& lay = plateLayout;   // laid out again only for a new text
        if (lay.text != plateText || lay.area != box)
        {
            lay.text = plateText;
            lay.area = box;
            lay.glyphs.clear();
            // the info line's font; a long text takes two lines (smaller) rather than running out of the plate
            const auto inner = box.reduced (14.0f, 4.0f);
            auto f = vocalypse::ui::font (12.5f);
            if (juce::GlyphArrangement::getStringWidth (f, plateText) > inner.getWidth())
                f = vocalypse::ui::font (11.0f);
            lay.glyphs.addFittedText (f, plateText, inner.getX(), inner.getY(), inner.getWidth(), inner.getHeight(),
                                      juce::Justification::centred, 2, 0.9f);
        }
        const float a = juce::jlimit (0.0f, 1.0f, plateAlpha);
        // the plate: dark glass like the panels, a thin pink edge, two short chain links it hangs from
        g.setColour (juce::Colour (0xff1e1a1e).withMultipliedAlpha (a));
        for (float x : { box.getX() + 22.0f, box.getRight() - 22.0f })
            for (int k = 0; k < 2; ++k)
                g.drawEllipse (x - (k % 2 ? 2.5f : 5.0f), box.getY() - 15.0f + (float) k * 8.0f, k % 2 ? 5.0f : 10.0f, 12.0f, 2.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xf0140610).withMultipliedAlpha (a), 0, box.getY(),
                                                 juce::Colour (0xf5080308).withMultipliedAlpha (a), 0, box.getBottom(), false));
        g.fillRoundedRectangle (box, 10.0f);
        g.setColour (seal::pink.withAlpha (0.45f * a));
        g.drawRoundedRectangle (box.reduced (0.5f), 10.0f, 1.0f);
        g.setColour (vocalypse::ui::colours::textStrong.withAlpha (a));
        // typing: the first glyphs of the finished layout, so the words never move while they appear
        const int total = lay.glyphs.getNumGlyphs();
        int n = total;
        if (talking && brain.fullText().isNotEmpty())
            n = juce::roundToInt ((float) total * (float) brain.shownText().length() / (float) brain.fullText().length());
        if (n >= total)
            lay.glyphs.draw (g);
        else if (n > 0)
        {
            auto part = lay.glyphs;
            part.removeRangeOfGlyphs (n, -1);
            part.draw (g);
        }
    }

    bool LiveMascotView::hitsFigure (juce::Point<float> p) const
    {
        {
            // the figure alone (the frame also holds the wall and the seal); 8 px around his outline still count.
            // If the drawing thread is busy with it right now: the ellipse below.
            const juce::ScopedTryLock rl (renderLock);
            const auto area = stageArea();
            if (rl.isLocked() && figure.isValid() && ! area.isEmpty())
            {
                const auto& img = figure;
                const float sx = (float) img.getWidth() / area.getWidth(), sy = (float) img.getHeight() / area.getHeight();
                for (float dy = -8.0f; dy <= 8.0f; dy += 4.0f)
                    for (float dx = -8.0f; dx <= 8.0f; dx += 4.0f)
                    {
                        const int x = (int) ((p.x + dx - area.getX()) * sx), y = (int) ((p.y + dy - area.getY()) * sy);
                        if (juce::isPositiveAndBelow (x, img.getWidth()) && juce::isPositiveAndBelow (y, img.getHeight())
                            && img.getPixelAt (x, y).getAlpha() > 40)
                            return true;
                    }
                return false;
            }
        }
        // nothing drawn yet: an ellipse around where he stands
        const auto toPicture = toStage().inverted();
        const auto q = p.transformedBy (toPicture);
        const float nx = (q.x - 512.0f) / 300.0f, ny = (q.y - 520.0f) / 370.0f;
        return nx * nx + ny * ny <= 1.0f;
    }

    void LiveMascotView::mouseMove (const juce::MouseEvent& e)
    {
        const bool on = hitsFigure (e.position);
        brain.setHover (on);
        setMouseCursor (on ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void LiveMascotView::mouseExit (const juce::MouseEvent&)
    {
        brain.setHover (false);
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void LiveMascotView::mouseUp (const juce::MouseEvent& e)
    {
        // the second click of a double-click would start and at once cancel AUTO
        if (e.mouseWasClicked() && e.getNumberOfClicks() == 1 && hitsFigure (e.position) && onClick)
        {
            brain.wake();
            onClick();
        }
    }
}
