// Headliner 0.6.1 chain items: module solo (spec item 4) and horizontal scrolling of the chain row (item 3).
// Solo is a monitoring state: every other module instance bypassed, latency unchanged, never saved, never an undo step.

#include "TestHelpers.h"
#include "DSP/Solo.h"
#include "UI/ChainView.h"
#include "UI/DetailPanels.h"
#include "UI/SoloButton.h"
#include "PluginEditor.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged,
                            juce::ModifierKeys mods = {}, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downPos, now, clicks, dragged);
    }

    void click (juce::Component& c, juce::Point<float> at, juce::ModifierKeys mods = {})
    {
        c.mouseMove (mouse (c, at, at, false));
        c.mouseDown (mouse (c, at, at, false, mods));
        c.mouseUp (mouse (c, at, at, false, mods));
    }

    void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = {})
    {
        c.mouseDown (mouse (c, from, from, false, mods));
        for (int i = 1; i <= 8; ++i)
            c.mouseDrag (mouse (c, from + (to - from) * ((float) i / 8.0f), from, true, mods));
        c.mouseUp (mouse (c, to, from, true, mods));
    }

    VelvoxAudioProcessorEditor& asVelvox (juce::AudioProcessorEditor& e) { return dynamic_cast<VelvoxAudioProcessorEditor&> (e); }

    std::unique_ptr<juce::AudioProcessorEditor> proEditor (Harness& h)
    {
        h.proc.getAutoMix().dismissPrompt();
        h.proc.getState().state.setProperty ("uiMode", "pro", nullptr);
        return std::unique_ptr<juce::AudioProcessorEditor> (h.proc.createEditor());
    }

    // Mean brightness of an image area (0..1)
    float brightness (const juce::Image& img, juce::Rectangle<int> r)
    {
        double sum = 0.0;
        int n = 0;
        r = r.getIntersection (img.getBounds());
        for (int y = r.getY(); y < r.getBottom(); ++y)
            for (int x = r.getX(); x < r.getRight(); ++x, ++n)
                sum += img.getPixelAt (x, y).getPerceivedBrightness();
        return n > 0 ? (float) (sum / n) : 0.0f;
    }

    // Pixels close to the solo colour
    int soloPixels (const juce::Image& img, juce::Rectangle<int> r)
    {
        int n = 0;
        r = r.getIntersection (img.getBounds());
        for (int y = r.getY(); y < r.getBottom(); ++y)
            for (int x = r.getX(); x < r.getRight(); ++x)
            {
                const auto c = img.getPixelAt (x, y);
                if (std::abs (c.getHue() - ui::soloColour.getHue()) < 0.03f && c.getSaturation() > 0.5f && c.getBrightness() > 0.7f)
                    ++n;
            }
        return n;
    }

    juce::Image render (juce::Component& c)
    {
        juce::Image img (juce::Image::ARGB, c.getWidth(), c.getHeight(), true);
        juce::Graphics g (img);
        c.paintEntireComponent (g, false);
        return img;
    }
    // Every module instance switched on (the flags applySolo works on)
    dsp::ChainSettings allOn()
    {
        dsp::ChainSettings s;
        for (int k = 0; k <= dsp::maxExtraInstances; ++k)
        {
            auto b = s.instance (k);
            b.eqOn = b.compOn = b.deessOn = b.delayOn = b.reverbOn = true;
            b.sat.enabled = b.doubler.enabled = b.smash.enabled = b.choir.enabled = b.smooth.enabled = b.ride.enabled = true;
            if (k == 0)
                s.setInstance0 (b);
            else
                s.extra[(size_t) k - 1] = b;
        }
        s.pitch.enabled = s.chop.enabled = s.pulse.enabled = s.checkOn = true;
        s.pitch.mix = 0.8f;
        return s;
    }

    // On-state of one instance as applySolo sees it
    bool onOf (const dsp::ChainSettings& s, ModuleId id, int k)
    {
        const auto b = s.instance (k);
        switch (id)
        {
            case ModuleId::eq:      return b.eqOn;
            case ModuleId::comp:    return b.compOn;
            case ModuleId::deess:   return b.deessOn;
            case ModuleId::sat:     return b.sat.enabled;
            case ModuleId::doubler: return b.doubler.enabled;
            case ModuleId::delay:   return b.delayOn;
            case ModuleId::reverb:  return b.reverbOn;
            case ModuleId::smash:   return b.smash.enabled;
            case ModuleId::choir:   return b.choir.enabled;
            case ModuleId::smooth:  return b.smooth.enabled;
            case ModuleId::ride:    return b.ride.enabled;
            case ModuleId::pitch:   return s.pitch.enabled && s.pitch.mix > 0.0f;
            case ModuleId::chop:    return s.chop.enabled;
            case ModuleId::pulse:   return s.pulse.enabled;
            case ModuleId::check:   return s.checkOn;
        }
        return false;
    }
}

class ChainSoloTests final : public juce::UnitTest
{
public:
    ChainSoloTests() : juce::UnitTest ("Chain solo and scrolling (0.6.1)", "Velvox") {}

    void runTest() override
    {
        beginTest ("applySolo: only the soloed instance stays on; Check (read only) untouched");
        {
            for (int m = 0; m < dsp::numModules; ++m)
            {
                const auto soloId = (ModuleId) m;
                if (! dsp::canSolo (soloId))
                    continue;
                for (int k = 0; k < dsp::maxInstances (soloId); ++k)
                {
                    auto s = allOn();
                    dsp::applySolo (s, soloId, k);
                    for (int o = 0; o < dsp::numModules; ++o)
                        for (int j = 0; j < dsp::maxInstances ((ModuleId) o); ++j)
                        {
                            const auto id = (ModuleId) o;
                            const bool expected = (id == soloId && j == k) || id == ModuleId::check;
                            expect (onOf (s, id, j) == expected,
                                    juce::String ("solo ") + dsp::moduleKey (soloId) + "#" + juce::String (k + 1) + ": "
                                        + dsp::moduleKey (id) + "#" + juce::String (j + 1));
                        }
                }
            }
            expect (! dsp::canSolo (ModuleId::check), "Check does not change the sound: no solo");
        }

        beginTest ("applySolo: the pitch shifter keeps its latency (on, mix 0); the soloed module keeps its settings");
        {
            auto s = allOn();
            dsp::applySolo (s, ModuleId::comp, 0);
            expect (s.pitch.enabled, "pitch stays on (latency unchanged)");
            expectEquals (s.pitch.mix, 0.0f, "pitch heard dry");

            auto p = allOn();
            dsp::applySolo (p, ModuleId::pitch, 0);
            expect (p.pitch.enabled);
            expectEquals (p.pitch.mix, 0.8f, "soloed pitch unchanged");

            auto off = allOn();
            off.compOn = false;
            dsp::applySolo (off, ModuleId::comp, 0);
            expect (! off.compOn, "a soloed module that is switched off stays off");
        }

        runProcessorTests();
        runSoloUiTests();
        runScrollTests();
    }

    // Every module, several duplicates, two sends: 21 instances
    static constexpr const char* fullChain = "v2,check,eq,eq#2,comp,comp#2,deess,sat,sat#2,smooth,ride,smash,doubler,choir,pitch,chop,"
                                             "pulse,delay:insert,eq#3,reverb:send,reverb#2:send";

    static juce::MouseWheelDetails wheelOf (float dy, float dx = 0.0f, bool smooth = false)
    {
        juce::MouseWheelDetails w;
        w.deltaX = dx;
        w.deltaY = dy;
        w.isReversed = false;
        w.isSmooth = smooth;
        w.isInertial = false;
        return w;
    }

    void runScrollTests()
    {
        constexpr float notch = 120.0f / 256.0f;   // one wheel notch on Windows (JUCE)

        beginTest ("Chain scroll: wheel (one notch ~ one tile), shift-wheel / tilt and trackpad scroll the row");
        {
            Harness h;
            h.proc.setRouting (dsp::Routing::fromString (fullChain));
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 886, ui::ChainView::preferredHeight);
            view.refresh();
            expect (view.getMaxScroll() > 1000.0f, "21 instances: the row scrolls a lot");
            const float step = view.scrollStep();
            expect (step >= ui::ChainView::minTileW, "a step is one tile");
            const auto at = view.getRowViewport().getCentre();

            view.mouseWheelMove (mouse (view, at, at, false), wheelOf (-notch));
            expect (view.getScroll() >= 0.75f * step && view.getScroll() <= 1.5f * step,
                    "one notch down = about one tile: " + juce::String (view.getScroll()));
            view.mouseWheelMove (mouse (view, at, at, false), wheelOf (notch));
            expectWithinAbsoluteError (view.getScroll(), 0.0f, 0.5f, "and back");

            view.mouseWheelMove (mouse (view, at, at, false, juce::ModifierKeys::shiftModifier), wheelOf (-notch));
            expect (view.getScroll() > 0.5f * step, "shift-wheel");
            view.scrollTo (0.0f, false);
            view.mouseWheelMove (mouse (view, at, at, false), wheelOf (0.0f, -notch));
            expect (view.getScroll() > 0.5f * step, "horizontal wheel / tilt (WM_MOUSEHWHEEL: right = negative)");

            view.scrollTo (0.0f, false);
            for (int i = 0; i < 10; ++i)
                view.mouseWheelMove (mouse (view, at, at, false), wheelOf (0.0f, -0.02f, true));
            expect (view.getScroll() > 10.0f, "trackpad swipe: " + juce::String (view.getScroll()));

            view.scrollTo (0.0f, false);
            for (int i = 0; i < 400; ++i)
                view.mouseWheelMove (mouse (view, at, at, false), wheelOf (-notch));
            expectWithinAbsoluteError (view.getScroll(), view.getMaxScroll(), 0.5f, "the wheel reaches the end");
        }

        beginTest ("Chain scroll: drag the thumb, drag the empty strip, middle-drag, edge arrows; a tile drag still reorders");
        {
            Harness h;
            h.proc.setRouting (dsp::Routing::fromString (fullChain));
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 886, ui::ChainView::preferredHeight);
            view.refresh();
            view.scrollTo (0.0f, false);

            const auto track = view.getScrollTrackBounds(), thumb = view.getScrollThumbBounds();
            expect (! track.isEmpty() && ! thumb.isEmpty() && track.contains (thumb), "a scroll thumb along the row");
            expect (thumb.getWidth() >= 24.0f, "thumb big enough to grab");
            const auto routingBefore = h.proc.getRouting();
            drag (view, thumb.getCentre(), thumb.getCentre() + juce::Point<float> (100.0f, 0.0f));
            const float expected = 100.0f * view.getMaxScroll() / (track.getWidth() - thumb.getWidth());
            expectWithinAbsoluteError (view.getScroll(), expected, 2.0f, "thumb drag scrolls proportionally");
            expect (h.proc.getRouting() == routingBefore, "no reorder from the thumb");

            // Empty strip space (behind the sends) drags the row like a sheet
            view.scrollTo (view.getMaxScroll(), false);
            const auto lane = view.getLaneBounds();
            const auto addCard = view.getAddCardBounds();
            expect (view.getRowViewport().expanded (0.5f).contains (addCard), "add card fully reachable at the end");
            for (auto [id, k] : { std::pair { ModuleId::reverb, 0 }, { ModuleId::reverb, 1 } })
                expect (view.isTileFullyVisible (id, k), "send tile fully reachable at the end");
            const float endScroll = view.getScroll();
            const juce::Point<float> empty (lane.getX() + 5.0f, lane.getY() + 6.0f);   // the divider in front of the sends
            expect (view.getTileAt (empty) < 0, "test setup: empty strip space");
            drag (view, empty, empty + juce::Point<float> (150.0f, 0.0f));
            expectWithinAbsoluteError (view.getScroll(), endScroll - 150.0f, 1.0f, "empty space drags the strip");
            expect (h.proc.getRouting() == routingBefore, "panning never reorders");

            // Shift + drag on a tile drags the strip too
            view.scrollTo (300.0f, false);
            const auto chop = view.getTileBounds (ModuleId::chop).getCentre();
            drag (view, chop, chop + juce::Point<float> (-60.0f, 0.0f), juce::ModifierKeys::shiftModifier);
            expectWithinAbsoluteError (view.getScroll(), 360.0f, 1.0f, "shift-drag pans");
            expect (h.proc.getRouting() == routingBefore, "shift-drag never reorders");

            // Middle button drag anywhere, also on a tile
            view.scrollTo (300.0f, false);
            const auto tile = view.getTileBounds (ModuleId::smooth);
            const auto from = tile.getCentre();
            drag (view, from, from + juce::Point<float> (-80.0f, 0.0f), juce::ModifierKeys::middleButtonModifier);
            expectWithinAbsoluteError (view.getScroll(), 380.0f, 1.0f, "middle-drag pans");
            expect (h.proc.getRouting() == routingBefore, "middle-drag never reorders");

            // Edge arrows page the row
            view.scrollTo (0.0f, false);
            expect (view.getScrollArrowBounds (false).isEmpty(), "no left arrow at the start");
            const auto right = view.getScrollArrowBounds (true);
            expect (! right.isEmpty(), "right arrow while more tiles are on the right");
            click (view, right.getCentre());
            expect (view.getScroll() > view.getRowViewport().getWidth() * 0.5f, "a page to the right");
            expect (! view.getScrollArrowBounds (false).isEmpty(), "left arrow now");
            click (view, view.getScrollArrowBounds (false).getCentre());
            expectWithinAbsoluteError (view.getScroll(), 0.0f, 0.5f, "and back");

            // A tile drag still reorders
            view.scrollTo (0.0f, false);
            const auto eq2 = view.getTileBounds (ModuleId::eq, 1), comp1 = view.getTileBounds (ModuleId::comp);
            drag (view, eq2.getCentre(), { comp1.getRight() + 10.0f, comp1.getCentreY() });
            expect (h.proc.getRouting() != routingBefore, "tile drag reorders");
        }

        beginTest ("Chain scroll: a clicked or selected tile is scrolled fully into view, at every window size");
        {
            Harness h;
            h.proc.setRouting (dsp::Routing::fromString (fullChain));
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 886, ui::ChainView::preferredHeight);
            view.refresh();
            view.scrollTo (0.0f, false);
            // The tile cut by the right edge
            const auto row = view.getRowViewport();
            for (const auto& s : h.proc.getRouting())
            {
                const auto b = view.getTileBounds (s.id, s.instance);
                if (s.insert && b.getX() < row.getRight() && b.getRight() > row.getRight())
                {
                    click (view, { b.getX() + 6.0f, b.getY() + 12.0f });
                    view.scrollTo (view.getScroll(), false);   // finish the glide
                    expect (view.isTileFullyVisible (s.id, s.instance), "a clicked half-hidden tile slides into view");
                    break;
                }
            }

            for (int width : { 889, 1000, 2000 })
            {
                auto e = proEditor (h);
                e->setSize (width, width * 600 / 1000);
                auto& ed = asVelvox (*e);
                auto& chain = ed.getChainView();
                for (const auto& s : h.proc.getRouting())
                {
                    ed.selectModule (dsp::selectionCode (s.id, s.instance));
                    chain.scrollTo (chain.getScroll(), false);
                    expect (chain.isTileFullyVisible (s.id, s.instance),
                            "width " + juce::String (width) + ": " + ui::ChainView::cardName (h.proc.getRouting(), s.id, s.instance));
                }
                expect (chain.getRowViewport().getRight() <= chain.getOutputBounds().getX(), "row never runs under OUTPUT");
            }
        }
    }

    void runSoloUiTests()
    {
        const int comp = dsp::selectionCode (ModuleId::comp, 0);

        beginTest ("Chain tile: S solos with one click, again releases; it never selects, drags or switches power");
        {
            Harness h;
            setupBusyChain (h, false);
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 886, ui::ChainView::preferredHeight);
            view.setSelected ((int) ModuleId::eq);
            const auto s = view.getSoloBounds (ModuleId::comp);
            expect (! s.isEmpty() && s.getWidth() >= 14.0f, "an S on the tile");
            expect (! s.intersects (view.getPowerBounds (ModuleId::comp).expanded (2.0f)), "apart from the power switch");
            expect (view.getTileBounds (ModuleId::comp).contains (s), "inside its tile");
            const auto routingBefore = h.proc.getRouting();
            click (view, s.getCentre());
            expect (h.proc.isSoloed (comp), "soloed");
            expectEquals (view.getSelected(), (int) ModuleId::eq, "selection unchanged");
            expect (h.get (pid::compOn) > 0.5f && h.get (pid::eqOn) > 0.5f, "no power switch touched");
            expect (h.proc.getRouting() == routingBefore, "no reorder");
            click (view, s.getCentre());
            expect (! h.proc.isSoloActive(), "click again releases");
        }

        beginTest ("Chain tile: Alt+click solos, the tile menu has Solo, the hover card names it; Check has no solo");
        {
            Harness h;
            setupBusyChain (h, false);
            auto r = h.proc.getRouting();
            r.insertDefault (ModuleId::check, 0);
            h.proc.setRouting (r);
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 886, ui::ChainView::preferredHeight);
            view.refresh();

            click (view, view.getIconBounds (ModuleId::sat).getCentre(), juce::ModifierKeys::altModifier);
            expect (h.proc.isSoloed ((int) ModuleId::sat), "Alt+click on the tile");
            click (view, view.getIconBounds (ModuleId::comp).getCentre(), juce::ModifierKeys::altModifier);
            expect (h.proc.isSoloed (comp), "Alt+click on another tile moves the solo");
            click (view, view.getIconBounds (ModuleId::comp).getCentre(), juce::ModifierKeys::altModifier);
            expect (! h.proc.isSoloActive(), "Alt+click again releases");

            expect (view.performTileAction (comp, ui::ChainView::TileAction::solo) && h.proc.isSoloed (comp), "menu action");
            bool hasSoloItem = false, ticked = false;
            const auto menu = view.buildTileMenu (comp);
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                if (it.getItem().text.startsWith ("Solo"))
                {
                    hasSoloItem = true;
                    ticked = it.getItem().isTicked;
                }
            expect (hasSoloItem && ticked, "Solo in the tile menu, ticked while soloed");
            view.performTileAction (comp, ui::ChainView::TileAction::solo);
            expect (! h.proc.isSoloActive());

            view.mouseMove (mouse (view, view.getIconBounds (ModuleId::comp).getCentre(), {}, false));
            expect (view.getTooltip().contains ("Solo"), "hover card: " + view.getTooltip());
            view.mouseMove (mouse (view, view.getSoloBounds (ModuleId::comp).getCentre(), {}, false));
            expect (view.getTooltip().startsWith ("Solo"), "hover card on the S: " + view.getTooltip());

            expect (view.getSoloBounds (ModuleId::check).isEmpty(), "Check: no S");
            click (view, view.getIconBounds (ModuleId::check).getCentre(), juce::ModifierKeys::altModifier);
            expect (! h.proc.isSoloActive(), "Check cannot be soloed");
        }

        beginTest ("Chain: solo shows clearly - soloed tile in solo yellow, the others dimmed, OUTPUT says SOLO");
        {
            Harness h;
            setupBusyChain (h, false);
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 886, ui::ChainView::preferredHeight);
            view.refresh();
            const auto before = render (view);
            h.proc.setSolo (comp);
            view.refresh();
            const auto after = render (view);
            const auto compTile = view.getTileBounds (ModuleId::comp).toNearestInt();
            const auto eqTile = view.getTileBounds (ModuleId::eq).toNearestInt();
            expect (soloPixels (after, compTile) > 40, "solo yellow on the soloed tile");
            expect (soloPixels (before, compTile) == 0, "no yellow without solo");
            expect (brightness (after, eqTile) < brightness (before, eqTile) * 0.8f, "the other tiles are dimmed");
            expect (soloPixels (after, view.getOutputBounds().toNearestInt()) > 10, "OUTPUT shows the solo");
        }

        beginTest ("Panel header: solo button follows and sets the processor state; editor closing ends the solo");
        {
            Harness h;
            setupBusyChain (h, false);
            {
                auto e = proEditor (h);
                auto& ed = asVelvox (*e);
                ed.selectModule (comp);
                auto* panel = ed.getDetail();
                expect (panel != nullptr && panel->getSoloButton() != nullptr, "S in the compressor's header");
                if (panel != nullptr && panel->getSoloButton() != nullptr)
                {
                    auto* b = panel->getSoloButton();
                    expect (b->isVisible() && panel->getHeaderArea().contains (b->getBounds()), "in the header");
                    expect (! b->getBounds().intersects (panel->getPresetsButton().getBounds()), "apart from Presets");
                    b->onClick();
                    expect (h.proc.isSoloed (comp), "header solo");
                    h.proc.setSolo (-1);
                    panel->refresh();
                    expect (! b->getToggleState(), "follows the processor");
                    h.proc.setSolo (comp);
                    panel->refresh();
                    expect (b->getToggleState(), "lit while soloed (also from the tile)");
                }
                ed.selectModule (ui::ChainView::outputTile);
                expect (ed.getDetail() != nullptr && ed.getDetail()->getSoloButton() == nullptr, "Output: no solo");
            }
            expect (! h.proc.isSoloActive(), "closing the editor ends the solo");
        }
    }

    // A chain with tone, dynamics, colour, latency and a send: EQ, Comp, Sat, Smash, Pitch (off), Reverb send
    static void setupBusyChain (Harness& h, bool pitchOn)
    {
        h.proc.setRouting (dsp::Routing::fromString ("v2,eq,comp,sat,smash,pitch,reverb:send"));
        h.set (pid::eqOn, 1.0f);
        h.set (pid::compOn, 1.0f);
        h.set (pid::satOn, 1.0f);
        h.set (pid::smashOn, 1.0f);
        h.set (pid::pitchOn, pitchOn ? 1.0f : 0.0f);
        h.set (pid::reverbOn, 1.0f);
        h.set (pid::deessOn, 0.0f);
        h.set (pid::delayOn, 0.0f);
    }

    // Largest second difference (a click shows as a spike of curvature)
    static float maxCurvature (const juce::AudioBuffer<float>& b, int from)
    {
        float m = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = std::max (2, from); i < b.getNumSamples(); ++i)
                m = std::max (m, std::abs (b.getSample (ch, i) - 2.0f * b.getSample (ch, i - 1) + b.getSample (ch, i - 2)));
        return m;
    }

    void runProcessorTests()
    {
        const int comp = dsp::selectionCode (ModuleId::comp, 0);
        const int sat = dsp::selectionCode (ModuleId::sat, 0);

        beginTest ("Processor solo: one at a time, click again releases, Check and absent instances cannot be soloed");
        {
            Harness h;
            setupBusyChain (h, false);
            expectEquals (h.proc.getSolo(), -1, "no solo by default");
            expect (h.proc.toggleSolo (comp), "solo on");
            expect (h.proc.isSoloed (comp));
            expect (h.proc.toggleSolo (sat), "another solo replaces it");
            expect (h.proc.isSoloed (sat) && ! h.proc.isSoloed (comp), "only one solo at a time");
            expect (! h.proc.toggleSolo (sat), "click again releases");
            expectEquals (h.proc.getSolo(), -1);

            h.proc.setSolo (dsp::selectionCode (ModuleId::comp, 1));
            expectEquals (h.proc.getSolo(), -1, "an instance not in the chain cannot be soloed");
            auto r = h.proc.getRouting();
            r.insertDefault (ModuleId::check, 0);
            h.proc.setRouting (r);
            h.proc.setSolo ((int) ModuleId::check);
            expectEquals (h.proc.getSolo(), -1, "Check cannot be soloed");
        }

        beginTest ("Processor solo: only the soloed module is heard (= the chain with every other module off)");
        {
            Harness a, b;
            setupBusyChain (a, false);
            setupBusyChain (b, false);
            for (auto* id : { pid::eqOn, pid::satOn, pid::smashOn, pid::reverbOn })
                b.set (id, 0.0f);
            a.proc.setSolo (comp);
            const auto in = a.noise (0.25f, 2.0, 7);
            const auto outA = a.run (in);
            const auto outB = b.run (in);
            const int from = (int) (0.8 * a.fs);
            float diff = 0.0f;
            for (int ch = 0; ch < outA.getNumChannels(); ++ch)
                for (int i = from; i < outA.getNumSamples(); ++i)
                    diff = std::max (diff, std::abs (outA.getSample (ch, i) - outB.getSample (ch, i)));
            expect (diff < 1.0e-3f, "solo comp == comp alone, max diff " + juce::String (diff, 6));
        }

        beginTest ("Processor solo: latency unchanged (saturation in the chain, pitch shifter on)");
        {
            Harness h;
            setupBusyChain (h, true);
            h.proc.updateHostLatency();
            h.run (h.sine (220.0, 0.2f, 0.3));
            const int hostBefore = h.proc.getLatencySamples(), chainBefore = h.proc.getChain().getLatencySamples();
            expect (hostBefore > 0 && chainBefore == hostBefore, "latent chain");
            const int switchesBefore = h.proc.getChain().meters.structureSwitches.load();
            for (int code : { comp, sat, (int) ModuleId::smash, (int) ModuleId::pitch, (int) ModuleId::reverb, -1 })
            {
                h.proc.setSolo (code);
                h.proc.updateHostLatency();
                h.run (h.sine (220.0, 0.2f, 0.3));
                expectEquals (h.proc.getLatencySamples(), hostBefore, "host latency, solo " + juce::String (code));
                expectEquals (h.proc.getChain().getLatencySamples(), chainBefore, "chain latency, solo " + juce::String (code));
            }
            expectEquals (h.proc.getChain().meters.structureSwitches.load(), switchesBefore, "no routing fade (dip) for a solo");
        }

        beginTest ("Processor solo: switching is click-free");
        {
            // Reference: the largest curvature of every static solo state
            const int codes[] { -1, (int) ModuleId::eq, comp, sat, (int) ModuleId::smash, (int) ModuleId::reverb, (int) ModuleId::pitch };
            float reference = 0.0f;
            for (int code : codes)
            {
                Harness h;
                setupBusyChain (h, true);
                h.proc.setSolo (code);
                const auto out = h.run (h.sine (180.0, 0.3f, 1.5));
                reference = std::max (reference, maxCurvature (out, (int) (0.7 * h.fs)));
            }

            // Solo switched every 0.25 s (whole sine periods, so the input itself is continuous)
            const auto switching = [&] (bool pitchOn, std::function<void (Harness&, int)> apply)
            {
                Harness h;
                setupBusyChain (h, pitchOn);
                h.run (h.sine (180.0, 0.3f, 0.7));
                float worst = 0.0f;
                for (int round = 0; round < 3; ++round)
                    for (int code : codes)
                    {
                        apply (h, code);
                        worst = std::max (worst, maxCurvature (h.run (h.sine (180.0, 0.3f, 0.25)), 0));
                    }
                return worst;
            };
            const float solo = switching (true, [] (Harness& h, int code) { h.proc.setSolo (code); });
            const float soloNoPitch = switching (false, [] (Harness& h, int code) { h.proc.setSolo (code); });
            // The same changes made by hand with the power switches (pitch off: its switch changes the latency)
            const auto byHand = [] (Harness& h, int code)
            {
                for (auto [id, param] : { std::pair { ModuleId::eq, pid::eqOn }, { ModuleId::comp, pid::compOn }, { ModuleId::sat, pid::satOn },
                                          { ModuleId::smash, pid::smashOn }, { ModuleId::reverb, pid::reverbOn } })
                    h.set (param, code < 0 || code == (int) id ? 1.0f : 0.0f);
                // the pitch shifter's mix knob turned like the solo does (its switch would change the latency)
                h.set (pid::pitchMix, code < 0 || code == (int) ModuleId::pitch ? 100.0f : 0.0f);
            };
            const float handNoPitch = switching (false, byHand), handPitch = switching (true, byHand);
            logMessage ("    curvature: solo " + juce::String (solo, 5) + " / pitch off " + juce::String (soloNoPitch, 5) + "; by hand "
                        + juce::String (handPitch, 5) + " / " + juce::String (handNoPitch, 5) + "; static " + juce::String (reference, 5));
            expect (soloNoPitch <= handNoPitch * 1.1f + 1.0e-5f, "as smooth as switching the modules by hand");
            expect (solo <= handPitch * 1.1f + 1.0e-5f, "as smooth as switching by hand, pitch shifter on");
            expect (solo < 0.01f, "far from a click (a 1 % step)");
        }

        beginTest ("Processor solo: a monitoring state - never saved, never an undo step, never marks parameters");
        {
            Harness h;
            setupBusyChain (h, false);
            const auto stateBefore = h.proc.getState().copyState().toXmlString();
            const int stepsBefore = h.proc.getUndo().getNumSteps();
            h.proc.setSolo (comp);
            h.run (h.sine (220.0, 0.2f, 0.1));
            expectEquals (h.proc.getState().copyState().toXmlString(), stateBefore, "state untouched");
            expectEquals (h.proc.getUndo().getNumSteps(), stepsBefore, "no undo step");
            expect (h.get (pid::eqOn) > 0.5f, "the other modules' switches stay on");

            juce::MemoryBlock saved;
            h.proc.getStateInformation (saved);
            Harness other;
            other.proc.setStateInformation (saved.getData(), (int) saved.getSize());
            expectEquals (other.proc.getSolo(), -1, "a loaded project never starts soloed");
            h.proc.setStateInformation (saved.getData(), (int) saved.getSize());
            expectEquals (h.proc.getSolo(), -1, "loading a project ends the solo");
        }

        beginTest ("Processor solo: the soloed instance leaving the chain ends the solo");
        {
            Harness h;
            setupBusyChain (h, false);
            h.proc.setSolo (comp);
            auto r = h.proc.getRouting();
            r.remove (ModuleId::comp, 0);
            h.proc.setRouting (r);
            expectEquals (h.proc.getSolo(), -1);
        }
    }
};

static ChainSoloTests chainSoloTests;
