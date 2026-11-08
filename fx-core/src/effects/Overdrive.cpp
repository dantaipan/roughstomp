#include "fx/effects/Overdrive.h"

#include "fx/Waveshapers.h"

#include <algorithm>
#include <array>
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

float identity (float v) noexcept { return v; }
} // namespace

//==============================================================================
const Overdrive::Voicing& Overdrive::voicing (Mode m) noexcept
{
    // Trims measured with rs-render on a guitar-like test file so the modes
    // sit within about 1 dB of each other at the same knob settings.
    static constexpr std::array<Voicing, numModes> voicings { {
        // focus  drive  trim   fizz
        { 720.0f, 0.0f,  0.0f, 40000.0f },   // soft: classic mid hump, tight lows; no extra roll-off
        { 400.0f, 6.0f, -0.5f,  6000.0f },   // hard: fuller lows, more gain, tamed fizz
        { 550.0f, 3.0f,  2.0f, 16000.0f },   // asymmetric: in between; quieter negative half
    } };
    return voicings[static_cast<size_t> (m)];
}

float Overdrive::driveDecibels (float drive) noexcept
{
    return maxDriveDb * std::clamp (drive, 0.0f, 1.0f);
}

float Overdrive::toneCutoffHz (float t) noexcept
{
    // Log sweep, so the knob feels even across its travel
    return toneMinHz * std::pow (toneMaxHz / toneMinHz, std::clamp (t, 0.0f, 1.0f));
}

float Overdrive::shape (Mode m, float x) noexcept
{
    switch (m)
    {
        case Mode::soft:       return shapers::tanhClip (x);
        case Mode::hard:       return shapers::hardKnee (x);
        case Mode::asymmetric: return shapers::asymmetric (x);
    }
    return x;
}

void Overdrive::setParameters (const Parameters& p) noexcept
{
    if (p.mode != toMode)
    {
        // Crossfade from wherever we're heading to the new mode. A change
        // mid-fade jumps to the previous target first; that's inaudible.
        fromMode = toMode;
        toMode = p.mode;
        modeFade.reset (0.0f);
        modeFade.setTarget (1.0f);
    }

    const auto& v = voicing (toMode);

    driveDb.setTarget (driveDecibels (p.drive) + v.driveOffsetDb);
    focusHz.setTarget (v.focusHz);
    fizzHz.setTarget (v.fizzHz);
    tone.setTarget (std::clamp (p.tone, 0.0f, 1.0f));
    level.setTarget (dbToGain (p.levelDb + v.trimDb));
    mix.setTarget (std::clamp (p.mix, 0.0f, 1.0f));
    bypass.setTarget (p.bypassed ? 1.0f : 0.0f);
}

void Overdrive::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    oversampler.prepare (numChannels, maxBlockSize, oversamplingLog2);
    osRate = sampleRate * oversampler.getFactor();
    maxBlock = maxBlockSize;

    // prepare() jumps each smoother to its target, so playback starts on the
    // current settings rather than ramping from defaults
    driveDb.prepare (osRate, 0.02);
    level.prepare (osRate, 0.02);
    mix.prepare (osRate, 0.02);
    bypass.prepare (osRate, 0.01);
    modeFade.prepare (osRate, 0.02);
    tone.prepare (osRate, 0.02);
    focusHz.prepare (osRate, 0.02);
    fizzHz.prepare (osRate, 0.02);
    fromMode = toMode;

    touchAttack  = static_cast<float> (1.0 - std::exp (-1.0 / (touchAttackSeconds * osRate)));
    touchRelease = static_cast<float> (1.0 - std::exp (-1.0 / (touchReleaseSeconds * osRate)));

    const auto n = static_cast<size_t> (maxBlockSize * oversampler.getFactor());
    driveGains.assign (n, 1.0f);
    makeups.assign (n, 1.0f);
    levels.assign (n, 1.0f);
    mixes.assign (n, 1.0f);
    bypasses.assign (n, 0.0f);
    toneCoeffs.assign (n, 0.0);
    fades.assign (n, 1.0f);
    focusCoeffs.assign (n, 0.0);
    fizzCoeffs.assign (n, 0.0);

    channelState.assign (static_cast<size_t> (numChannels), {});

    for (auto& c : channelState)
    {
        c.focus.setup     (OnePole::Mode::highPass, osRate, focusHz.getTarget());
        c.preClip.setup   (OnePole::Mode::lowPass,  osRate, preClipLowPassHz);
        c.tone.setup      (OnePole::Mode::lowPass,  osRate, toneCutoffHz (tone.getTarget()));
        c.post.setCoefficients (BiquadCoeffs::lowPass (osRate, postLowPassHz));
        c.fizz.setup (OnePole::Mode::lowPass, osRate, fizzHz.getTarget());
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
        c.fizz.reset();
        c.dc.reset();
        c.envelope = 0.0f;
    }
}

void Overdrive::process (float* const* channels, int numChannels, int numSamples)
{
    // Hosts occasionally send more than they promised in prepare()
    float* chunk[8] {};
    numChannels = std::min ({ numChannels, static_cast<int> (channelState.size()), 8 });

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

    // Parameter curves for this block, shared by every channel. Drive gain
    // and its makeup come from the same smoother, so fill both in one pass.
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

    fill (level,    levels.data(),      n, identity);
    fill (mix,      mixes.data(),       n, identity);
    fill (bypass,   bypasses.data(),    n, identity);
    fill (modeFade, fades.data(),       n, identity);
    fill (tone,     toneCoeffs.data(),  n, [this] (float t) { return OnePole::coefficient (osRate, toneCutoffHz (t)); });
    fill (focusHz,  focusCoeffs.data(), n, [this] (float hz) { return OnePole::coefficient (osRate, hz); });
    fill (fizzHz,   fizzCoeffs.data(),  n, [this] (float hz) { return OnePole::coefficient (osRate, hz); });

    const bool fading = fades[0] < 1.0f || fades[static_cast<size_t> (n - 1)] < 1.0f;
    const Mode a = fromMode, b = toMode;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& c = channelState[static_cast<size_t> (ch)];
        float* x = up[ch];

        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<size_t> (i);
            const float dry = x[i];

            // Touch: follow how hard the string was hit
            const float level01 = std::abs (dry);
            c.envelope += (level01 > c.envelope ? touchAttack : touchRelease) * (level01 - c.envelope);

            // Drive boosts the content above the focus frequency; lows pass at
            // unity, which keeps the bass tight instead of farting out
            c.focus.setCoefficient (focusCoeffs[k]);
            float s = dry + (driveGains[k] - 1.0f) * c.focus.processSample (dry);
            s = c.preClip.processSample (s);

            // Bias the clipper by playing strength: harder picking pushes it
            // off-centre for more even harmonics. Normalised to the threshold.
            const float e = c.envelope / clipThreshold;
            s = s / clipThreshold - touchDepth * e / (1.0f + e);

            float clipped = shape (b, s);
            if (fading)
                clipped = fades[k] * clipped + (1.0f - fades[k]) * shape (a, s);
            s = clipThreshold * clipped;

            c.tone.setCoefficient (toneCoeffs[k]);
            s = c.tone.processSample (s);
            s = c.post.processSample (s);
            c.fizz.setCoefficient (fizzCoeffs[k]);
            s = c.fizz.processSample (s);
            s = c.dc.processSample (s) * makeups[k];

            const float wet = levels[k] * (mixes[k] * s + (1.0f - mixes[k]) * dry);
            x[i] = dry + (1.0f - bypasses[k]) * (wet - dry);
        }

        if (c.envelope < 1.0e-9f)
            c.envelope = 0.0f; // no denormals in long silences
    }

    if (fading && ! modeFade.isSmoothing())
        fromMode = toMode;

    oversampler.downsample (channels, numChannels, numSamples);
}

} // namespace fx
