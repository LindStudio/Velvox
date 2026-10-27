// Vocalypse mascot (the 3D devil): which animation plays when, which mouth he shows, the packed
// sprite clips in the binary data and the place in SIMPEL (approved assistant concept, artboard "Teufel").

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "UI/MascotBrain.h"
#include "UI/MascotClips.h"
#include "UI/MascotView.h"

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
        c[(size_t) Clip::wakeUp] = { 30, 30.0, false };
        c[(size_t) Clip::spin] = { 30, 30.0, false };
        return c;
    }

    Inputs singing (int score, float db = -20.0f) { Inputs in; in.inputDb = db; in.score = score; return in; }
    Inputs silence() { Inputs in; in.inputDb = -100.0f; return in; }

    // Advances in 1/60 s steps (the editor's frame rate)
    void run (Brain& b, const Inputs& in, double seconds)
    {
        for (int i = 0; i < juce::roundToInt (seconds * 60.0); ++i)
            b.update (in, 1.0 / 60.0);
    }

    struct Fixture
    {
        Brain brain { syntheticClips() };
        Fixture() { run (brain, silence(), 1.2); }   // past the wake-up
    };

    juce::Component* findMascot (juce::Component& root)
    {
        if (dynamic_cast<ui::MascotView*> (&root) != nullptr)
            return &root;
        for (auto* c : root.getChildren())
            if (auto* m = findMascot (*c))
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
        beginTest ("He wakes up once when the plugin opens, then waits");
        {
            Brain b (syntheticClips());
            expect (b.clip() == Clip::wakeUp);
            run (b, silence(), 0.5);
            expect (b.clip() == Clip::wakeUp, "the wake-up plays to its end");
            run (b, silence(), 0.7);
            expect (b.clip() == Clip::idle);
        }

        beginTest ("The vocal quality picks the dance: bad, problem, okay, good, super");
        {
            const std::pair<int, Clip> cases[] { { 20, Clip::bad }, { 50, Clip::problem }, { 68, Clip::okay },
                                                 { 82, Clip::good }, { 93, Clip::super } };
            for (const auto& [score, clip] : cases)
            {
                Fixture f;
                run (f.brain, singing (score), 3.0);
                expectEquals ((int) f.brain.clip(), (int) clip, "score " + juce::String (score));
            }
            Fixture f;
            run (f.brain, singing (-1), 3.0);
            expect (f.brain.clip() == Clip::okay, "no score yet: a neutral dance");
        }

        beginTest ("Silence brings him back to idle; while the auto mix listens he stands and listens (no running)");
        {
            Fixture f;
            run (f.brain, singing (82), 3.0);
            run (f.brain, silence(), 1.0);
            expect (f.brain.clip() == Clip::good, "short pauses between phrases keep the dance");
            run (f.brain, silence(), 3.0);
            expectEquals ((int) f.brain.clip(), (int) Clip::idle);

            auto busy = singing (82);
            busy.autoMixBusy = true;
            run (f.brain, busy, 3.0);
            expect (f.brain.clip() == Clip::idle, "Jakob: running at the start and on play looked odd");
        }

        beginTest ("No flicker: a new state holds a moment, scores near a border keep the current dance");
        {
            Fixture f;
            run (f.brain, singing (82), 3.0);
            run (f.brain, singing (93), 0.5);
            expect (f.brain.clip() == Clip::good, "a change waits for the minimum hold time");
            run (f.brain, singing (93), 3.0);
            expect (f.brain.clip() == Clip::super);
            run (f.brain, singing (89), 4.0);
            expect (f.brain.clip() == Clip::super, "89 is within the hysteresis of the 90 border");
            run (f.brain, singing (84), 4.0);
            expect (f.brain.clip() == Clip::good);
        }

        beginTest ("A top score earns one spin jump, then the super dance; not again right away");
        {
            Fixture f;
            run (f.brain, singing (97), 3.0);
            expect (f.brain.clip() == Clip::spin || f.brain.clip() == Clip::super);
            bool sawSpin = false;
            Fixture g;
            for (int i = 0; i < 300; ++i)
            {
                g.brain.update (singing (97), 1.0 / 60.0);
                sawSpin = sawSpin || g.brain.clip() == Clip::spin;
            }
            expect (sawSpin);
            run (g.brain, singing (97), 2.0);
            expect (g.brain.clip() == Clip::super);
            run (g.brain, singing (97), 20.0);
            expect (g.brain.clip() == Clip::super, "no second spin within the cool-down");
        }

        beginTest ("Frames advance with the clip's frame rate and loops wrap");
        {
            Fixture f;
            run (f.brain, singing (82), 3.0);
            const int a = f.brain.frame();
            f.brain.update (singing (82), 0.1);
            expectEquals ((f.brain.frame() - a + 60) % 60, 3);
            for (int i = 0; i < 200; ++i)
            {
                f.brain.update (singing (82), 1.0 / 60.0);
                expect (f.brain.frame() >= 0 && f.brain.frame() < 60);
            }
        }

        beginTest ("A state change cross-fades from the last frame of the old clip");
        {
            Fixture f;
            run (f.brain, singing (82), 3.0);
            for (int i = 0; i < 300 && f.brain.clip() != Clip::bad; ++i)
                f.brain.update (singing (20), 1.0 / 60.0);
            expect (f.brain.clip() == Clip::bad);
            run (f.brain, singing (20), 0.05);
            expect (f.brain.fadeFrom().has_value() && f.brain.fadeFrom()->clip == Clip::good);
            expect (f.brain.fadeAmount() > 0.0f && f.brain.fadeAmount() < 1.0f);
            run (f.brain, singing (20), 0.5);
            expect (! f.brain.fadeFrom().has_value());
        }

        beginTest ("Loops shortened by the packer cross-fade at the wrap, so the seam doesn't jump");
        {
            Fixture f;
            run (f.brain, singing (82), 3.0);
            bool faded = false;
            int last = f.brain.frame();
            for (int i = 0; i < 180 && ! faded; ++i)
            {
                f.brain.update (singing (82), 1.0 / 60.0);
                if (f.brain.frame() < last && f.brain.clip() == Clip::good)
                    faded = f.brain.fadeFrom().has_value() && f.brain.fadeFrom()->clip == Clip::good
                         && f.brain.fadeFrom()->frame == 59;
                last = f.brain.frame();
            }
            expect (faded, "the last frame fades out over the first");
        }

        beginTest ("The mouth follows the input level: closed, half, open");
        {
            Fixture f;
            run (f.brain, singing (82, -60.0f), 0.3);
            expect (f.brain.mouth() == Mouth::closed);
            run (f.brain, singing (82, -30.0f), 0.3);
            expect (f.brain.mouth() == Mouth::half);
            run (f.brain, singing (82, -12.0f), 0.3);
            expect (f.brain.mouth() == Mouth::open);
            run (f.brain, singing (82, -100.0f), 0.5);
            expect (f.brain.mouth() == Mouth::closed, "closes again in silence");
        }

        beginTest ("Every clip is packed into the plugin, decodes, and the mouth patches fit the frame");
        {
            const auto& lib = ClipLibrary::get();
            int w = 0, h = 0;
            for (int i = 0; i < numClips; ++i)
            {
                const auto& c = lib.clip ((Clip) i);
                expect (c.frameCount() > 0, "clip " + juce::String (i));
                expect (c.info().loops == (i != (int) Clip::wakeUp && i != (int) Clip::spin), "loops " + juce::String (i));
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
        }

        beginTest ("SIMPEL shows the devil bottom left; a click on him opens the Vocal Check");
        {
            test::Harness h;
            h.proc.getAutoMix().dismissPrompt();
            h.proc.getState().state.setProperty ("uiMode", "simple", nullptr);
            h.proc.getState().state.setProperty ("uiTab", "automix", nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> ed (h.proc.createEditor());
            auto* mascot = dynamic_cast<ui::MascotView*> (findMascot (*ed));
            bool visible = mascot != nullptr;
            for (juce::Component* c = mascot; c != nullptr && c != ed.get(); c = c->getParentComponent())
                visible = visible && c->isVisible();
            expect (visible, "the editor is not on screen in tests: every parent up to it must be visible");
            if (mascot == nullptr)
                return;
            const auto inEditor = ed->getLocalArea (mascot, mascot->getLocalBounds());
            expect (inEditor.getX() < ed->getWidth() / 4 && inEditor.getBottom() > ed->getHeight() * 3 / 4, "bottom left");

            // Only the figure takes clicks: he reaches over the spectrum and the bubble, which stay usable
            for (int i = 0; i < 300; ++i)
                mascot->tick ({}, 1.0 / 60.0);   // past the wake-up, standing
            const auto stand = mascot->getStandArea();
            expect (! stand.isEmpty() && mascot->getLocalBounds().toFloat().contains (stand));
            expect (mascot->hitTest (juce::roundToInt (stand.getCentreX()), juce::roundToInt (stand.getY() + stand.getHeight() * 0.25f)), "his head");
            expect (! mascot->hitTest (1, 1), "a transparent corner");

            mascot->simulateClick();
            const auto tab = h.proc.getState().state.getProperty ("uiTab").toString();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
            expectEquals (h.proc.getState().state.getProperty ("uiTab").toString(), juce::String ("check"));
            juce::ignoreUnused (tab);
        }
    }
};

static MascotTests mascotTests;
