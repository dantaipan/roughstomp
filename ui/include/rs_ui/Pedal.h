#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "rs_ui/Family.h"

/* Shared pedal GUI. Every RoughStomp pedal uses these so they look like one
   family; each pedal picks its own PedalColours. Everything is drawn in
   code, with no image files. */
namespace rs::ui
{

struct PedalColours
{
    juce::Colour body     { 0xffb8402a };   // enclosure, top-left of the gradient
    juce::Colour bodyDark { 0xff8e2c1c };   // enclosure, bottom-right
    juce::Colour ink      { family::cream };   // lettering
    juce::Colour knob     { 0xff1c1c1c };
    juce::Colour ledOn    { 0xffff3b2f };
    juce::Colour ledOff   { 0xff4a1410 };
};

juce::Font pedalFont (float height, bool bold = true);

//==============================================================================
/** Black pedal knobs with a white pointer and scale ticks, small faders and
    dark menu plates on the enclosure. Everything else (popup menus, buttons,
    tooltips) is the family look. */
class PedalLookAndFeel final : public FamilyLookAndFeel
{
public:
    explicit PedalLookAndFeel (const PedalColours& = {});

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    /** Vertical sliders are drawn as small faders: a slot and a cap. */
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;

    const PedalColours colours;
};

//==============================================================================
/** A rotary knob with its label underneath, attached to a parameter. */
class PedalKnob final : public juce::Component
{
public:
    PedalKnob (juce::AudioProcessorValueTreeState&, const juce::ParameterID&, const juce::String& label,
               bool small = false);

    /** Where the value popup appears while dragging (usually the editor). */
    void setPopupParent (juce::Component*);

    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider slider;

private:
    juce::String label;
    bool isSmall;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

//==============================================================================
/** Round metal footswitch. Its toggle state is attached to a bypass parameter. */
class Footswitch final : public juce::Button
{
public:
    Footswitch (juce::AudioProcessorValueTreeState&, const juce::ParameterID& bypass);

    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;

private:
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
};

//==============================================================================
/** Status LED with a glow. Leave room around the lamp for the glow. */
class Led final : public juce::Component
{
public:
    explicit Led (const PedalColours& c) : colours (c) { setInterceptsMouseClicks (false, false); }

    void setOn (bool shouldBeOn);
    bool isOn() const noexcept { return on; }

    void paint (juce::Graphics&) override;

private:
    PedalColours colours;
    bool on = true;
};

//==============================================================================
/** Mini toggle switch for a choice parameter, with a label over each position.
    Click a position, or drag the lever. */
class ToggleSwitch final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    ToggleSwitch (juce::RangedAudioParameter&, const juce::StringArray& positionLabels, const PedalColours& = {});

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    int getPosition() const noexcept { return position; }

private:
    int positionAt (float x) const;
    float positionX (int index) const;
    void choose (int index);

    juce::StringArray labels;
    PedalColours colours;
    int position = 0;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Draws the enclosure: a rounded, shaded box on a dark background. */
void paintEnclosure (juce::Graphics&, juce::Rectangle<float> bounds, const PedalColours&);

/** Draws the pedal's name with the RoughStomp brand underneath. */
void paintTitle (juce::Graphics&, juce::Rectangle<float> area, const juce::String& pedalName, const PedalColours&);

} // namespace rs::ui
