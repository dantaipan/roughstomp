#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

/** Draws knobs as black pedal knobs with a white pointer. Moves to the shared
    ui/ library in Phase 3. */
class PedalLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PedalLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;
};

/** Round metal footswitch. Its toggle state is the bypass parameter. */
class Footswitch final : public juce::Button
{
public:
    Footswitch() : juce::Button ("Footswitch") { setClickingTogglesState (true); }

    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
};

class OverdriveEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit OverdriveEditor (OverdriveProcessor&);
    ~OverdriveEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider;
        juce::String label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setUpKnob (Knob&, const juce::ParameterID&, const juce::String& label);

    OverdriveProcessor& overdriveProcessor;
    PedalLookAndFeel lookAndFeel;

    Knob level, tone, drive, mix;
    Footswitch footswitch;
    juce::ComboBox presetBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> footswitchAttachment;

    juce::Rectangle<float> ledArea;
    bool ledOn = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OverdriveEditor)
};
