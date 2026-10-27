#include "VocalCheckView.h"
#include "../PluginProcessor.h"
#include "../DSP/Instances.h"
#include "../Text/ParamText.h"

// The finding texts (problem, fix, detail, why) hold both languages (i18n::both, see
// CheckRules.cpp): they are shown with i18n::show.
namespace velvox::ui
{
    using dsp::ModuleId;
    using i18n::tr;

    namespace
    {
        const juce::Colour amber { 0xfff59e0b };       // medium findings, middle scores (design mockup)

        juce::Colour checkColour() { return moduleColour (ModuleId::check); }

        juce::String seconds (float s) { return i18n::number (s, 1); }

        // Layout of the header (design units)
        constexpr int pad = 16, headerH = 112, ringSize = 88, rightW = 252;
        constexpr int buttonW = 104, buttonH = 28, tileGap = 8;

        const juce::String dash = juce::String::fromUTF8 ("\xe2\x80\x94");
    }

    //==========================================================================
    juce::Colour checkScoreColour (int score)
    {
        if (score < 0)  return colours::textFaint;
        if (score >= 80) return colours::ok;
        if (score >= 60) return amber;
        return colours::warning;
    }

    juce::Colour checkSeverityColour (check::Severity level)
    {
        switch (level)
        {
            case check::Severity::high:   return colours::warning;
            case check::Severity::medium: return amber;
            case check::Severity::low:    break;
        }
        return checkColour();
    }

    juce::String checkPreviewText (VelvoxAudioProcessor& proc, const check::ResolvedFix& fix)
    {
        juce::StringArray lines;
        if (fix.routing.has_value())
        {
            const auto now = proc.getRouting();
            for (int m = 0; m < dsp::numModules; ++m)
                for (int k = 0; k < dsp::maxInstances ((ModuleId) m); ++k)
                    if (fix.routing->contains ((ModuleId) m, k) && ! now.contains ((ModuleId) m, k))
                        lines.add (i18n::format (tr ("{0} einfügen", "Insert {0}"), { dsp::instanceDisplayName ((ModuleId) m, k) }));
        }
        auto& apvts = proc.getState();
        for (auto& p : fix.params)
            if (auto* param = apvts.getParameter (p.paramId))
            {
                const float normalised = param->convertTo0to1 (p.value);
                auto value = param->getText (normalised, 32);
                if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
                    value = i18n::choiceName (p.paramId, choice->choices.indexOf (value), value);
                else if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
                    value = normalised > 0.5f ? tr ("An", "On") : tr ("Aus", "Off");
                if (param->getLabel().isNotEmpty() && ! value.endsWith (param->getLabel()))
                    value << " " << param->getLabel();
                lines.add (i18n::paramName (p.paramId, param->getName (64)) + ": " + value);
            }
        return lines.joinIntoString ("\n");
    }

    void drawScoreRing (juce::Graphics& g, juce::Rectangle<float> area, int score, float thickness, bool withCaption)
    {
        const auto box = area.withSizeKeepingCentre (std::min (area.getWidth(), area.getHeight()), std::min (area.getWidth(), area.getHeight()))
                             .reduced (thickness * 0.5f);
        const auto c = box.getCentre();
        const float r = box.getWidth() * 0.5f;
        juce::Path track;
        track.addCentredArc (c.x, c.y, r, r, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
        g.setColour (colours::control);
        g.strokePath (track, juce::PathStrokeType (thickness));

        const auto colour = checkScoreColour (score);
        if (score > 0)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, r, r, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * (float) juce::jlimit (0, 100, score) / 100.0f, true);
            g.setColour (colour.withAlpha (0.18f));
            g.strokePath (arc, juce::PathStrokeType (thickness + 6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (colour);
            g.strokePath (arc, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        const float numberH = box.getHeight() * 0.36f;
        auto text = box.withSizeKeepingCentre (box.getWidth(), numberH).translated (0.0f, withCaption ? -numberH * 0.12f : 0.0f);
        g.setColour (score < 0 ? colours::textFaint : colours::textStrong);
        g.setFont (semiBold (numberH));
        g.drawText (score < 0 ? dash : juce::String (score), text, juce::Justification::centred, false);
        if (withCaption)
        {
            g.setColour (colours::textFaint);
            g.setFont (semiBold (8.5f, 0.15f));
            g.drawText ("SCORE", text.withY (text.getBottom() + 1.0f).withHeight (11.0f), juce::Justification::centred, false);
        }
    }

    //==========================================================================
    CheckButton::CheckButton (const juce::String& text, Style s) : juce::Button (text), style (s), accent (checkColour())
    {
        setWantsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void CheckButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool enabled = isEnabled();
        const float corner = style == Style::segment ? 6.0f : r.getHeight() * 0.5f;
        juce::Colour text = colours::textSoft;
        auto f = semiBold (11.5f);

        switch (style)
        {
            case Style::filled:
            {
                // The normal text button of the design (VelvoxLookAndFeel::drawButtonBackground): a faint
                // white fill and the panel line, brighter text than the ghost buttons. No colour fill, no
                // glow: the findings stay the loudest part of the view.
                g.setColour (! enabled ? colours::control
                                       : colours::accent.withAlpha (down ? 0.08f : (highlighted ? 0.05f : 0.02f)));
                g.fillRoundedRectangle (r, corner);
                g.setColour (! enabled ? colours::borderInset : highlighted ? colours::line.brighter (0.4f) : colours::line.brighter (0.15f));
                g.drawRoundedRectangle (r, corner, 1.0f);
                text = enabled ? colours::textStrong : colours::textFaint;
                break;
            }
            case Style::ghost:
                g.setColour (down ? colours::raisedHi : colours::control);
                g.fillRoundedRectangle (r, corner);
                g.setColour (enabled && highlighted ? accent.withAlpha (0.55f) : colours::borderInset);
                g.drawRoundedRectangle (r, corner, 1.0f);
                text = ! enabled ? colours::textFaint : highlighted ? colours::text : colours::textMuted;
                break;
            case Style::segment:
                if (getToggleState())
                {
                    g.setColour (accent.withAlpha (0.16f));
                    g.fillRoundedRectangle (r, corner);
                    text = accent.interpolatedWith (juce::Colours::white, 0.55f);
                }
                else
                {
                    if (highlighted)
                    {
                        g.setColour (colours::raisedHi);
                        g.fillRoundedRectangle (r, corner);
                    }
                    text = juce::Colour (0xff7c859b);
                }
                f = semiBold (11.0f);
                break;
            case Style::link:
                text = ! enabled ? colours::textFaint : highlighted ? colours::text : colours::textMuted;
                f = semiBold (11.0f, 0.04f);
                break;
        }

        g.setColour (text);
        g.setFont (f);
        if (style != Style::link)
        {
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
            return;
        }
        if (chevron == 0)
        {
            g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centredRight, false);
            return;
        }
        // "2 ignoriert" + a small chevron (drawn: the fonts have no triangle glyphs)
        g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centredLeft, false);
        const float x = juce::GlyphArrangement::getStringWidth (f, getButtonText()) + 8.0f;
        const float cy = (float) getHeight() * 0.5f;
        juce::Path p;
        if (chevron == 1)
            p.addTriangle (x, cy - 4.0f, x, cy + 4.0f, x + 4.5f, cy);
        else
            p.addTriangle (x - 1.0f, cy - 2.5f, x + 7.0f, cy - 2.5f, x + 3.0f, cy + 2.5f);
        g.fillPath (p);
    }

    //==========================================================================
    VocalCheckView::Tile::Tile (const juce::String& findingId, bool ignored) : id (findingId), ignoredRow (ignored)
    {
        for (auto* b : { &applyButton, &ignoreButton, &restoreButton })
            addChildComponent (*b);
        ignoreButton.setVisible (! ignoredRow);
        restoreButton.setVisible (ignoredRow);
        ignoreButton.setTooltip (tr ("Diesen Befund ausblenden (wird mit dem Projekt gespeichert)", "Hide this finding (saved with the project)"));
        restoreButton.setTooltip (tr ("Wieder in der Liste zeigen", "Show it in the list again"));
    }

    bool VocalCheckView::Tile::set (const juce::String& p, const juce::String& fx, juce::Colour e, bool can, bool done, const juce::String& d)
    {
        const bool changed = p != problem || fx != fix || e != edge || can != canApply || done != applied || d != details;
        problem = p;
        fix = fx;
        edge = e;
        canApply = can;
        applied = done;
        details = d;
        if (changed)
        {
            applyButton.setVisible (! ignoredRow && canApply);
            resized();
            repaint();
        }
        return changed;
    }

    void VocalCheckView::Tile::resized()
    {
        auto r = getLocalBounds().reduced (14, 0);
        const int y = (getHeight() - buttonH) / 2;
        if (ignoredRow)
        {
            restoreButton.setBounds (r.getRight() - 132, y, 132, buttonH - 2);
            textArea = { 18, 0, restoreButton.getX() - 12 - 18, getHeight() };
            return;
        }
        // Two fixed columns, so the buttons of all tiles line up
        applyButton.setBounds (r.getRight() - buttonW, y, buttonW, buttonH);
        ignoreButton.setBounds (applyButton.getX() - tileGap - buttonW, y, buttonW, buttonH);
        textArea = { 18, 0, ignoreButton.getX() - 12 - 18, getHeight() };
    }

    void VocalCheckView::Tile::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        if (ignoredRow)
        {
            g.setColour (colours::lineSoft);
            g.drawRoundedRectangle (r, 8.0f, 1.0f);
            g.setColour (colours::textFaint);
            g.fillRoundedRectangle ({ 8.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f }, 3.0f);
            g.setColour (colours::textMuted);
            g.setFont (semiBold (12.0f));
            g.drawFittedText (problem, textArea, juce::Justification::centredLeft, 1, 0.9f);
            return;
        }

        g.setColour (colours::panelInset);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (edge.withAlpha (0.28f));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        // Severity edge
        g.setColour (edge.withAlpha (0.16f));
        g.fillRoundedRectangle ({ 1.0f, 6.0f, 10.0f, r.getHeight() - 11.0f }, 3.0f);
        g.setColour (edge);
        g.fillRoundedRectangle ({ 1.0f, 8.0f, 3.0f, r.getHeight() - 15.0f }, 1.5f);

        auto text = textArea.toFloat();
        auto top = text.removeFromTop (text.getHeight() * 0.5f);
        g.setColour (colours::text);
        g.setFont (semiBold (13.5f));
        g.drawFittedText (problem, top.withTrimmedBottom (1.0f).toNearestInt(), juce::Justification::bottomLeft, 1, 0.9f);
        g.setColour (colours::textMuted);
        g.setFont (font (12.5f));
        g.drawFittedText (fix, text.withTrimmedTop (3.0f).toNearestInt(), juce::Justification::topLeft, 1, 0.9f);

        if (applied)
        {
            g.setColour (colours::ok);
            g.setFont (semiBold (11.5f));
            g.drawText (tr ("\xe2\x9c\x93 Angewendet", "\xe2\x9c\x93 Applied"), applyButton.getBounds(), juce::Justification::centred, false);
        }
    }

    //==========================================================================
    // The scrolling list: tiles, the "x ignoriert" section and the empty states
    class VocalCheckView::List final : public juce::Component
    {
    public:
        explicit List (VocalCheckView& v) : owner (v) {}

        void paint (juce::Graphics& g) override
        {
            const auto w = (float) getWidth();
            switch (owner.empty)
            {
                case Empty::off:
                    g.setColour (colours::textSoft);
                    g.setFont (semiBold (14.0f));
                    g.drawText (tr ("VOCAL CHECK ist aus", "VOCAL CHECK is off"), juce::Rectangle<float> (0.0f, 36.0f, w, 22.0f),
                                juce::Justification::centred, false);
                    break;
                case Empty::collecting:
                {
                    // Empty state: a clear sentence in the size of the other empty states, the progress below
                    g.setColour (colours::textSoft);
                    g.setFont (semiBold (14.0f));
                    g.drawFittedText (tr ("Spiel die Vocal ab – Vocal Check hört zu.", "Play the vocal – Vocal Check is listening."),
                                      juce::Rectangle<float> (8.0f, 20.0f, w - 16.0f, 22.0f).toNearestInt(), juce::Justification::centred, 1, 0.85f);
                    const auto bar = juce::Rectangle<float> (w * 0.5f - 120.0f, 54.0f, 240.0f, 4.0f);
                    g.setColour (colours::control);
                    g.fillRoundedRectangle (bar, 2.0f);
                    const float share = juce::jlimit (0.0f, 1.0f, owner.collected / dsp::VocalAnalyzer::minVoicedSeconds);
                    g.setColour (checkColour());
                    g.fillRoundedRectangle (bar.withWidth (std::max (4.0f, bar.getWidth() * share)), 2.0f);
                    g.setColour (colours::textMuted);
                    g.setFont (font (typesize::small));
                    g.drawText (i18n::format (tr ("{0} / {1} s Stimme", "{0} / {1} s of voice"),
                                              { seconds (std::min (owner.collected, dsp::VocalAnalyzer::minVoicedSeconds)),
                                                juce::String (juce::roundToInt (dsp::VocalAnalyzer::minVoicedSeconds)) }),
                                juce::Rectangle<float> (0.0f, 64.0f, w, 18.0f), juce::Justification::centred, false);
                    break;
                }
                case Empty::clean:
                    g.setColour (colours::ok);
                    g.setFont (semiBold (14.0f));
                    g.drawText (tr ("Keine Auffälligkeiten", "No issues found"), juce::Rectangle<float> (0.0f, 30.0f, w, 22.0f),
                                juce::Justification::centred, false);
                    break;
                case Empty::none:
                    break;
            }
            if (owner.ignoredToggle.isVisible() && ! owner.tiles.empty())
            {
                g.setColour (colours::lineSoft);
                g.fillRect (juce::Rectangle<float> (4.0f, (float) owner.ignoredToggle.getY() - 6.0f, w - 8.0f, 1.0f));
            }
        }

    private:
        VocalCheckView& owner;
    };

    //==========================================================================
    VocalCheckView::VocalCheckView (VelvoxAudioProcessor& p)
        : proc (p),
          power (p.getState(), pid::checkOn, checkColour()),
          mode (p.getState(), pid::checkMode, { tr ("Laufend", "Continuous"), "Capture" }, checkColour()),
          list (std::make_unique<List> (*this))
    {
        // Captions only: the genre is stored as its index (check::Genre)
        const juce::String genreNames[] { "Party", "Pop", tr ("Akustik", "Acoustic") };
        const juce::String genreTips[] { tr ("Zielbereiche für Party / EDM (wie der Auto-Mix)", "Target ranges for party / EDM (like the auto mix)"),
                                         tr ("Zielbereiche für Pop", "Target ranges for pop"),
                                         tr ("Zielbereiche für akustische, ruhige Songs", "Target ranges for acoustic, quiet songs") };
        for (int i = 0; i < 3; ++i)
        {
            genres[(size_t) i] = std::make_unique<CheckButton> (genreNames[i], CheckButton::Style::segment);
            auto& b = *genres[(size_t) i];
            b.setClickingTogglesState (false);
            b.setTooltip (genreTips[i]);
            b.onClick = [this, i]
            {
                proc.getVocalCheck().setGenre ((check::Genre) i);
                updateHeader();
                afterClick();
            };
            addAndMakeVisible (b);
        }

        power.setTooltip (tr ("Vocal Check ein / aus", "Vocal Check on / off"));
        startStop.setTooltip (tr ("Capture: analysiert alles zwischen Start und Stop", "Capture: analyzes everything between Start and Stop"));
        reset.setTooltip (tr ("Alles bisher Analysierte vergessen", "Forget everything analyzed so far"));
        applyAll.setTooltip (tr ("Alle Vorschläge auf einmal anwenden (ein Rückgängig-Schritt)", "Apply all suggestions at once (one undo step)"));
        for (auto* c : std::initializer_list<juce::Component*> { &power, &mode, &startStop, &reset, &applyAll, &viewport })
            addAndMakeVisible (*c);

        startStop.onClick = [this]
        {
            auto& a = proc.getAnalyzer();
            if (a.isCapturing()) a.stopCapture(); else a.startCapture();
            proc.refreshAnalysis();
            refreshNow();
        };
        reset.onClick = [this]
        {
            proc.getAnalyzer().clear();
            proc.refreshAnalysis();
            refreshNow();
        };
        applyAll.onClick = [this]
        {
            proc.getVocalCheck().applyAll();
            afterClick();
        };
        ignoredToggle.onClick = [this] { setIgnoredExpanded (! ignoredExpanded); };
        restoreAll.onClick = [this]
        {
            proc.getVocalCheck().restoreAll();
            afterClick();
        };
        enable.onClick = [this]
        {
            if (auto* param = proc.getState().getParameter (pid::checkOn))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (1.0f);
                param->endChangeGesture();
            }
            refreshNow();
        };

        for (auto* b : { &ignoredToggle, &restoreAll, &enable })
            list->addChildComponent (*b);
        viewport.setViewedComponent (list.get(), false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (8);
        updateHeader();
        rebuildList();
    }

    VocalCheckView::~VocalCheckView()
    {
        auto& animator = juce::Desktop::getInstance().getAnimator();
        for (auto& t : tiles)
            animator.cancelAnimation (t.get(), false);
        for (auto& t : ignoredRows)
            animator.cancelAnimation (t.get(), false);
        viewport.setViewedComponent (nullptr, false);
    }

    VocalCheckView::Tile* VocalCheckView::getTileAt (int index) const noexcept
    {
        return juce::isPositiveAndBelow (index, (int) tiles.size()) ? tiles[(size_t) index].get() : nullptr;
    }

    VocalCheckView::Tile* VocalCheckView::getTile (const juce::String& id) const noexcept
    {
        for (auto& t : tiles)
            if (t->getId() == id)
                return t.get();
        return nullptr;
    }

    VocalCheckView::Tile* VocalCheckView::getIgnoredRow (const juce::String& id) const noexcept
    {
        for (auto& t : ignoredRows)
            if (t->getId() == id)
                return t.get();
        return nullptr;
    }

    juce::String VocalCheckView::getScoreText() const
    {
        return score < 0 ? dash : juce::String (score);
    }

    void VocalCheckView::setIgnoredExpanded (bool expanded)
    {
        if (expanded == ignoredExpanded)
            return;
        ignoredExpanded = expanded;
        refreshNow();
    }

    void VocalCheckView::afterClick()
    {
        // After the click has finished: the list may delete the tile whose button was clicked
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<VocalCheckView> (this)]
        {
            if (safe != nullptr)
                safe->refreshNow();
        });
    }

    //==========================================================================
    void VocalCheckView::refresh()
    {
        if (++tick % 3 == 0)
            updateHeader();
        if (tick % 6 == 0 && listKey() != lastKey)
            rebuildList();
    }

    void VocalCheckView::refreshNow()
    {
        updateHeader();
        rebuildList();
    }

    void VocalCheckView::updateHeader()
    {
        auto& a = proc.getAnalyzer();
        const auto& r = proc.getLatestAnalysis();
        const bool on = proc.getState().getRawParameterValue (pid::checkOn)->load() > 0.5f;
        const bool capture = a.getMode() == dsp::VocalAnalyzer::Mode::capture;

        const int newScore = on ? proc.getVocalCheck().score() : -1;
        juce::String text;
        if (! on)
            text = tr ("Aus", "Off");
        else if (! r.enoughMaterial)
            text = tr ("Singe/spiele ab, um zu analysieren", "Sing or play back to analyze");
        else if (capture)
            text = i18n::format (a.isCapturing() ? tr ("Capture läuft · {0} s Stimme", "Capturing · {0} s of voice")
                                                 : tr ("Capture · {0} s Stimme", "Capture · {0} s of voice"),
                                 { seconds (r.voicedSeconds) });
        else
            text = i18n::format (tr ("Laufend · {0} s Stimme", "Continuous · {0} s of voice"), { seconds (r.voicedSeconds) });

        if (newScore != score || text != status)
        {
            score = newScore;
            status = text;
            repaint (0, 0, getWidth(), headerH);
        }

        startStop.setEnabled (on && capture);
        startStop.setButtonText (a.isCapturing() ? "Stop" : "Start");
        reset.setEnabled (on);
        const auto genre = (int) proc.getVocalCheck().getGenre();
        for (int i = 0; i < 3; ++i)
            genres[(size_t) i]->setToggleState (i == genre, juce::dontSendNotification);
    }

    juce::String VocalCheckView::listKey() const
    {
        auto& vc = proc.getVocalCheck();
        const auto& r = proc.getLatestAnalysis();
        const bool on = proc.getState().getRawParameterValue (pid::checkOn)->load() > 0.5f;
        juce::String key;
        key << (on ? "1" : "0") << (r.enoughMaterial ? "1" : "0") << (ignoredExpanded ? "1" : "0") << "|";
        if (on && ! r.enoughMaterial)
            key << juce::roundToInt (r.voicedSeconds * 10.0f);
        if (on)
        {
            for (auto* f : vc.visibleFindings())
                key << "|" << f->id << "~" << f->problem << "~" << f->fix << "~" << (int) f->level
                    << (vc.canApply (f->id) ? "a" : "-") << (vc.wasApplied (f->id) ? "d" : "-");
            key << "#";
            for (auto* f : vc.ignoredFindings())
                key << "|" << f->id << "~" << f->problem;
        }
        return key;
    }

    void VocalCheckView::rebuildList()
    {
        lastKey = listKey();
        auto& vc = proc.getVocalCheck();
        const auto& r = proc.getLatestAnalysis();
        const bool on = proc.getState().getRawParameterValue (pid::checkOn)->load() > 0.5f;
        auto& animator = juce::Desktop::getInstance().getAnimator();

        const auto visible = on ? vc.visibleFindings() : std::vector<const dsp::Finding*> {};
        const auto ignored = on ? vc.ignoredFindings() : std::vector<const dsp::Finding*> {};

        // Keep the tiles of findings that stay (by id), create the new ones, fade out the rest
        const auto sync = [&] (std::vector<std::unique_ptr<Tile>>& current, const std::vector<const dsp::Finding*>& wanted, bool ignoredRow)
        {
            std::vector<std::unique_ptr<Tile>> next;
            for (auto* f : wanted)
            {
                std::unique_ptr<Tile> tile;
                for (auto& t : current)
                    if (t != nullptr && t->getId() == f->id)
                        tile = std::move (t);
                if (tile == nullptr)
                {
                    tile = std::make_unique<Tile> (f->id, ignoredRow);
                    const auto id = f->id;
                    tile->applyButton.onClick = [this, id] { proc.getVocalCheck().apply (id); afterClick(); };
                    tile->applyButton.tooltipFn = [this, id]
                    {
                        return checkPreviewText (proc, proc.getVocalCheck().preview (id));
                    };
                    tile->ignoreButton.onClick = [this, id] { proc.getVocalCheck().ignore (id); afterClick(); };
                    tile->restoreButton.onClick = [this, id] { proc.getVocalCheck().restore (id); afterClick(); };
                    if (animate && isShowing())
                        tile->setAlpha (0.0f);
                    list->addAndMakeVisible (*tile);
                }
                const bool can = ! ignoredRow && vc.canApply (f->id);
                const bool done = ! ignoredRow && vc.wasApplied (f->id);
                tile->set (i18n::show (f->problem), i18n::show (f->fix), checkSeverityColour (f->level), can, done,
                           i18n::show (f->detail) + (f->why.isNotEmpty() ? "\n" + i18n::show (f->why) : juce::String()));
                tile->setTooltip (tile->details);
                next.push_back (std::move (tile));
            }
            for (auto& t : current)
                if (t != nullptr)
                {
                    animator.cancelAnimation (t.get(), false);
                    if (animate && isShowing())
                        animator.fadeOut (t.get(), 160);
                    list->removeChildComponent (t.get());
                }
            current = std::move (next);
        };
        sync (tiles, visible, false);
        sync (ignoredRows, ignoredExpanded ? ignored : std::vector<const dsp::Finding*> {}, true);
        if (ignored.empty())
            ignoredExpanded = false;

        int applicable = 0;
        for (auto* f : visible)
            applicable += vc.canApply (f->id) ? 1 : 0;
        applyAll.setButtonText (applicable > 0 ? i18n::format (tr ("Alles anwenden ({0})", "Apply all ({0})"), { juce::String (applicable) })
                                               : tr ("Alles anwenden", "Apply all"));
        applyAll.setEnabled (applicable > 0);

        ignoredToggle.setButtonText (i18n::format (tr ("{0} ignoriert", "{0} ignored"), { juce::String ((int) ignored.size()) }));
        ignoredToggle.chevron = ignoredExpanded ? 2 : 1;
        ignoredToggle.repaint();
        ignoredToggle.setVisible (! ignored.empty());
        restoreAll.setVisible (ignoredExpanded && ! ignored.empty());

        empty = ! on ? Empty::off : ! r.enoughMaterial ? Empty::collecting : visible.empty() ? Empty::clean : Empty::none;
        collected = r.voicedSeconds;
        enable.setVisible (empty == Empty::off);

        layoutList();
        list->repaint();
    }

    void VocalCheckView::layoutList()
    {
        const int w = std::max (300, viewport.getWidth() - viewport.getScrollBarThickness() - 2);
        auto& animator = juce::Desktop::getInstance().getAnimator();
        const bool anim = animate && isShowing();
        const auto place = [&] (Tile& t, juce::Rectangle<int> b)
        {
            if (anim && (t.getBounds() != b || t.getAlpha() < 1.0f))
            {
                if (t.getBounds().isEmpty())
                    t.setBounds (b.translated (0, 6));
                animator.animateComponent (&t, b, 1.0f, 180, false, 1.0, 0.0);
            }
            else
            {
                animator.cancelAnimation (&t, false);
                t.setBounds (b);
                t.setAlpha (1.0f);
            }
        };

        int y = empty == Empty::none ? 2 : empty == Empty::clean ? 76 : 92;
        if (empty == Empty::off)
            enable.setBounds (w / 2 - 66, 70, 132, 30);
        for (auto& t : tiles)
        {
            place (*t, { 0, y, w, Tile::height });
            y += Tile::height + tileGap;
        }
        if (ignoredToggle.isVisible())
        {
            y += tiles.empty() ? 0 : 6;
            ignoredToggle.setBounds (6, y, 150, 26);
            restoreAll.setBounds (w - 170, y, 164, 26);
            y += 32;
            for (auto& t : ignoredRows)
            {
                place (*t, { 0, y, w, Tile::ignoredHeight });
                y += Tile::ignoredHeight + 6;
            }
        }
        list->setSize (w, std::max (y + 4, viewport.getHeight()));
    }

    void VocalCheckView::resized()
    {
        const int w = getWidth();
        const int x = pad + ringSize + 20;
        power.setBounds (x, 16, 22, 22);
        mode.setBounds (x, 70, 168, 26);
        startStop.setBounds (x + 176, 70, 70, 26);
        reset.setBounds (x + 252, 70, 70, 26);

        const int rx = w - pad - rightW;
        for (int i = 0; i < 3; ++i)
            genres[(size_t) i]->setBounds (rx + 2 + i * (rightW - 4) / 3, 32, (rightW - 4) / 3, 24);
        applyAll.setBounds (rx, 68, rightW, 32);

        viewport.setBounds (pad - 6, headerH + 8, w - 2 * (pad - 6), getHeight() - headerH - 8 - 10);
        layoutList();
    }

    void VocalCheckView::paint (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (b, radius::card);
        g.setColour (colours::border);
        g.drawRoundedRectangle (b.reduced (0.5f), radius::card, 1.0f);

        drawScoreRing (g, { (float) pad, 12.0f, (float) ringSize, (float) ringSize }, score, 7.0f, true);

        const float x = (float) (pad + ringSize + 20);
        g.setColour (colours::textStrong);
        g.setFont (semiBold (15.0f, 2.5f / 15.0f));
        g.drawText ("VOCAL CHECK", juce::Rectangle<float> (x + 30.0f, 14.0f, 260.0f, 26.0f), juce::Justification::centredLeft, false);
        g.setColour (colours::textMuted);
        g.setFont (font (12.5f));
        g.drawText (status, juce::Rectangle<float> (x, 42.0f, 420.0f, 18.0f), juce::Justification::centredLeft, true);

        // Genre segment bar
        const auto rx = (float) (getWidth() - pad - rightW);
        g.setColour (colours::textFaint);
        g.setFont (semiBold (9.5f, 0.2f));
        g.drawText ("GENRE", juce::Rectangle<float> (rx, 14.0f, (float) rightW, 12.0f), juce::Justification::centredLeft, false);
        g.setColour (colours::control);
        g.fillRoundedRectangle ({ rx, 30.0f, (float) rightW, 28.0f }, 7.0f);

        g.setColour (colours::lineSoft);
        g.fillRect (juce::Rectangle<float> ((float) pad, (float) headerH, b.getWidth() - 2.0f * pad, 1.0f));
    }

    //==========================================================================
    CheckSummary::CheckSummary (VelvoxAudioProcessor& p) : proc (p)
    {
        addAndMakeVisible (open);
        open.setTooltip (tr ("Befunde, Ignorieren und Anwenden im Tab VOCAL CHECK", "Findings, Ignore and Apply in the VOCAL CHECK tab"));
        open.onClick = [this] { if (onOpenTab) onOpenTab(); };
        refresh();
    }

    juce::String CheckSummary::getScoreText() const { return score < 0 ? dash : juce::String (score); }

    void CheckSummary::refresh()
    {
        const bool on = proc.getState().getRawParameterValue (pid::checkOn)->load() > 0.5f;
        const auto& r = proc.getLatestAnalysis();
        const int newScore = on ? proc.getVocalCheck().score() : -1;
        const int n = (int) proc.getVocalCheck().visibleFindings().size();
        const juce::String text = ! on ? tr ("Aus", "Off")
                                : ! r.enoughMaterial ? tr ("Sammle Stimme …", "Collecting voice …")
                                : n == 0 ? tr ("Keine Auffälligkeiten", "No issues found")
                                : i18n::format (n == 1 ? tr ("{0} Befund", "{0} finding") : tr ("{0} Befunde", "{0} findings"), { juce::String (n) });
        if (newScore != score || text != count)
        {
            score = newScore;
            count = text;
            repaint();
        }
    }

    void CheckSummary::resized()
    {
        const int ring = std::min (64, getHeight() - 8);
        open.setBounds (ring + 18, getHeight() / 2 + 2, std::min (170, getWidth() - ring - 22), 26);
    }

    void CheckSummary::paint (juce::Graphics& g)
    {
        const float ring = (float) std::min (64, getHeight() - 8);
        drawScoreRing (g, { 4.0f, (getHeight() - ring) * 0.5f, ring, ring }, score, 5.0f, false);
        g.setColour (colours::text);
        g.setFont (semiBold (13.0f));
        g.drawText (count, juce::Rectangle<float> (ring + 18.0f, getHeight() * 0.5f - 24.0f, (float) getWidth() - ring - 20.0f, 20.0f),
                    juce::Justification::centredLeft, true);
    }
}
