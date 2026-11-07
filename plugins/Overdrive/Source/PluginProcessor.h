#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "fx/effects/Overdrive.h"

namespace ParamIDs
{
    // Stable once released: presets and host automation depend on these
    inline const juce::ParameterID drive  { "drive",  1 };
    inline const juce::ParameterID tone   { "tone",   1 };
    inline const juce::ParameterID level  { "level",  1 };
    inline const juce::ParameterID mix    { "mix",    1 };
    inline const juce::ParameterID bypass { "bypass", 1 };
}

/** Plugin wrapper: owns the parameters and presets, and pushes values into
    fx::Overdrive each block. The DSP lives in fx-core. */
class OverdriveProcessor final : public juce::AudioProcessor
{
public:
    OverdriveProcessor();
    ~OverdriveProcessor() override = default;

    // Audio
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    // Our bypass parameter doubles as the host's bypass button, so host
    // bypass gets the same click-free crossfade and constant latency
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypass; }

    // Editor
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // Info
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Factory presets
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    // State
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioParameterFloat* drive = nullptr;
    juce::AudioParameterFloat* tone  = nullptr;
    juce::AudioParameterFloat* level = nullptr;
    juce::AudioParameterFloat* mix   = nullptr;
    juce::AudioParameterBool*  bypass = nullptr;

    int currentProgram = 0;
    fx::Overdrive overdrive;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OverdriveProcessor)
};
