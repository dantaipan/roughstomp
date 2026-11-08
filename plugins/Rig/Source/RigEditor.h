#pragma once

#include "RigProcessor.h"
#include "SlotView.h"
#include "rs_ui/Pedal.h"

/** A box for something loaded from a file (amp capture, cabinet IR):
    title, Load/Clear buttons, the file's name, any problem, and an on switch. */
class FilePanel final : public juce::Component
{
public:
    FilePanel (const juce::String& title, const juce::String& emptyText,
               juce::AudioProcessorValueTreeState&, const juce::ParameterID& onParam);

    void show (const juce::File&, const juce::String& problem);

    std::function<void()> onLoad, onClear;

    /** Room on the right for extra controls (the amp's knobs). */
    juce::Rectangle<int> getExtraArea() const noexcept { return extraArea; }
    void setExtraWidth (int w) { extraWidth = w; resized(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title, emptyText;
    juce::TextButton load { "Load..." }, clear { "Clear" };
    juce::Label name, problem;
    juce::ToggleButton on { "On" };
    juce::AudioProcessorValueTreeState::ButtonAttachment onAttachment;
    juce::Rectangle<int> extraArea;
    int extraWidth = 0;
};

//==============================================================================
class RigEditor final : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit RigEditor (RigProcessor&);
    ~RigEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseAmp (rig::AmpStage);
    void chooseCabinet();
    void refreshPanels();

    RigProcessor& rigProcessor;
    rs::ui::PedalLookAndFeel lookAndFeel;

    std::array<std::unique_ptr<SlotView>, rig::slotsPerSection> preSlots, loopSlots;

    rs::ui::PedalKnob input, ampGain, ampLevel, loopSend, loopReturn, loopMix, output;
    juce::ToggleButton loopOnButton { "Loop on" };
    juce::AudioProcessorValueTreeState::ButtonAttachment loopOnAttachment;

    FilePanel ampPanel, powerPanel, cabPanel;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastFolder;

    juce::Rectangle<int> preArea, ampArea, loopArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigEditor)
};
