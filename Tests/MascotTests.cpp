// Vocalypse mascot Memphy as a comic sticker (Jakob, 2026-10-28): which sticker shows when, its motion,
// the embedded stickers, his place in SIMPEL and his part in the tour.

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "UI/MascotBrain.h"
#include "UI/MascotView.h"
#include "UI/SimpleView.h"
#include "UI/TutorialOverlay.h"

using namespace velvox;
using namespace velvox::ui::mascot;

namespace
{
    Inputs singing (int score, float db = -20.0f) { Inputs in; in.inputDb = db; in.score = score; return in; }
    Inputs silence() { Inputs in; in.inputDb = -100.0f; return in; }

    // Advances in 1/60 s steps (the editor's frame rate)
    void run (Brain& b, const Inputs& in, double seconds)
    {
        for (int i = 0; i < juce::roundToInt (seconds * 60.0); ++i)
            b.update (in, 1.0 / 60.0);
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
        stickerTests();
        editorTests();
    }

    void brainTests()
    {
        beginTest ("He listens when nothing happens");
        {
            Brain b;
            expect (b.mood() == Mood::listening);
            run (b, silence(), 5.0);
            expect (b.mood() == Mood::listening);
        }

        beginTest ("The vocal decides: sad, listening, wink, celebrate");
        {
            const std::pair<int, Mood> cases[] { { 20, Mood::sad }, { 60, Mood::listening }, { -1, Mood::listening },
                                                 { 82, Mood::wink }, { 95, Mood::celebrate } };
            for (const auto& [score, mood] : cases)
            {
                Brain b;
                run (b, singing (score), 3.0);
                expectEquals ((int) b.mood(), (int) mood, "score " + juce::String (score));
            }
        }

        beginTest ("Auto mix: he thinks while it analyses, gets annoyed without a signal, thumbs up when done");
        {
            Brain b;
            auto busy = singing (-1);
            busy.autoMixBusy = true;
            run (b, busy, 2.0);
            expect (b.mood() == Mood::thinking);
            auto quiet = silence();
            quiet.autoMixBusy = true;
            run (b, quiet, 4.0);
            expect (b.mood() == Mood::annoyed, "nothing comes in while it waits");
            auto done = silence();
            done.mixFinished = true;
            b.update (done, 1.0 / 60.0);
            run (b, silence(), 1.6);
            expect (b.mood() == Mood::proud, "thumbs up after the mix");
            run (b, silence(), 4.0);
            expect (b.mood() == Mood::listening, "then back to listening");
        }

        beginTest ("Silence ends a reaction; short pauses keep it; no flicker near a border");
        {
            Brain b;
            run (b, singing (82), 3.0);
            run (b, silence(), 1.0);
            expect (b.mood() == Mood::wink, "short pauses between phrases keep it");
            run (b, silence(), 3.0);
            expect (b.mood() == Mood::listening);
            run (b, singing (20), 3.0);
            expect (b.mood() == Mood::sad);
            run (b, singing (Brain::badBelow + 2), 3.0);
            expect (b.mood() == Mood::sad, "within the hysteresis");
            run (b, singing (60), 0.5);
            expect (b.mood() == Mood::listening);
            run (b, singing (20), 0.5);
            expect (b.mood() == Mood::listening, "a new mood holds a moment before the next one");
            run (b, singing (20), 1.5);
            expect (b.mood() == Mood::sad);
        }

        beginTest ("Talking: the talking sticker, bouncing with the syllables");
        {
            Brain b;
            Inputs in = silence();
            in.talking = true;
            run (b, in, 2.0);
            expect (b.mood() == Mood::talking);
            float lo = 10.0f, hi = 0.0f;
            for (int i = 0; i < 120; ++i)
            {
                b.update (in, 1.0 / 60.0);
                lo = std::min (lo, b.scale());
                hi = std::max (hi, b.scale());
            }
            expect (hi - lo > 0.02f, "he bounces while he talks");
            run (b, silence(), 2.0);
            expect (b.mood() == Mood::listening);
        }

        beginTest ("Motion: a pop when the mood changes, breathing, a hop when he celebrates; cross-fade");
        {
            Brain b;
            run (b, silence(), 2.0);
            run (b, singing (82), 0.05);
            expect (b.mood() == Mood::wink);
            expect (b.fadeFrom() == Mood::listening && b.fadeAmount() > 0.0f, "the old sticker fades out");
            expect (b.scale() < 0.98f, "the pop starts small");
            run (b, singing (82), 0.5);
            expect (b.fadeAmount() == 0.0f);
            expectWithinAbsoluteError (b.scale(), 1.0f, 0.02f, "settled (breathing only)");
            run (b, singing (95), 2.0);
            float lift = 0.0f;
            for (int i = 0; i < 60; ++i)
            {
                b.update (singing (95), 1.0 / 60.0);
                lift = std::max (lift, b.lift());
            }
            expect (b.mood() == Mood::celebrate && lift > 0.03f, "he hops");
        }

        beginTest ("He blinks every few seconds, quickly");
        {
            Brain b (3);
            int blinks = 0, closedFrames = 0;
            bool wasClosed = false;
            for (int i = 0; i < 60 * 30; ++i)
            {
                b.update (silence(), 1.0 / 60.0);
                const bool closed = b.blink() > 0.8f;
                closedFrames += closed ? 1 : 0;
                blinks += closed && ! wasClosed ? 1 : 0;
                wasClosed = closed;
            }
            logMessage (juce::String (blinks) + " blinks in 30 s");
            expect (blinks >= 5 && blinks <= 20, juce::String (blinks));
            expect (closedFrames < 60 * 30 / 20, "the eyes are open nearly all the time");
        }

        beginTest ("Tour scenes: pops in shocked, then winks; at the end the thumbs up");
        {
            Brain b;
            b.playIntro();
            expect (b.mood() == Mood::shocked && b.sceneRunning());
            run (b, singing (20), 1.0);
            expect (b.mood() == Mood::shocked, "a scene plays to its end, singing does not cut it");
            run (b, silence(), 1.0);
            expect (b.mood() == Mood::wink);
            run (b, silence(), 2.5);
            expect (! b.sceneRunning() && b.mood() == Mood::listening);
            b.playBow();
            expect (b.mood() == Mood::proud && b.sceneRunning());
            run (b, silence(), 4.0);
            expect (! b.sceneRunning());
        }
    }

    void stickerTests()
    {
        beginTest ("Every mood has its sticker embedded: square, transparent around the disc, sharp");
        {
            for (int i = 0; i < numMoods; ++i)
            {
                const auto img = ui::MascotView::sticker ((Mood) i);
                expect (img.isValid(), "mood " + juce::String (i));
                if (! img.isValid())
                    continue;
                expect (img.getWidth() == img.getHeight() && img.getWidth() >= 320, "square, sharp up to about 3x");
                expect (img.hasAlphaChannel() && img.getPixelAt (2, 2).getAlpha() == 0, "transparent corner");
                expect (img.getPixelAt (img.getWidth() / 2, img.getHeight() * 3 / 4).getAlpha() == 255, "the sticker itself");
            }
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

        beginTest ("SIMPEL shows Memphy bottom left; only the sticker takes clicks");
        {
            bool visible = mascot != nullptr;
            for (juce::Component* c = mascot; c != nullptr && c != ed.get(); c = c->getParentComponent())
                visible = visible && c->isVisible();
            expect (visible, "the editor is not on screen in tests: every parent up to it must be visible");
            if (mascot == nullptr)
                return;
            const auto inEditor = ed->getLocalArea (mascot, mascot->getLocalBounds());
            expect (inEditor.getX() < ed->getWidth() / 4 && inEditor.getBottom() > ed->getHeight() * 3 / 4, "bottom left");
            for (int i = 0; i < 60; ++i)
                mascot->tick ({}, 1.0 / 60.0);
            const auto stand = mascot->getStandArea();
            expect (! stand.isEmpty() && mascot->getLocalBounds().toFloat().contains (stand));
            expect (mascot->hitTest (juce::roundToInt (stand.getCentreX()), juce::roundToInt (stand.getBottom() - stand.getHeight() * 0.3f)), "the sticker");
            expect (! mascot->hitTest (1, 1), "a transparent corner");
        }

        beginTest ("Tour: the first step is Memphy - he pops in, winks and introduces himself; at the end the thumbs up");
        {
            if (tour == nullptr || simple == nullptr || mascot == nullptr)
            {
                expect (false, "tour, SIMPEL and mascot in the editor");
                return;
            }
            expectEquals (ui::TutorialOverlay::steps().front().target, juce::String ("mascot"));
            tour->start (true);
            auto& greeter = tour->getWelcomeMascot();
            expect (greeter.isVisible(), "Memphy on the welcome card");
            const auto card = greeter.getBounds();
            expect (card.getRight() > tour->getWidth() / 2, "top right of the card");
            tour->goTo (0);
            expect (! greeter.isVisible(), "only on the welcome card");
            tour->finish (ui::TutorialOverlay::Outcome::later);
            tour->start (false);
            expect (mascot->getBrain().mood() == Mood::shocked);
            juce::StringArray lines;
            bool talked = false;
            for (int i = 0; i < 60 * 45; ++i)
            {
                simple->tick (1.0 / 60.0);
                lines.addIfNotAlreadyThere (simple->getBubbleText());
                talked = talked || mascot->getBrain().mood() == Mood::talking;
            }
            const auto all = lines.joinIntoString (" | ");
            logMessage (all);
            expect (all.contains ("Memphy"), "he says his name");
            expect (lines.size() >= 4, "a few lines, one after the other");
            expect (all.contains ("Vocal Check"), "what you can do with him: click = Vocal Check");
            expect (talked, "the talking sticker while he speaks");

            tour->goTo ((int) ui::TutorialOverlay::steps().size() - 1);
            tour->finish (ui::TutorialOverlay::Outcome::finished);
            expect (mascot->getBrain().mood() == Mood::proud, "thumbs up at the end");
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
