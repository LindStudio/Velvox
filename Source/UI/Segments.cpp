#include "Segments.h"

namespace velvox::ui
{
    Segments::Segments (APVTS& state, const char* id, juce::StringArray labels, juce::Colour accent)
        : paramId (id)
    {
        auto* param = state.getParameter (paramId);
        jassert (param != nullptr);
        const auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param);

        for (int i = 0; i < labels.size(); ++i)
        {
            const int byName = choice != nullptr ? choice->choices.indexOf (labels[i]) : -1;
            choiceIndex.push_back (byName >= 0 ? byName : i);

            auto* b = buttons.add (new juce::TextButton (labels[i]));
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonOnColourId, accent);
            b->getProperties().set ("segment", true);
            b->onClick = [this, i] { clickSegment (i); };
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

    void Segments::showValue (float value)
    {
        const int v = juce::roundToInt (value);
        selectedSegment = -1;
        for (int i = 0; i < (int) choiceIndex.size(); ++i)
            if (choiceIndex[(size_t) i] == v)
                selectedSegment = i;
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
        const int w = r.getWidth() / buttons.size();
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setBounds (i == buttons.size() - 1 ? r : r.removeFromLeft (w));
    }
}
