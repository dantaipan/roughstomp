#include "PluginProcessor.h"

namespace ParamIDs
{
    // Stable once released: presets and host automation depend on these
    inline const juce::ParameterID level { "level", 1 };
}

OverdriveProcessor::OverdriveProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    level = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (ParamIDs::level.getParamID()));
    jassert (level != nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout OverdriveProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        ParamIDs::level, "Level",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    return layout;
}

void OverdriveProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    gain.setGainDecibels (level->get());
    gain.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    setLatencySamples (gain.getLatencySamples());
}

bool OverdriveProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    // Mono guitar in, mono or stereo out; or stereo in/out
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    const auto in = layouts.getMainInputChannelSet();
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void OverdriveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    // Mono in → stereo out: copy the input to the other channels
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, buffer.getNumSamples());

    gain.setGainDecibels (level->get());
    gain.process (buffer.getArrayOfWritePointers(), numOut, buffer.getNumSamples());
}

juce::AudioProcessorEditor* OverdriveProcessor::createEditor()
{
    // Generic editor for now; the pedal GUI arrives in Phase 2
    return new juce::GenericAudioProcessorEditor (*this);
}

void OverdriveProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void OverdriveProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OverdriveProcessor();
}
