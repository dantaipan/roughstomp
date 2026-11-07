#pragma once

#include "fx/Effect.h"
#include "fx/Smoother.h"

namespace fx
{

/** Smoothed gain stage. At 0 dB it is a bit-exact pass-through. */
class Gain final : public Effect
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;

    /** Safe to call from the audio thread at the start of each block. */
    void setGainDecibels (float dB) noexcept;

    float getCurrentGain() const noexcept { return gain.getCurrent(); }

private:
    LinearSmoother gain { 1.0f };   // 20 ms ramps
};

} // namespace fx
