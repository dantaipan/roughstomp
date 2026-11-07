#include "fx/effects/Overdrive.h"

#include <algorithm>
#include <cmath>

namespace fx
{

namespace
{
float dbToGain (float dB) noexcept { return std::pow (10.0f, dB / 20.0f); }

/** Fills out[0..n) from a smoother, skipping the per-sample work when it is idle. */
template <typename Smoother, typename Out, typename Map>
void fill (Smoother& s, Out* out, int n, Map&& map) noexcept
{
    if (! s.isSmoothing())
    {
        std::fill (out, out + n, static_cast<Out> (map (s.getCurrent())));
        return;
    }

    for (int i = 0; i < n; ++i)
        out[i] = static_cast<Out> (map (s.next()));
}
} // namespace

float Overdrive::driveDecibels (float drive) noexcept
{
    return maxDriveDb * std::clamp (drive, 0.0f, 1.0f);
}

float Overdrive::toneCutoffHz (float t) noexcept
{
    // Log sweep, so the knob feels even across its travel
    return toneMinHz * std::pow (toneMaxHz / toneMinHz, std::clamp (t, 0.0f, 1.0f));
}

void Overdrive::setParameters (const Parameters& p) noexcept
{
    driveDb.setTarget (driveDecibels (p.drive));
    tone.setTarget (std::clamp (p.tone, 0.0f, 1.0f));
    level.setTarget (dbToGain (p.levelDb));
    mix.setTarget (std::clamp (p.mix, 0.0f, 1.0f));
    bypass.setTarget (p.bypassed ? 1.0f : 0.0f);
}

void Overdrive::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    oversampler.prepare (numChannels, maxBlockSize, oversamplingLog2);
    osRate = sampleRate * oversampler.getFactor();
    maxBlock = maxBlockSize;

    driveDb.prepare (osRate, 0.02);
    level.prepare (osRate, 0.02);
    mix.prepare (osRate, 0.02);
    bypass.prepare (osRate, 0.01);
    tone.prepare (osRate, 0.02);

    const auto n = static_cast<size_t> (maxBlockSize * oversampler.getFactor());
    driveGains.assign (n, 1.0f);
    makeups.assign (n, 1.0f);
    levels.assign (n, 1.0f);
    mixes.assign (n, 1.0f);
    bypasses.assign (n, 0.0f);
    toneCoeffs.assign (n, 0.0);

    channelState.assign (static_cast<size_t> (numChannels), {});

    for (auto& c : channelState)
    {
        c.focus.setup   (OnePole::Mode::highPass, osRate, driveFocusHz);
        c.preClip.setup (OnePole::Mode::lowPass,  osRate, preClipLowPassHz);
        c.tone.setup    (OnePole::Mode::lowPass,  osRate, toneCutoffHz (tone.getTarget()));
        c.post.setCoefficients (BiquadCoeffs::lowPass (osRate, postLowPassHz));
        c.dc.setup (osRate);
    }

    reset();
}

void Overdrive::reset()
{
    oversampler.reset();

    for (auto& c : channelState)
    {
        c.focus.reset();
        c.preClip.reset();
        c.tone.reset();
        c.post.reset();
        c.dc.reset();
    }
}

void Overdrive::process (float* const* channels, int numChannels, int numSamples)
{
    numChannels = std::min (numChannels, static_cast<int> (channelState.size()));

    // Hosts occasionally send more than they promised in prepare()
    float* chunk[8] {};
    numChannels = std::min (numChannels, 8);

    for (int pos = 0; pos < numSamples; pos += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - pos);
        for (int ch = 0; ch < numChannels; ++ch)
            chunk[ch] = channels[ch] + pos;

        processChunk (chunk, numChannels, n);
    }
}

void Overdrive::processChunk (float* const* channels, int numChannels, int numSamples) noexcept
{
    float* const* up = oversampler.upsample (channels, numChannels, numSamples);
    const int n = numSamples * oversampler.getFactor();

    // Parameter curves for this block, shared by every channel
    // Drive gain and its makeup come from the same smoother, so fill both in one pass
    if (! driveDb.isSmoothing())
    {
        std::fill (driveGains.begin(), driveGains.begin() + n, dbToGain (driveDb.getCurrent()));
        std::fill (makeups.begin(), makeups.begin() + n, dbToGain (makeupPerDriveDb * driveDb.getCurrent()));
    }
    else
    {
        for (size_t i = 0; i < static_cast<size_t> (n); ++i)
        {
            const float dB = driveDb.next();
            driveGains[i] = dbToGain (dB);
            makeups[i]    = dbToGain (makeupPerDriveDb * dB);
        }
    }

    fill (level,   levels.data(),     n, [] (float v) { return v; });
    fill (mix,     mixes.data(),      n, [] (float v) { return v; });
    fill (bypass,  bypasses.data(),   n, [] (float v) { return v; });
    fill (tone,    toneCoeffs.data(), n, [this] (float t) { return OnePole::coefficient (osRate, toneCutoffHz (t)); });

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& c = channelState[static_cast<size_t> (ch)];
        float* x = up[ch];

        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<size_t> (i);
            const float dry = x[i];

            // Drive boosts the content above ~720 Hz; lows pass at unity, which
            // keeps the bass tight instead of farting out
            float s = dry + (driveGains[k] - 1.0f) * c.focus.processSample (dry);
            s = c.preClip.processSample (s);
            s = clipThreshold * std::tanh (s / clipThreshold);

            c.tone.setCoefficient (toneCoeffs[k]);
            s = c.tone.processSample (s);
            s = c.post.processSample (s);
            s = c.dc.processSample (s) * makeups[k];

            const float wet = levels[k] * (mixes[k] * s + (1.0f - mixes[k]) * dry);
            x[i] = dry + (1.0f - bypasses[k]) * (wet - dry);
        }
    }

    oversampler.downsample (channels, numChannels, numSamples);
}

} // namespace fx
