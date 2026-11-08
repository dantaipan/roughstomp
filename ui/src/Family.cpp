#include "rs_ui/Family.h"

namespace rs::ui
{

namespace
{
// RoughTrack's console fader caps
constexpr float capLength = 34.0f;
constexpr float capWidth = 26.0f;
constexpr float slotWidth = 4.0f;
constexpr float smallCapLength = 14.0f;
constexpr float smallCapWidth = 18.0f;
constexpr float cornerSize = 3.0f;
constexpr int labelHeight = 16;
constexpr int maxKnobSize = 44;
} // namespace

juce::Font family::font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
FamilyLookAndFeel::FamilyLookAndFeel()
{
    // Exactly RoughTrack's console set-up: everything else (menus, combo boxes,
    // toggles, dialogs) follows from the colour scheme, as it does there
    using namespace family;
    setColourScheme ({ panel, widget, strip, outline, cream, amber, juce::Colours::black, amber, cream });

    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::TextButton::textColourOffId, cream);
    setColour (juce::Label::textColourId, cream);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxTextColourId, cream);
    setColour (juce::ScrollBar::thumbColourId, outline);
    setColour (juce::TooltipWindow::backgroundColourId, recess);
    setColour (juce::TooltipWindow::textColourId, cream);
}

void FamilyLookAndFeel::drawCap (juce::Graphics& g, juce::Rectangle<float> cap, bool isVertical)
{
    const auto dark = juce::Colour (0xff202225);
    const auto light = juce::Colour (0xff6a6e75);

    g.setGradientFill (isVertical ? juce::ColourGradient::vertical (light, cap.getY(), dark, cap.getBottom())
                                  : juce::ColourGradient::horizontal (light, cap.getX(), dark, cap.getRight()));
    g.fillRoundedRectangle (cap, 2.0f);

    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (cap, 2.0f, 1.0f);

    g.setColour (family::cream);
    if (isVertical)
        g.fillRect (cap.withHeight (2.0f).withCentre (cap.getCentre()).reduced (1.0f, 0.0f));
    else
        g.fillRect (cap.withWidth (2.0f).withCentre (cap.getCentre()).reduced (0.0f, 1.0f));
}

void FamilyLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                                          float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const bool isVertical = style == juce::Slider::LinearVertical;

    g.setColour (family::recess);
    if (isVertical)
    {
        g.fillRoundedRectangle (area.withWidth (slotWidth).withCentre (area.getCentre()), 2.0f);
        drawCap (g, juce::Rectangle<float> (juce::jmin (capWidth, area.getWidth()), capLength).withCentre ({ area.getCentreX(), sliderPos }), true);
    }
    else
    {
        g.fillRoundedRectangle (area.withHeight (slotWidth).withCentre (area.getCentre()), 2.0f);
        drawCap (g, juce::Rectangle<float> (smallCapLength, juce::jmin (smallCapWidth, area.getHeight())).withCentre ({ sliderPos, area.getCentreY() }), false);
    }
}

void FamilyLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPosProportional,
                                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto knob = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (bounds.getCentre());
    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    g.setGradientFill (juce::ColourGradient::vertical (juce::Colour (0xff5a5e65), knob.getY(), juce::Colour (0xff1e2023), knob.getBottom()));
    g.fillEllipse (knob);

    g.setColour (juce::Colours::black);
    g.drawEllipse (knob, 1.0f);

    const auto centre = knob.getCentre();
    g.setColour (family::cream);
    g.drawLine ({ centre.getPointOnCircumference (radius * 0.4f, angle), centre.getPointOnCircumference (radius - 2.0f, angle) }, 2.0f);
}

void FamilyLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                              bool isHighlighted, bool isDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = backgroundColour;

    if (isDown)
        colour = colour.darker (0.2f);
    else if (isHighlighted)
        colour = colour.brighter (0.1f);

    g.setGradientFill (juce::ColourGradient::vertical (colour.brighter (0.12f), bounds.getY(), colour.darker (0.12f), bounds.getBottom()));
    g.fillRoundedRectangle (bounds, cornerSize);

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds, cornerSize, 1.0f);
}

int FamilyLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    if (slider.getSliderStyle() == juce::Slider::LinearVertical)
        return static_cast<int> (capLength / 2.0f);
    return static_cast<int> (smallCapLength / 2.0f);
}

//==============================================================================
ConsoleKnob::ConsoleKnob (juce::AudioProcessorValueTreeState& apvts, const juce::ParameterID& id, const juce::String& labelText)
    : label (labelText), attachment (apvts, id.getParamID(), slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    addAndMakeVisible (slider);
}

void ConsoleKnob::setPopupParent (juce::Component* parent)
{
    slider.setPopupDisplayEnabled (true, true, parent);
}

void ConsoleKnob::paint (juce::Graphics& g)
{
    g.setColour (family::dimText);
    g.setFont (family::font (13.0f));
    g.drawFittedText (label, getLocalBounds().removeFromBottom (labelHeight), juce::Justification::centred, 1, 0.8f);
}

void ConsoleKnob::resized()
{
    // RoughTrack's console knobs are small: about 44 px across at most
    auto b = getLocalBounds();
    b.removeFromBottom (labelHeight + 2);
    const int size = juce::jmin (b.getWidth(), b.getHeight(), maxKnobSize);
    slider.setBounds (b.withSizeKeepingCentre (size, size).withY (b.getBottom() - size));
}

} // namespace rs::ui
