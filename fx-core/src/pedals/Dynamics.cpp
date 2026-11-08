#include "LoopPedals.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fx
{

namespace
{
using K = ParamInfo::Kind;
using T = ParamInfo::Taper;

constexpr float kneeDb = 6.0f;
constexpr double detectorRelease = 0.05;   // the peak follower's fall
constexpr double gainRelease = 0.15;       // how fast the squeeze lets go

float dbToGain (float dB) noexcept { return std::pow (10.0f, dB / 20.0f); }
float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-6f)); }
float coeff (double sampleRate, double seconds) noexcept
{
    return static_cast<float> (std::exp (-1.0 / std::max (1.0, seconds * sampleRate)));
}
} // namespace

//==============================================================================
const PedalInfo& CompressorPedal::staticInfo()
{
    static const PedalInfo i { "compressor", "Compressor", {
        { "sustain", "Sustain", 0.0f, 10.0f,  5.0f },
        { "attack",  "Attack",  1.0f, 50.0f,  10.0f, "ms", K::continuous, {}, T::log },
        { "level",   "Level",   -12.0f, 12.0f, 0.0f, "dB" },
        { "blend",   "Blend",   0.0f, 100.0f, 100.0f, "%" },
    } };
    return i;
}

float CompressorPedal::gainDb (float x, float threshold, float ratio) noexcept
{
    // Soft knee: a quadratic blend over kneeDb around the threshold
    const float over = x - threshold;
    if (2.0f * over < -kneeDb)
        return 0.0f;
    if (2.0f * over > kneeDb)
        return over * (1.0f / ratio - 1.0f);
    const float t = over + kneeDb / 2.0f;
    return (1.0f / ratio - 1.0f) * t * t / (2.0f * kneeDb);
}

float CompressorPedal::makeupFor (float s) noexcept
{
    // Half of what a full-scale signal loses: louder playing stays a bit
    // louder, quiet playing comes up
    return -0.5f * gainDb (0.0f, thresholdFor (s), ratioFor (s));
}

void CompressorPedal::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    peakRelease = coeff (sampleRate, detectorRelease);
    for (auto* s : { &levelGain, &wet, &makeup })
        s->prepare (sampleRate, 0.03);
    primed = false;
    reset();
}

void CompressorPedal::reset()
{
    peak = 0.0f;
    reductionDb = 0.0f;
}

void CompressorPedal::process (float* const* channels, int numChannels, int numSamples)
{
    const float s = value (sustain);
    const float threshold = thresholdFor (s), ratio = ratioFor (s);
    const float attackCoeff = coeff (sampleRate, value (attack) / 1000.0);
    const float releaseCoeff = coeff (sampleRate, gainRelease);

    levelGain.setTarget (dbToGain (value (level)));
    makeup.setTarget (dbToGain (makeupFor (s)));
    wet.setTarget (isBypassed() ? 0.0f : value (blend) / 100.0f);
    if (! primed)
    {
        for (auto* sm : { &levelGain, &wet, &makeup })
            sm->reset (sm->getTarget());
        primed = true;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        // One detector for all channels: the loudest
        float x = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            x = std::max (x, std::abs (channels[c][i]));
        peak = std::max (x, peak * peakRelease);

        const float target = gainDb (gainToDb (peak), threshold, ratio);
        const float c = target < reductionDb ? attackCoeff : releaseCoeff;   // squeeze at the attack rate
        reductionDb = target + c * (reductionDb - target);

        const float g = dbToGain (reductionDb) * makeup.next();
        const float lv = levelGain.next(), w = wet.next();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float in = channels[ch][i];
            // Bypassed (w = 0) it's the dry signal exactly; Blend mixes the
            // compressed signal (at Level) with the dry
            channels[ch][i] = in + w * (in * g * lv - in);
        }
    }
}

} // namespace fx
