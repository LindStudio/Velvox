#include "AssistantView.h"
#include "../PluginProcessor.h"
#include "../Parameters/ParameterLayout.h"
#include "../Assistant/IntentParser.h"
#include "../Assistant/AssistantPlanner.h"
#include "../Assistant/AssistantContextBuilder.h"
#include "UserSettings.h"

// Source is compiled as UTF-8 (/utf-8): German texts are plain literals.
namespace velvox::ui
{
    using namespace velvox::assistant;

    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }

        constexpr int pad = 24;

        // Module that owns a parameter (to open it from a suggestion); -2 = none
        int moduleIdForParam (const juce::String& id)
        {
            const auto* spec = findParameterSpec (id);
            if (spec == nullptr)
                return -2;
            const juce::String m (spec->module);
            using M = dsp::ModuleId;
            if (m == "EQ")         return (int) M::eq;
            if (m == "Compressor") return (int) M::comp;
            if (m == "De-Esser")   return (int) M::deess;
            if (m == "Saturation") return (int) M::sat;
            if (m == "Delay")      return (int) M::delay;
            if (m == "Reverb")     return (int) M::reverb;
            if (m == "Doubler")    return (int) M::doubler;
            if (m == "Smash")      return (int) M::smash;
            if (m == "Smooth")     return (int) M::smooth;
            if (m == "Ride")       return (int) M::ride;
            if (m == "Ducking")    return id.startsWith ("delay") ? (int) M::delay : (id.startsWith ("reverb") ? (int) M::reverb : -2);
            if (m == "Stereo" || m == "Global") return -1;
            return -2;
        }

        juce::TextLayout wrap (const juce::String& text, const juce::Font& f, juce::Colour c, float width)
        {
            juce::AttributedString s;
            s.setJustification (juce::Justification::topLeft);
            s.setLineSpacing (2.0f);
            s.append (text, f, c);
            juce::TextLayout layout;
            layout.createLayout (s, std::max (40.0f, width));
            return layout;
        }

        void drawTag (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, juce::Colour c, bool filled)
        {
            if (filled)
            {
                g.setColour (c.withAlpha (0.14f));
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            }
            g.setColour (c.withAlpha (0.6f));
            g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
            g.setColour (c.interpolatedWith (colours::text, 0.35f));
            g.setFont (medium (12.0f));
            g.drawText (text, r, juce::Justification::centred, false);
        }
    }

    //==========================================================================
    // Proposed changes and notes, drawn as cards (inside a viewport)
    class AssistantView::SuggestionList final : public juce::Component
    {
    public:
        std::function<void (int index, bool selected)> onToggle;
        std::function<void (int moduleId)> onOpenModule;

        void setContent (const Plan& p, bool applied)
        {
            plan = p;
            isApplied = applied;
            if (optional.size() != plan.changes.size() || ! sameChanges (p))
            {
                optional.clear();
                for (auto& c : plan.changes)
                    optional.push_back (! c.selected);
            }
            lastIds.clear();
            for (auto& c : plan.changes)
                lastIds.add (c.paramId);
            layoutFor (getWidth());
            repaint();
        }

        int layoutFor (int width)
        {
            cards.clear();
            int y = 0;
            const float textW = (float) width - 52.0f - 20.0f;
            // Questions and warnings first, then the changes, then background info
            const auto addNotes = [&] (bool urgent)
            {
                for (int i = 0; i < (int) plan.notes.size(); ++i)
                {
                    const auto kind = plan.notes[(size_t) i].kind;
                    const bool isUrgent = kind == Note::Kind::question || kind == Note::Kind::warning;
                    if (isUrgent != urgent)
                        continue;
                    const auto layout = wrap (plan.notes[(size_t) i].text, font (13.5f), colours::text, (float) width - 150.0f - 20.0f);
                    const int h = std::max (40, (int) std::ceil (layout.getHeight()) + 22);
                    cards.push_back ({ juce::Rectangle<int> (0, y, width, h), -1, i });
                    y += h + 8;
                }
            };
            addNotes (true);
            if (y > 0)
                y += 8;
            for (int i = 0; i < (int) plan.changes.size(); ++i)
            {
                const auto layout = wrap (plan.changes[(size_t) i].reason, font (13.5f), colours::text, textW);
                const int h = 72 + (int) std::ceil (layout.getHeight()) + 16;
                cards.push_back ({ juce::Rectangle<int> (0, y, width, h), i, -1 });
                y += h + 10;
            }
            if (! plan.changes.empty())
                y += 8;
            addNotes (false);
            return std::max (0, y - 8);
        }

        void paint (juce::Graphics& g) override
        {
            for (auto& card : cards)
            {
                if (card.change >= 0)
                    paintChange (g, plan.changes[(size_t) card.change], card.area.toFloat(), card.change);
                else
                    paintNote (g, plan.notes[(size_t) card.note], card.area.toFloat());
            }
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            for (auto& card : cards)
            {
                if (card.change < 0 || ! card.area.contains (e.getPosition()))
                    continue;
                const auto& c = plan.changes[(size_t) card.change];
                if (moduleLink (card.area.toFloat(), c).contains (e.position))
                {
                    if (onOpenModule)
                        onOpenModule (moduleIdForParam (c.paramId));
                    return;
                }
                if (! isApplied && onToggle)
                    onToggle (card.change, ! c.selected);
                return;
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            bool hand = false;
            for (auto& card : cards)
                if (card.change >= 0 && card.area.contains (e.getPosition()))
                    hand = ! isApplied || moduleLink (card.area.toFloat(), plan.changes[(size_t) card.change]).contains (e.position);
            setMouseCursor (hand ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }

    private:
        struct Card { juce::Rectangle<int> area; int change, note; };

        bool sameChanges (const Plan& p) const
        {
            if ((int) p.changes.size() != lastIds.size())
                return false;
            for (int i = 0; i < lastIds.size(); ++i)
                if (p.changes[(size_t) i].paramId != lastIds[i])
                    return false;
            return true;
        }

        juce::Rectangle<float> moduleLink (juce::Rectangle<float> r, const Change& c) const
        {
            if (moduleIdForParam (c.paramId) < -1)
                return {};
            return { r.getRight() - 16.0f - 120.0f, r.getY() + 44.0f, 120.0f, 22.0f };
        }

        void paintChange (juce::Graphics& g, const Change& c, juce::Rectangle<float> r, int index)
        {
            const bool sel = c.selected;
            g.setColour (sel ? colours::raised : colours::panel);
            g.fillRoundedRectangle (r, radius::card);
            g.setColour (sel ? (isApplied ? colours::teal.withAlpha (0.45f) : colours::cyan.withAlpha (0.45f)) : colours::line);
            g.drawRoundedRectangle (r.reduced (0.5f), radius::card, 1.0f);

            // Checkbox
            const auto box = juce::Rectangle<float> (r.getX() + 16.0f, r.getY() + 15.0f, 22.0f, 22.0f);
            g.setColour (sel ? colours::cyan.withAlpha (isApplied ? 0.5f : 0.9f) : colours::background);
            g.fillRoundedRectangle (box, 5.0f);
            g.setColour (sel ? colours::cyan : colours::line.brighter (0.6f));
            g.drawRoundedRectangle (box.reduced (0.5f), 5.0f, 1.2f);
            if (sel)
            {
                juce::Path tick;
                tick.startNewSubPath (box.getX() + 5.5f, box.getCentreY());
                tick.lineTo (box.getX() + 9.5f, box.getBottom() - 6.0f);
                tick.lineTo (box.getRight() - 5.0f, box.getY() + 6.0f);
                g.setColour (colours::background);
                g.strokePath (tick, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            const float x = r.getX() + 52.0f;
            const float right = r.getRight() - 16.0f;
            const auto textColour = sel ? colours::text : colours::textDim;

            // Line 1: module badge, title, tags
            auto line1 = juce::Rectangle<float> (x, r.getY() + 13.0f, right - x, 26.0f);
            const auto badgeFont = medium (12.5f);
            const float badgeW = (float) juce::GlyphArrangement::getStringWidthInt (badgeFont, c.module) + 22.0f;
            auto badge = line1.removeFromLeft (badgeW).withSizeKeepingCentre (badgeW, 22.0f);
            g.setColour (colours::accent.withAlpha (0.06f));
            g.fillRoundedRectangle (badge, 11.0f);
            g.setColour (colours::textDim);
            g.setFont (badgeFont);
            g.drawText (c.module, badge, juce::Justification::centred, false);
            line1.removeFromLeft (12.0f);

            auto tags = line1.removeFromRight (190.0f);
            {
                auto t = tags.removeFromRight (82.0f).withSizeKeepingCentre (82.0f, 22.0f);
                drawTag (g, t, c.measured ? de ("Messung") : de ("Regel"), c.measured ? colours::cyan : colours::textDim, c.measured);
                tags.removeFromRight (8.0f);
                if (isApplied && sel)
                    drawTag (g, tags.removeFromRight (100.0f).withSizeKeepingCentre (100.0f, 22.0f), de ("angewendet"), colours::teal, true);
                else if ((size_t) index < optional.size() && optional[(size_t) index])
                    drawTag (g, tags.removeFromRight (82.0f).withSizeKeepingCentre (82.0f, 22.0f), de ("optional"), colours::bypass, false);
            }

            g.setColour (textColour);
            g.setFont (font (15.5f, true));
            g.drawFittedText (c.title, line1.toNearestInt(), juce::Justification::centredLeft, 1, 0.9f);

            // Line 2: parameter, current -> proposed
            auto line2 = juce::Rectangle<float> (x, r.getY() + 44.0f, right - x - 130.0f, 22.0f);
            const auto f = medium (14.0f);
            const auto draw = [&] (const juce::String& text, juce::Colour col, const juce::Font& fnt)
            {
                const float w = (float) juce::GlyphArrangement::getStringWidthInt (fnt, text) + 2.0f;
                g.setColour (col);
                g.setFont (fnt);
                g.drawText (text, line2.removeFromLeft (w), juce::Justification::centredLeft, false);
            };
            draw (c.paramName + ":  ", colours::textDim, f);
            draw (de ("Aktuell ") + c.currentText, sel ? colours::text.withAlpha (0.85f) : colours::textDim, f);
            draw (de ("   →   "), colours::textFaint, f);
            draw (de ("Vorschlag ") + c.proposedText, sel ? colours::cyan.interpolatedWith (colours::text, 0.25f) : colours::textDim, font (14.0f, true));

            // Open the module
            const auto link = moduleLink (r, c);
            if (! link.isEmpty())
            {
                g.setColour (colours::cyan.withAlpha (0.85f));
                g.setFont (medium (13.0f));
                g.drawText (de ("Modul öffnen  ›"), link, juce::Justification::centredRight, false);
            }

            // Reason
            const float textW = r.getWidth() - 52.0f - 20.0f;
            wrap (c.reason, font (13.5f), sel ? colours::text.withAlpha (0.72f) : colours::textFaint, textW)
                .draw (g, juce::Rectangle<float> (x, r.getY() + 72.0f, textW, r.getHeight() - 72.0f));
        }

        void paintNote (juce::Graphics& g, const Note& n, juce::Rectangle<float> r)
        {
            juce::Colour c = colours::cyan;
            juce::String tag = de ("Info");
            switch (n.kind)
            {
                case Note::Kind::info:        c = colours::cyan;      tag = de ("Info"); break;
                case Note::Kind::question:    c = colours::blue;      tag = de ("Rückfrage"); break;
                case Note::Kind::warning:     c = colours::bypass;    tag = de ("Hinweis"); break;
                case Note::Kind::unavailable: c = colours::textDim;   tag = de ("Nicht verfügbar"); break;
            }
            g.setColour (colours::accent.withAlpha (0.025f));
            g.fillRoundedRectangle (r, radius::card);
            g.setColour (c.withAlpha (0.25f));
            g.drawRoundedRectangle (r.reduced (0.5f), radius::card, 1.0f);
            g.setColour (c.withAlpha (0.8f));
            g.fillRoundedRectangle (r.getX() + 1.0f, r.getY() + 8.0f, 3.0f, r.getHeight() - 16.0f, 1.5f);

            drawTag (g, juce::Rectangle<float> (r.getX() + 16.0f, r.getY() + 9.0f, 118.0f, 22.0f), tag, c, true);
            wrap (n.text, font (13.5f), colours::text.withAlpha (0.86f), r.getWidth() - 150.0f - 20.0f)
                .draw (g, juce::Rectangle<float> (r.getX() + 150.0f, r.getY() + 11.0f, r.getWidth() - 170.0f, r.getHeight() - 14.0f));
        }

        Plan plan;
        bool isApplied = false;
        std::vector<bool> optional;
        juce::StringArray lastIds;
        std::vector<Card> cards;
    };

    //==========================================================================
    bool AssistantView::PromptEditor::keyPressed (const juce::KeyPress& key)
    {
        if (key.getKeyCode() == juce::KeyPress::returnKey)
        {
            if (key.getModifiers().isShiftDown())
            {
                insertTextAtCaret ("\n");
                return true;
            }
            if (onSubmit)
                onSubmit();
            return true;
        }
        return juce::TextEditor::keyPressed (key);
    }

    //==========================================================================
    AssistantView::AssistantView (VelvoxAudioProcessor& p)
        : proc (p), session (p.getState())
    {
        input.setMultiLine (true, true);
        input.setReturnKeyStartsNewLine (false);
        input.setScrollbarsShown (true);
        input.setFont (font (16.0f));
        input.setIndents (14, 12);
        input.setTextToShowWhenEmpty (de ("z. B. weniger Tiefen, mehr Reverb und die Stimme stärker in den Vordergrund bringen …"), colours::textFaint);
        input.setColour (juce::TextEditor::backgroundColourId, colours::background.darker (0.2f));
        input.setColour (juce::TextEditor::outlineColourId, colours::line.brighter (0.2f));
        input.setColour (juce::TextEditor::focusedOutlineColourId, colours::cyan.withAlpha (0.7f));
        input.setTitle (de ("Wie soll deine Vocal klingen?"));
        input.onSubmit = [this] { requestAnalysis(); };
        input.onTextChange = [this] { updateButtons(); };
        input.onEscapeKey = [this] { input.unfocusAllComponents(); };
        addAndMakeVisible (input);

        analyseButton.setButtonText (de ("Analysieren"));
        applyButton.setButtonText (de ("Vorschläge anwenden"));
        undoButton.setButtonText (de ("Rückgängig"));
        resetButton.setButtonText (de ("Alles zurücksetzen"));
        analyseButton.setTooltip (de ("Interpretiert deine Eingabe und erstellt einen Änderungsplan (Enter)"));
        applyButton.setTooltip (de ("Übernimmt die ausgewählten Änderungen in die Module"));
        undoButton.setTooltip (de ("Stellt die Werte vor der letzten Assistant-Aktion wieder her"));
        resetButton.setTooltip (de ("Verwirft Eingabe und Vorschlagsplan - die Plugin-Einstellungen bleiben unverändert"));
        analyseButton.setToggleState (true, juce::dontSendNotification);   // primary action: lit
        analyseButton.onClick = [this] { requestAnalysis(); };
        applyButton.onClick = [this] { applySelected(); };
        undoButton.onClick = [this] { undoLast(); };
        resetButton.onClick = [this] { resetPlan(); };
        for (auto* b : { &analyseButton, &applyButton, &undoButton, &resetButton })
            addAndMakeVisible (b);

        autoApply.setButtonText (de ("Sofort anwenden"));
        autoApply.setTooltip (de ("An: Velvox setzt erkannte Wünsche direkt nach der Analyse um (Rückgängig bleibt möglich). Aus: erst prüfen, dann anwenden."));
        autoApply.setToggleState (settings::getBool (settings::assistantAutoApply, true), juce::dontSendNotification);
        autoApply.onClick = [this]
        {
            settings::set (settings::assistantAutoApply, autoApply.getToggleState());
            updateButtons();
        };
        addAndMakeVisible (autoApply);

        // Examples (empty state): click to take one over
        for (auto* text : { "Weniger Tiefen.", "Etwas mehr Reverb.", "Mach die Stimme präsenter, aber nicht schärfer.",
                            "Die Vocal soll sich besser gegen den Beat durchsetzen.", "Mach die Stimme breiter, aber mono-kompatibel.",
                            "Weniger Schärfe, aber die Stimme soll weiterhin klar klingen." })
        {
            auto* b = examples.add (new juce::TextButton (de (text)));
            b->setTooltip (de ("Beispiel übernehmen"));
            b->onClick = [this, b] { setText (b->getButtonText()); input.grabKeyboardFocus(); };
            addAndMakeVisible (b);
        }

        list = std::make_unique<SuggestionList>();
        list->onToggle = [this] (int index, bool selected) { setSelected (index, selected); };
        list->onOpenModule = [this] (int id) { if (onOpenModule && id >= -1) onOpenModule (id); };
        viewport.setViewedComponent (list.get(), false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);

        refresh();
        updateButtons();
    }

    AssistantView::~AssistantView() = default;

    void AssistantView::setText (const juce::String& text)
    {
        input.setText (text, true);
        input.moveCaretToEnd();
        updateButtons();
    }

    juce::String AssistantView::getStatusText() const
    {
        juce::String s;
        switch (status)
        {
            case Status::ready:          s = de ("Bereit"); break;
            case Status::analysing:      s = de ("Eingabe wird analysiert …"); break;
            case Status::proposalsReady: s = de ("Änderungsvorschläge bereit"); break;
            case Status::applied:        s = de ("Änderungen angewendet"); break;
            case Status::noMatch:        s = de ("Keine passende Funktion gefunden"); break;
            case Status::understood:     s = de ("Verstanden - keine Änderung nötig"); break;
            case Status::question:       s = de ("Rückfrage - bitte genauer beschreiben"); break;
            case Status::conflict:       s = de ("Widersprüchliche Eingabe"); break;
            case Status::stale:          s = de ("Werte wurden geändert - Vorschläge neu berechnet"); break;
            case Status::undone:         s = de ("Rückgängig gemacht"); break;
            case Status::failed:         s = de ("Analyse fehlgeschlagen"); break;
        }
        if (beatNotConnected && (status == Status::proposalsReady || status == Status::question || status == Status::applied))
            s << de ("  ·  Beat-Sidechain nicht verbunden");
        return s;
    }

    void AssistantView::setStatus (Status s, const juce::String& detail)
    {
        status = s;
        statusDetail = detail;
        repaint (statusArea.expanded (4));
    }

    void AssistantView::updateButtons()
    {
        const bool hasText = input.getText().trim().isNotEmpty();
        analyseButton.setEnabled (hasText && ! pendingAnalysis);
        const auto analyseText = autoApply.getToggleState() ? de ("Analysieren & anwenden") : de ("Analysieren");
        if (analyseButton.getButtonText() != analyseText)
            analyseButton.setButtonText (analyseText);
        applyButton.setEnabled (! planApplied && plan.numSelected() > 0);
        undoButton.setEnabled (session.canUndo());
        resetButton.setEnabled (hasText || ! plan.changes.empty() || ! plan.notes.empty());

        const bool empty = plan.changes.empty() && plan.notes.empty();
        for (auto* b : examples)
            b->setVisible (empty);
        viewport.setVisible (! empty);
    }

    void AssistantView::rebuildList()
    {
        list->setSize (std::max (100, viewport.getMaximumVisibleWidth()), 10);
        list->setContent (plan, planApplied);
        list->setSize (list->getWidth(), list->layoutFor (list->getWidth()));
        updateButtons();
        repaint();
    }

    void AssistantView::setSelected (int index, bool shouldBeSelected)
    {
        if (index < 0 || index >= (int) plan.changes.size() || planApplied)
            return;
        plan.changes[(size_t) index].selected = shouldBeSelected;
        list->setContent (plan, planApplied);
        updateButtons();
        repaint (listHeaderArea);
    }

    void AssistantView::requestAnalysis()
    {
        if (input.getText().trim().isEmpty() || pendingAnalysis)
            return;   // an empty input never triggers anything
        pendingAnalysis = true;
        setStatus (Status::analysing);
        updateButtons();
        juce::Component::SafePointer<AssistantView> safe (this);
        juce::MessageManager::callAsync ([safe]
        {
            if (safe != nullptr)
            {
                safe->pendingAnalysis = false;
                safe->analyseNow();
            }
        });
    }

    void AssistantView::analyseNow()
    {
        pendingAnalysis = false;
        const auto text = input.getText();
        if (text.trim().isEmpty())
        {
            setStatus (Status::ready);
            updateButtons();
            return;
        }

        bool ok = true;
        ParsedRequest parsed;
        AssistantContext ctx;
        try
        {
            ctx = buildContext (proc);
            parsed = IntentParser::parse (text);
            plan = AssistantPlanner::plan (parsed, ctx);
        }
        catch (...)
        {
            ok = false;
            plan = {};
        }

        planText = text;
        planApplied = false;
        beatNotConnected = ok && plan.beatRequested && ! ctx.sidechainConnected;

        if (! ok)
            setStatus (Status::failed);
        else if (plan.hasChanges())
            setStatus (Status::proposalsReady);
        else if (! parsed.conflicts.empty())
            setStatus (Status::conflict);
        else if (! parsed.ambiguous.empty())
            setStatus (Status::question);
        else if (parsed.intents.empty() && ! parsed.constraints.empty() && parsed.unrecognised.empty())
            setStatus (Status::understood);
        else
            setStatus (Status::noMatch);

        if (ok && ! plan.hasChanges() && plan.notes.empty())
            plan.notes.push_back ({ Note::Kind::unavailable,
                                    de ("Zu dieser Eingabe gibt es keine passende Funktion. Beschreibe den Klang, z. B. „weniger Tiefen“, „mehr Hall“ oder „breiter“.") });
        rebuildList();
        viewport.setViewPosition (0, 0);

        // "Sofort anwenden": take over everything that is selected right away
        if (ok && autoApply.getToggleState() && plan.numSelected() > 0)
            applySelected();
    }

    bool AssistantView::applySelected()
    {
        if (planApplied || plan.numSelected() == 0)
            return false;

        const auto result = session.apply (plan);
        using S = AssistantSession::Result::Status;
        switch (result.status)
        {
            case S::applied:
            {
                planApplied = true;
                for (auto& m : result.messages)
                    plan.notes.insert (plan.notes.begin(), { Note::Kind::warning, m });
                setStatus (Status::applied, juce::String (result.count) + (result.count == 1 ? de (" Änderung") : de (" Änderungen")));
                rebuildList();
                return true;
            }
            case S::stale:
            {
                // Somebody changed a value since the analysis: plan again from the current state
                input.setText (planText, false);
                analyseNow();
                plan.notes.insert (plan.notes.begin(), { Note::Kind::warning, result.messages.joinIntoString (" ") });
                setStatus (Status::stale);
                rebuildList();
                return false;
            }
            case S::nothingSelected:
                return false;
            case S::invalid:
            case S::failed:
                plan.notes.insert (plan.notes.begin(), { Note::Kind::warning, result.messages.joinIntoString (" ") });
                setStatus (Status::failed, de ("Nichts angewendet"));
                rebuildList();
                return false;
        }
        return false;
    }

    bool AssistantView::undoLast()
    {
        if (! session.canUndo())
            return false;
        const auto result = session.undo();
        plan.notes.insert (plan.notes.begin(), { Note::Kind::info,
                                                 de ("Rückgängig: ") + juce::String (result.count) + de (" Wert(e) wiederhergestellt.")
                                                     + (result.messages.isEmpty() ? juce::String() : " " + result.messages.joinIntoString (" ")) });
        planApplied = false;
        // The shown plan referred to the state before undo - make it consistent again
        if (! plan.changes.empty() && session.isStale (plan))
            for (auto& c : plan.changes)
                c.selected = false;
        setStatus (Status::undone);
        rebuildList();
        return true;
    }

    void AssistantView::resetPlan()
    {
        plan = {};
        planText.clear();
        planApplied = false;
        beatNotConnected = false;
        input.clear();
        setStatus (Status::ready);
        rebuildList();
    }

    void AssistantView::refresh()
    {
        const auto sc = proc.getSidechainStatus();
        if (sc.connected != scConnected || sc.hasSignal() != scSignal)
        {
            scConnected = sc.connected;
            scSignal = sc.hasSignal();
            repaint (headerArea);
        }
        if (undoButton.isEnabled() != session.canUndo())
            updateButtons();
    }

    //==========================================================================
    void AssistantView::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, radius::panel);
        drawGlass (g, r, radius::panel, 0.012f);

        // Header: sparkle, title, one line about what this is
        auto header = headerArea;
        {
            const auto c = juce::Point<float> ((float) header.getX() + 12.0f, (float) header.getCentreY() - 8.0f);
            juce::Path star;
            for (int i = 0; i < 8; ++i)
            {
                const float a = juce::MathConstants<float>::pi * (float) i / 4.0f;
                const float rad = i % 2 == 0 ? 11.0f : 3.5f;
                const auto pnt = c.getPointOnCircumference (rad, a);
                if (i == 0) star.startNewSubPath (pnt); else star.lineTo (pnt);
            }
            star.closeSubPath();
            g.setColour (colours::cyan);
            g.fillPath (star);
            header.removeFromLeft (34);
        }
        auto titleRow = header.removeFromTop (header.getHeight() / 2 + 4);
        g.setColour (colours::text);
        g.setFont (font (typesize::title, true));
        g.drawText ("Assistant", titleRow, juce::Justification::bottomLeft, false);
        g.setColour (colours::textDim);
        g.setFont (font (typesize::body));
        g.drawFittedText (de ("Beschreibe deinen Wunschklang - Velvox schlägt passende Einstellungen vor. Komplett lokal, ohne Internet."),
                          header.withTrimmedRight (330), juce::Justification::centredLeft, 1, 0.9f);

        // Beat sidechain status
        {
            auto pill = juce::Rectangle<float> ((float) headerArea.getRight() - 310.0f, (float) headerArea.getY() + 6.0f, 310.0f, 30.0f);
            const auto c = scSignal ? colours::cyan : (scConnected ? colours::bypass : colours::textDim);
            g.setColour (c.withAlpha (0.10f));
            g.fillRoundedRectangle (pill, 15.0f);
            g.setColour (c.withAlpha (0.5f));
            g.drawRoundedRectangle (pill.reduced (0.5f), 15.0f, 1.0f);
            g.setColour (c);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ pill.getX() + 18.0f, pill.getCentreY() }));
            g.setColour (colours::text.withAlpha (0.9f));
            g.setFont (medium (13.0f));
            g.drawText (scSignal ? de ("Beat-Sidechain: verbunden, Signal")
                                 : (scConnected ? de ("Beat-Sidechain: verbunden, kein Signal") : de ("Beat-Sidechain: nicht verbunden")),
                        pill.withTrimmedLeft (32.0f), juce::Justification::centredLeft, false);
        }

        // Prompt label and keyboard hint
        g.setColour (colours::text);
        g.setFont (font (17.0f, true));
        g.drawText (de ("Wie soll deine Vocal klingen?"), labelArea, juce::Justification::centredLeft, false);
        g.setColour (colours::textFaint);
        g.setFont (medium (12.5f));
        g.drawText (de ("Enter: Analysieren   ·   Shift+Enter: neue Zeile"), labelArea.withTrimmedRight (196), juce::Justification::centredRight, false);

        // Status
        {
            juce::Colour c = colours::textDim;
            switch (status)
            {
                case Status::analysing: case Status::proposalsReady: c = colours::cyan; break;
                case Status::applied: case Status::undone: case Status::understood: c = colours::teal; break;
                case Status::noMatch: case Status::question: case Status::conflict: case Status::stale: c = colours::bypass; break;
                case Status::failed:                                 c = colours::warning; break;
                case Status::ready:                                  break;
            }
            auto s = statusArea.toFloat();
            const auto dot = juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ s.getX() + 6.0f, s.getCentreY() });
            g.setColour (c);
            g.fillEllipse (dot);
            s.removeFromLeft (20.0f);
            g.setColour (colours::text);
            g.setFont (medium (14.0f));
            juce::String text = getStatusText();
            if (statusDetail.isNotEmpty())
                text << "  (" << statusDetail << ")";
            g.drawFittedText (text, s.toNearestInt(), juce::Justification::centredLeft, 2, 0.85f);
        }

        // List header
        {
            auto h = listHeaderArea;
            const bool empty = plan.changes.empty() && plan.notes.empty();
            g.setColour (colours::text.withAlpha (0.9f));
            g.setFont (font (typesize::heading + 1.0f, true));
            const juce::String title = empty ? de ("Beispiele - klicke eines an, um es zu übernehmen") : de ("Änderungsvorschläge");
            g.drawText (title, h, juce::Justification::centredLeft, false);
            if (! plan.changes.empty())
            {
                g.setColour (colours::textDim);
                g.setFont (medium (13.5f));
                g.drawText (juce::String (plan.numSelected()) + de (" von ") + juce::String ((int) plan.changes.size()) + de (" ausgewählt"),
                            h, juce::Justification::centredRight, false);
            }
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (font (typesize::heading + 1.0f, true), title) + 14.0f;
            g.setGradientFill (juce::ColourGradient (colours::accent.withAlpha (0.10f), (float) h.getX() + w, 0.0f,
                                                     colours::accent.withAlpha (0.0f), (float) h.getRight() - 150.0f, 0.0f, false));
            g.fillRect (juce::Rectangle<float> ((float) h.getX() + w, (float) h.getCentreY(), (float) h.getWidth() - w - 150.0f, 1.0f));
        }
    }

    void AssistantView::resized()
    {
        auto r = getLocalBounds().reduced (pad, 20);
        headerArea = r.removeFromTop (54);
        r.removeFromTop (12);
        labelArea = r.removeFromTop (30);
        autoApply.setBounds (labelArea.withTrimmedLeft (labelArea.getWidth() - 180));
        r.removeFromTop (8);
        input.setBounds (r.removeFromTop (96));
        r.removeFromTop (14);

        auto buttons = r.removeFromTop (40);
        analyseButton.setBounds (buttons.removeFromLeft (210));
        buttons.removeFromLeft (10);
        applyButton.setBounds (buttons.removeFromLeft (190));
        buttons.removeFromLeft (10);
        undoButton.setBounds (buttons.removeFromLeft (130));
        buttons.removeFromLeft (10);
        resetButton.setBounds (buttons.removeFromLeft (170));
        buttons.removeFromLeft (20);
        statusArea = buttons;

        r.removeFromTop (20);
        listHeaderArea = r.removeFromTop (28);
        r.removeFromTop (10);
        viewport.setBounds (r);

        // Example chips in the list area (two columns)
        auto ex = r;
        const int colW = (ex.getWidth() - 12) / 2;
        for (int i = 0; i < examples.size(); ++i)
        {
            const int col = i % 2, rowIndex = i / 2;
            examples[i]->setBounds (ex.getX() + col * (colW + 12), ex.getY() + rowIndex * 50, colW, 40);
        }

        rebuildList();
    }
}
