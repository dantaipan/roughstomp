#pragma once

#include "PluginProcessor.h"
#include "rs_ui/Pedal.h"

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

    OverdriveProcessor& overdriveProcessor;
    rs::ui::PedalLookAndFeel lookAndFeel;

    juce::ComboBox presetBox;
    rs::ui::PedalKnob level, tone, drive, mix;
    rs::ui::ToggleSwitch modeSwitch;
    rs::ui::Led led;
    rs::ui::Footswitch footswitch;

    juce::Rectangle<float> titleArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OverdriveEditor)
};
