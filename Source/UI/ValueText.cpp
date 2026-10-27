#include "ValueText.h"
#include "../Parameters/ParameterLayout.h"
#include "../Text/I18n.h"
#include "../Text/ParamText.h"

namespace velvox::ui
{
    namespace
    {
        // Units a typed value may carry (after the number and an optional k)
        bool isKnownUnit (const juce::String& u)
        {
            static const juce::StringArray units { "", "db", "hz", "ms", "s", "sec", "%", "st", "ct", "x", ":1", "bpm", "semitones", "cents" };
            return units.contains (u);
        }
    }

    std::optional<float> parseValueText (const juce::String& text, const juce::RangedAudioParameter& p)
    {
        const auto t = text.trim();
        if (t.isEmpty())
            return std::nullopt;
        const auto lower = t.toLowerCase();

        // Choices by name ("1/8", "Plate"), case and spaces ignored; the English and the German name
        if (auto* c = dynamic_cast<const juce::AudioParameterChoice*> (&p))
        {
            const auto squeezed = lower.removeCharacters (" ");
            const auto matches = [&squeezed] (const juce::String& name) { return name.toLowerCase().removeCharacters (" ") == squeezed; };
            for (int i = 0; i < c->choices.size(); ++i)
                if (matches (c->choices[i]) || matches (i18n::germanChoiceName (p.getParameterID(), i, c->choices[i])))
                    return (float) i;
            return std::nullopt;
        }

        if (dynamic_cast<const juce::AudioParameterBool*> (&p) != nullptr)
        {
            if (lower == "on" || lower == "an" || lower == "ein" || lower == "true" || lower == "1" || lower == "yes" || lower == "ja")
                return 1.0f;
            if (lower == "off" || lower == "aus" || lower == "false" || lower == "0" || lower == "no" || lower == "nein")
                return 0.0f;
            return std::nullopt;
        }

        const auto& range = p.getNormalisableRange();
        const auto* spec = findParameterSpec (p.getParameterID());
        const juce::String unit = spec != nullptr ? juce::String (spec->unit).toLowerCase() : juce::String();

        if (lower == "off" || lower == "aus")
            return range.start;

        // The number: optional sign, digits, one decimal point (or a German comma)
        const auto s = lower.replaceCharacter (',', '.').removeCharacters (" ");
        int i = 0;
        juce::String number;
        if (i < s.length() && (s[i] == '-' || s[i] == '+' || s[i] == juce::juce_wchar (0x2212)))   // also the typographic minus
            number << (s[i++] == '+' ? "" : "-");
        bool digits = false, point = false;
        for (; i < s.length(); ++i)
        {
            const auto ch = s[i];
            if (juce::CharacterFunctions::isDigit (ch)) { number << juce::String::charToString (ch); digits = true; }
            else if (ch == '.' && ! point)              { number << "."; point = true; }
            else break;
        }
        if (! digits)
            return std::nullopt;

        double v = number.getDoubleValue();
        auto rest = s.substring (i);
        if (rest.startsWith ("k"))   // "2k", "7.5 kHz"
        {
            v *= 1000.0;
            rest = rest.substring (1);
        }

        // Time units: convert to the parameter's own unit
        if (unit == "ms" && (rest == "s" || rest == "sec"))
            v *= 1000.0;
        else if (unit == "s" && rest == "ms")
            v /= 1000.0;

        if (! isKnownUnit (rest))
            return std::nullopt;
        if (! std::isfinite (v))
            return std::nullopt;

        const float clamped = juce::jlimit (range.start, range.end, (float) v);
        return range.snapToLegalValue (clamped);
    }

    //==========================================================================
    namespace gestures
    {
        namespace
        {
            juce::String& clipboard()
            {
                // Shared by every control of every Headliner instance in this process. Deliberately not
                // the system clipboard: copying a value never destroys what the user copied elsewhere.
                static juce::String text;
                return text;
            }
        }

        void setReal (juce::RangedAudioParameter& p, float value)
        {
            p.beginChangeGesture();
            p.setValueNotifyingHost (p.convertTo0to1 (value));
            p.endChangeGesture();
        }

        void resetToDefault (juce::RangedAudioParameter& p)
        {
            p.beginChangeGesture();
            p.setValueNotifyingHost (p.getDefaultValue());
            p.endChangeGesture();
        }

        void stepBy (juce::RangedAudioParameter& p, int steps)
        {
            const int n = p.getNumSteps();
            if (n > 1 && n < 1000)
            {
                const int current = juce::roundToInt (p.getValue() * (float) (n - 1));
                const int next = juce::jlimit (0, n - 1, current + steps);
                if (next != current)
                {
                    p.beginChangeGesture();
                    p.setValueNotifyingHost ((float) next / (float) (n - 1));
                    p.endChangeGesture();
                }
                return;
            }
            p.beginChangeGesture();
            p.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p.getValue() + 0.01f * (float) steps));
            p.endChangeGesture();
        }

        bool copyValue (juce::RangedAudioParameter& p)
        {
            clipboard() = p.getCurrentValueAsText();
            return clipboard().isNotEmpty();
        }

        bool canPaste (const juce::RangedAudioParameter& p)
        {
            return parseValueText (clipboard(), p).has_value();
        }

        bool pasteValue (juce::RangedAudioParameter& p)
        {
            const auto v = parseValueText (clipboard(), p);
            if (! v.has_value())
                return false;
            setReal (p, *v);
            return true;
        }

        juce::PopupMenu buildMenu (juce::RangedAudioParameter& p, PanelMenuHost* panel, std::function<void()> enterValue)
        {
            auto* param = &p;
            juce::PopupMenu m;
            m.addSectionHeader (i18n::paramName (p.getParameterID(), p.getName (48)));
            m.addItem (i18n::tr ("Standardwert", "Default value"), [param] { resetToDefault (*param); });
            m.addItem (i18n::tr ("Wert eingeben …", "Enter value …"), enterValue != nullptr, false, [enterValue] { if (enterValue) enterValue(); });
            m.addSeparator();
            m.addItem (i18n::tr ("Wert kopieren", "Copy value"), [param] { copyValue (*param); });
            m.addItem (i18n::tr ("Wert einfügen", "Paste value") + (canPaste (p) ? "  (" + clipboard() + ")" : juce::String()), canPaste (p), false,
                       [param] { pasteValue (*param); });
            if (panel != nullptr)
            {
                m.addSeparator();
                panel->addPanelItems (m);
            }
            return m;
        }

        void showMenu (juce::Component& control, juce::RangedAudioParameter& p, std::function<void()> enterValue)
        {
            auto* panel = control.findParentComponentOfClass<PanelMenuHost>();
            buildMenu (p, panel, std::move (enterValue))
                .showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&control).withMinimumWidth (200));
        }
    }
}
