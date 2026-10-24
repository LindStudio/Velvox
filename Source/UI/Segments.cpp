#include "Segments.h"

namespace velvox::ui
{
    namespace
    {
        const juce::String overflowLabel = juce::String::fromUTF8 ("\xe2\x80\xa6");   // …
    }

    Segments::Segments (APVTS& state, const char* id, juce::StringArray labels, juce::Colour accent, bool withOverflow)
        : paramId (id)
    {
        auto* param = state.getParameter (paramId);
        jassert (param != nullptr);
        const auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param);
        if (choice != nullptr)
            allChoices = choice->choices;

        for (int i = 0; i < labels.size(); ++i)
        {
            const int byName = allChoices.indexOf (labels[i]);
            choiceIndex.push_back (byName >= 0 ? byName : i);

            auto* b = buttons.add (new juce::TextButton (labels[i]));
            b->onClick = [this, i] { clickSegment (i); };
        }
        if (withOverflow && allChoices.size() > labels.size())
        {
            overflow = buttons.add (new juce::TextButton (overflowLabel));
            overflow->setTooltip ("All values");
            overflow->onClick = [this] { showOverflowMenu(); };
        }
        for (auto* b : buttons)
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, accent);
            b->getProperties().set ("segment", true);
            addAndMakeVisible (b);
        }

        if (param != nullptr)
        {
            attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { showValue (v); }, nullptr);
            attachment->sendInitialUpdate();
        }
    }

    Segments::~Segments() = default;

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

    void Segments::showOverflowMenu()
    {
        juce::PopupMenu menu;
        const int current = selectedSegment >= 0 && selectedSegment < (int) choiceIndex.size() ? choiceIndex[(size_t) selectedSegment]
                                                                                             : allChoices.indexOf (overflow->getButtonText());
        for (int i = 0; i < allChoices.size(); ++i)
            menu.addItem (i + 1, allChoices[i], true, i == current);
        juce::Component::SafePointer<Segments> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (overflow),
                            [safe] (int result) { if (safe != nullptr && result > 0) safe->chooseFromOverflow (result - 1); });
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
            overflow->setButtonText (inOverflow ? allChoices[v] : overflowLabel);
            if (inOverflow)
                selectedSegment = getOverflowSegment();
        }

        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == selectedSegment, juce::dontSendNotification);
    }

    void Segments::paint (juce::Graphics& g)
    {
        g.setColour (colours::control);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
    }

    void Segments::resized()
    {
        if (buttons.isEmpty())
            return;
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
