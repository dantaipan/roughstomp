#include "fx/effects/Gain.h"

#include <cmath>

namespace fx
{

void Gain::prepare (double sampleRate, int, int)
{
    gain.prepare (sampleRate, 0.02);
}

void Gain::reset()
{
    gain.reset (gain.getTarget());
}

void Gain::setGainDecibels (float dB) noexcept
{
    gain.setTarget (dB <= -100.0f ? 0.0f : std::pow (10.0f, dB / 20.0f));
}

void Gain::process (float* const* channels, int numChannels, int numSamples)
{
    if (! gain.isSmoothing())
    {
        const float g = gain.getCurrent();

        if (g == 1.0f)
            return; // true pass-through

        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numSamples; ++i)
                channels[ch][i] *= g;
        return;
    }

    // Same ramp on every channel
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = gain.next();

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][i] *= g;
    }
}

} // namespace fx
