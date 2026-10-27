#include "Segments.h"
#include "ValueText.h"
#include "../Parameters/ParameterLayout.h"
#include "../Text/ParamText.h"

namespace velvox::ui
{
    namespace
    {
        const juce::String overflowLabel = juce::String::fromUTF8 ("\xe2\x80\xa6");   // …
    }

    // A segment: a click chooses it; the other gestures belong to the whole group
    class Segments::SegmentButton final : public juce::TextButton
    {
    public:
        SegmentButton (Segments& o, const juce::String& text) : juce::TextButton (text), owner (o) {}

        void mouseDown (const juce::MouseEvent& e) override
        {
            gestureClick = e.mods.isPopupMenu() || e.mods.isAltDown();
            if (e.mods.isPopupMenu())
            {
                owner.showMenu (*this);
                return;
            }
            if (e.mods.isAltDown())
            {
                owner.resetToDefault();
                return;
            }
            juce::TextButton::mouseDown (e);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (gestureClick)
            {
                gestureClick = false;
                return;
            }
            juce::TextButton::mouseUp (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! gestureClick)
                juce::TextButton::mouseDrag (e);
        }

        void mouseDoubleClick (const juce::MouseEvent&) override { owner.resetToDefault(); }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const int index = owner.buttons.indexOf (this);
            if (owner.tilePainter != nullptr && index >= 0 && this != owner.overflow)
            {
                owner.tilePainter (g, index, getLocalBounds().toFloat(), getToggleState(), highlighted || down);
                return;
            }
            juce::TextButton::paintButton (g, highlighted, down);
        }

        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        {
            const float d = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? -wheel.deltaX : wheel.deltaY;
            if (std::abs (d) > 1.0e-4f)
                owner.step ((d > 0.0f) != wheel.isReversed ? -1 : 1);   // up = previous (to the left)
        }

    private:
        Segments& owner;
        bool gestureClick = false;
    };

    Segments::Segments (APVTS& state, const char* id, juce::StringArray labels, juce::Colour accent, bool withOverflow)
        : paramId (id)
    {
        parameter = state.getParameter (paramId);
        jassert (parameter != nullptr);
        const auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter);
        if (choice != nullptr)
            allChoices = choice->choices;

        for (int i = 0; i < labels.size(); ++i)
        {
            // Labels are matched as data (the English choice names); a matched label is shown in the
            // interface language, any other label as given
            const int byName = allChoices.indexOf (labels[i]);
            choiceIndex.push_back (byName >= 0 ? byName : i);

            auto* b = buttons.add (new SegmentButton (*this, byName >= 0 ? i18n::choiceName (paramId, byName, labels[i]) : labels[i]));
            b->onClick = [this, i] { clickSegment (i); };
        }
        if (withOverflow && allChoices.size() > labels.size())
        {
            overflow = buttons.add (new SegmentButton (*this, overflowLabel));
            overflow->onClick = [this] { showOverflowMenu(); };
        }
        for (auto* b : buttons)
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, accent);
            b->getProperties().set ("segment", true);
            addAndMakeVisible (b);
        }

        if (parameter != nullptr)
        {
            attachment = std::make_unique<juce::ParameterAttachment> (*parameter, [this] (float v) { showValue (v); }, nullptr);
            attachment->sendInitialUpdate();

            const auto* spec = findParameterSpec (paramId);
            const juce::String description = spec != nullptr ? i18n::paramDescription (paramId, spec->description) : juce::String();
            info = std::make_unique<HoverInfo> (*this, [this] { return i18n::paramName (paramId, parameter->getName (48)); },
                                                [this] { return shownValueText(); },
                                                [description] { return description; });
        }
    }

    Segments::~Segments() = default;

    juce::String Segments::choiceText (int choice) const
    {
        return juce::isPositiveAndBelow (choice, allChoices.size()) ? i18n::choiceName (paramId, choice, allChoices[choice]) : juce::String();
    }

    juce::String Segments::shownValueText() const
    {
        if (parameter == nullptr)
            return {};
        if (allChoices.isEmpty())
            return parameter->getCurrentValueAsText();
        return choiceText (juce::roundToInt (parameter->convertFrom0to1 (parameter->getValue())));
    }

    int Segments::getChoiceForSegment (int segment) const
    {
        return juce::isPositiveAndBelow (segment, (int) choiceIndex.size()) ? choiceIndex[(size_t) segment] : -1;
    }

    void Segments::clickSegment (int segment)
    {
        const int choice = getChoiceForSegment (segment);
        if (choice < 0 || attachment == nullptr)
            return;
        attachment->setValueAsCompleteGesture ((float) choice);
        showValue ((float) choice);
    }

    void Segments::chooseFromOverflow (int choice)
    {
        if (attachment == nullptr || ! juce::isPositiveAndBelow (choice, allChoices.size()))
            return;
        attachment->setValueAsCompleteGesture ((float) choice);
        showValue ((float) choice);
    }

    void Segments::resetToDefault()
    {
        if (parameter == nullptr || ! isEnabled())
            return;
        gestures::resetToDefault (*parameter);
        showValue (parameter->convertFrom0to1 (parameter->getValue()));
    }

    void Segments::step (int direction)
    {
        if (parameter == nullptr || ! isEnabled() || choiceIndex.empty())
            return;
        // Along the visible segments; from a value without a segment, the nearest end
        int next = selectedSegment >= 0 && selectedSegment < (int) choiceIndex.size()
                     ? selectedSegment + direction
                     : (direction > 0 ? 0 : (int) choiceIndex.size() - 1);
        next = juce::jlimit (0, (int) choiceIndex.size() - 1, next);
        if (next != selectedSegment)
            clickSegment (next);
    }

    void Segments::showMenu (juce::Component& target)
    {
        if (parameter == nullptr || ! isEnabled())
            return;
        gestures::showMenu (target, *parameter, nullptr);
    }

    void Segments::showOverflowMenu()
    {
        juce::PopupMenu menu;
        const int current = selectedSegment >= 0 && selectedSegment < (int) choiceIndex.size() ? choiceIndex[(size_t) selectedSegment]
                                                                                             : overflowChoice;
        for (int i = 0; i < allChoices.size(); ++i)
            menu.addItem (i + 1, choiceText (i), true, i == current);
        juce::Component::SafePointer<Segments> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (overflow),
                            [safe] (int result) { if (safe != nullptr && result > 0) safe->chooseFromOverflow (result - 1); });
    }

    void Segments::setTilePainter (TilePainter painter)
    {
        tilePainter = std::move (painter);
        resized();
        repaint();
    }

    void Segments::setAccent (juce::Colour accent)
    {
        for (auto* b : buttons)
            b->setColour (juce::TextButton::buttonOnColourId, accent);
        repaint();
    }

    void Segments::showValue (float value)
    {
        const int v = juce::roundToInt (value);
        selectedSegment = -1;
        for (int i = 0; i < (int) choiceIndex.size(); ++i)
            if (choiceIndex[(size_t) i] == v)
                selectedSegment = i;

        if (overflow != nullptr)
        {
            // A value without its own segment: the overflow segment carries its name
            const bool inOverflow = selectedSegment < 0 && juce::isPositiveAndBelow (v, allChoices.size());
            overflowChoice = inOverflow ? v : -1;
            overflow->setButtonText (inOverflow ? choiceText (v) : overflowLabel);
            if (inOverflow)
                selectedSegment = getOverflowSegment();
        }

        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == selectedSegment, juce::dontSendNotification);
    }

    void Segments::paint (juce::Graphics& g)
    {
        if (tilePainter != nullptr)
            return;   // tiles paint themselves, no group bar
        g.setColour (colours::control);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
    }

    void Segments::resized()
    {
        if (buttons.isEmpty())
            return;
        if (tilePainter != nullptr)
        {
            // Separate tiles with a gap, all of the group's height
            const int gap = 8, n = buttons.size();
            const float w = (float) (getWidth() - gap * (n - 1)) / (float) n;
            for (int i = 0; i < n; ++i)
                buttons[i]->setBounds (juce::roundToInt ((w + (float) gap) * (float) i), 0, juce::roundToInt (w), getHeight());
            return;
        }
        auto r = getLocalBounds().reduced (3);
        // The overflow segment is a little wider so a name like "1/16T" fits
        const float weightTotal = (float) choiceIndex.size() + (overflow != nullptr ? 1.2f : 0.0f);
        const float unit = (float) r.getWidth() / weightTotal;
        for (int i = 0; i < buttons.size(); ++i)
        {
            const bool last = i == buttons.size() - 1;
            const int w = last ? r.getWidth() : juce::roundToInt (unit * (buttons[i] == overflow ? 1.2f : 1.0f));
            buttons[i]->setBounds (r.removeFromLeft (w));
        }
    }
}
