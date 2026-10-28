// Vocalypse mascot Memphy (the Flame Hoodie Imp): which animation plays when, which mouth he shows, the
// packed sprite clips in the binary data, his place in SIMPEL and his part in the tour (Jakob, 2026-10-28).

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "UI/MascotBrain.h"
#include "UI/MascotClips.h"
#include "UI/MascotView.h"
#include "UI/SimpleView.h"
#include "UI/TutorialOverlay.h"

using namespace velvox;
using namespace velvox::ui::mascot;

namespace
{
    // Short synthetic clips: 30 fps, loops of 2 s, one-shots of 1 s
    std::array<ClipInfo, numClips> syntheticClips()
    {
        std::array<ClipInfo, numClips> c {};
        for (int i = 0; i < numClips; ++i)
            c[(size_t) i] = { 60, 30.0, true };
        for (auto once : { Clip::fall, Clip::wave, Clip::bow, Clip::relax })
            c[(size_t) once] = { 30, 30.0, false };
        return c;
    }

    Inputs singing (int score, float db = -20.0f) { Inputs in; in.inputDb = db; in.score = score; return in; }
    Inputs silence() { Inputs in; in.inputDb = -100.0f; return in; }
    Inputs talking() { Inputs in = silence(); in.talking = true; return in; }

    bool dancing (Clip c) { return c == Clip::hipHop || c == Clip::breakdance; }

    // Advances in 1/60 s steps (the editor's frame rate)
    void run (Brain& b, const Inputs& in, double seconds)
    {
        for (int i = 0; i < juce::roundToInt (seconds * 60.0); ++i)
            b.update (in, 1.0 / 60.0);
    }

    // Runs until `clip` plays (at most `seconds`); true when it did
    bool runUntil (Brain& b, const Inputs& in, Clip clip, double seconds)
    {
        for (int i = 0; i < juce::roundToInt (seconds * 60.0); ++i)
        {
            if (b.clip() == clip)
                return true;
            b.update (in, 1.0 / 60.0);
        }
        return b.clip() == clip;
    }

    template <typename T>
    T* find (juce::Component& root)
    {
        if (auto* t = dynamic_cast<T*> (&root))
            return t;
        for (auto* c : root.getChildren())
            if (auto* m = find<T> (*c))
                return m;
        return nullptr;
    }
}

class MascotTests final : public juce::UnitTest
{
public:
    MascotTests() : juce::UnitTest ("Mascot", "Headliner") {}

    void runTest() override
    {
        brainTests();
        clipTests();
        editorTests();
    }

    void brainTests()
    {
        beginTest ("He starts listening (the Listening Gesture is his idle)");
        {
            Brain b (syntheticClips());
            expect (b.clip() == Clip::idle);
            run (b, silence(), 5.0);
            expect (b.clip() == Clip::idle);
        }

        beginTest ("Intro: he falls in (no cross-fade - he was not there), waves, then listens");
        {
            Brain b (syntheticClips());
            run (b, silence(), 1.0);
            b.playIntro();
            expect (b.clip() == Clip::fall && ! b.fadeFrom().has_value());
            run (b, singing (20), 0.8);
            expect (b.clip() == Clip::fall, "a one-shot plays to its end, singing does not cut it");
            run (b, silence(), 0.4);
            expect (b.clip() == Clip::wave);
            run (b, silence(), 1.2);
            expect (b.clip() == Clip::idle);
            expect (! b.sceneRunning());
        }

        beginTest ("Bow at the end of the tour, then back to listening");
        {
            Brain b (syntheticClips());
            run (b, silence(), 1.0);
            b.playBow();
            expect (b.clip() == Clip::bow && b.sceneRunning());
            run (b, silence(), 1.2);
            expect (b.clip() == Clip::idle && ! b.sceneRunning());
        }

        beginTest ("The vocal decides: bad = depressed turn, good = hip hop or breakdance, in between he listens");
        {
            const std::pair<int, Clip> cases[] { { 20, Clip::bad }, { 60, Clip::idle }, { -1, Clip::idle } };
            for (const auto& [score, clip] : cases)
            {
                Brain b (syntheticClips());
                run (b, singing (score), 3.0);
                expectEquals ((int) b.clip(), (int) clip, "score " + juce::String (score));
            }
            Brain b (syntheticClips());
            run (b, singing (82), 3.0);
            expect (dancing (b.clip()), "score 82 dances");
        }

        beginTest ("Good singing: he switches between hip hop and breakdance");
        {
            for (unsigned seed : { 1u, 2u, 3u })
            {
                Brain b (syntheticClips(), seed);
                bool hipHop = false, breakdance = false;
                for (int i = 0; i < 60 * 40; ++i)
                {
                    b.update (singing (85), 1.0 / 60.0);
                    hipHop = hipHop || b.clip() == Clip::hipHop;
                    breakdance = breakdance || b.clip() == Clip::breakdance;
                    expect (b.clip() != Clip::relax, "no arm relaxing while he dances");
                }
                expect (hipHop && breakdance, "seed " + juce::String (seed));
            }
        }

        beginTest ("Silence brings him back to listening; while the auto mix listens he listens too");
        {
            Brain b (syntheticClips());
            run (b, singing (82), 3.0);
            run (b, silence(), 1.0);
            expect (dancing (b.clip()), "short pauses between phrases keep the dance");
            run (b, silence(), 3.0);
            expectEquals ((int) b.clip(), (int) Clip::idle);

            auto busy = singing (82);
            busy.autoMixBusy = true;
            run (b, busy, 3.0);
            expect (b.clip() == Clip::idle);
        }

        beginTest ("No flicker: a new state holds a moment, scores near a border keep the current one");
        {
            Brain b (syntheticClips());
            run (b, singing (82), 3.0);
            run (b, singing (20), 0.5);
            expect (dancing (b.clip()), "a change waits for the minimum hold time");
            run (b, singing (20), 3.0);
            expect (b.clip() == Clip::bad);
            run (b, singing (Brain::badBelow + 2), 4.0);
            expect (b.clip() == Clip::bad, "within the hysteresis of the bad border");
            run (b, singing (60), 4.0);
            expect (b.clip() == Clip::idle);
            run (b, singing (Brain::goodFrom + 1), 4.0);
            expect (b.clip() == Clip::idle, "just over the border: still within the hysteresis");
            run (b, singing (Brain::goodFrom + Brain::hysteresis), 4.0);
            expect (dancing (b.clip()));
            run (b, singing (Brain::goodFrom - 2), 4.0);
            expect (dancing (b.clip()), "within the hysteresis of the good border");
        }

        beginTest ("Now and then he relaxes his arms while he listens - at random times, never right away");
        {
            for (unsigned seed : { 1u, 7u })
            {
                Brain b (syntheticClips(), seed);
                double firstRelax = -1.0;
                int relaxes = 0;
                Clip last = b.clip();
                for (int i = 0; i < 60 * 120; ++i)
                {
                    b.update (silence(), 1.0 / 60.0);
                    if (b.clip() == Clip::relax && last != Clip::relax)
                    {
                        ++relaxes;
                        if (firstRelax < 0.0)
                            firstRelax = i / 60.0;
                    }
                    last = b.clip();
                }
                logMessage ("seed " + juce::String (seed) + ": " + juce::String (relaxes) + " relaxes, first after " + juce::String (firstRelax, 1) + " s");
                expect (relaxes >= 2 && relaxes <= 8, "relaxes in 2 min: " + juce::String (relaxes));
                expect (firstRelax >= Brain::relaxMinGap, "not right away");
            }
        }

        beginTest ("Frames advance with the clip's frame rate and loops wrap with a cross-fade");
        {
            Brain b (syntheticClips());
            run (b, singing (20), 3.0);
            expect (b.clip() == Clip::bad);
            const int a = b.frame();
            b.update (singing (20), 0.1);
            expectEquals ((b.frame() - a + 60) % 60, 3);
            bool faded = false;
            int last = b.frame();
            for (int i = 0; i < 200; ++i)
            {
                b.update (singing (20), 1.0 / 60.0);
                expect (b.frame() >= 0 && b.frame() < 60);
                if (b.frame() < last)
                    faded = faded || (b.fadeFrom().has_value() && b.fadeFrom()->clip == Clip::bad && b.fadeFrom()->frame == 59);
                last = b.frame();
            }
            expect (faded, "the last frame fades out over the first");
        }

        beginTest ("A state change cross-fades from the old clip");
        {
            Brain b (syntheticClips());
            run (b, singing (20), 3.0);
            expect (runUntil (b, singing (60), Clip::idle, 5.0));
            run (b, singing (60), 0.05);
            expect (b.fadeFrom().has_value() && b.fadeFrom()->clip == Clip::bad);
            expect (b.fadeAmount() > 0.0f && b.fadeAmount() < 1.0f);
            run (b, singing (60), 0.5);
            expect (! b.fadeFrom().has_value());
        }

        beginTest ("The mouth moves only while he talks: closed, half and open");
        {
            Brain b (syntheticClips());
            run (b, singing (60, -10.0f), 2.0);
            expect (b.mouth() == Mouth::closed, "he listens to the singer, he does not sing along");
            bool half = false, open = false, closed = false;
            for (int i = 0; i < 120; ++i)
            {
                b.update (talking(), 1.0 / 60.0);
                half = half || b.mouth() == Mouth::half;
                open = open || b.mouth() == Mouth::open;
                closed = closed || b.mouth() == Mouth::closed;
            }
            expect (half && open && closed);
            run (b, silence(), 0.3);
            expect (b.mouth() == Mouth::closed);
        }
    }

    void clipTests()
    {
        beginTest ("Every clip is packed into the plugin and decodes; he can talk while he listens");
        {
            const auto& lib = ClipLibrary::get();
            int w = 0, h = 0;
            for (int i = 0; i < numClips; ++i)
            {
                const auto& c = lib.clip ((Clip) i);
                const bool once = i == (int) Clip::fall || i == (int) Clip::wave || i == (int) Clip::bow || i == (int) Clip::relax;
                expect (c.frameCount() > 0, "clip " + juce::String (i));
                expect (c.info().loops == ! once, "loops " + juce::String (i));
                for (int fr : { 0, c.frameCount() / 2, c.frameCount() - 1 })
                {
                    const auto img = c.decodeBase (fr);
                    expect (img.isValid() && img.hasAlphaChannel());
                    if (w == 0) { w = img.getWidth(); h = img.getHeight(); }
                    expect (img.getWidth() == w && img.getHeight() == h, "one frame size for every clip");
                    for (auto m : { Mouth::half, Mouth::open })
                    {
                        const auto p = c.patch (fr, m);
                        if (p.image.isValid())
                            expect (juce::Rectangle<int> (w, h).contains (p.image.getBounds() + p.position));
                    }
                }
            }
            expect (h >= 224, "sharp at twice the design size (he is about 112 units tall in SIMPEL)");
            const auto& idle = lib.clip (Clip::idle);
            expect (idle.patch (idle.frameCount() / 2, Mouth::open).image.isValid(), "an open mouth to talk with");
        }
    }

    void editorTests()
    {
        test::Harness h;
        h.proc.getAutoMix().dismissPrompt();
        h.proc.getState().state.setProperty ("uiMode", "simple", nullptr);
        h.proc.getState().state.setProperty ("uiTab", "automix", nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (h.proc.createEditor());
        auto* mascot = find<ui::MascotView> (*ed);
        auto* simple = find<ui::SimpleView> (*ed);
        auto* tour = find<ui::TutorialOverlay> (*ed);
        if (tour != nullptr && tour->isActive())
            tour->finish (ui::TutorialOverlay::Outcome::later);

        beginTest ("SIMPEL shows Memphy bottom left; a click on him opens the Vocal Check");
        {
            bool visible = mascot != nullptr;
            for (juce::Component* c = mascot; c != nullptr && c != ed.get(); c = c->getParentComponent())
                visible = visible && c->isVisible();
            expect (visible, "the editor is not on screen in tests: every parent up to it must be visible");
            if (mascot == nullptr)
                return;
            const auto inEditor = ed->getLocalArea (mascot, mascot->getLocalBounds());
            expect (inEditor.getX() < ed->getWidth() / 4 && inEditor.getBottom() > ed->getHeight() * 3 / 4, "bottom left");

            // Only the figure takes clicks: he reaches over the spectrum and the bubble, which stay usable
            for (int i = 0; i < 60; ++i)
                mascot->tick ({}, 1.0 / 60.0);
            const auto stand = mascot->getStandArea();
            expect (! stand.isEmpty() && mascot->getLocalBounds().toFloat().contains (stand));
            expect (mascot->hitTest (juce::roundToInt (stand.getCentreX()), juce::roundToInt (stand.getY() + stand.getHeight() * 0.25f)), "his head");
            expect (! mascot->hitTest (1, 1), "a transparent corner");
        }

        beginTest ("Tour: the first step is Memphy - he falls in, waves and introduces himself; at the end he bows");
        {
            if (tour == nullptr || simple == nullptr || mascot == nullptr)
            {
                expect (false, "tour, SIMPEL and mascot in the editor");
                return;
            }
            expectEquals (ui::TutorialOverlay::steps().front().target, juce::String ("mascot"));
            tour->start (false);
            expect (mascot->getBrain().clip() == Clip::fall);
            expect (simple->isShowing() || simple->isVisible(), "the tour opens SIMPEL for him");
            juce::StringArray lines;
            for (int i = 0; i < 60 * 40; ++i)
            {
                simple->tick (1.0 / 60.0);
                lines.addIfNotAlreadyThere (simple->getBubbleText());
            }
            const auto all = lines.joinIntoString (" | ");
            logMessage (all);
            expect (all.contains ("Memphy"), "he says his name");
            expect (lines.size() >= 4, "a few lines, one after the other");
            expect (all.contains ("Vocal Check"), "what you can do with him: click = Vocal Check");

            tour->goTo ((int) ui::TutorialOverlay::steps().size() - 1);
            tour->finish (ui::TutorialOverlay::Outcome::finished);
            expect (mascot->getBrain().clip() == Clip::bow, "he bows at the end");
            expect (simple->isVisible(), "back in SIMPEL to see it");
        }

        beginTest ("A click on Memphy opens the Vocal Check");
        {
            mascot->simulateClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
            expectEquals (h.proc.getState().state.getProperty ("uiTab").toString(), juce::String ("check"));
        }
    }
};

static MascotTests mascotTests;
