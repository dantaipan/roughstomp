#include "AmpModel.h"

#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <filesystem>

namespace rig
{

namespace
{
// Captures come out at whatever level their amp was, so each is brought to
// the same loudness. Otherwise changing amps could jump 20 dB.
constexpr double targetLoudness = -18.0;
} // namespace

AmpModel::LoadResult AmpModel::load (const juce::File& file, double sampleRate, int maximumBlockSize, bool preferLite)
{
    try
    {
        auto dsp = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()));

        if (dsp == nullptr)
            return { nullptr, "It isn't a NAM capture." };

        if (dsp->NumInputChannels() != 1 || dsp->NumOutputChannels() != 1)
            return { nullptr, "Only mono captures are supported." };

        if (auto* slimmable = dynamic_cast<nam::SlimmableModel*> (dsp.get()))
            slimmable->SetSlimmableSize (preferLite ? 0.0 : 1.0);

        std::unique_ptr<AmpModel> model (new AmpModel (std::move (dsp), maximumBlockSize));
        model->prepare (sampleRate, maximumBlockSize);
        return { std::move (model), {} };
    }
    catch (const std::exception& error)
    {
        return { nullptr, error.what() };
    }
}

AmpModel::AmpModel (std::unique_ptr<nam::DSP> dspToUse, int maximumBlockSize)
    : dsp (std::move (dspToUse)), output (static_cast<size_t> (maximumBlockSize))
{
    if (dsp->HasLoudness())
        outputGain = static_cast<float> (std::pow (10.0, (targetLoudness - dsp->GetLoudness()) / 20.0));
}

AmpModel::~AmpModel() = default;

double AmpModel::getExpectedSampleRate() const noexcept
{
    return dsp->SupportsArbitrarySampleRate() ? 0.0 : juce::jmax (0.0, dsp->GetExpectedSampleRate());
}

void AmpModel::prepare (double sampleRate, int maximumBlockSize)
{
    const double modelRate = getExpectedSampleRate();
    bridge.prepare (sampleRate, modelRate, maximumBlockSize);

    const int innerBlock = bridge.getMaxInnerBlock();
    dsp->Reset (bridge.isActive() ? modelRate : sampleRate, innerBlock);
    output.resize (static_cast<size_t> (innerBlock));
}

void AmpModel::process (float* samples, int numSamples, bool modelOn) noexcept
{
    bridge.process (samples, numSamples, [this, modelOn] (float* s, int n)
    {
        if (modelOn)
            runModel (s, n);
    });
}

void AmpModel::runModel (float* samples, int numSamples) noexcept
{
    const auto capacity = static_cast<int> (output.size());

    for (int done = 0; done < numSamples;)
    {
        const auto chunk = juce::jmin (numSamples - done, capacity);

        float* inputs[] { samples + done };
        float* outputs[] { output.data() };

        dsp->process (inputs, outputs, chunk);
        juce::FloatVectorOperations::copyWithMultiply (samples + done, output.data(), outputGain, chunk);

        done += chunk;
    }
}

} // namespace rig
