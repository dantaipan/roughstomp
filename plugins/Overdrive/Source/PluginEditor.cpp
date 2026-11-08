#include "PluginEditor.h"

OverdriveEditor::OverdriveEditor (OverdriveProcessor& p)
    : AudioProcessorEditor (p),
      overdriveProcessor (p),
      level (p.apvts, ParamIDs::level, "LEVEL"),
      tone  (p.apvts, ParamIDs::tone,  "TONE"),
      drive (p.apvts, ParamIDs::drive, "DRIVE"),
      mix   (p.apvts, ParamIDs::mix,   "MIX", true),
      modeSwitch (*p.apvts.getParameter (ParamIDs::mode.getParamID()), { "SOFT", "HARD", "ASYM" }, lookAndFeel.colours),
      led (lookAndFeel.colours),
      footswitch (p.apvts, ParamIDs::bypass)
{
    setLookAndFeel (&lookAndFeel);

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

    for (auto* knob : { &level, &tone, &drive, &mix })
    {
        knob->setPopupParent (this);
        addAndMakeVisible (*knob);
    }

    modeSwitch.setTooltip ("Clipping: soft (smooth), hard (distortion), asymmetric (warm, tube-like)");
    addAndMakeVisible (modeSwitch);
    addAndMakeVisible (led);
    addAndMakeVisible (footswitch);

    setSize (320, 540);
    startTimerHz (20);
    timerCallback();
}

OverdriveEditor::~OverdriveEditor()
{
    setLookAndFeel (nullptr);
}

void OverdriveEditor::timerCallback()
{
    // LED follows bypass, including host automation
    led.setOn (! footswitch.getToggleState());

    // Follow preset changes made from the host's own menu
    const int program = overdriveProcessor.getCurrentProgram() + 1;
    if (presetBox.getSelectedId() != program && ! presetBox.isPopupActive())
        presetBox.setSelectedId (program, juce::dontSendNotification);
}

void OverdriveEditor::paint (juce::Graphics& g)
{
    rs::ui::paintEnclosure (g, getLocalBounds().toFloat(), lookAndFeel.colours);
    rs::ui::paintTitle (g, titleArea, "Overdrive", lookAndFeel.colours);
}

void OverdriveEditor::resized()
{
    auto box = getLocalBounds().reduced (8);

    presetBox.setBounds (box.removeFromTop (40).reduced (60, 8).translated (0, 6));

    // Level, Tone, Drive (classic pedal order)
    auto row = box.removeFromTop (138).reduced (14, 10);
    const int w = row.getWidth() / 3;
    level.setBounds (row.removeFromLeft (w).reduced (4, 0));
    tone.setBounds  (row.removeFromLeft (w).reduced (4, 0));
    drive.setBounds (row.reduced (4, 0));

    // Mix, mode toggle, LED
    auto controls = box.removeFromTop (92).reduced (18, 4);
    mix.setBounds (controls.removeFromLeft (70).withSizeKeepingCentre (70, 80));
    led.setBounds (controls.removeFromRight (50).withSizeKeepingCentre (50, 50));
    modeSwitch.setBounds (controls.withSizeKeepingCentre (juce::jmin (150, controls.getWidth()), 44));

    titleArea = box.removeFromTop (70).toFloat().reduced (10.0f, 6.0f);

    // Footswitch at the bottom, where your foot would go
    footswitch.setBounds (box.removeFromBottom (150).withSizeKeepingCentre (110, 110));
}
