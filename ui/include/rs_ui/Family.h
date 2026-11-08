#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/* The look RoughStomp shares with RoughTrack: dark panels, cream lettering,
   amber highlights, soft buttons and faders with real caps.

   This mirrors RoughTrack's console look (app/source/ConsoleLookAndFeel.* in
   the RoughTrack repo) colour for colour. Keep the two in step until
   RoughTrack uses this copy directly. */
namespace rs::ui
{

namespace family
{
    const juce::Colour panel   { 0xff24262a };
    const juce::Colour strip   { 0xff2e3136 };
    const juce::Colour recess  { 0xff15171a };
    const juce::Colour widget  { 0xff3b3f46 };
    const juce::Colour outline { 0xff4a4e55 };
    const juce::Colour cream   { 0xffe9e2d0 };
    const juce::Colour dimText { 0xff9a9689 };
    const juce::Colour amber   { 0xffe8a33d };
    const juce::Colour red     { 0xffd8432f };
    const juce::Colour green   { 0xff6fbf73 };
    const juce::Colour blue    { 0xff5b8fbf };

    /** Text in the family style: the system font, regular weight unless bold. */
    juce::Font font (float height, bool bold = false);
}

/** Buttons, menus, knobs and faders in the family style. */
class FamilyLookAndFeel : public juce::LookAndFeel_V4
{
public:
    FamilyLookAndFeel();

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    /** The console knob: a plain dark disc with a cream pointer. */
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour, bool isHighlighted, bool isDown) override;

    int getSliderThumbRadius (juce::Slider&) override;

    /** A fader cap: light-to-dark gradient, black edge, cream line across. */
    static void drawCap (juce::Graphics&, juce::Rectangle<float> cap, bool isVertical);
};

/** A console knob with its name underneath, attached to a parameter. For the
    controls around the pedals (input, output, loop); pedals use PedalKnob. */
class ConsoleKnob final : public juce::Component
{
public:
    ConsoleKnob (juce::AudioProcessorValueTreeState&, const juce::ParameterID&, const juce::String& label);

    /** Where the value popup appears while dragging (usually the editor). */
    void setPopupParent (juce::Component*);

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider slider;

private:
    juce::String label;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

} // namespace rs::ui
