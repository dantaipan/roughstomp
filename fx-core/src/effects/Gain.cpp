#include "fx/effects/Gain.h"

#include <algorithm>
#include <cmath>

namespace fx
{

void Gain::prepare (double newSampleRate, int, int)
{
    sampleRate  = newSampleRate;
    rampSamples = std::max (1, static_cast<int> (sampleRate * 0.02)); // 20 ms
    reset();
}

void Gain::reset()
{
    current = target;
}

void Gain::setGainDecibels (float dB) noexcept
{
    target = (dB <= -100.0f) ? 0.0f : std::pow (10.0f, dB / 20.0f);
}

void Gain::process (float* const* channels, int numChannels, int numSamples)
{
    if (current == target)
    {
        if (current == 1.0f)
            return; // true pass-through

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                channels[ch][i] *= current;
        return;
    }

    // Linear ramp towards the target, same curve on every channel
    const float step  = (target - current) / static_cast<float> (rampSamples);
    const float start = current;
    float g = start;

    for (int i = 0; i < numSamples; ++i)
    {
        if ((step > 0.0f && g + step >= target) || (step < 0.0f && g + step <= target))
            g = target;
        else
            g += step;

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= g;
    }

    current = g;
}

} // namespace fx
