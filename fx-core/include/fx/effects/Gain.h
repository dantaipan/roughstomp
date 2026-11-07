#pragma once

#include "fx/Effect.h"

namespace fx
{

/** Phase 0 placeholder: a smoothed gain stage. At 0 dB it is a clean
    pass-through, which proves the whole build and host pipeline works.
    Phase 1 replaces the inline ramp with the shared Smoother. */
class Gain final : public Effect
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;

    /** Safe to call from the audio thread at the start of each block. */
    void setGainDecibels (float dB) noexcept;

    float getCurrentGain() const noexcept { return current; }

private:
    double sampleRate = 44100.0;
    float  target  = 1.0f;
    float  current = 1.0f;
    int    rampSamples = 0;   // length of a full ramp, set in prepare()
};

} // namespace fx
