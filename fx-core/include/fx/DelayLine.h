#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace fx
{

/** A mono delay line with fractional reads.

    Call read() before write() for each sample: read (d) then returns the
    input from d samples before the one about to be written. That way a
    feedback loop is simply  y = read (d); write (x + fb * y);

    Interpolation:
    - linear: cheap, but dulls highs at fractional delays (-2 dB at 10 kHz,
      48 kHz, half a sample) and the dulling moves as the delay is modulated
    - cubic: 4-point Lagrange, -0.5 dB there and steadier; use it for
      anything modulated (chorus, flanger, vibrato)

    prepare() allocates; everything else is real-time safe. */
class DelayLine
{
public:
    /** The longest delay that will be read, in samples. */
    void prepare (int maxDelaySamples)
    {
        int size = 4;
        while (size < maxDelaySamples + 4)
            size <<= 1;
        buffer.assign (static_cast<size_t> (size), 0.0f);
        mask = size - 1;
        maxDelay = static_cast<float> (maxDelaySamples);
        writePos = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    float getMaxDelay() const noexcept { return maxDelay; }

    void write (float x) noexcept
    {
        buffer[static_cast<size_t> (writePos)] = x;
        writePos = (writePos + 1) & mask;
    }

    /** Whole-sample delay, at least 1. */
    float readInteger (int delay) const noexcept
    {
        return at (std::clamp (delay, 1, static_cast<int> (maxDelay)));
    }

    /** Delay of at least 1 sample. */
    float readLinear (float delay) const noexcept
    {
        delay = std::clamp (delay, 1.0f, maxDelay);
        const int i = static_cast<int> (delay);
        const float f = delay - static_cast<float> (i);
        const float a = at (i), b = at (i + 1);
        return a + f * (b - a);
    }

    /** Delay of at least 2 samples (the interpolator looks one sample newer). */
    float readCubic (float delay) const noexcept
    {
        delay = std::clamp (delay, 2.0f, maxDelay);
        const int i = static_cast<int> (delay);
        const float f = delay - static_cast<float> (i);

        // Samples at delays i-1, i, i+1, i+2, i.e. positions -1, 0, 1, 2 around f
        const float ym1 = at (i - 1), y0 = at (i), y1 = at (i + 1), y2 = at (i + 2);
        const float c0 = -f * (f - 1.0f) * (f - 2.0f) / 6.0f;
        const float c1 = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) / 2.0f;
        const float c2 = -(f + 1.0f) * f * (f - 2.0f) / 2.0f;
        const float c3 = (f + 1.0f) * f * (f - 1.0f) / 6.0f;
        return c0 * ym1 + c1 * y0 + c2 * y1 + c3 * y2;
    }

private:
    float at (int delay) const noexcept { return buffer[static_cast<size_t> ((writePos - delay) & mask)]; }

    std::vector<float> buffer;
    int mask = 0;
    int writePos = 0;
    float maxDelay = 0.0f;
};

} // namespace fx
