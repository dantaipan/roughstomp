#pragma once

#include <juce_core/juce_core.h>

#include "fx/RateBridge.h"

#include <memory>
#include <vector>

namespace nam
{
class DSP;
}

namespace rig
{

/** A Neural Amp Modeler capture (ported from RoughTrack). Loading is slow and
    allocates, so it happens away from the audio thread. process() is
    real-time safe and mono.

    A capture only works at the sample rate it was made at, so at any other
    rate it runs inside a resampler, which adds a fixed latency. */
class AmpModel
{
public:
    struct LoadResult
    {
        std::unique_ptr<AmpModel> model;
        juce::String error;
    };

    /** preferLite picks the smaller version of an A2 capture, which sounds
        nearly the same for much less CPU. */
    static LoadResult load (const juce::File&, double sampleRate, int maximumBlockSize, bool preferLite = false);

    ~AmpModel();

    /** Must not run alongside process(). */
    void prepare (double sampleRate, int maximumBlockSize);

    /** The sample rate the capture was made at, or 0 if it runs at any rate. */
    double getExpectedSampleRate() const noexcept;

    /** Host samples of delay added by resampling (0 at the capture's own rate). */
    int getLatencySamples() const noexcept { return bridge.getLatencySamples(); }

    /** With modelOn false the signal only goes through the resampler, so
        switching the amp off doesn't change the latency. */
    void process (float* samples, int numSamples, bool modelOn = true) noexcept;

private:
    AmpModel (std::unique_ptr<nam::DSP>, int maximumBlockSize);

    void runModel (float* samples, int numSamples) noexcept;

    std::unique_ptr<nam::DSP> dsp;
    fx::RateBridge bridge;
    std::vector<float> output;
    float outputGain = 1.0f;
};

} // namespace rig
