#include "HeadlinerLookAndFeel.h"

namespace
{
    // Font-Erzeugung für JUCE 7 und JUCE 8
    juce::Font makeFont (const juce::Typeface::Ptr& tf, float height, bool bold)
    {
       #if JUCE_MAJOR_VERSION >= 8
        if (tf != nullptr)
            return juce::Font (juce::FontOptions (tf).withHeight (height));
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
       #else
        if (tf != nullptr)
            return juce::Font (tf).withHeight (height);
        return juce::Font (height, bold ? juce::Font::bold : juce::Font::plain);
       #endif
    }

    // Kreis-Rechteck um einen Mittelpunkt (Radius in Pixeln)
    juce::Rectangle<float> circleAt (juce::Point<float> c, float radius)
    {
        return juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c);
    }
}

//==============================================================================
HeadlinerLookAndFeel::HeadlinerLookAndFeel()
{
    using namespace hv::colours;

    setColour (juce::ResizableWindow::backgroundColourId, windowBg);

    setColour (juce::Slider::rotarySliderFillColourId,    levelRide);
    setColour (juce::Slider::rotarySliderOutlineColourId, tickOff);
    setColour (juce::Slider::thumbColourId,               levelRide);

    setColour (juce::Label::textColourId, textMuted);

    setColour (juce::ToggleButton::textColourId, textSoft);
    setColour (juce::ToggleButton::tickColourId, levelRide);

    setColour (juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
    setColour (juce::TextButton::buttonOnColourId, levelRide);
    setColour (juce::TextButton::textColourOffId,  juce::Colour (0xff7c859b));
    setColour (juce::TextButton::textColourOnId,   textStrong);

    setColour (juce::PopupMenu::backgroundColourId,            juce::Colour (0xff0c0e15));
    setColour (juce::PopupMenu::textColourId,                  text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff1a1e2a));
    setColour (juce::PopupMenu::highlightedTextColourId,       textStrong);
}

void HeadlinerLookAndFeel::setTypefaces (juce::Typeface::Ptr sansRegular,
                                         juce::Typeface::Ptr sansSemiBold,
                                         juce::Typeface::Ptr monoRegular)
{
    sans     = std::move (sansRegular);
    sansSemi = std::move (sansSemiBold);
    mono     = std::move (monoRegular);
}

juce::Font HeadlinerLookAndFeel::uiFont (float height, bool semiBold) const
{
    return makeFont (semiBold && sansSemi != nullptr ? sansSemi : sans, height, semiBold);
}

juce::Font HeadlinerLookAndFeel::monoFont (float height) const
{
    return makeFont (mono, height, false);
}

void HeadlinerLookAndFeel::styleKnob (juce::Slider& s, juce::Colour accent)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    // 270° Drehbereich: von 7:30 Uhr bis 4:30 Uhr (wie im Design)
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                           juce::MathConstants<float>::pi * 2.75f, true);
    s.setMouseDragSensitivity (220);
    s.setColour (juce::Slider::rotarySliderFillColourId, accent);
}

void HeadlinerLookAndFeel::drawSegmentGroupBackground (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (hv::colours::control);
    g.fillRoundedRectangle (area, 7.0f);
}

//==============================================================================
// KNOB
// Alle Maße sind in "Design-Einheiten": der Knopf ist 100 x 100 Einheiten groß,
// genau wie im Canvas (SVG viewBox 0 0 100 100). u = Pixel pro Einheit.
//
//   Strichkranz   Radius 40.5 … 47.5, 57 Striche über 270°, Stärke 1.3
//   Schatten      Kreis r 37, 7 Einheiten nach unten versetzt, weich auslaufend
//   Körper        Kreis r 35, Verlauf #34373F (oben) -> #121418 (unten)
//   Lichtkante    Ring r 34.4, weiß 14 % oben -> 0 % Mitte
//   Kappe         Kreis r 26, Verlauf #17191D (oben) -> #2A2D34 (unten) = vertieft
//   Kappenkante   Ring r 25.4, weiß 14 % unten
//   Punkt         Radius 3.4, sitzt auf r 27, leuchtet in der Akzentfarbe
//==============================================================================
void HeadlinerLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float pos, float startAngle, float endAngle,
                                             juce::Slider& slider)
{
    using namespace hv::colours;

    const auto area    = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float size   = juce::jmin (area.getWidth(), area.getHeight());
    const float u      = size / 100.0f;
    const auto centre  = area.getCentre();
    const bool enabled = slider.isEnabled();
    const auto accent  = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const float valueAngle = startAngle + pos * (endAngle - startAngle);

    auto polar = [&] (float angle, float radius) { return centre.getPointOnCircumference (radius * u, angle); };

    // 1) Strichkranz ------------------------------------------------------------
    constexpr int numTicks = 57;
    for (int i = 0; i < numTicks; ++i)
    {
        const float t   = (float) i / (float) (numTicks - 1);
        const float a   = startAngle + t * (endAngle - startAngle);
        const bool  lit = enabled && t <= pos + 0.0001f;
        const juce::Line<float> tick (polar (a, 40.5f), polar (a, 47.5f));

        if (lit)
        {
            g.setColour (accent.withAlpha (0.22f));     // Glow
            g.drawLine (tick, 3.4f * u);
        }

        g.setColour (lit ? accent : tickOff);
        g.drawLine (tick, 1.3f * u);
    }

    // 2) Schatten ----------------------------------------------------------------
    {
        const auto sc = centre.translated (0.0f, 7.0f * u);
        juce::ColourGradient shadow (juce::Colours::black.withAlpha (0.85f), sc.x, sc.y,
                                     juce::Colours::transparentBlack, sc.x + 37.0f * u, sc.y, true);
        shadow.addColour (0.5, juce::Colours::black.withAlpha (0.85f));
        g.setGradientFill (shadow);
        g.fillEllipse (circleAt (sc, 37.0f * u));
    }

    // 3) Körper ------------------------------------------------------------------
    {
        const auto r = circleAt (centre, 35.0f * u);
        g.setGradientFill (juce::ColourGradient (knobTop, r.getX(), r.getY(),
                                                 knobBottom, r.getX() + r.getWidth() * 0.3f, r.getBottom(), false));
        g.fillEllipse (r);

        // Lichtkante oben
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.14f), r.getCentreX(), r.getY(),
                                                 juce::Colours::transparentWhite, r.getCentreX(), r.getCentreY(), false));
        g.drawEllipse (circleAt (centre, 34.4f * u), 1.2f * u);
    }

    // 4) Kappe (vertieft) --------------------------------------------------------
    {
        const auto r = circleAt (centre, 26.0f * u);
        g.setGradientFill (juce::ColourGradient (capTop, r.getX(), r.getY(),
                                                 capBottom, r.getX() + r.getWidth() * 0.3f, r.getBottom(), false));
        g.fillEllipse (r);

        // feine Lichtkante unten -> wirkt eingelassen
        g.setGradientFill (juce::ColourGradient (juce::Colours::transparentWhite, r.getCentreX(), r.getCentreY(),
                                                 juce::Colours::white.withAlpha (0.14f), r.getCentreX(), r.getBottom(), false));
        g.drawEllipse (circleAt (centre, 25.4f * u), 0.8f * u);
    }

    // Hover-Feedback
    if (enabled && slider.isMouseOverOrDragging())
    {
        g.setColour (juce::Colours::white.withAlpha (0.03f));
        g.fillEllipse (circleAt (centre, 35.0f * u));
    }

    // 5) Leuchtpunkt -------------------------------------------------------------
    {
        const auto p   = polar (valueAngle, 27.0f);
        const auto dot = enabled ? accent : tickOff;

        for (int i = 3; i >= 1; --i)                     // Glow in 3 Stufen
        {
            g.setColour (dot.withAlpha (0.10f));
            g.fillEllipse (circleAt (p, (3.4f + (float) i * 1.7f) * u));
        }

        g.setColour (dot);
        g.fillEllipse (circleAt (p, 3.4f * u));
    }
}

//==============================================================================
// SWITCH (ToggleButton): Track 26 x 14, Knopf 10 px, Label rechts daneben
//==============================================================================
void HeadlinerLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                             bool highlighted, bool /*down*/)
{
    using namespace hv::colours;

    const auto bounds = button.getLocalBounds().toFloat();
    const auto accent = button.findColour (juce::ToggleButton::tickColourId);
    const bool on     = button.getToggleState();

    const auto track = juce::Rectangle<float> (26.0f, 14.0f)
                           .withPosition (bounds.getX(), bounds.getCentreY() - 7.0f);

    if (on)
    {
        g.setColour (accent.withAlpha (0.25f));
        g.fillRoundedRectangle (track.expanded (2.0f), 9.0f);
    }

    g.setColour (on ? accent : switchOff);
    g.fillRoundedRectangle (track, 7.0f);

    const float knobX = on ? track.getRight() - 7.0f : track.getX() + 7.0f;
    g.setColour (textStrong);
    g.fillEllipse (circleAt ({ knobX, track.getCentreY() }, 5.0f));

    auto textCol = button.findColour (juce::ToggleButton::textColourId);
    if (highlighted) textCol = textCol.brighter (0.2f);
    if (! button.isEnabled()) textCol = textCol.withMultipliedAlpha (0.5f);

    g.setColour (textCol);
    g.setFont (uiFont (11.0f));
    g.drawFittedText (button.getButtonText(),
                      bounds.withTrimmedLeft (track.getWidth() + 8.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1);
}

//==============================================================================
// SEGMENT-BUTTONS (TextButton mit setClickingTogglesState + RadioGroup)
//==============================================================================
void HeadlinerLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                 const juce::Colour& /*backgroundColour*/,
                                                 bool highlighted, bool down)
{
    const auto r      = button.getLocalBounds().toFloat();
    const auto accent = button.findColour (juce::TextButton::buttonOnColourId);

    if (button.getToggleState())
    {
        g.setColour (accent.withAlpha (0.16f));
        g.fillRoundedRectangle (r, 5.0f);
    }
    else if (highlighted || down)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.06f : 0.03f));
        g.fillRoundedRectangle (r, 5.0f);
    }
}

void HeadlinerLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                           bool /*highlighted*/, bool /*down*/)
{
    const bool on     = button.getToggleState();
    const auto accent = button.findColour (juce::TextButton::buttonOnColourId);

    // aktiv: helle Variante der Akzentfarbe, inaktiv: grau
    auto col = on ? accent.interpolatedWith (juce::Colours::white, 0.55f)
                  : button.findColour (juce::TextButton::textColourOffId);
    if (! button.isEnabled()) col = col.withMultipliedAlpha (0.5f);

    g.setColour (col);
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, 1);
}

juce::Font HeadlinerLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return uiFont (11.0f, true);
}

juce::Font HeadlinerLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();   // Labels bekommen ihre Font explizit (uiFont / monoFont)
}
