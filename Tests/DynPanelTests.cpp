// Dynamics panels (spec 5.2, 5.3, 5.5, 5.7): Compressor "Comp 1" (scrolling level / GR display + draggable transfer
// curve), De-Esser "De-Esser 1" (spectrum with draggable S band, live reduction), Vocal Smash "Smash 1" (big SMASH
// control + GR column), Level Ride "Ride 2" (word bars before / after, draggable target, big Ausgleich); browser-added
// de-essers start on Relative detection.

#include "TestHelpers.h"
#include "PluginEditor.h"
#include "UI/DynPanels.h"
#include "UI/ChainView.h"

using namespace velvox;
using namespace velvox::test;
using dsp::ModuleId;

namespace
{
    juce::Rectangle<int> panelSize() { return VelvoxAudioProcessorEditor::panelArea().withZeroOrigin(); }

    juce::String idFor (const char* base, int instance) { return dsp::instanceParamId (base, instance); }
    float real (Harness& h, const juce::String& id) { return h.proc.getState().getRawParameterValue (id)->load(); }
    void setReal (Harness& h, const juce::String& id, float v)
    {
        auto* p = h.proc.getState().getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
    int indexOf (Harness& h, const juce::String& id) { return h.proc.getState().getParameter (id)->getParameterIndex(); }

    struct HostLog final : juce::AudioProcessorListener
    {
        explicit HostLog (juce::AudioProcessor& p) : proc (p) { proc.addListener (this); }
        ~HostLog() override { proc.removeListener (this); }
        void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override { events.add ("v" + juce::String (index)); }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { events.add ("b" + juce::String (index)); }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override { events.add ("e" + juce::String (index)); }
        juce::AudioProcessor& proc;
        juce::StringArray events;
    };

    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos, bool dragged,
                              juce::ModifierKeys mods = {}, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downPos, now, clicks, dragged);
    }

    juce::MouseWheelDetails wheel (float dy)
    {
        juce::MouseWheelDetails w;
        w.deltaX = 0.0f;
        w.deltaY = dy;
        w.isReversed = false;
        w.isSmooth = false;
        w.isInertial = false;
        return w;
    }

    void drag (juce::Component& c, juce::Point<float> start, juce::Point<float> by, juce::ModifierKeys mods = {})
    {
        c.mouseDown (mouseAt (c, start, start, false, mods));
        for (int i = 1; i <= 10; ++i)
            c.mouseDrag (mouseAt (c, start + by * ((float) i / 10.0f), start, true, mods));
        c.mouseUp (mouseAt (c, start + by, start, true, mods));
    }

    void ensureInChain (Harness& h, ModuleId id, int instance)
    {
        auto r = h.proc.getRouting();
        for (int k = 0; k <= instance; ++k)
            if (! r.contains (id, k))
                r.insertDefault (id, k);
        h.proc.setRouting (r);
    }

    // Feeds the processor `seconds` of a sine (both channels)
    void play (Harness& h, float hz, float amp, double seconds)
    {
        h.run (h.sine (hz, amp, seconds));
    }

    // Word-like bursts: `words` tones of 300 ms with 200 ms gaps, alternating loud / quiet
    juce::AudioBuffer<float> words (double fs, int count, float loudDb, float quietDb)
    {
        const int word = (int) (0.3 * fs), gap = (int) (0.2 * fs);
        juce::AudioBuffer<float> b (2, count * (word + gap) + gap);
        b.clear();
        for (int w = 0; w < count; ++w)
        {
            const float amp = juce::Decibels::decibelsToGain ((w % 2 == 0) ? loudDb : quietDb) * 1.4142f;
            const int start = gap + w * (word + gap);
            for (int i = 0; i < word; ++i)
            {
                const float env = std::min (1.0f, std::min ((float) i, (float) (word - i)) / (0.01f * (float) fs));
                const float v = amp * env * std::sin (juce::MathConstants<float>::twoPi * 400.0f * (float) i / (float) fs);
                b.setSample (0, start + i, v);
                b.setSample (1, start + i, v);
            }
        }
        return b;
    }

    struct Live
    {
        explicit Live (Harness& hh) : h (hh) {}
        ui::LiveData& next()
        {
            data->settings = h.proc.readEffectiveSettings();
            data->time += 0.05;
            data->sampleRate = h.fs;
            return *data;
        }
        Harness& h;
        std::unique_ptr<ui::LiveData> data = std::make_unique<ui::LiveData>();
    };

    template <typename T>
    T* findChild (juce::Component& parent)
    {
        for (auto* c : parent.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (c))
                return t;
            if (auto* t = findChild<T> (*c))
                return t;
        }
        return nullptr;
    }

    juce::StringArray labelsOf (const std::vector<juce::Component*>& controls)
    {
        juce::StringArray out;
        for (auto* c : controls)
        {
            if (auto* k = dynamic_cast<ui::Knob*> (c)) out.add (k->getLabel());
            else if (auto* t = dynamic_cast<ui::Toggle*> (c)) out.add (t->getButtonText());
            else if (auto* s = dynamic_cast<ui::Segments*> (c)) out.add (s->getParamId());
        }
        return out;
    }

    juce::StringArray paramsOf (const std::vector<juce::Component*>& controls)
    {
        juce::StringArray out;
        for (auto* c : controls)
        {
            if (auto* k = dynamic_cast<ui::Knob*> (c)) out.add (k->getParamId());
            else if (auto* t = dynamic_cast<ui::Toggle*> (c)) out.add (t->getParamId());
            else if (auto* s = dynamic_cast<ui::Segments*> (c)) out.add (s->getParamId());
        }
        return out;
    }

    juce::StringArray menuTexts (const juce::PopupMenu& m)
    {
        juce::StringArray texts;
        for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
            texts.add (it.getItem().text);
        return texts;
    }
}

class DynPanelTests final : public juce::UnitTest
{
public:
    DynPanelTests() : juce::UnitTest ("Headliner dynamics panels", "Headliner") {}

    void runTest() override
    {
        using namespace ui;
        const auto dash = juce::String::fromUTF8 ("…");

        //======================================================================
        // Panels
        beginTest ("Compressor panel: Threshold Ratio Attack Release; Mehr Knee Makeup Auto Makeup Mix Detector Auto Release SC Filter");
        {
            Harness h;
            auto panel = createDetailPanel (h.proc, (int) ModuleId::comp);
            panel->setBounds (panelSize());
            expectEquals (labelsOf (panel->getMainControls()).joinIntoString (","), juce::String ("THRESHOLD,RATIO,ATTACK,RELEASE"));
            const auto more = paramsOf (panel->getMoreControls());
            for (auto* id : { pid::compKnee, pid::compMakeup, pid::compAutoMakeup, pid::compMix, pid::compDetector, pid::compAutoRelease, pid::compScHpf })
                expect (more.contains (id), juce::String ("Mehr has ") + id);
            expect (findChild<CompGraph> (*panel) != nullptr, "the visual is the compressor graph");
        }

        beginTest ("De-Esser panel: Threshold Range Mode Listen; Mehr Detection, Frequency");
        {
            Harness h;
            auto panel = createDetailPanel (h.proc, (int) ModuleId::deess);
            panel->setBounds (panelSize());
            expectEquals (paramsOf (panel->getMainControls()).joinIntoString (","),
                          juce::String ("deessThreshold,deessRange,deessMode,deessListen"));
            const auto more = paramsOf (panel->getMoreControls());
            expect (more.contains (pid::deessDetection) && more.contains (pid::deessFreq), more.joinIntoString (","));
            auto* g = findChild<DeEssGraph> (*panel);
            expect (g != nullptr, "the visual is the de-esser graph");
            if (g != nullptr)
                expect (g->getEditedParameters().contains (pid::deessFreq) && g->getEditedParameters().contains (pid::deessRange));
        }

        beginTest ("Vocal Smash panel: big SMASH and the GR column in the visual; Blend Tone Output; Mehr Attack Release");
        {
            Harness h;
            ensureInChain (h, ModuleId::smash, 0);
            auto panel = createDetailPanel (h.proc, (int) ModuleId::smash);
            panel->setBounds (panelSize());
            expectEquals (labelsOf (panel->getMainControls()).joinIntoString (","), juce::String ("BLEND,TONE,OUTPUT"));
            expectEquals (paramsOf (panel->getMoreControls()).joinIntoString (","), juce::String ("smashAttack,smashRelease"));
            Knob* smash = nullptr;
            for (auto* c : panel->getChildren())
                if (auto* k = dynamic_cast<Knob*> (c); k != nullptr && k->getParamId() == pid::smashAmount)
                    smash = k;
            expect (smash != nullptr && smash->isVisible(), "SMASH knob on the panel");
            if (smash != nullptr)
            {
                expect (panel->getVisualArea().contains (smash->getBounds()), "inside the visual " + smash->getBounds().toString());
                expect (smash->getDialSize() >= 120, "big dial " + juce::String (smash->getDialSize()));
            }
            auto* stage = findChild<SmashStage> (*panel);
            expect (stage != nullptr, "the GR stage");
            if (stage != nullptr && smash != nullptr)
            {
                const auto column = stage->getColumnArea().translated ((float) stage->getX(), (float) stage->getY());
                expect (column.getWidth() >= 20.0f && column.getX() >= (float) smash->getRight() - 1.0f, "GR column right of SMASH " + column.toString());
                expect (! column.intersects (smash->getBounds().toFloat().reduced (2.0f)), "column and knob do not overlap");
            }
            panel->setMoreOpen (true);
            if (smash != nullptr)
                expect (panel->getVisualArea().contains (smash->getBounds()), "Mehr open: still inside the visual");
        }

        beginTest ("Level Ride panel: big Ausgleich in the visual; Target Speed Ignore Below; Mehr Max Boost Max Cut Phrase Transients");
        {
            Harness h;
            ensureInChain (h, ModuleId::ride, 0);
            auto panel = createDetailPanel (h.proc, (int) ModuleId::ride);
            panel->setBounds (panelSize());
            expectEquals (paramsOf (panel->getMainControls()).joinIntoString (","), juce::String ("rideTarget,rideResponse,rideSilence"));
            expectEquals (paramsOf (panel->getMoreControls()).joinIntoString (","),
                          juce::String ("rideMaxBoost,rideMaxCut,ridePhrase,rideTransient"));
            Knob* amount = nullptr;
            for (auto* c : panel->getChildren())
                if (auto* k = dynamic_cast<Knob*> (c); k != nullptr && k->getParamId() == pid::rideAmount)
                    amount = k;
            expect (amount != nullptr && amount->getLabel() == "AUSGLEICH", "Ausgleich on the panel");
            if (amount != nullptr)
            {
                expect (panel->getVisualArea().contains (amount->getBounds()), "inside the visual");
                expect (amount->getDialSize() >= 100, "big dial " + juce::String (amount->getDialSize()));
            }
            auto* bars = findChild<RideBars> (*panel);
            expect (bars != nullptr, "the word bars");
            if (bars != nullptr && amount != nullptr)
            {
                const auto plot = bars->plotArea().translated ((float) bars->getX(), (float) bars->getY());
                expect (! plot.intersects (amount->getBounds().toFloat()), "bars beside the Ausgleich control");
                expect (bars->getEditedParameters().contains (pid::rideTarget));
            }
        }

        //======================================================================
        // Compressor graph
        beginTest ("Compressor graph: feed frames and the GR read-out follow the module");
        {
            Harness h;
            h.neutral();
            h.set (pid::compOn, 1.0f);
            h.set (pid::compThreshold, -30.0f);
            h.set (pid::compRatio, 4.0f);
            CompGraph g (h.proc, 0);
            g.setBounds (0, 0, 900, 360);
            Live live (h);
            g.update (live.next());   // drains what the setup left
            play (h, 1000.0f, 0.5f, 1.0);
            g.update (live.next());
            auto& module = h.proc.getChain().getCompressor (0);
            expect (g.getNumFrames() >= 190, "about 200 frames per second: " + juce::String (g.getNumFrames()));
            const auto last = g.getFrame (0);
            expectWithinAbsoluteError (last.inputDb, -6.02f, 0.3f, "input peak");
            expectWithinAbsoluteError (last.gainReductionDb, module.currentGainReductionDb.load(), 0.5f, "frame GR");
            expectWithinAbsoluteError (last.outputDb, last.inputDb - last.gainReductionDb, 0.3f, "output = input - GR (no makeup)");
            expect (module.currentGainReductionDb.load() > 15.0f, "the module compresses: " + juce::String (module.currentGainReductionDb.load()));
            expectWithinAbsoluteError (g.getShownGainReductionDb(), module.currentGainReductionDb.load(), 0.1f, "read-out");
            dsp::DynamicsFrame rest;
            expectEquals (module.history.pull (&rest, 1), 0, "the graph drained the feed");

            // Off: no new frames, no reduction shown
            const int before = g.getNumFrames();
            h.set (pid::compOn, 0.0f);
            play (h, 1000.0f, 0.5f, 0.5);
            g.update (live.next());
            g.update (live.next());
            expect (g.getNumFrames() - before < 40, "frames stop while off (fade-out only)");
            expectWithinAbsoluteError (g.getShownGainReductionDb(), 0.0f, 0.01f, "no reduction shown while off");
        }

        beginTest ("Compressor / Smash: a full feed queue (panel closed for a while) is stale - drained and discarded on open");
        {
            Harness h;
            h.neutral();
            ensureInChain (h, ModuleId::smash, 0);
            h.set (pid::compOn, 1.0f);
            h.set (pid::smashOn, 1.0f);
            play (h, 500.0f, 0.3f, 12.0);
            CompGraph g (h.proc, 0);
            g.setBounds (0, 0, 900, 360);
            SmashStage st (h.proc, 0);
            st.setBounds (0, 0, 900, 360);
            Live live (h);
            g.update (live.next());
            st.update (live.next());
            expectEquals (g.getNumFrames(), 0, "the 10 s old backlog is not shown as live");
            expectEquals (st.getNumFrames(), 0, "smash: neither");
            dsp::DynamicsFrame f;
            expectEquals (h.proc.getChain().getCompressor (0).history.pull (&f, 1), 0, "queue empty");
            expectEquals (h.proc.getChain().getSmash (0).history.pull (&f, 1), 0, "smash queue empty");
            play (h, 500.0f, 0.3f, 0.5);
            g.update (live.next());
            st.update (live.next());
            expect (std::abs (g.getNumFrames() - 100) <= 2 && std::abs (st.getNumFrames() - 100) <= 2,
                    "then live: " + juce::String (g.getNumFrames()) + " / " + juce::String (st.getNumFrames()));
        }

        beginTest ("Compressor graph: knee point drag = threshold (one step, host gesture, fine, clamped), reset, menu, card");
        {
            Harness h;
            CompGraph g (h.proc, 0);
            g.setBounds (0, 0, 900, 360);
            Live live (h);
            g.update (live.next());
            auto& undo = h.proc.getUndo();
            // Every action a second apart (quick repeats on one parameter coalesce, see UndoTests)
            undo.setClockForTests ([t = juce::uint32 (0)]() mutable { return t += 1000; });
            undo.clear();
            HostLog log (h.proc);
            const float dbPerPx = 60.0f / g.curveArea().getWidth();
            const auto knee = g.kneePoint();
            expectEquals (g.handleAtPoint (knee), (int) CompGraph::kneeHandle, "knee point hit");
            drag (g, knee, { 40.0f, 0.0f });
            expectWithinAbsoluteError (real (h, pid::compThreshold), -18.0f + 40.0f * dbPerPx, 0.15f, "right = higher");
            expectEquals (undo.getNumSteps(), 1, "one step");
            const int it = indexOf (h, pid::compThreshold);
            expect (log.events.indexOf ("b" + juce::String (it)) == 0 && log.events[log.events.size() - 1] == "e" + juce::String (it), log.events.joinIntoString (" "));

            const float t1 = real (h, pid::compThreshold);
            g.update (live.next());
            drag (g, g.kneePoint(), { 0.0f, 40.0f });
            expectWithinAbsoluteError (real (h, pid::compThreshold), t1 - 40.0f * dbPerPx, 0.15f, "down = lower");
            const float t2 = real (h, pid::compThreshold);
            g.update (live.next());
            drag (g, g.kneePoint(), { 40.0f, 0.0f }, juce::ModifierKeys::shiftModifier);
            expectWithinAbsoluteError (real (h, pid::compThreshold), t2 + 4.0f * dbPerPx, 0.05f, "Shift = 1/10");
            g.update (live.next());
            drag (g, g.kneePoint(), { -3000.0f, 0.0f });
            expectWithinAbsoluteError (real (h, pid::compThreshold), -60.0f, 0.01f, "clamped at -60");
            g.update (live.next());
            expect (g.getLocalBounds().toFloat().contains (g.kneePoint()), "knee point stays on screen");

            const int steps = undo.getNumSteps();
            const auto k = g.kneePoint();
            g.mouseDown (mouseAt (g, k, k, false));
            g.mouseDoubleClick (mouseAt (g, k, k, false, {}, 2));
            g.mouseUp (mouseAt (g, k, k, false, {}, 2));
            expectWithinAbsoluteError (real (h, pid::compThreshold), -18.0f, 0.01f, "double-click = default");
            expectEquals (undo.getNumSteps(), steps + 1, "reset = one step");
            setReal (h, pid::compThreshold, -40.0f);
            g.update (live.next());
            g.mouseDown (mouseAt (g, g.kneePoint(), g.kneePoint(), false, juce::ModifierKeys::altModifier));
            g.mouseUp (mouseAt (g, g.kneePoint(), g.kneePoint(), false, juce::ModifierKeys::altModifier));
            expectWithinAbsoluteError (real (h, pid::compThreshold), -18.0f, 0.01f, "Alt+click = default");

            const auto texts = menuTexts (g.buildHandleMenu (CompGraph::kneeHandle));
            expect (texts.contains ("Standardwert") && texts.contains ("Wert eingeben " + dash), texts.joinIntoString (", "));
            expect (texts.contains (juce::String::fromUTF8 ("Compressor zurücksetzen")) || texts.size() >= 4, "value items (+ panel items inside a panel)");

            g.update (live.next());
            const auto k2 = g.kneePoint();
            g.mouseDown (mouseAt (g, k2, k2, false));
            g.mouseDrag (mouseAt (g, k2.translated (5.0f, 0.0f), k2, true));
            expect (g.isCardShown() && g.getCardTitle() == "THRESHOLD", "card while dragging: " + g.getCardTitle());
            expect (g.getCardValues().contains ("dB"), g.getCardValues());
            g.mouseUp (mouseAt (g, k2.translated (5.0f, 0.0f), k2, true));
            const auto ids = g.getEditedParameters();
            expect (ids.contains (pid::compThreshold) && ids.contains (pid::compRatio) && ids.contains (pid::compKnee), ids.joinIntoString (","));
        }

        beginTest ("Compressor graph: upper segment drag = ratio (down = more), wheel over the curve = knee");
        {
            Harness h;
            CompGraph g (h.proc, 0);
            g.setBounds (0, 0, 900, 360);
            Live live (h);
            g.update (live.next());
            auto& undo = h.proc.getUndo();
            undo.clear();
            const auto seg = g.segmentPoint();
            expectEquals (g.handleAtPoint (seg), (int) CompGraph::ratioHandle, "segment hit");
            drag (g, seg, { 0.0f, 44.0f });
            expectWithinAbsoluteError (real (h, pid::compRatio), 3.0f * std::pow (20.0f, 0.2f), 0.05f, "44 px down: ratio x 20^(44/220)");
            expectEquals (undo.getNumSteps(), 1);
            g.update (live.next());
            drag (g, g.segmentPoint(), { 0.0f, -2000.0f });
            expectWithinAbsoluteError (real (h, pid::compRatio), 1.0f, 0.01f, "clamped at 1:1");
            g.update (live.next());
            drag (g, g.segmentPoint(), { 0.0f, 4000.0f });
            expectWithinAbsoluteError (real (h, pid::compRatio), 20.0f, 0.01f, "clamped at 20:1");

            g.update (live.next());
            const auto inCurve = g.curveArea().getRelativePoint (0.15f, 0.2f);
            expectEquals (g.handleAtPoint (inCurve), -1, "empty curve space");
            const int steps = undo.getNumSteps();
            g.mouseWheelMove (mouseAt (g, inCurve, inCurve, false), wheel (0.11f));
            expectWithinAbsoluteError (real (h, pid::compKnee), 7.0f, 0.01f, "one notch = +1 dB knee");
            expectEquals (undo.getNumSteps(), steps + 1, "one step per wheel event");
            const auto inLevels = g.levelArea().getCentre();
            g.mouseWheelMove (mouseAt (g, inLevels, inLevels, false), wheel (0.11f));
            expectWithinAbsoluteError (real (h, pid::compKnee), 7.0f, 0.01f, "the level display ignores the wheel");
        }

        beginTest ("Compressor graph: transfer curve = CompressorModule::transferDb (makeup and auto makeup)");
        {
            Harness h;
            h.set (pid::compMakeup, 4.0f);
            h.set (pid::compAutoMakeup, 1.0f);
            CompGraph g (h.proc, 0);
            g.setBounds (0, 0, 900, 360);
            Live live (h);
            g.update (live.next());
            const auto& s = g.getSettings();
            expect (s.autoMakeup && std::abs (s.makeupDb - 4.0f) < 0.01f, "settings read");
            const float t = dsp::CompressorModule::transferDb (s, s.thresholdDb);
            expectWithinAbsoluteError (g.kneePoint().y, g.yForOutput (t), 0.01f, "the knee point sits on the curve");
        }

        beginTest ("Compressor graph instance 2: edits only _2 and reads compressor 2's feed");
        {
            Harness h;
            h.neutral();
            ensureInChain (h, ModuleId::comp, 1);
            setReal (h, idFor (pid::compOn, 1), 1.0f);
            setReal (h, idFor (pid::compThreshold, 1), -40.0f);
            CompGraph g (h.proc, 1);
            g.setBounds (0, 0, 900, 360);
            Live live (h);
            g.update (live.next());
            play (h, 1000.0f, 0.5f, 0.5);
            g.update (live.next());
            expect (g.getNumFrames() > 50, "frames of instance 2");
            drag (g, g.kneePoint(), { 30.0f, 0.0f });
            expect (real (h, idFor (pid::compThreshold, 1)) > -40.0f + 1.0f, "threshold _2 moved");
            expectWithinAbsoluteError (real (h, pid::compThreshold), -18.0f, 0.001f, "instance 1 untouched");
        }

        //======================================================================
        // De-esser graph
        beginTest ("De-esser graph: S band drag x = Frequency, y = Range (one step), wheel, reset, card");
        {
            Harness h;
            DeEssGraph g (h.proc, 0);
            g.setBounds (0, 0, 900, 360);
            Live live (h);
            g.update (live.next());
            auto& undo = h.proc.getUndo();
            undo.clear();
            const auto band = g.bandArea();
            const auto start = band.getRelativePoint (0.3f, 0.5f);
            expectEquals (g.handleAtPoint (start), 0, "band hit");
            const float x0 = g.xForFreq (6500.0f);
            drag (g, start, { -60.0f, 0.0f });
            expectWithinAbsoluteError (real (h, pid::deessFreq), g.freqForX (x0 - 60.0f), g.freqForX (x0 - 60.0f) * 0.01f, "x = frequency");
            expectWithinAbsoluteError (real (h, pid::deessRange), 6.0f, 0.01f, "range kept");
            expectEquals (undo.getNumSteps(), 1);
            g.update (live.next());
            const float dbPerPx = DeEssGraph::reductionScaleDb / (g.plotArea().getHeight() * DeEssGraph::reductionDepth);
            drag (g, g.bandArea().getRelativePoint (0.3f, 0.5f), { 0.0f, 30.0f });
            expectWithinAbsoluteError (real (h, pid::deessRange), 6.0f + 30.0f * dbPerPx, 0.1f, "down = more range");
            expectEquals (undo.getNumSteps(), 2);

            const float f = real (h, pid::deessFreq);
            g.update (live.next());
            const auto p = g.bandArea().getRelativePoint (0.3f, 0.5f);
            g.mouseWheelMove (mouseAt (g, p, p, false), wheel (0.11f));
            expectWithinAbsoluteError (real (h, pid::deessFreq), f * std::exp2 (1.0f / 24.0f), f * 0.002f, "wheel = 1/24 octave");
            expectEquals (undo.getNumSteps(), 3);

            g.update (live.next());
            const auto q = g.bandArea().getRelativePoint (0.3f, 0.5f);
            g.mouseDown (mouseAt (g, q, q, false));
            g.mouseDoubleClick (mouseAt (g, q, q, false, {}, 2));
            g.mouseUp (mouseAt (g, q, q, false, {}, 2));
            expectWithinAbsoluteError (real (h, pid::deessFreq), 6500.0f, 0.5f, "double-click: frequency default");
            expectWithinAbsoluteError (real (h, pid::deessRange), 6.0f, 0.01f, "and range default");
            expectEquals (undo.getNumSteps(), 4, "one step");

            g.update (live.next());
            const auto r = g.bandArea().getRelativePoint (0.3f, 0.5f);
            g.mouseDown (mouseAt (g, r, r, false));
            g.mouseDrag (mouseAt (g, r.translated (4.0f, 0.0f), r, true));
            expect (g.isCardShown() && g.getCardTitle() == "S-BAND", g.getCardTitle());
            expect (g.getCardValues().contains ("kHz") && g.getCardValues().contains ("dB"), g.getCardValues());
            g.mouseUp (mouseAt (g, r.translated (4.0f, 0.0f), r, true));
            const auto texts = menuTexts (g.buildHandleMenu (0));
            expect (texts.contains ("Standardwert"), texts.joinIntoString (", "));
            expectEquals (g.handleAtPoint (g.plotArea().getRelativePoint (0.1f, 0.5f)), -1, "below the band: no handle");
            drag (g, g.bandArea().getRelativePoint (0.3f, 0.5f), { -3000.0f, -3000.0f });
            expectWithinAbsoluteError (real (h, pid::deessFreq), 2000.0f, 0.5f, "clamped at 2 kHz");
            expectWithinAbsoluteError (real (h, pid::deessRange), 0.0f, 0.01f, "range clamped at 0");
        }

        beginTest ("De-esser graph: the module's spectrum, the live reduction and the band shade; off: nothing reduced");
        {
            Harness h;
            h.neutral();
            h.set (pid::deessOn, 1.0f);
            h.set (pid::deessThreshold, -45.0f);
            h.set (pid::deessRange, 12.0f);
            DeEssGraph g (h.proc, 0);
            g.setBounds (0, 0, 900, 360);
            Live live (h);
            g.update (live.next());
            expect (g.bandShade() < 0.15f, "quiet band before any S");
            for (int i = 0; i < 6; ++i)
            {
                play (h, 8000.0f, 0.3f, 0.1);
                g.update (live.next());
            }
            auto& module = h.proc.getChain().getDeEsser (0);
            expect (module.currentReductionDb.load() > 3.0f, "the module reduces: " + juce::String (module.currentReductionDb.load()));
            expect (g.hasSpectrum(), "spectrum from the module tap");
            expect (g.shownInputDb (8000.0f) > g.shownInputDb (500.0f) + 20.0f, "the tone shows at 8 kHz");
            expect (g.getShownReductionDb() > 3.0f, "reduction shown: " + juce::String (g.getShownReductionDb()));
            expect (g.bandShade() > 0.3f, "band shade grows with the reduction: " + juce::String (g.bandShade()));
            expect (g.shownOutputDb (8000.0f) < g.shownInputDb (8000.0f) - 2.0f, "after < before in the band");
            expectWithinAbsoluteError (g.shownOutputDb (500.0f), g.shownInputDb (500.0f), 0.5f, "split band: below the band untouched");

            h.set (pid::deessOn, 0.0f);
            for (int i = 0; i < 30; ++i)
            {
                play (h, 8000.0f, 0.3f, 0.02);
                g.update (live.next());
            }
            expect (g.getShownReductionDb() < 0.1f, "off: no reduction shown " + juce::String (g.getShownReductionDb()));
        }

        //======================================================================
        // Vocal Smash
        beginTest ("Smash stage: the GR column and the history follow the squashed path");
        {
            Harness h;
            h.neutral();
            ensureInChain (h, ModuleId::smash, 0);
            h.set (pid::smashOn, 1.0f);
            h.set (pid::smashAmount, 80.0f);
            SmashStage st (h.proc, 0);
            st.setBounds (0, 0, 900, 360);
            Live live (h);
            st.update (live.next());
            play (h, 1000.0f, 0.5f, 1.0);
            for (int i = 0; i < 20; ++i)
                st.update (live.next());
            const float gr = h.proc.getChain().getSmash (0).currentGainReductionDb.load();
            expect (gr > 5.0f, "the module squashes: " + juce::String (gr));
            expect (st.getNumFrames() > 150, "history frames " + juce::String (st.getNumFrames()));
            expectWithinAbsoluteError (st.getShownGainReductionDb(), gr, 0.5f, "column = current GR");
            expect (st.getPeakHoldDb() >= st.getShownGainReductionDb() - 0.01f, "peak hold at least the current value");
            h.set (pid::smashOn, 0.0f);
            for (int i = 0; i < 60; ++i)
                st.update (live.next());
            expect (st.getShownGainReductionDb() < 0.1f, "off: the column falls to 0");
        }

        //======================================================================
        // Level Ride
        beginTest ("Ride bars: one bar per word, placed by samplePosition, before grey / after; off: frozen");
        {
            Harness h;
            h.neutral();
            ensureInChain (h, ModuleId::ride, 0);
            h.set (pid::rideOn, 1.0f);
            RideBars bars (h.proc, 0);
            bars.setBounds (0, 0, 900, 360);
            Live live (h);
            bars.update (live.next());
            h.run (words (h.fs, 8, -12.0f, -30.0f));
            bars.update (live.next());
            auto& module = h.proc.getChain().getRide (0);
            expect (bars.getNumSegments() >= 6, "word bars " + juce::String (bars.getNumSegments()));
            expect (bars.getNow() == module.samplePosition.load(), "time axis at the module's position");
            const auto seg = bars.getSegment (0);
            const auto after = bars.getSegmentRect (0, true), before = bars.getSegmentRect (0, false);
            expectWithinAbsoluteError (after.getRight(), bars.xForSample (seg.startSample + seg.lengthSamples), 1.5f, "bar ends at its last sample");
            expectWithinAbsoluteError (before.getY(), bars.yForLevel (seg.levelBeforeDb), 0.5f, "grey = level before");
            expectWithinAbsoluteError (after.getY(), bars.yForLevel (seg.levelAfterDb), 0.5f, "after = level after");
            const float ageSec = (float) ((double) (bars.getNow() - seg.startSample) / h.fs);
            expectWithinAbsoluteError (bars.xForSample (seg.startSample),
                                       bars.plotArea().getRight() - bars.plotArea().getWidth() * ageSec / (float) RideBars::secondsShown, 0.5f, "x by age");
            // Loud and quiet words come closer together after the ride
            float spreadBefore = 0.0f, spreadAfter = 0.0f;
            for (int i = 0; i + 1 < std::min (6, bars.getNumSegments()); ++i)
            {
                spreadBefore = std::max (spreadBefore, std::abs (bars.getSegment (i).levelBeforeDb - bars.getSegment (i + 1).levelBeforeDb));
                spreadAfter = std::max (spreadAfter, std::abs (bars.getSegment (i).levelAfterDb - bars.getSegment (i + 1).levelAfterDb));
            }
            expect (spreadAfter < spreadBefore - 3.0f, "evened: " + juce::String (spreadBefore) + " -> " + juce::String (spreadAfter));

            const int n = bars.getNumSegments();
            h.set (pid::rideOn, 0.0f);
            h.run (words (h.fs, 4, -12.0f, -30.0f));
            bars.update (live.next());
            expectEquals (bars.getNumSegments(), n, "no new bars while off");
        }

        beginTest ("Ride bars: target line drag (one step), Shift fine, wheel, reset, clamp, card");
        {
            Harness h;
            ensureInChain (h, ModuleId::ride, 0);
            RideBars bars (h.proc, 0);
            bars.setBounds (0, 0, 900, 360);
            Live live (h);
            bars.update (live.next());
            auto& undo = h.proc.getUndo();
            undo.clear();
            const auto p = juce::Point<float> (bars.plotArea().getCentreX(), bars.targetY());
            expectEquals (bars.handleAtPoint (p), 0, "target line hit");
            expectEquals (bars.handleAtPoint (p.translated (0.0f, 30.0f)), -1, "off the line: none");
            const float dbPerPx = (RideBars::topDb - RideBars::bottomDb) / bars.plotArea().getHeight();
            drag (bars, p, { 0.0f, -20.0f });
            expectWithinAbsoluteError (real (h, pid::rideTarget), -18.0f + 20.0f * dbPerPx, 0.05f, "up = higher target");
            expectEquals (undo.getNumSteps(), 1);
            bars.update (live.next());
            const float t = real (h, pid::rideTarget);
            drag (bars, { p.x, bars.targetY() }, { 0.0f, 20.0f }, juce::ModifierKeys::shiftModifier);
            expectWithinAbsoluteError (real (h, pid::rideTarget), t - 2.0f * dbPerPx, 0.02f, "Shift = 1/10");
            bars.update (live.next());
            const auto q = juce::Point<float> (p.x, bars.targetY());
            bars.mouseWheelMove (mouseAt (bars, q, q, false), wheel (-0.11f));
            expectWithinAbsoluteError (real (h, pid::rideTarget), t - 2.0f * dbPerPx - 0.5f, 0.02f, "wheel down = -0.5 dB");
            bars.update (live.next());
            const auto r = juce::Point<float> (p.x, bars.targetY());
            bars.mouseDown (mouseAt (bars, r, r, false));
            bars.mouseDoubleClick (mouseAt (bars, r, r, false, {}, 2));
            bars.mouseUp (mouseAt (bars, r, r, false, {}, 2));
            expectWithinAbsoluteError (real (h, pid::rideTarget), -18.0f, 0.01f, "double-click = default");
            bars.update (live.next());
            drag (bars, { p.x, bars.targetY() }, { 0.0f, -3000.0f });
            expectWithinAbsoluteError (real (h, pid::rideTarget), -6.0f, 0.01f, "clamped at -6 dB");
            bars.update (live.next());
            const auto s = juce::Point<float> (p.x, bars.targetY());
            bars.mouseDown (mouseAt (bars, s, s, false));
            bars.mouseDrag (mouseAt (bars, s.translated (0.0f, 3.0f), s, true));
            expect (bars.isCardShown() && bars.getCardTitle() == "TARGET", bars.getCardTitle());
            bars.mouseUp (mouseAt (bars, s.translated (0.0f, 3.0f), s, true));
        }

        beginTest ("Ride bars instance 2: edits rideTarget_2, reads ride 2's segments");
        {
            Harness h;
            h.neutral();
            ensureInChain (h, ModuleId::ride, 1);
            setReal (h, idFor (pid::rideOn, 1), 1.0f);
            RideBars bars (h.proc, 1);
            bars.setBounds (0, 0, 900, 360);
            Live live (h);
            bars.update (live.next());
            h.run (words (h.fs, 6, -12.0f, -30.0f));
            bars.update (live.next());
            expect (bars.getNumSegments() >= 4, "segments of instance 2: " + juce::String (bars.getNumSegments()));
            drag (bars, { bars.plotArea().getCentreX(), bars.targetY() }, { 0.0f, -20.0f });
            expect (real (h, idFor (pid::rideTarget, 1)) > -18.0f + 1.0f, "target _2 moved");
            expectWithinAbsoluteError (real (h, pid::rideTarget), -18.0f, 0.001f, "instance 1 untouched");
        }

        //======================================================================
        // Fresh instances (dyn Ruling, Task 5): browser-added de-essers detect relative; nothing else changes
        beginTest ("Fresh instance: a de-esser added from the browser starts on Relative detection (same undo step)");
        {
            Harness h;
            expectEquals ((int) real (h, pid::deessDetection), 0, "a new plugin's default de-esser stays Absolute (parameter default)");
            ui::ChainView view (h.proc);
            view.setBounds (0, 0, 886, ui::ChainView::preferredHeight);
            auto& undo = h.proc.getUndo();
            undo.clear();
            const int code = view.insertModule (ModuleId::deess);
            expect (code >= 0, "inserted");
            const int k = dsp::selectionInstance (code);
            expectEquals (k, 1, "the second de-esser");
            expectEquals ((int) real (h, idFor (pid::deessDetection, k)), 1, "Relative");
            expectEquals ((int) real (h, pid::deessDetection), 0, "the existing one keeps Absolute");
            expectEquals (undo.getNumSteps(), 1, "one step");
            expect (undo.undo());
            expect (! h.proc.getRouting().contains (ModuleId::deess, k), "undo removes it");

            // Reset module = the fresh-instance defaults
            setReal (h, pid::deessDetection, 0.0f);
            ui::ChainView::resetInstance (h.proc, ModuleId::deess, 0);
            expectEquals ((int) real (h, pid::deessDetection), 1, "reset = like a new de-esser");

            // A duplicate copies its original
            setReal (h, pid::deessDetection, 0.0f);
            const int copy = view.duplicateInstance (ModuleId::deess, 0);
            expect (copy >= 0);
            expectEquals ((int) real (h, idFor (pid::deessDetection, dsp::selectionInstance (copy))), 0, "duplicate keeps Absolute");

            // The compressor and the others get no overrides
            const int comp = view.insertModule (ModuleId::comp);
            expect (comp >= 0);
            expectEquals ((int) real (h, idFor (pid::compAutoRelease, dsp::selectionInstance (comp))), 0, "comp: parameter defaults");
        }

        beginTest ("Fresh instance: an old project (no deessDetection in its state) loads Absolute");
        {
            Harness a;
            a.set (pid::deessDetection, 1.0f);
            juce::MemoryBlock blob;
            a.proc.getStateInformation (blob);
            auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
            expect (xml != nullptr);
            if (xml == nullptr)
                return;
            for (auto* child = xml->getFirstChildElement(); child != nullptr;)
            {
                auto* next = child->getNextElement();
                if (child->getStringAttribute ("id").startsWith ("deessDetection"))
                    xml->removeChildElement (child, true);
                child = next;
            }
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*xml, old);
            Harness b;
            b.set (pid::deessDetection, 1.0f);
            b.proc.setStateInformation (old.getData(), (int) old.getSize());
            expectEquals ((int) b.get (pid::deessDetection), 0, "Absolute (0.5 behaviour)");
        }
    }
};

static DynPanelTests dynPanelTests;
