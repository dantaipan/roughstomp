#include "PluginEditor.h"

namespace Colours
{
    const juce::Colour body      { 0xffb8402a };   // brick red enclosure
    const juce::Colour bodyDark  { 0xff8e2c1c };
    const juce::Colour ink       { 0xfff3e6cf };   // cream lettering
    const juce::Colour knob      { 0xff1c1c1c };
    const juce::Colour ledOn     { 0xffff3b2f };
    const juce::Colour ledOff    { 0xff4a1410 };
}

namespace
{
juce::Font font (float height, bool bold = true)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace

//==============================================================================
PedalLookAndFeel::PedalLookAndFeel()
{
    setColour (juce::BubbleComponent::backgroundColourId, juce::Colours::black.withAlpha (0.85f));
    setColour (juce::BubbleComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::textColourId, Colours::ink);

    // Preset menu: dark plate on the enclosure
    setColour (juce::ComboBox::backgroundColourId, juce::Colours::black.withAlpha (0.35f));
    setColour (juce::ComboBox::outlineColourId, juce::Colours::black.withAlpha (0.5f));
    setColour (juce::ComboBox::textColourId, Colours::ink);
    setColour (juce::ComboBox::arrowColourId, Colours::ink);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1c1c1c));
    setColour (juce::PopupMenu::textColourId, Colours::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::body);
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
    g.setColour (Colours::ink.withAlpha (0.7f));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
        const auto p1 = centre.getPointOnCircumference (radius * 0.88f, a);
        const auto p2 = centre.getPointOnCircumference (radius * (i % 5 == 0 ? 1.0f : 0.95f), a);
        g.drawLine ({ p1, p2 }, i % 5 == 0 ? 2.0f : 1.2f);
    }

    // Knob body with a soft drop shadow and a subtle top highlight
    const float r = radius * 0.78f;
    const auto knobRect = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (knobRect.translated (0.0f, 3.0f));

    g.setGradientFill (juce::ColourGradient (Colours::knob.brighter (0.35f), centre.translated (0.0f, -r),
                                             Colours::knob, centre.translated (0.0f, r), false));
    g.fillEllipse (knobRect);

    g.setColour (juce::Colours::black);
    g.drawEllipse (knobRect, 1.5f);

    // Pointer
    g.setColour (juce::Colours::white);
    g.drawLine ({ centre.getPointOnCircumference (r * 0.25f, angle),
                  centre.getPointOnCircumference (r * 0.9f, angle) }, 3.0f);
}

//==============================================================================
void Footswitch::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    const auto b = getLocalBounds().toFloat().reduced (6.0f);
    const float size = juce::jmin (b.getWidth(), b.getHeight());
    auto outer = juce::Rectangle<float> (size, size).withCentre (b.getCentre());

    // Hex-nut style collar
    g.setColour (juce::Colour (0xff9a9a9a));
    g.fillEllipse (outer);
    g.setColour (juce::Colour (0xff5a5a5a));
    g.drawEllipse (outer, 1.5f);

    // Chrome cap, pressed in slightly while held
    auto cap = outer.reduced (size * (isDown ? 0.2f : 0.17f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f4f4), cap.getTopLeft(),
                                             juce::Colour (0xff7d7d7d), cap.getBottomRight(), false));
    g.fillEllipse (cap);
    g.setColour (juce::Colour (isHighlighted ? 0xff303030 : 0xff505050));
    g.drawEllipse (cap, 1.5f);
}

//==============================================================================
OverdriveEditor::OverdriveEditor (OverdriveProcessor& p)
    : AudioProcessorEditor (p), overdriveProcessor (p)
{
    setLookAndFeel (&lookAndFeel);

    setUpKnob (level, ParamIDs::level, "LEVEL");
    setUpKnob (tone,  ParamIDs::tone,  "TONE");
    setUpKnob (drive, ParamIDs::drive, "DRIVE");
    setUpKnob (mix,   ParamIDs::mix,   "MIX");

    footswitch.setTooltip ("Bypass");
    addAndMakeVisible (footswitch);
    footswitchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        overdriveProcessor.apvts, ParamIDs::bypass.getParamID(), footswitch);

    // Factory presets. Hosts like Logic also list these, but RoughTrack
    // doesn't, so the pedal carries its own menu.
    for (int i = 0; i < overdriveProcessor.getNumPrograms(); ++i)
        presetBox.addItem (overdriveProcessor.getProgramName (i), i + 1);
    presetBox.setSelectedId (overdriveProcessor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.setJustificationType (juce::Justification::centred);
    presetBox.setTooltip ("Factory presets");
    presetBox.onChange = [this]
    {
        if (const int index = presetBox.getSelectedId() - 1; index >= 0)
            overdriveProcessor.setCurrentProgram (index);
    };
    addAndMakeVisible (presetBox);

    setSize (320, 500);
    startTimerHz (20); // LED follows bypass, including host automation
    timerCallback();
}

OverdriveEditor::~OverdriveEditor()
{
    setLookAndFeel (nullptr);
}

void OverdriveEditor::setUpKnob (Knob& k, const juce::ParameterID& id, const juce::String& label)
{
    k.label = label;
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k.slider.setPopupDisplayEnabled (true, true, this);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    addAndMakeVisible (k.slider);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        overdriveProcessor.apvts, id.getParamID(), k.slider);
}

void OverdriveEditor::timerCallback()
{
    // Follow preset changes made from the host's own menu
    const int program = overdriveProcessor.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != program && ! presetBox.isPopupActive())
        presetBox.setSelectedId (program, juce::dontSendNotification);

    const bool on = ! footswitch.getToggleState();
    if (on != ledOn)
    {
        ledOn = on;
        repaint (ledArea.toNearestInt().expanded (12));
    }
}

void OverdriveEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Enclosure
    g.fillAll (juce::Colour (0xff141414));
    const auto box = bounds.reduced (8.0f);
    g.setGradientFill (juce::ColourGradient (Colours::body, box.getTopLeft(),
                                             Colours::bodyDark, box.getBottomRight(), false));
    g.fillRoundedRectangle (box, 18.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (box, 18.0f, 2.0f);

    // Knob labels
    g.setColour (Colours::ink);
    g.setFont (font (13.0f));
    for (auto* k : { &level, &tone, &drive })
        g.drawText (k->label, k->slider.getBounds().withY (k->slider.getBottom() - 4).withHeight (16),
                    juce::Justification::centred);

    g.setFont (font (11.0f));
    g.drawText (mix.label, mix.slider.getBounds().withY (mix.slider.getBottom() - 4).withHeight (14),
                juce::Justification::centred);

    // LED with glow
    if (ledOn)
    {
        g.setGradientFill (juce::ColourGradient (Colours::ledOn.withAlpha (0.6f), ledArea.getCentre(),
                                                 Colours::ledOn.withAlpha (0.0f),
                                                 ledArea.getCentre().translated (ledArea.getWidth() * 1.6f, 0.0f), true));
        g.fillEllipse (ledArea.expanded (ledArea.getWidth()));
    }
    g.setColour (ledOn ? Colours::ledOn : Colours::ledOff);
    g.fillEllipse (ledArea);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (ledArea, 1.0f);

    // Branding
    auto text = box.reduced (20.0f);
    text.removeFromTop (260.0f);
    g.setColour (Colours::ink);
    g.setFont (font (34.0f));
    g.drawText ("OVERDRIVE", text.removeFromTop (40.0f), juce::Justification::centred);
    g.setFont (font (13.0f, false));
    g.drawText ("RoughStomp", text.removeFromTop (18.0f), juce::Justification::centred);
}

void OverdriveEditor::resized()
{
    auto box = getLocalBounds().reduced (8);

    presetBox.setBounds (box.removeFromTop (40).reduced (60, 8).translated (0, 6));

    // Top row: Level, Tone, Drive (classic pedal order)
    auto row = box.removeFromTop (130).reduced (14, 18);
    const int w = row.getWidth() / 3;
    level.slider.setBounds (row.removeFromLeft (w).reduced (4));
    tone.slider.setBounds  (row.removeFromLeft (w).reduced (4));
    drive.slider.setBounds (row.reduced (4));

    // Small Mix knob, centred, with the LED beside it
    auto mixRow = box.removeFromTop (88);
    mix.slider.setBounds (mixRow.withSizeKeepingCentre (64, 64).translated (0, -6));
    ledArea = juce::Rectangle<float> (14.0f, 14.0f)
                  .withCentre (mixRow.getCentre().toFloat().translated (90.0f, -6.0f));

    // Footswitch at the bottom, where your foot would go
    footswitch.setBounds (box.removeFromBottom (140).withSizeKeepingCentre (110, 110));
}
