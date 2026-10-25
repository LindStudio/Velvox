#include "VoicesPanels.h"
#include "../PluginProcessor.h"
#include "HelpContent.h"

namespace velvox::ui
{
    using dsp::ModuleId;
    using Field = doubleredit::Field;

    namespace
    {
        juce::String de (const char* utf8) { return juce::String::fromUTF8 (utf8); }

        juce::String idFor (const char* base, int instance)
        {
            return instance > 0 ? dsp::instanceParamId (base, instance) : juce::String (base);
        }

        juce::RangedAudioParameter* paramFor (VelvoxAudioProcessor& proc, const char* base, int instance)
        {
            auto& state = proc.getState();
            if (instance > 0)
                if (auto* p = state.getParameter (dsp::instanceParamId (base, instance)))
                    return p;
            return state.getParameter (base);
        }

        float realOf (const juce::RangedAudioParameter* p) { return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f; }
        float realValue (VelvoxAudioProcessor& proc, const char* base, int instance) { return realOf (paramFor (proc, base, instance)); }
        float defaultOf (const juce::RangedAudioParameter* p) { return p != nullptr ? p->convertFrom0to1 (p->getDefaultValue()) : 0.0f; }

        void write (juce::RangedAudioParameter* p, float real)
        {
            if (p == nullptr)
                return;
            const float v = p->convertTo0to1 (real);
            if (std::abs (v - p->getValue()) > 1.0e-7f)
                p->setValueNotifyingHost (v);
        }

        const char* baseFor (int v, Field f) noexcept
        {
            const auto i = (size_t) juce::jlimit (0, 3, v);
            switch (f)
            {
                case Field::on:     return pid::doublerVoiceOn[i];
                case Field::delay:  return pid::doublerVoiceDelay[i];
                case Field::detune: return pid::doublerVoiceDetune[i];
                case Field::gain:   return pid::doublerVoiceGain[i];
                case Field::pan:    break;
            }
            return pid::doublerVoicePan[i];
        }

        constexpr Field allFields[] { Field::on, Field::delay, Field::detune, Field::gain, Field::pan };

        const char* fieldName (Field f) noexcept
        {
            switch (f)
            {
                case Field::on:     return "On";
                case Field::delay:  return "Delay";
                case Field::detune: return "Detune";
                case Field::gain:   return "Gain";
                case Field::pan:    break;
            }
            return "Pan";
        }

        // Live pictures repaint at 30 Hz (the editor frame runs at 60)
        constexpr double liveInterval = 1.0 / 30.0 - 1.0e-3;

        float wheelNotches (const juce::MouseWheelDetails& w) noexcept
        {
            const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : w.deltaY;
            if (d == 0.0f)
                return 0.0f;
            return (w.isReversed ? -d : d) > 0.0f ? 1.0f : -1.0f;
        }

        bool isFine (const juce::ModifierKeys& m) noexcept { return m.isShiftDown() || m.isCtrlDown() || m.isCommandDown(); }

        void addPanelItems (juce::Component& c, juce::PopupMenu& menu)
        {
            if (auto* host = c.findParentComponentOfClass<gestures::PanelMenuHost>())
            {
                menu.addSeparator();
                host->addPanelItems (menu);
            }
        }
    }

    //==========================================================================
    namespace doubleredit
    {
        int mode (VelvoxAudioProcessor& proc, int instance)
        {
            return juce::jlimit (0, 2, juce::roundToInt (realValue (proc, pid::doublerVoices, instance)));
        }

        int voiceCount (VelvoxAudioProcessor& proc, int instance) { return mode (proc, instance) == 2 ? 4 : 2; }

        juce::String paramId (int instance, int v, Field f) { return idFor (baseFor (v, f), instance); }

        juce::RangedAudioParameter* param (VelvoxAudioProcessor& proc, int instance, int v, Field f)
        {
            return paramFor (proc, baseFor (v, f), instance);
        }

        VoiceValues voice (VelvoxAudioProcessor& proc, int instance, int v)
        {
            v = juce::jlimit (0, 3, v);
            VoiceValues out;
            if (mode (proc, instance) == 0 && v < 2)
            {
                // Classic: the voices the 0.2 controls make (Width is applied by the engine on top, as in 2 / 4)
                dsp::DoublerSettings s;
                s.mode = dsp::DoublerMode::classic;
                s.width = 1.0f;
                s.delayMs = realValue (proc, pid::doublerDelay, instance);
                const auto r = dsp::DoublerModule::resolveVoices (s);
                const auto& rv = r.v[(size_t) v];
                out.on = true;
                out.delayMs = rv.delayMs;
                out.detuneCents = 0.0f;
                out.gainDb = juce::Decibels::gainToDecibels (rv.gain);
                out.panPercent = rv.pan * 100.0f;
                return out;
            }
            out.on = realOf (param (proc, instance, v, Field::on)) > 0.5f;
            out.delayMs = realOf (param (proc, instance, v, Field::delay));
            out.detuneCents = realOf (param (proc, instance, v, Field::detune));
            out.gainDb = realOf (param (proc, instance, v, Field::gain));
            out.panPercent = realOf (param (proc, instance, v, Field::pan));
            return out;
        }

        float value (const VoiceValues& vv, Field f) noexcept
        {
            switch (f)
            {
                case Field::on:     return vv.on ? 1.0f : 0.0f;
                case Field::delay:  return vv.delayMs;
                case Field::detune: return vv.detuneCents;
                case Field::gain:   return vv.gainDb;
                case Field::pan:    break;
            }
            return vv.panPercent;
        }

        juce::String valueText (Field f, float v)
        {
            switch (f)
            {
                case Field::on:     return v > 0.5f ? "On" : "Off";
                case Field::delay:  return juce::String (v, 1) + " ms";
                case Field::detune: return std::abs (v) < 0.05f ? juce::String ("0.0 ct") : (v > 0.0f ? "+" : "") + juce::String (v, 1) + " ct";
                case Field::gain:   return std::abs (v) < 0.05f ? juce::String ("0.0 dB") : (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
                case Field::pan:    break;
            }
            const int r = juce::roundToInt (v);
            return r == 0 ? juce::String ("C") : (r < 0 ? "L" + juce::String (-r) : "R" + juce::String (r));
        }

        std::optional<float> parseValue (VelvoxAudioProcessor& proc, int instance, int v, Field f, const juce::String& text)
        {
            auto* p = param (proc, instance, v, f);
            if (p == nullptr)
                return std::nullopt;
            const auto t = text.trim();
            if (f == Field::pan && t.isNotEmpty())
            {
                const auto side = juce::CharacterFunctions::toUpperCase (t[0]);
                if (side == 'L' || side == 'R' || side == 'C')
                {
                    const auto rest = t.substring (1).trim().trimCharactersAtEnd ("%").trim();
                    if (side == 'C')
                        return rest.isEmpty() ? std::optional<float> (0.0f) : std::nullopt;
                    if (rest.isNotEmpty() && ! rest.containsOnly ("0123456789.,"))
                        return std::nullopt;
                    const float amount = rest.isEmpty() ? 100.0f : rest.replaceCharacter (',', '.').getFloatValue();
                    return juce::jlimit (-100.0f, 100.0f, side == 'L' ? -amount : amount);
                }
            }
            return parseValueText (t, *p);
        }

        void leaveClassic (VelvoxAudioProcessor& proc, int instance)
        {
            if (mode (proc, instance) != 0)
                return;
            const std::array<VoiceValues, 2> classic { voice (proc, instance, 0), voice (proc, instance, 1) };
            for (int v = 0; v < 2; ++v)
                for (auto f : allFields)
                    if (auto* p = param (proc, instance, v, f))
                        gestures::setReal (*p, value (classic[(size_t) v], f));
            // The Classic wobble (Detune) becomes the Mod Depth of the voice modes
            if (auto* depth = paramFor (proc, pid::doublerModDepth, instance))
                gestures::setReal (*depth, juce::jlimit (0.0f, dsp::DoublerModule::maxModCents, realValue (proc, pid::doublerDetune, instance)));
            if (auto* voices = paramFor (proc, pid::doublerVoices, instance))
                gestures::setReal (*voices, 1.0f);
        }

        Edit::Edit (VelvoxAudioProcessor& p, int k, int v, std::initializer_list<Field> fields, const juce::String& name)
            : proc (p), instance (k), voiceIndex (juce::jlimit (0, 3, v))
        {
            proc.getUndo().beginTransaction (name);
            leaveClassic (proc, instance);
            for (auto f : fields)
                if (auto* prm = param (proc, instance, voiceIndex, f))
                {
                    prm->beginChangeGesture();
                    gestures.push_back (prm);
                }
        }

        Edit::~Edit()
        {
            for (auto* prm : gestures)
                prm->endChangeGesture();
            proc.getUndo().endTransaction();
        }

        void Edit::set (Field f, float realValue)
        {
            write (param (proc, instance, voiceIndex, f), realValue);
        }

        namespace
        {
            juce::String stepName (int v, const char* what) { return "Doubler: Stimme " + juce::String (v + 1) + " " + de (what); }
        }

        void setValue (VelvoxAudioProcessor& proc, int instance, int v, Field f, float realValue)
        {
            Edit e (proc, instance, v, { f }, stepName (v, fieldName (f)));
            e.set (f, realValue);
        }

        void toggleOn (VelvoxAudioProcessor& proc, int instance, int v)
        {
            const bool wasOn = voice (proc, instance, v).on;
            Edit e (proc, instance, v, { Field::on }, stepName (v, wasOn ? "aus" : "an"));
            e.set (Field::on, wasOn ? 0.0f : 1.0f);
        }

        void resetVoice (VelvoxAudioProcessor& proc, int instance, int v)
        {
            Edit e (proc, instance, v, { Field::on, Field::delay, Field::detune, Field::gain, Field::pan }, stepName (v, "zurücksetzen"));
            for (auto f : allFields)
                e.set (f, defaultOf (param (proc, instance, v, f)));
        }

        void resetPosition (VelvoxAudioProcessor& proc, int instance, int v)
        {
            Edit e (proc, instance, v, { Field::pan, Field::gain }, stepName (v, "Position zurücksetzen"));
            for (auto f : { Field::pan, Field::gain })
                e.set (f, defaultOf (param (proc, instance, v, f)));
        }
    }

    //==========================================================================
    // DOUBLER: stage strip and voice rows

    juce::Colour DoublerStage::voiceColour (int v) noexcept
    {
        const auto base = moduleColour (ModuleId::doubler);
        const juce::Colour tint[4] { colours::teal, colours::violet, colours::cyan, colours::pink };
        return base.interpolatedWith (tint[juce::jlimit (0, 3, v)], 0.28f);
    }

    namespace
    {
        // Columns of a voice row: number badge, on switch, then the four value fields
        struct RowColumns
        {
            juce::Rectangle<int> badge, on;
            std::array<juce::Rectangle<int>, 4> fields;   // delay, detune, gain, pan
        };

        RowColumns rowColumns (juce::Rectangle<int> r) noexcept
        {
            RowColumns c;
            c.badge = r.removeFromLeft (22);
            r.removeFromLeft (8);
            c.on = r.removeFromLeft (32);
            r.removeFromLeft (12);
            const int gap = 8;
            const int w = std::max (40, (r.getWidth() - 3 * gap) / 4);
            for (auto& f : c.fields)
            {
                f = r.removeFromLeft (w);
                r.removeFromLeft (gap);
            }
            return c;
        }

        constexpr const char* voiceNumbers[4] { "1", "2", "3", "4" };

        constexpr int rowHeight = 20, rowGap = 3, captionHeight = 14, rowsMaxWidth = 520;
    }

    //--------------------------------------------------------------------------
    // A value field of a voice row: drag (220 px = full range; Shift / Ctrl fine), click = type a value,
    // double-click / Alt+click = default, wheel = step, right-click = menu. The on switch toggles on click.
    class DoublerStage::ValueField final : public juce::Component, private juce::Timer
    {
    public:
        ValueField (DoublerStage& s, int v, Field f) : stage (s), voice (v), field (f)
        {
            getProperties().set ("paramId", doubleredit::paramId (stage.instance, voice, field));
            setMouseCursor (field == Field::on ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::UpDownResizeCursor);
            const auto* spec = findParameterSpec (doubleredit::paramId (0, voice, field));
            description = spec != nullptr ? spec->description : juce::String();
            info = std::make_unique<HoverInfo> (*this,
                                                [this] { return "Stimme " + juce::String (voice + 1) + juce::String::fromUTF8 (" · ") + fieldName (field); },
                                                [this] { return text; },
                                                [this] { return description; });
        }

        ~ValueField() override { endEdit(); }

        void refresh (const doubleredit::VoiceValues& vv, bool linked)
        {
            const float v = doubleredit::value (vv, field);
            if (v == shown && vv.on == voiceOn && linked == isLinked && text.isNotEmpty())
                return;
            shown = v;
            voiceOn = vv.on;
            isLinked = linked;
            text = doubleredit::valueText (field, v);
            info->refresh();
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto hue = DoublerStage::voiceColour (voice);
            auto r = getLocalBounds().toFloat();
            if (field == Field::on)
            {
                const auto pill = r.withSizeKeepingCentre (26.0f, 14.0f);
                g.setColour (voiceOn ? hue.withAlpha (0.85f) : colours::switchOff);
                g.fillRoundedRectangle (pill, 7.0f);
                const float kx = voiceOn ? pill.getRight() - 7.0f : pill.getX() + 7.0f;
                g.setColour (voiceOn ? juce::Colours::white : colours::textMuted);
                g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ kx, pill.getCentreY() }));
                return;
            }
            g.setColour (colours::control);
            g.fillRoundedRectangle (r.reduced (0.5f), 5.0f);
            g.setColour (hover || editor != nullptr ? hue.withAlpha (0.6f) : colours::borderInset.brighter (0.25f));
            g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
            g.setFont (monoFont (11.0f));
            g.setColour (! voiceOn ? colours::textFaint : (isLinked ? colours::textMuted : colours::text));
            g.drawText (text, r, juce::Justification::centred, false);
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            stopTimer();
            ignore = false;
            moved = false;
            if (e.mods.isPopupMenu())
            {
                ignore = true;
                showMenu();
                return;
            }
            if (e.mods.isAltDown())
            {
                ignore = true;
                resetField();
                return;
            }
            lastPos = e.position;
            const auto* p = param();
            proportion = p != nullptr ? p->convertTo0to1 (shown) : 0.0f;
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (ignore || field == Field::on || e.mods.isPopupMenu())
                return;
            const float diff = (e.position.x - lastPos.x) + (lastPos.y - e.position.y);
            lastPos = e.position;
            if (diff == 0.0f)
                return;
            auto* p = param();
            if (p == nullptr)
                return;
            if (edit == nullptr)
                edit = std::make_unique<doubleredit::Edit> (stage.proc, stage.instance, voice, std::initializer_list<Field> { field },
                                                            "Doubler: Stimme " + juce::String (voice + 1) + " " + fieldName (field));
            moved = true;
            const float pixels = (float) FineSlider::normalPixels * (isFine (e.mods) ? (float) FineSlider::fineFactor : 1.0f);
            proportion = juce::jlimit (0.0f, 1.0f, proportion + diff / pixels);
            edit->set (field, p->convertFrom0to1 (proportion));
            stage.readParameters();
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (edit != nullptr)
            {
                endEdit();
                return;
            }
            if (ignore || moved || e.mods.isPopupMenu() || e.getNumberOfClicks() > 1)
                return;
            if (field == Field::on)
            {
                doubleredit::toggleOn (stage.proc, stage.instance, voice);
                stage.readParameters();
                return;
            }
            // A single click types a value - after the double-click time, so a double-click stays a reset
            startTimer (juce::MouseEvent::getDoubleClickTimeout() + 20);
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            stopTimer();
            if (e.mods.isPopupMenu() || field == Field::on)
                return;
            endEdit();
            ignore = true;
            resetField();
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            const float n = wheelNotches (w);
            if (n == 0.0f || field == Field::on)
                return;
            float step = 1.0f;
            switch (field)
            {
                case Field::delay: step = 0.5f; break;
                case Field::gain:  step = 0.5f; break;
                case Field::pan:   step = 5.0f; break;
                default:           break;
            }
            doubleredit::setValue (stage.proc, stage.instance, voice, field, shown + n * step * (isFine (e.mods) ? 0.1f : 1.0f));
            stage.readParameters();
        }

        void showEditor()
        {
            if (editor != nullptr)
                return;
            editor = std::make_unique<juce::TextEditor>();
            editor->setFont (monoFont (11.0f));
            editor->setJustification (juce::Justification::centred);
            editor->setColour (juce::TextEditor::backgroundColourId, colours::raisedHi);
            editor->setColour (juce::TextEditor::outlineColourId, DoublerStage::voiceColour (voice));
            editor->setColour (juce::TextEditor::textColourId, colours::text);
            editor->setText (text, false);
            editor->setBounds (getLocalBounds());
            addAndMakeVisible (*editor);
            juce::Component::SafePointer<ValueField> safe (this);
            editor->onReturnKey = [safe] { if (safe != nullptr) safe->commitEditor (true); };
            editor->onEscapeKey = [safe] { if (safe != nullptr) safe->commitEditor (false); };
            editor->onFocusLost = [safe] { if (safe != nullptr) safe->commitEditor (true); };
            editor->selectAll();
            editor->grabKeyboardFocus();
            repaint();
        }

        Field getField() const noexcept { return field; }

    private:
        juce::RangedAudioParameter* param() const { return doubleredit::param (stage.proc, stage.instance, voice, field); }

        void timerCallback() override
        {
            stopTimer();
            if (isShowing())
                showEditor();
        }

        void commitEditor (bool apply)
        {
            if (editor == nullptr)
                return;
            const auto typed = editor->getText();
            // The editor is deleted after its own callback has returned
            juce::Component::SafePointer<ValueField> safe (this);
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->editor.reset(); safe->repaint(); } });
            editor->onFocusLost = nullptr;
            editor->setVisible (false);
            if (apply)
                if (const auto v = doubleredit::parseValue (stage.proc, stage.instance, voice, field, typed))
                {
                    doubleredit::setValue (stage.proc, stage.instance, voice, field, *v);
                    stage.readParameters();
                }
        }

        void resetField()
        {
            doubleredit::setValue (stage.proc, stage.instance, voice, field, defaultOf (param()));
            stage.readParameters();
        }

        void endEdit()
        {
            if (edit == nullptr)
                return;
            edit.reset();
            stage.readParameters();
        }

        void showMenu()
        {
            juce::PopupMenu m;
            juce::Component::SafePointer<ValueField> safe (this);
            m.addItem (de ("Standardwert"), field != Field::on, false, [safe] { if (safe != nullptr) safe->resetField(); });
            m.addItem (de ("Wert eingeben …"), field != Field::on, false, [safe] { if (safe != nullptr) safe->showEditor(); });
            m.addSeparator();
            const auto voiceMenu = stage.buildMenu (voice);
            for (juce::PopupMenu::MenuItemIterator it (voiceMenu); it.next();)
                m.addItem (it.getItem());
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (190));
        }

        DoublerStage& stage;
        const int voice;
        const Field field;
        juce::String text, description;
        float shown = std::numeric_limits<float>::quiet_NaN(), proportion = 0.0f;
        bool voiceOn = true, isLinked = false, hover = false, ignore = false, moved = false;
        juce::Point<float> lastPos;
        std::unique_ptr<doubleredit::Edit> edit;
        std::unique_ptr<juce::TextEditor> editor;
        std::unique_ptr<HoverInfo> info;
    };

    //--------------------------------------------------------------------------
    // One voice row: number badge, on switch, delay / detune / gain / pan fields
    class DoublerStage::Row final : public juce::Component
    {
    public:
        Row (DoublerStage& s, int v) : voice (v)
        {
            for (auto f : allFields)
            {
                fields.push_back (std::make_unique<ValueField> (s, v, f));
                addAndMakeVisible (*fields.back());
            }
        }

        void resized() override
        {
            const auto c = rowColumns (getLocalBounds());
            badge = c.badge;
            fields[0]->setBounds (c.on);
            for (size_t i = 0; i < 4; ++i)
                fields[i + 1]->setBounds (c.fields[i]);
        }

        void refresh (const doubleredit::VoiceValues& vv, bool linked)
        {
            if (vv.on != voiceOn)
            {
                voiceOn = vv.on;
                repaint();
            }
            for (auto& f : fields)
                f->refresh (vv, linked);
        }

        void paint (juce::Graphics& g) override
        {
            const auto hue = DoublerStage::voiceColour (voice);
            const auto dot = badge.toFloat().withSizeKeepingCentre (17.0f, 17.0f);
            g.setColour (voiceOn ? hue : hue.withAlpha (0.25f));
            g.fillEllipse (dot);
            g.setFont (semiBold (10.0f));
            g.setColour (voiceOn ? colours::windowBg : colours::textMuted);
            g.drawText (voiceNumbers[voice], dot, juce::Justification::centred, false);
        }

        ValueField* field (Field f) noexcept
        {
            for (auto& vf : fields)
                if (vf->getField() == f)
                    return vf.get();
            return nullptr;
        }

    private:
        const int voice;
        std::vector<std::unique_ptr<ValueField>> fields;
        juce::Rectangle<int> badge;
        bool voiceOn = true;
    };

    //--------------------------------------------------------------------------
    // Invisible handle over a voice dot: cursor, hover card; events go to the stage
    class DoublerStage::Handle final : public juce::Component
    {
    public:
        Handle (DoublerStage& s, int v) : stage (s), voice (v)
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            info = std::make_unique<HoverInfo> (*this, [this] { return "Stimme " + juce::String (voice + 1); },
                                                [this] { return stage.cardValue (voice); },
                                                [] { return de ("Ziehen: Panorama und Pegel · Mausrad: Detune"); });
        }
        void refreshInfo() { info->refresh(); }

        void mouseEnter (const juce::MouseEvent& e) override { stage.mouseMove (e.getEventRelativeTo (&stage)); }
        void mouseMove (const juce::MouseEvent& e) override { stage.mouseMove (e.getEventRelativeTo (&stage)); }
        void mouseExit (const juce::MouseEvent& e) override { stage.mouseExit (e.getEventRelativeTo (&stage)); }
        void mouseDown (const juce::MouseEvent& e) override { stage.mouseDown (e.getEventRelativeTo (&stage)); }
        void mouseDrag (const juce::MouseEvent& e) override { stage.mouseDrag (e.getEventRelativeTo (&stage)); }
        void mouseUp (const juce::MouseEvent& e) override { stage.mouseUp (e.getEventRelativeTo (&stage)); }
        void mouseDoubleClick (const juce::MouseEvent& e) override { stage.mouseDoubleClick (e.getEventRelativeTo (&stage)); }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { stage.mouseWheelMove (e.getEventRelativeTo (&stage), w); }

    private:
        DoublerStage& stage;
        const int voice;
        std::unique_ptr<HoverInfo> info;
    };

    //--------------------------------------------------------------------------
    DoublerStage::DoublerStage (VelvoxAudioProcessor& p, int k) : proc (p), instance (k)
    {
        for (int v = 0; v < 4; ++v)
        {
            handles[(size_t) v] = std::make_unique<Handle> (*this, v);
            addChildComponent (*handles[(size_t) v]);
            rows[(size_t) v] = std::make_unique<Row> (*this, v);
            addChildComponent (*rows[(size_t) v]);
        }
        rowCaptions = { "DELAY", "DETUNE", "GAIN", "PAN" };
        width = realValue (proc, pid::doublerWidth, instance) * 0.01f;
        dryDb = realValue (proc, pid::doublerDry, instance);
        on = realValue (proc, pid::doublerOn, instance) > 0.5f;
        readParameters();
    }

    DoublerStage::~DoublerStage() { edit.reset(); }

    juce::Component& DoublerStage::getVoiceHandle (int v) noexcept { return *handles[(size_t) juce::jlimit (0, 3, v)]; }
    juce::Component& DoublerStage::getRow (int v) noexcept { return *rows[(size_t) juce::jlimit (0, 3, v)]; }
    juce::Component* DoublerStage::getField (int v, doubleredit::Field f) noexcept { return rows[(size_t) juce::jlimit (0, 3, v)]->field (f); }

    juce::String DoublerStage::cardValue (int v) const
    {
        const auto& vv = values[(size_t) juce::jlimit (0, 3, v)];
        if (! vv.on)
            return "Off";
        const juce::String dot = juce::String::fromUTF8 (" · ");
        return doubleredit::valueText (Field::delay, vv.delayMs) + dot + doubleredit::valueText (Field::detune, vv.detuneCents) + dot
             + doubleredit::valueText (Field::gain, vv.gainDb) + dot + doubleredit::valueText (Field::pan, vv.panPercent);
    }

    void DoublerStage::readParameters()
    {
        const int m = doubleredit::mode (proc, instance);
        const bool nowClassic = m == 0;
        const int n = m == 2 ? 4 : 2;
        for (int v = 0; v < 4; ++v)
        {
            auto vv = doubleredit::voice (proc, instance, v);
            if (v == dragVoice && edit != nullptr)
            {
                vv.panPercent = dragPan;
                vv.gainDb = dragGain;
            }
            values[(size_t) v] = vv;
        }
        if (n != shownVoices || nowClassic != classic)
        {
            shownVoices = n;
            classic = nowClassic;
            for (int v = 0; v < 4; ++v)
                rows[(size_t) v]->setVisible (v < n);
            resized();
        }
        for (int v = 0; v < n; ++v)
            rows[(size_t) v]->refresh (values[(size_t) v], classic);

        // Read-out: the dragged / hovered voice, else the mode's own values
        const int focus = dragVoice >= 0 ? dragVoice : hovered;
        juce::String text;
        if (focus >= 0 && focus < n)
            text = juce::String (focus + 1) + "   " + cardValue (focus);
        else if (classic)
            text = "CLASSIC   " + doubleredit::valueText (Field::delay, values[0].delayMs) + " / " + doubleredit::valueText (Field::delay, values[1].delayMs);
        if (text != readout)
        {
            readout = text;
            repaint();
        }
        layoutRows();
    }

    float DoublerStage::xForPan (float panPercent) const noexcept
    {
        return plot.getCentreX() + juce::jlimit (-100.0f, 100.0f, panPercent) * 0.01f * (plot.getWidth() * 0.5f - 16.0f);
    }

    float DoublerStage::yForGain (float gainDb) const noexcept
    {
        return juce::jmap (juce::jlimit (-24.0f, 6.0f, gainDb), -24.0f, 6.0f, plot.getBottom() - 12.0f, plot.getY() + 12.0f);
    }

    juce::Point<float> DoublerStage::voicePoint (int v) const noexcept
    {
        const auto& vv = values[(size_t) juce::jlimit (0, 3, v)];
        return { xForPan (vv.panPercent), yForGain (vv.gainDb) };
    }

    juce::Point<float> DoublerStage::leadPoint() const noexcept { return { plot.getCentreX(), yForGain (dryDb) }; }

    int DoublerStage::voiceAt (juce::Point<float> p) const noexcept
    {
        int best = -1;
        float bestD = 14.0f;
        for (int v = shownVoices; --v >= 0;)   // the later voice is drawn on top
        {
            const float d = voicePoint (v).getDistanceFrom (p);
            if (d < bestD)
            {
                bestD = d;
                best = v;
            }
        }
        return best;
    }

    void DoublerStage::resized()
    {
        auto b = getLocalBounds().reduced (16, 10);
        const int n = shownVoices;
        const int rowsH = captionHeight + n * rowHeight + (n - 1) * rowGap;
        auto rowsBox = b.removeFromBottom (rowsH);
        b.removeFromBottom (10);
        b.removeFromTop (18);
        plot = b.toFloat().withTrimmedLeft (22.0f);
        const int rw = std::min (rowsMaxWidth, rowsBox.getWidth());
        rowsArea = rowsBox.withSizeKeepingCentre (rw, rowsBox.getHeight());
        layoutRows();
    }

    void DoublerStage::layoutRows()
    {
        auto r = rowsArea.withTrimmedTop (captionHeight);
        for (int v = 0; v < 4; ++v)
        {
            const auto rowBounds = v < shownVoices ? r.removeFromTop (rowHeight) : juce::Rectangle<int>();
            r.removeFromTop (rowGap);
            if (rows[(size_t) v]->getBounds() != rowBounds)
                rows[(size_t) v]->setBounds (rowBounds);
        }
        for (int v = 0; v < 4; ++v)
        {
            auto& h = *handles[(size_t) v];
            const bool show = v < shownVoices && ! plot.isEmpty();
            h.setVisible (show);
            if (show)
            {
                const auto hb = juce::Rectangle<int> (26, 26).withCentre (voicePoint (v).roundToInt());
                if (h.getBounds() != hb)
                    h.setBounds (hb);
                h.refreshInfo();
            }
        }
    }

    void DoublerStage::update (const LiveData& d)
    {
        const auto inst = d.settings.instance (instance).doubler;
        bool changed = false;
        if (inst.width != width || inst.dryDb != dryDb || inst.enabled != on)
        {
            width = inst.width;
            dryDb = inst.dryDb;
            on = inst.enabled;
            changed = true;
        }
        if (auto wt = juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, width) * 100.0f)) + " %"; wt != widthText)
            widthText = std::move (wt);
        const auto key = values;
        const auto oldReadout = readout;
        const int oldN = shownVoices;
        readParameters();
        for (int v = 0; v < 4; ++v)
        {
            const auto& a = key[(size_t) v];
            const auto& b = values[(size_t) v];
            changed = changed || a.on != b.on || a.delayMs != b.delayMs || a.detuneCents != b.detuneCents || a.gainDb != b.gainDb || a.panPercent != b.panPercent;
        }
        changed = changed || oldReadout != readout || oldN != shownVoices;

        // Live: the engine's voices (effective pan, current pitch incl. the wobble, current delay, level)
        auto& m = proc.getChain().getDoubler (instance);
        for (size_t v = 0; v < 4; ++v)
        {
            livePan[v] = m.voicePan[v].load (std::memory_order_relaxed);
            liveCents[v] = m.voiceCents[v].load (std::memory_order_relaxed);
            liveDelay[v] = m.voiceDelayMs[v].load (std::memory_order_relaxed);
            const float db = m.voiceGainDb[v].load (std::memory_order_relaxed);
            liveLevel[v] = d.hasSignal && on ? juce::jlimit (0.0f, 1.0f, juce::jmap (db + d.inputDb.ago (0), -60.0f, -12.0f, 0.0f, 1.0f)) : 0.0f;
        }
        leadLevel = d.level();

        if (changed || (on && d.time - lastPaint >= liveInterval))
        {
            lastPaint = d.time;
            repaint();
        }
    }

    void DoublerStage::paint (juce::Graphics& g)
    {
        const auto hue = moduleColour (ModuleId::doubler);
        const float a = on ? 1.0f : 0.5f;
        const float cx = plot.getCentreX();

        // Width: the band the voices are heard in
        const float w = juce::jlimit (0.0f, 1.0f, width);
        const auto band = juce::Rectangle<float>::leftTopRightBottom (xForPan (-100.0f * w), plot.getY(), xForPan (100.0f * w), plot.getBottom());
        g.setColour (hue.withAlpha (0.06f * a));
        g.fillRect (band);
        g.setColour (hue.withAlpha (0.16f * a));
        g.fillRect (band.withWidth (1.0f));
        g.fillRect (band.withX (band.getRight() - 1.0f).withWidth (1.0f));

        // Grid: level lines with their scale, the centre line, L / R
        g.setFont (monoFont (9.5f));
        for (float db : { 6.0f, 0.0f, -12.0f, -24.0f })
        {
            const float y = yForGain (db);
            g.setColour (db == 0.0f ? colours::borderSoft.brighter (0.5f) : colours::borderSoft.brighter (0.15f));
            g.fillRect (juce::Rectangle<float> (plot.getX(), y - 0.5f, plot.getWidth(), 1.0f));
            g.setColour (colours::textFaint);
            g.drawText (db > 0.0f ? "+6" : (db == 0.0f ? "0" : (db < -13.0f ? "-24" : "-12")), juce::Rectangle<float> (plot.getX() - 24.0f, y - 6.0f, 20.0f, 12.0f),
                        juce::Justification::centredRight, false);
        }
        g.setColour (colours::borderSoft.brighter (0.3f));
        g.fillRect (juce::Rectangle<float> (cx - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
        g.setFont (semiBold (9.0f, 0.12f));
        g.setColour (colours::textFaint);
        g.drawText ("L", juce::Rectangle<float> (plot.getX() + 4.0f, plot.getY(), 12.0f, 12.0f), juce::Justification::centred, false);
        g.drawText ("R", juce::Rectangle<float> (plot.getRight() - 16.0f, plot.getY(), 12.0f, 12.0f), juce::Justification::centred, false);

        // Lead: the original at the Dry level (a ring when the dry is off)
        const auto lead = leadPoint();
        const bool dryOff = dryDb <= -59.5f;
        drawGlow (g, lead, 34.0f + 16.0f * leadLevel, (0.10f + 0.20f * leadLevel) * a * (dryOff ? 0.3f : 1.0f));
        g.setColour (juce::Colours::white.withAlpha (a));
        if (dryOff)
            g.drawEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (lead), 1.2f);
        else
            g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (lead));
        g.setFont (semiBold (8.5f, 0.12f));
        g.setColour (colours::textFaint);
        g.drawText ("LEAD", juce::Rectangle<float> (60.0f, 10.0f).withCentre (lead.translated (0.0f, 16.0f)), juce::Justification::centred, false);

        // Classic: the two voices are one linked pair (Delay / Detune / Width)
        if (classic && shownVoices >= 2)
        {
            const auto p0 = voicePoint (0), p1 = voicePoint (1);
            const float dash[] { 3.0f, 4.0f };
            g.setColour (hue.withAlpha (0.35f * a));
            g.drawDashedLine ({ p0.translated (10.0f, 0.0f), p1.translated (-10.0f, 0.0f) }, dash, 2, 1.0f);
        }

        // Voices: a line from the lead, the live ghost (heard position and wobble), the dot with its number
        for (int v = 0; v < shownVoices; ++v)
        {
            const auto& vv = values[(size_t) v];
            const auto c = voiceColour (v);
            const auto p = voicePoint (v);
            const bool active = v == dragVoice || (dragVoice < 0 && v == hovered);
            const float va = (vv.on ? 1.0f : 0.35f) * a;

            if (! classic)
            {
                g.setColour (c.withAlpha (0.14f * va));
                g.drawLine ({ lead, p }, 1.0f);
            }
            if (on && vv.on)
            {
                const auto ghost = juce::Point<float> (xForPan (livePan[(size_t) v] * 100.0f),
                                                       p.y + juce::jlimit (-7.0f, 7.0f, (liveCents[(size_t) v] - vv.detuneCents) * 0.7f));
                const float lv = liveLevel[(size_t) v];
                if (ghost.getDistanceFrom (p) > 12.0f)
                {
                    // Where Width puts the voice: a faint link from its set position
                    const float dash[] { 2.0f, 3.0f };
                    g.setColour (c.withAlpha (0.3f * a));
                    g.drawDashedLine ({ p, ghost }, dash, 2, 1.0f);
                }
                drawGlow (g, ghost, 16.0f + 10.0f * lv, (0.16f + 0.4f * lv) * a, c);
                const float ring = 5.0f + juce::jlimit (-2.0f, 3.0f, (liveDelay[(size_t) v] - vv.delayMs) * 0.6f);
                g.setColour (c.withAlpha (0.55f * a));
                g.drawEllipse (juce::Rectangle<float> (ring * 2.0f, ring * 2.0f).withCentre (ghost), 1.0f);
            }

            const float rad = active ? 9.5f : 8.5f;
            const auto dot = juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (p);
            if (vv.on)
            {
                g.setColour (c.withAlpha (va));
                g.fillEllipse (dot);
            }
            else
            {
                g.setColour (colours::windowBg);
                g.fillEllipse (dot);
                g.setColour (c.withAlpha (0.6f));
                g.drawEllipse (dot.reduced (0.5f), 1.2f);
            }
            if (active)
            {
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.drawEllipse (dot.expanded (3.0f), 1.0f);
            }
            g.setFont (semiBold (10.0f));
            g.setColour (vv.on ? colours::windowBg : c);
            g.drawText (voiceNumbers[v], dot, juce::Justification::centred, false);
        }

        // Read-outs: the voice being edited / the Classic pair (left), Width (right)
        const auto top = getLocalBounds().toFloat().reduced (16.0f, 10.0f).withHeight (14.0f);
        g.setFont (monoFont (11.0f));
        g.setColour (colours::text.withAlpha (0.4f + 0.6f * a));
        g.drawText (readout, top.withTrimmedLeft (22.0f), juce::Justification::centredLeft, false);
        g.setFont (semiBold (9.0f, 0.12f));
        g.setColour (colours::textFaint);
        g.drawText ("WIDTH", top.withTrimmedRight (40.0f), juce::Justification::centredRight, false);
        g.setFont (monoFont (11.0f));
        g.setColour (colours::text.withAlpha (0.4f + 0.6f * a));
        g.drawText (widthText, top, juce::Justification::centredRight, false);

        // Column captions over the voice rows
        if (shownVoices > 0)
        {
            const auto cols = rowColumns (rowsArea.withHeight (captionHeight));
            g.setFont (semiBold (8.5f, 0.12f));
            g.setColour (colours::textFaint);
            g.drawText ("ON", cols.on.toFloat(), juce::Justification::centred, false);
            for (size_t i = 0; i < 4; ++i)
                g.drawText (rowCaptions[i], cols.fields[i].toFloat(), juce::Justification::centred, false);
        }
    }

    //--------------------------------------------------------------------------
    // Voice gestures

    juce::PopupMenu DoublerStage::buildMenu (int v)
    {
        juce::Component::SafePointer<DoublerStage> safe (this);
        juce::PopupMenu m;
        m.addSectionHeader ("Stimme " + juce::String (v + 1));
        m.addItem (values[(size_t) juce::jlimit (0, 3, v)].on ? "Stimme aus" : "Stimme an",
                   [safe, v] { if (safe != nullptr) { doubleredit::toggleOn (safe->proc, safe->instance, v); safe->readParameters(); } });
        m.addItem (de ("Stimme zurücksetzen"),
                   [safe, v] { if (safe != nullptr) { doubleredit::resetVoice (safe->proc, safe->instance, v); safe->readParameters(); } });
        addPanelItems (*this, m);
        return m;
    }

    void DoublerStage::mouseMove (const juce::MouseEvent& e)
    {
        const int h = voiceAt (e.position);
        if (h != hovered)
        {
            hovered = h;
            readParameters();
            repaint();
        }
    }

    void DoublerStage::mouseExit (const juce::MouseEvent& e)
    {
        // Moving from the stage onto a handle is no exit
        if (getLocalBounds().toFloat().contains (e.position) && voiceAt (e.position) >= 0)
            return;
        if (hovered >= 0)
        {
            hovered = -1;
            readParameters();
            repaint();
        }
    }

    void DoublerStage::mouseDown (const juce::MouseEvent& e)
    {
        ignoreDrag = false;
        const int v = voiceAt (e.position);
        if (e.mods.isPopupMenu())
        {
            ignoreDrag = true;
            juce::PopupMenu m;
            if (v >= 0)
                m = buildMenu (v);
            else
                addPanelItems (*this, m);
            if (m.getNumItems() > 0)
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (190));
            return;
        }
        endDrag();
        if (v < 0)
            return;
        if (e.mods.isAltDown())
        {
            ignoreDrag = true;
            doubleredit::resetPosition (proc, instance, v);
            readParameters();
            return;
        }
        dragVoice = v;
        lastPos = e.position;
        dragPan = values[(size_t) v].panPercent;
        dragGain = values[(size_t) v].gainDb;
    }

    void DoublerStage::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragVoice < 0 || ignoreDrag || e.mods.isPopupMenu())
            return;
        const auto delta = e.position - lastPos;
        lastPos = e.position;
        if (delta.x == 0.0f && delta.y == 0.0f)
            return;
        if (edit == nullptr)
            edit = std::make_unique<doubleredit::Edit> (proc, instance, dragVoice, std::initializer_list<Field> { Field::pan, Field::gain },
                                                        "Doubler: Stimme " + juce::String (dragVoice + 1) + " Position");
        const float scale = isFine (e.mods) ? 0.1f : 1.0f;
        const float pxPerPan = std::max (1.0f, xForPan (100.0f) - xForPan (0.0f)) / 100.0f;
        const float pxPerDb = std::max (1.0f, yForGain (-24.0f) - yForGain (6.0f)) / 30.0f;
        dragPan = juce::jlimit (-100.0f, 100.0f, dragPan + delta.x * scale / pxPerPan);
        dragGain = juce::jlimit (-24.0f, 6.0f, dragGain - delta.y * scale / pxPerDb);
        edit->set (Field::pan, dragPan);
        edit->set (Field::gain, dragGain);
        readParameters();
        repaint();
    }

    void DoublerStage::mouseUp (const juce::MouseEvent&) { endDrag(); }

    void DoublerStage::endDrag()
    {
        const bool was = edit != nullptr || dragVoice >= 0;
        edit.reset();
        dragVoice = -1;
        if (was)
        {
            readParameters();
            repaint();
        }
    }

    void DoublerStage::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            return;
        const int v = voiceAt (e.position);
        if (v < 0)
            return;
        endDrag();
        ignoreDrag = true;
        doubleredit::resetPosition (proc, instance, v);
        readParameters();
    }

    void DoublerStage::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        if (edit != nullptr)
            return;
        const int v = voiceAt (e.position);
        const float n = wheelNotches (w);
        if (v < 0 || n == 0.0f)
            return;
        const float step = wheelCents * (isFine (e.mods) ? 0.1f : 1.0f);
        doubleredit::setValue (proc, instance, v, Field::detune,
                               juce::jlimit (-dsp::DoublerModule::maxDetuneCents, dsp::DoublerModule::maxDetuneCents, values[(size_t) v].detuneCents + n * step));
        readParameters();
        repaint();
    }

    //==========================================================================
    // CHOIR: the half-circle stage

    ChoirStage::ChoirStage (VelvoxAudioProcessor& p, int k) : proc (p), instance (k)
    {
        info = std::make_unique<HoverInfo> (*this,
                                            [this] { return hovered >= 0 ? "Stimme " + juce::String (hovered + 1) : juce::String ("Width"); },
                                            [this] { return hovered >= 0 ? cardValue (hovered) : juce::String (juce::roundToInt (settings.width * 100.0f)) + " %"; },
                                            [this] { return hovered >= 0 ? juce::String() : de ("Ziehen: Breite des Chors"); });
        dsp::ChoirSettings s;
        s.voices = dsp::ChoirModule::voiceCountForIndex (juce::roundToInt (realValue (proc, pid::choirVoices, instance)));
        s.detuneCents = realValue (proc, pid::choirDetune, instance);
        s.timingMs = realValue (proc, pid::choirTiming, instance);
        s.humanize = realValue (proc, pid::choirHumanize, instance) * 0.01f;
        s.width = realValue (proc, pid::choirWidth, instance) * 0.01f;
        s.density = realValue (proc, pid::choirDensity, instance) * 0.01f;
        s.mix = realValue (proc, pid::choirMix, instance) * 0.01f;
        s.enabled = realValue (proc, pid::choirOn, instance) > 0.5f;
        setSettings (s);
    }

    ChoirStage::~ChoirStage() { endDrag(); }

    juce::RangedAudioParameter* ChoirStage::widthParam() const { return paramFor (proc, pid::choirWidth, instance); }

    void ChoirStage::setSettings (const dsp::ChoirSettings& s)
    {
        settings = s;
        settings.voices = juce::jlimit (2, dsp::ChoirModule::maxVoices, s.voices);
        on = s.enabled;
        const juce::String dot = juce::String::fromUTF8 (" · ");
        auto text = juce::String (settings.voices) + " VOICES" + dot + juce::String::fromUTF8 ("±") + juce::String (juce::roundToInt (settings.detuneCents))
                  + " ct" + dot + juce::String (juce::roundToInt (settings.timingMs)) + " ms";
        if (text != readout)
        {
            readout = std::move (text);
            repaint();
        }
        auto wt = juce::String (juce::roundToInt (settings.width * 100.0f)) + " %";
        if (wt != widthText)
            widthText = std::move (wt);
    }

    void ChoirStage::resized()
    {
        const auto area = getLocalBounds().toFloat().reduced (16.0f, 12.0f);
        lead = { area.getCentreX(), area.getBottom() - 20.0f };
        radiusY = std::max (20.0f, lead.y - area.getY() - 30.0f);
        radius = std::max (20.0f, std::min (area.getWidth() * 0.42f, radiusY * 1.8f));
        // The stage: the front and back row of the half circle
        arcs.clear();
        for (float k : { 0.8f, 1.0f })
            arcs.addCentredArc (lead.x, lead.y, radius * k, radiusY * k, 0.0f,
                                -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
    }

    float ChoirStage::seatAngle (int v) const noexcept
    {
        // Seats on the half circle: the voices of one side stand in the order of their heard pans, the
        // outermost exactly at its pan (sin of the angle = pan), the others evenly spaced towards the
        // centre; Width 0 puts every voice behind the lead
        const int n = settings.voices;
        v = juce::jlimit (0, dsp::ChoirModule::maxVoices - 1, v);
        if (v >= n)
            return 0.0f;
        std::array<float, dsp::ChoirModule::maxVoices> pan {};
        for (int i = 0; i < n; ++i)
            pan[(size_t) i] = juce::jlimit (-1.0f, 1.0f, dsp::ChoirModule::voicePosition (i, settings).pan);
        const float p = pan[(size_t) v];
        if (std::abs (p) < 1.0e-4f)
            return 0.0f;
        int rank = 0, onSide = 0;
        float outer = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float q = pan[(size_t) i];
            if ((q < 0.0f) != (p < 0.0f) || std::abs (q) < 1.0e-4f)
                continue;
            ++onSide;
            outer = std::max (outer, std::abs (q));
            if (std::abs (q) > std::abs (p) || (std::abs (q) == std::abs (p) && i < v))
                ++rank;
        }
        const float maxAngle = std::asin (outer);
        return std::copysign (maxAngle * (1.0f - 0.75f * (float) rank / (float) std::max (1, onSide)), p);
    }

    juce::Point<float> ChoirStage::voicePoint (int v) const noexcept
    {
        v = juce::jlimit (0, dsp::ChoirModule::maxVoices - 1, v);
        // Later entries stand further back: the choir's timing spread over the two rows of the stage
        float maxDepth = 0.0f;
        for (int i = 0; i < settings.voices; ++i)
            maxDepth = std::max (maxDepth, dsp::ChoirModule::voicePosition (i, settings).depth);
        const float depth = dsp::ChoirModule::voicePosition (v, settings).depth;
        const float k = 0.8f + 0.2f * (maxDepth > 1.0e-4f ? depth / maxDepth : 0.0f);
        const float a = seatAngle (v);
        return { lead.x + radius * k * std::sin (a), lead.y - radiusY * k * std::cos (a) };
    }

    int ChoirStage::voiceAt (juce::Point<float> p) const noexcept
    {
        int best = -1;
        float bestD = 12.0f;
        for (int v = 0; v < settings.voices; ++v)
        {
            const float d = voicePoint (v).getDistanceFrom (p);
            if (d < bestD)
            {
                bestD = d;
                best = v;
            }
        }
        return best;
    }

    juce::String ChoirStage::cardValue (int v) const
    {
        v = juce::jlimit (0, dsp::ChoirModule::maxVoices - 1, v);
        const auto pos = dsp::ChoirModule::voicePosition (v, settings);
        const float cents = dsp::ChoirModule::shapes()[(size_t) v].detune * settings.detuneCents;
        const juce::String dot = juce::String::fromUTF8 (" · ");
        const float db = juce::Decibels::gainToDecibels (pos.level, -60.0f);
        return doubleredit::valueText (Field::pan, pos.pan * 100.0f) + dot + (cents >= 0.5f ? "+" : "") + juce::String (juce::roundToInt (cents)) + " ct"
             + dot + juce::String (db, 1) + " dB";
    }

    void ChoirStage::update (const LiveData& d)
    {
        const auto s = d.settings.instance (instance).choir;
        const bool changed = s.voices != settings.voices || s.width != settings.width || s.timingMs != settings.timingMs
                          || s.detuneCents != settings.detuneCents || s.density != settings.density || s.enabled != on;
        if (! dragging)
            setSettings (s);
        else
            on = s.enabled;

        auto& m = proc.getChain().getChoir (instance);
        const auto& sh = dsp::ChoirModule::shapes();
        for (size_t v = 0; v < liveBob.size(); ++v)
        {
            // The live pitch drift around the voice's own offset, as a small bob
            const float drift = m.voiceCents[v].load (std::memory_order_relaxed) - sh[v].detune * settings.detuneCents;
            liveBob[v] = on ? juce::jlimit (-5.0f, 5.0f, drift * 0.8f) : 0.0f;
            liveLevel[v] = on ? m.voiceLevel[v].load (std::memory_order_relaxed) : 0.0f;
        }
        correlation = on ? m.outputCorrelation.load (std::memory_order_relaxed) : 1.0f;
        auto mono = "MONO " + juce::String (correlation, 2);
        if (mono != monoText)
            monoText = std::move (mono);
        leadLevel = d.level();
        info->refresh();

        if (changed || (on && d.time - lastPaint >= liveInterval))
        {
            lastPaint = d.time;
            repaint();
        }
    }

    void ChoirStage::paint (juce::Graphics& g)
    {
        const auto hue = moduleColour (ModuleId::choir);
        const float a = on ? 1.0f : 0.5f;

        // The stage: two rows of the half circle (front and back) and the floor line
        g.setColour (colours::borderSoft.brighter (0.3f).withAlpha (a));
        g.strokePath (arcs, juce::PathStrokeType (1.0f));
        g.setColour (colours::borderSoft.brighter (0.2f));
        g.fillRect (juce::Rectangle<float> (lead.x - radius - 10.0f, lead.y - 0.5f, (radius + 10.0f) * 2.0f, 1.0f));
        g.setFont (semiBold (9.0f, 0.12f));
        g.setColour (colours::textFaint);
        g.drawText ("L", juce::Rectangle<float> (lead.x - radius - 24.0f, lead.y - 6.0f, 12.0f, 12.0f), juce::Justification::centred, false);
        g.drawText ("R", juce::Rectangle<float> (lead.x + radius + 12.0f, lead.y - 6.0f, 12.0f, 12.0f), juce::Justification::centred, false);

        // Voices: thin line to the lead, glow by level, dot size by level, bob with the live drift
        const float maxLevel = 1.6f;
        for (int v = 0; v < settings.voices; ++v)
        {
            const auto pos = dsp::ChoirModule::voicePosition (v, settings);
            const auto p = voicePoint (v).translated (0.0f, liveBob[(size_t) v]);
            const auto c = hue.interpolatedWith (pos.pan < 0.0f ? colours::teal : colours::violet, 0.3f * std::abs (pos.pan));
            const float lv = juce::jlimit (0.0f, 1.0f, pos.level / maxLevel);
            const float live = juce::jlimit (0.0f, 1.0f, liveLevel[(size_t) v] / maxLevel);
            const bool active = v == hovered;

            g.setColour (c.withAlpha (0.12f * a));
            g.drawLine ({ lead, p }, 1.0f);
            drawGlow (g, p, 16.0f + 12.0f * live * leadLevel, (0.18f + 0.35f * live * leadLevel) * a, c);
            const float rad = 4.5f + 3.0f * lv + (active ? 1.5f : 0.0f);
            g.setColour (c.interpolatedWith (juce::Colours::white, 0.25f).withAlpha (a));
            g.fillEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (p));
            if (active)
            {
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.drawEllipse (juce::Rectangle<float> (rad * 2.0f + 6.0f, rad * 2.0f + 6.0f).withCentre (p), 1.0f);
            }
        }

        // The lead in front
        drawGlow (g, lead, 34.0f + 16.0f * leadLevel, (0.12f + 0.22f * leadLevel) * a);
        g.setColour (juce::Colours::white.withAlpha (a));
        g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (lead));
        g.setFont (semiBold (8.5f, 0.12f));
        g.setColour (colours::textFaint);
        g.drawText ("LEAD", juce::Rectangle<float> (60.0f, 10.0f).withCentre (lead.translated (0.0f, 15.0f)), juce::Justification::centred, false);

        // Read-outs: voices, detune, timing (left); width and mono correlation (right)
        const auto top = getLocalBounds().toFloat().reduced (16.0f, 10.0f).withHeight (14.0f);
        g.setFont (monoFont (11.0f));
        g.setColour (colours::text.withAlpha (0.4f + 0.6f * a));
        g.drawText (readout, top, juce::Justification::centredLeft, false);
        const bool risk = on && correlation < 0.0f;
        g.setColour (risk ? colours::warning : colours::text.withAlpha (0.4f + 0.6f * a));
        g.drawText (monoText, top, juce::Justification::centredRight, false);
        g.setColour (colours::text.withAlpha (0.4f + 0.6f * a));
        g.drawText (widthText, top.withTrimmedRight (110.0f), juce::Justification::centredRight, false);
        g.setFont (semiBold (9.0f, 0.12f));
        g.setColour (colours::textFaint);
        g.drawText ("WIDTH", top.withTrimmedRight (156.0f), juce::Justification::centredRight, false);
    }

    void ChoirStage::mouseMove (const juce::MouseEvent& e)
    {
        const int h = voiceAt (e.position);
        if (h != hovered)
        {
            hovered = h;
            repaint();
        }
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    }

    void ChoirStage::mouseExit (const juce::MouseEvent&)
    {
        if (hovered >= 0)
        {
            hovered = -1;
            repaint();
        }
    }

    void ChoirStage::mouseDown (const juce::MouseEvent& e)
    {
        ignoreDrag = false;
        if (e.mods.isPopupMenu())
        {
            ignoreDrag = true;
            juce::PopupMenu m;
            juce::Component::SafePointer<ChoirStage> safe (this);
            m.addItem (de ("Width zurücksetzen"), [safe] { if (safe != nullptr) if (auto* p = safe->widthParam()) gestures::resetToDefault (*p); });
            addPanelItems (*this, m);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (190));
            return;
        }
        endDrag();
        if (e.mods.isAltDown())
        {
            ignoreDrag = true;
            if (auto* p = widthParam())
                gestures::resetToDefault (*p);
            return;
        }
        lastX = e.position.x;
        dragWidth = realOf (widthParam()) * 0.01f;
    }

    void ChoirStage::mouseDrag (const juce::MouseEvent& e)
    {
        if (ignoreDrag || e.mods.isPopupMenu())
            return;
        const float dx = e.position.x - lastX;
        lastX = e.position.x;
        if (dx == 0.0f)
            return;
        auto* p = widthParam();
        if (p == nullptr)
            return;
        if (! dragging)
        {
            dragging = true;
            p->beginChangeGesture();
        }
        dragWidth = juce::jlimit (0.0f, 1.0f, dragWidth + dx / (pixelsPerWidth * (isFine (e.mods) ? 10.0f : 1.0f)));
        write (p, dragWidth * 100.0f);
        auto s = settings;
        s.width = dragWidth;
        setSettings (s);
        repaint();
    }

    void ChoirStage::mouseUp (const juce::MouseEvent&) { endDrag(); }

    void ChoirStage::endDrag()
    {
        if (! dragging)
            return;
        dragging = false;
        if (auto* p = widthParam())
            p->endChangeGesture();
    }

    void ChoirStage::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (e.mods.isPopupMenu())
            return;
        endDrag();
        ignoreDrag = true;
        if (auto* p = widthParam())
            gestures::resetToDefault (*p);
    }

    void ChoirStage::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        const float n = wheelNotches (w);
        auto* p = widthParam();
        if (n == 0.0f || p == nullptr || dragging)
            return;
        gestures::setReal (*p, juce::jlimit (0.0f, 100.0f, realOf (p) + n * (isFine (e.mods) ? 0.2f : 2.0f)));
    }

    //==========================================================================
    // Panels
    namespace
    {
        // DOUBLER ("like Waves Doubler"): voice mode, Mix, the wobble (Classic: Detune, 2 / 4: Mod Depth), Dry;
        // Width, Mono Safe, Tone, Low Cut, Mod Rate, Delay (Classic) in Mehr
        class DoublerPanel final : public DetailPanel
        {
        public:
            explicit DoublerPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Doubler", help::shortDescription ((int) ModuleId::doubler), pid::doublerOn)
            {
                voices = &segments (pid::doublerVoices);
                setCaption (*voices, "Stimmen");
                mix = &knob (pid::doublerMix, "Mix");
                mix->setDescription (de ("Anteil der Dopplungen am Signal."));
                detune = &knob (pid::doublerDetune, "Detune");
                modDepth = &knob (pid::doublerModDepth, "Mod Depth");
                dry = &knob (pid::doublerDry, "Dry");
                knob (pid::doublerWidth, "Width");
                knob (pid::doublerMonoSafe, "Mono Safe");
                knob (pid::doublerTone, "Tone");
                knob (pid::doublerLowCut, "Low Cut");
                knob (pid::doublerModRate, "Mod Rate");
                delay = &knob (pid::doublerDelay, "Delay");
                showMode (classicNow());
                setMixKnob (mix);
            }

            void refresh() override
            {
                DetailPanel::refresh();
                if (const bool c = classicNow(); c != shownClassic)
                    showMode (c);
            }

        private:
            bool classicNow() const { return apvts.getRawParameterValue (pidFor (pid::doublerVoices))->load() < 0.5f; }

            // Classic: Detune is the wobble and Delay sets the pair; 2 / 4: Mod Depth, the per-voice delays
            void showMode (bool classic)
            {
                shownClassic = classic;
                Knob* toMain = classic ? detune : modDepth;
                Knob* toMore = classic ? modDepth : detune;
                getMoreDrawer().addAndMakeVisible (toMore);
                toMore->setProminence (Knob::Prominence::secondary);
                toMore->setDialSize (32);
                for (auto* k : { toMore, delay })
                {
                    const bool usable = k == delay ? classic : false;
                    k->setEnabled (usable);
                    k->setAlpha (usable ? 1.0f : 0.45f);
                }
                toMain->setEnabled (true);
                toMain->setAlpha (1.0f);
                setMainControls ({ voices, mix, toMain, dry });
            }

            Segments* voices = nullptr;
            Knob* mix = nullptr;
            Knob* detune = nullptr;
            Knob* modDepth = nullptr;
            Knob* dry = nullptr;
            Knob* delay = nullptr;
            bool shownClassic = true;
        };

        // CHOIR ("half-circle stage"): Voices, Detune, Width, Mix; Timing, Humanize, Density in Mehr
        class ChoirPanel final : public DetailPanel
        {
        public:
            explicit ChoirPanel (VelvoxAudioProcessor& p)
                : DetailPanel (p, "Choir", help::shortDescription ((int) ModuleId::choir), pid::choirOn)
            {
                auto& voices = segments (pid::choirVoices);
                setCaption (voices, "Stimmen");
                auto& detune = knob (pid::choirDetune, "Detune");
                knob (pid::choirTiming, "Timing");
                knob (pid::choirHumanize, "Humanize");
                auto& width = knob (pid::choirWidth, "Width");
                knob (pid::choirDensity, "Density");
                auto& mix = knob (pid::choirMix, "Mix");
                setMainControls ({ &voices, &detune, &width, &mix });
            }
        };
    }

    std::unique_ptr<ModuleView> createVoicesModuleView (VelvoxAudioProcessor& p, ModuleId id, int instance)
    {
        if (id == ModuleId::doubler)
            return std::make_unique<DoublerStage> (p, instance);
        if (id == ModuleId::choir)
            return std::make_unique<ChoirStage> (p, instance);
        return nullptr;
    }

    std::unique_ptr<DetailPanel> createVoicesModulePanel (VelvoxAudioProcessor& p, ModuleId id)
    {
        if (id == ModuleId::doubler)
            return std::make_unique<DoublerPanel> (p);
        if (id == ModuleId::choir)
            return std::make_unique<ChoirPanel> (p);
        return nullptr;
    }
}
