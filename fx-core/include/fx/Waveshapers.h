#pragma once

#include <algorithm>
#include <cmath>

/* Static (memoryless) clipping curves. All of them create harmonics above
   the input frequency, so run them inside an Oversampler to keep those
   harmonics from folding back down as aliasing.

   Unless noted, the small-signal gain is 1 (quiet input passes unchanged),
   so the drive amount is set entirely by the gain before the shaper. */
namespace fx::shapers
{

/** Smooth, symmetric saturation; approaches +/-1. Odd harmonics only. */
inline float tanhClip (float x) noexcept
{
    return std::tanh (x);
}

/** Cubic soft clip: 1.5x - 0.5x^3, flat at +/-1 beyond |x| = 1.
    Cheaper than tanh with a firmer knee. Small-signal gain is 1.5. */
inline float softClipCubic (float x) noexcept
{
    const float c = std::clamp (x, -1.0f, 1.0f);
    return 1.5f * c - 0.5f * c * c * c;
}

/** Brick-wall clip at +/-threshold. Harsh, very bright; distortion-pedal tone. */
inline float hardClip (float x, float threshold = 1.0f) noexcept
{
    return std::clamp (x, -threshold, threshold);
}

/** Hard clip with a rounded knee: x / (1 + x^4)^(1/4). Sounds close to
    hardClip() but has no sharp corner, so it makes far less aliasing.
    Two square roots, so it's cheap. */
inline float hardKnee (float x) noexcept
{
    const float x2 = x * x;
    return x / std::sqrt (std::sqrt (1.0f + x2 * x2));
}

/** Diode-style asymmetric clip: the positive half approaches +1, the
    negative half clips earlier and harder at -negativeLimit. The asymmetry
    adds even harmonics (warmer, more "tube") and a DC offset, so follow it
    with a DcBlocker. Slope is 1 at zero from both sides, so no kink. */
inline float asymmetric (float x, float negativeLimit = 0.6f) noexcept
{
    if (x >= 0.0f)
        return 1.0f - std::exp (-x);

    return -negativeLimit * (1.0f - std::exp (x / negativeLimit));
}

} // namespace fx::shapers
