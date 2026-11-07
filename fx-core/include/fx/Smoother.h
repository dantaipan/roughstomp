#pragma once

#include <algorithm>
#include <cmath>

namespace fx
{

/** Ramps linearly to a new target over a fixed time, then lands on it exactly.
    Good for gains and mix amounts: predictable, and settles bit-exact.

    Call setTarget() at the start of a block and next() once per sample.
    Everything here is real-time safe. */
class LinearSmoother
{
public:
    explicit LinearSmoother (float initialValue = 0.0f) noexcept
        : current (initialValue), target (initialValue) {}

    /** Sets the ramp time and jumps to the current target. */
    void prepare (double sampleRate, double rampSeconds) noexcept
    {
        rampLength = std::max (1, static_cast<int> (sampleRate * rampSeconds));
        reset (target);
    }

    /** Jumps straight to a value, with no ramp. */
    void reset (float value) noexcept
    {
        current = target = value;
        step = 0.0f;
        remaining = 0;
    }

    /** Starts a new ramp from wherever we are now. */
    void setTarget (float newTarget) noexcept
    {
        if (! (newTarget < target || newTarget > target))
            return; // unchanged (written this way to avoid -Wfloat-equal)

        target = newTarget;
        remaining = rampLength;
        step = (target - current) / static_cast<float> (rampLength);
    }

    float next() noexcept
    {
        if (remaining > 0)
        {
            --remaining;
            current = (remaining == 0) ? target : current + step;
        }
        return current;
    }

    /** Advances by numSamples without producing output. */
    void skip (int numSamples) noexcept
    {
        if (remaining <= 0)
            return;

        if (numSamples >= remaining)
        {
            current = target;
            remaining = 0;
        }
        else
        {
            current += step * static_cast<float> (numSamples);
            remaining -= numSamples;
        }
    }

    bool  isSmoothing() const noexcept { return remaining > 0; }
    float getCurrent()  const noexcept { return current; }
    float getTarget()   const noexcept { return target; }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int   rampLength = 1, remaining = 0;
};

/** Exponential (one-pole lowpass) smoothing. Fast at first, then eases in,
    which suits frequencies and other values heard on a log scale.

    The time constant is the time to cover ~63% of a change; it is within
    0.1% after about 7 time constants, then snaps to the target. */
class OnePoleSmoother
{
public:
    explicit OnePoleSmoother (float initialValue = 0.0f) noexcept
        : current (initialValue), target (initialValue) {}

    void prepare (double sampleRate, double timeConstantSeconds) noexcept
    {
        const double samples = std::max (1.0, sampleRate * timeConstantSeconds);
        coeff = static_cast<float> (std::exp (-1.0 / samples));
        reset (target);
    }

    void reset (float value) noexcept { current = target = value; }

    void setTarget (float newTarget) noexcept { target = newTarget; }

    float next() noexcept
    {
        const float diff = current - target;

        if (std::abs (diff) <= snapThreshold)
            current = target;
        else
            current = target + coeff * diff;

        return current;
    }

    bool  isSmoothing() const noexcept { return std::abs (current - target) > snapThreshold; }
    float getCurrent()  const noexcept { return current; }
    float getTarget()   const noexcept { return target; }

private:
    static constexpr float snapThreshold = 1.0e-6f;

    float current = 0.0f, target = 0.0f;
    float coeff = 0.0f;
};

} // namespace fx
