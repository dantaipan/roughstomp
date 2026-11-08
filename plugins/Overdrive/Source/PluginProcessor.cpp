#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using Mode = fx::Overdrive::Mode;

struct Preset
{
    const char* name;
    float drive, tone, level, mix; // knob units: 0-10, 0-10, dB, %
    Mode mode;
};

// Index 0 is the default state
constexpr Preset presets[] {
    { "Default",         5.0f, 5.0f,  0.0f, 100.0f, Mode::soft },
    { "Edge of Breakup", 1.5f, 5.5f,  0.5f, 100.0f, Mode::soft },
    { "Classic Drive",   5.5f, 5.0f,  0.0f, 100.0f, Mode::soft },
    { "Warm Tube Push",  3.5f, 5.0f,  0.0f, 100.0f, Mode::asymmetric },
    { "Bright Crunch",   5.0f, 7.5f, -1.0f, 100.0f, Mode::hard },
    { "Full Distortion", 8.5f, 6.0f, -1.0f, 100.0f, Mode::hard },
    { "Thick Lead",      9.0f, 3.5f,  0.0f, 100.0f, Mode::soft },
    { "Bass Blend",      7.0f, 4.0f,  0.0f,  45.0f, Mode::soft },
};

template <typename T>
T* getParam (juce::AudioProcessorValueTreeState& apvts, const juce::ParameterID& id)
{
    auto* p = dynamic_cast<T*> (apvts.getParameter (id.getParamID()));
    jassert (p != nullptr);
    return p;
}

juce::String knobText (float v, int) { return juce::String (v, 1); }
} // namespace

OverdriveProcessor::OverdriveProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    drive  = getParam<juce::AudioParameterFloat> (apvts, ParamIDs::drive);
    tone   = getParam<juce::AudioParameterFloat> (apvts, ParamIDs::tone);
    level  = getParam<juce::AudioParameterFloat> (apvts, ParamIDs::level);
    mix    = getParam<juce::AudioParameterFloat> (apvts, ParamIDs::mix);
    bypass = getParam<juce::AudioParameterBool>  (apvts, ParamIDs::bypass);
    mode   = getParam<juce::AudioParameterChoice> (apvts, ParamIDs::mode);
}

fx::Overdrive::Parameters OverdriveProcessor::currentParameters() const
{
    return { drive->get() / 10.0f, tone->get() / 10.0f, level->get(), mix->get() / 100.0f,
             bypass->get(), static_cast<Mode> (mode->getIndex()) };
}

juce::AudioProcessorValueTreeState::ParameterLayout OverdriveProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const auto& d = presets[0];

    layout.add (std::make_unique<AudioParameterFloat> (
        ParamIDs::drive, "Drive", NormalisableRange<float> (0.0f, 10.0f, 0.01f), d.drive,
        AudioParameterFloatAttributes().withStringFromValueFunction (knobText)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParamIDs::tone, "Tone", NormalisableRange<float> (0.0f, 10.0f, 0.01f), d.tone,
        AudioParameterFloatAttributes().withStringFromValueFunction (knobText)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParamIDs::level, "Level", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), d.level,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (knobText)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParamIDs::mix, "Mix", NormalisableRange<float> (0.0f, 100.0f, 1.0f), d.mix,
        AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)); })));

    layout.add (std::make_unique<AudioParameterBool> (ParamIDs::bypass, "Bypass", false));

    // Order matches fx::Overdrive::Mode; append new modes at the end so saved sessions keep theirs
    layout.add (std::make_unique<AudioParameterChoice> (ParamIDs::mode, "Mode",
                                                        StringArray { "Soft", "Hard", "Asymmetric" },
                                                        static_cast<int> (d.mode)));

    return layout;
}

void OverdriveProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Start on the current settings rather than ramping up from defaults
    overdrive.setParameters (currentParameters());
    overdrive.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    setLatencySamples (overdrive.getLatencySamples());
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

    // Mono in -> stereo out: copy the input to the other channels
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, buffer.getNumSamples());

    overdrive.setParameters (currentParameters());
    overdrive.process (buffer.getArrayOfWritePointers(), numOut, buffer.getNumSamples());
}

juce::AudioProcessorEditor* OverdriveProcessor::createEditor()
{
    return new OverdriveEditor (*this);
}

//==============================================================================
int OverdriveProcessor::getNumPrograms()
{
    return static_cast<int> (std::size (presets));
}

const juce::String OverdriveProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? presets[index].name : "";
}

void OverdriveProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    currentProgram = index;
    const auto& p = presets[index];

    auto set = [] (juce::RangedAudioParameter* param, float value)
    {
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    };

    set (drive, p.drive);
    set (tone,  p.tone);
    set (level, p.level);
    set (mix,   p.mix);
    set (mode,  static_cast<float> (p.mode));
}

//==============================================================================
void OverdriveProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OverdriveProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, getNumPrograms() - 1, static_cast<int> (state.getProperty ("program", 0)));
            apvts.replaceState (state);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OverdriveProcessor();
}
