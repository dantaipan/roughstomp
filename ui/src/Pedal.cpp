#include "rs_ui/Pedal.h"

namespace rs::ui
{

juce::Font pedalFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
PedalLookAndFeel::PedalLookAndFeel (const PedalColours& c) : colours (c)
{
    setColour (juce::BubbleComponent::backgroundColourId, juce::Colours::black.withAlpha (0.85f));
    setColour (juce::BubbleComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::textColourId, colours.ink);

    // Preset menus: a dark plate on the enclosure
    setColour (juce::ComboBox::backgroundColourId, juce::Colours::black.withAlpha (0.35f));
    setColour (juce::ComboBox::outlineColourId, juce::Colours::black.withAlpha (0.5f));
    setColour (juce::ComboBox::textColourId, colours.ink);
    setColour (juce::ComboBox::arrowColourId, colours.ink);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1c1c1c));
    setColour (juce::PopupMenu::textColourId, colours.ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours.body);
}

void PedalLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float startAngle, float endAngle,
                                         juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    // Scale ticks around the knob
    g.setColour (colours.ink.withAlpha (0.7f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + static_cast<float> (i) / 10.0f * (endAngle - startAngle);
        const auto p1 = centre.getPointOnCircumference (radius * 0.88f, a);
        const auto p2 = centre.getPointOnCircumference (radius * (i % 5 == 0 ? 1.0f : 0.95f), a);
        g.drawLine ({ p1, p2 }, i % 5 == 0 ? 2.0f : 1.2f);
    }

    // Knob body with a soft drop shadow and a subtle top highlight
    const float r = radius * 0.78f;
    const auto knobRect = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (knobRect.translated (0.0f, 3.0f));

    g.setGradientFill (juce::ColourGradient (colours.knob.brighter (0.35f), centre.translated (0.0f, -r),
                                             colours.knob, centre.translated (0.0f, r), false));
    g.fillEllipse (knobRect);

    g.setColour (juce::Colours::black);
    g.drawEllipse (knobRect, 1.5f);

    // Pointer
    g.setColour (juce::Colours::white);
    g.drawLine ({ centre.getPointOnCircumference (r * 0.25f, angle),
                  centre.getPointOnCircumference (r * 0.9f, angle) }, 3.0f);
}

int PedalLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    return slider.isVertical() ? 5 : LookAndFeel_V4::getSliderThumbRadius (slider);
}

void PedalLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float minSliderPos, float maxSliderPos,
                                         juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float cx = bounds.getCentreX();

    // The slot the fader runs in
    // (JUCE only fills in min/maxSliderPos for two- and three-value sliders,
    // so the travel comes from the bounds and the thumb radius)
    const float inset = static_cast<float> (getSliderThumbRadius (slider));
    const auto slot = juce::Rectangle<float> (cx - 1.5f, bounds.getY() + inset - 3.0f, 3.0f, bounds.getHeight() - 2.0f * inset + 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (slot, 1.5f);

    // The cap
    const float capW = juce::jmin (bounds.getWidth() - 2.0f, 13.0f), capH = 9.0f;
    const auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ cx, sliderPos });
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (cap.translated (0.0f, 1.5f), 2.0f);
    g.setGradientFill (juce::ColourGradient (colours.knob.brighter (0.5f), cap.getTopLeft(),
                                             colours.knob, cap.getBottomLeft(), false));
    g.fillRoundedRectangle (cap, 2.0f);
    g.setColour (juce::Colours::white);
    g.drawLine (cap.getX() + 2.0f, sliderPos, cap.getRight() - 2.0f, sliderPos, 1.5f);
}

//==============================================================================
PedalKnob::PedalKnob (juce::AudioProcessorValueTreeState& apvts, const juce::ParameterID& id,
                      const juce::String& labelText, bool small)
    : label (labelText),
      isSmall (small),
      attachment (apvts, id.getParamID(), slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    addAndMakeVisible (slider);
}

void PedalKnob::setPopupParent (juce::Component* parent)
{
    slider.setPopupDisplayEnabled (true, true, parent);
}

void PedalKnob::paint (juce::Graphics& g)
{
    if (auto* lf = dynamic_cast<PedalLookAndFeel*> (&getLookAndFeel()))
        g.setColour (lf->colours.ink);
    else
        g.setColour (juce::Colours::white);

    g.setFont (pedalFont (isSmall ? 11.0f : 13.0f));
    g.drawFittedText (label, getLocalBounds().removeFromBottom (isSmall ? 14 : 16), juce::Justification::centred, 1, 0.7f);
}

void PedalKnob::resized()
{
    auto b = getLocalBounds();
    b.removeFromBottom (isSmall ? 12 : 14);
    slider.setBounds (b);
}

//==============================================================================
Footswitch::Footswitch (juce::AudioProcessorValueTreeState& apvts, const juce::ParameterID& bypass)
    : juce::Button ("Footswitch"),
      attachment (apvts, bypass.getParamID(), *this)
{
    setClickingTogglesState (true);
    setTooltip ("Bypass");
}

void Footswitch::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    const auto b = getLocalBounds().toFloat().reduced (6.0f);
    const float size = juce::jmin (b.getWidth(), b.getHeight());
    const auto outer = juce::Rectangle<float> (size, size).withCentre (b.getCentre());

    // Collar
    g.setColour (juce::Colour (0xff9a9a9a));
    g.fillEllipse (outer);
    g.setColour (juce::Colour (0xff5a5a5a));
    g.drawEllipse (outer, 1.5f);

    // Chrome cap, pressed in slightly while held
    const auto cap = outer.reduced (size * (isDown ? 0.2f : 0.17f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f4f4), cap.getTopLeft(),
                                             juce::Colour (0xff7d7d7d), cap.getBottomRight(), false));
    g.fillEllipse (cap);
    g.setColour (juce::Colour (isHighlighted ? 0xff303030 : 0xff505050));
    g.drawEllipse (cap, 1.5f);
}

//==============================================================================
void Led::setOn (bool shouldBeOn)
{
    if (on != shouldBeOn)
    {
        on = shouldBeOn;
        repaint();
    }
}

void Led::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float lamp = juce::jmin (14.0f, juce::jmin (b.getWidth(), b.getHeight()) / 3.0f);
    const auto area = juce::Rectangle<float> (lamp, lamp).withCentre (b.getCentre());

    if (on)
    {
        g.setGradientFill (juce::ColourGradient (colours.ledOn.withAlpha (0.6f), area.getCentre(),
                                                 colours.ledOn.withAlpha (0.0f),
                                                 area.getCentre().translated (lamp * 1.6f, 0.0f), true));
        g.fillEllipse (area.expanded (lamp));
    }

    g.setColour (on ? colours.ledOn : colours.ledOff);
    g.fillEllipse (area);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (area, 1.0f);
}

//==============================================================================
ToggleSwitch::ToggleSwitch (juce::RangedAudioParameter& param, const juce::StringArray& positionLabels,
                            const PedalColours& c)
    : labels (positionLabels),
      colours (c),
      attachment (param, [this] (float value)
                  {
                      position = juce::jlimit (0, labels.size() - 1, juce::roundToInt (value));
                      repaint();
                  })
{
    attachment.sendInitialUpdate();
}

float ToggleSwitch::positionX (int index) const
{
    const auto w = static_cast<float> (getWidth());
    const auto slot = w / static_cast<float> (labels.size());
    return slot * (static_cast<float> (index) + 0.5f);
}

int ToggleSwitch::positionAt (float x) const
{
    const auto slot = static_cast<float> (getWidth()) / static_cast<float> (labels.size());
    return juce::jlimit (0, labels.size() - 1, static_cast<int> (x / slot));
}

void ToggleSwitch::choose (int index)
{
    if (index != position)
        attachment.setValueAsCompleteGesture (static_cast<float> (index));
}

void ToggleSwitch::mouseDown (const juce::MouseEvent& e) { choose (positionAt (e.position.x)); }
void ToggleSwitch::mouseDrag (const juce::MouseEvent& e) { choose (positionAt (e.position.x)); }

void ToggleSwitch::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // Labels across the top; the chosen one is brighter
    auto labelRow = b.removeFromTop (16.0f);
    g.setFont (pedalFont (10.5f));
    for (int i = 0; i < labels.size(); ++i)
    {
        g.setColour (colours.ink.withAlpha (i == position ? 1.0f : 0.55f));
        g.drawText (labels[i], juce::Rectangle<float> (positionX (i) - 30.0f, labelRow.getY(), 60.0f, labelRow.getHeight()),
                    juce::Justification::centred);
    }

    // Slot plate
    const auto plate = juce::Rectangle<float> (positionX (0) - 10.0f, b.getCentreY() - 8.0f,
                                               positionX (labels.size() - 1) - positionX (0) + 20.0f, 16.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (plate, 8.0f);

    // Detent dots
    g.setColour (colours.ink.withAlpha (0.35f));
    for (int i = 0; i < labels.size(); ++i)
        g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ positionX (i), plate.getCentreY() }));

    // Chrome lever
    const auto lever = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ positionX (position), plate.getCentreY() });
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f4f4), lever.getTopLeft(),
                                             juce::Colour (0xff7d7d7d), lever.getBottomRight(), false));
    g.fillEllipse (lever);
    g.setColour (juce::Colour (0xff404040));
    g.drawEllipse (lever, 1.0f);
}

//==============================================================================
void paintEnclosure (juce::Graphics& g, juce::Rectangle<float> bounds, const PedalColours& colours)
{
    g.setColour (juce::Colour (0xff141414));
    g.fillRect (bounds);

    const auto box = bounds.reduced (8.0f);
    g.setGradientFill (juce::ColourGradient (colours.body, box.getTopLeft(),
                                             colours.bodyDark, box.getBottomRight(), false));
    g.fillRoundedRectangle (box, 18.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (box, 18.0f, 2.0f);
}

void paintTitle (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& pedalName, const PedalColours& colours)
{
    g.setColour (colours.ink);
    g.setFont (pedalFont (34.0f));
    g.drawText (pedalName.toUpperCase(), area.removeFromTop (40.0f), juce::Justification::centred);
    g.setFont (pedalFont (13.0f, false));
    g.drawText ("RoughStomp", area.removeFromTop (18.0f), juce::Justification::centred);
}

} // namespace rs::ui
