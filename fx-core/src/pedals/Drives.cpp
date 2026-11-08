#include "LoopPedals.h"

#include "fx/Waveshapers.h"

#include <algorithm>
#include <cmath>

namespace fx
{

namespace
{
float dbToGain (float dB) noexcept { return std::pow (10.0f, dB / 20.0f); }
} // namespace

//==============================================================================
void DrivePedal::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    oversampler.prepare (numChannels, maxBlockSize, oversamplingLog2);
    maxBlock = maxBlockSize;
    const double osRate = sampleRate * oversampler.getFactor();
    const int maxOs = maxBlockSize * oversampler.getFactor();

    wet.prepare (osRate, 0.01);
    wetCurve.assign (static_cast<size_t> (maxOs), 1.0f);
    dry.assign (static_cast<size_t> (numChannels), std::vector<float> (static_cast<size_t> (maxOs), 0.0f));

    prepareDrive (osRate, maxOs, numChannels);
    primed = false;
    reset();
}

void DrivePedal::reset()
{
    oversampler.reset();
    resetDrive();
}

void DrivePedal::process (float* const* channels, int numChannels, int numSamples)
{
    numChannels = std::min (numChannels, static_cast<int> (dry.size()));
    wet.setTarget (isBypassed() ? 0.0f : 1.0f);
    setTargets (! primed);
    if (! primed)
    {
        wet.reset (wet.getTarget());
        primed = true;
    }

    float* chunk[8] {};
    numChannels = std::min (numChannels, 8);
    for (int pos = 0; pos < numSamples; pos += maxBlock)
    {
        const int count = std::min (maxBlock, numSamples - pos);
        for (int c = 0; c < numChannels; ++c)
            chunk[c] = channels[c] + pos;

        float* const* up = oversampler.upsample (chunk, numChannels, count);
        const int n = count * oversampler.getFactor();
        for (int c = 0; c < numChannels; ++c)
            std::copy (up[c], up[c] + n, dry[static_cast<size_t> (c)].begin());

        processOversampled (up, numChannels, n);

        fill (wet, wetCurve.data(), n);
        if (wetCurve[0] < 1.0f || wetCurve[static_cast<size_t> (n - 1)] < 1.0f)
            for (int c = 0; c < numChannels; ++c)
            {
                const auto& d = dry[static_cast<size_t> (c)];
                for (int i = 0; i < n; ++i)
                    up[c][i] = d[static_cast<size_t> (i)] + wetCurve[static_cast<size_t> (i)] * (up[c][i] - d[static_cast<size_t> (i)]);
            }

        oversampler.downsample (chunk, numChannels, count);
    }
}

//==============================================================================
// Fuzz

const PedalInfo& FuzzPedal::staticInfo()
{
    static const PedalInfo i { "fuzz", "Fuzz", {
        { "fuzz",  "Fuzz",  0.0f, 10.0f, 7.0f },
        { "bias",  "Bias",  0.0f, 10.0f, 10.0f },
        { "level", "Level", -24.0f, 12.0f, 0.0f, "dB" },
    } };
    return i;
}

void FuzzPedal::prepareDrive (double osRate, int maxOs, int numChannels)
{
    for (auto* s : { &gain1, &offset, &levelGain })
        s->prepare (osRate, 0.02);
    for (auto* v : { &g1, &off, &lv })
        v->assign (static_cast<size_t> (maxOs), 0.0f);
    state.assign (static_cast<size_t> (numChannels), {});
    for (auto& c : state)
    {
        c.input.setup (OnePole::Mode::highPass, osRate, 90.0);   // the small input cap: tightens the lows
        c.fizz.setup (OnePole::Mode::lowPass, osRate, 5500.0);
        c.dc.setup (osRate, 15.0);
    }
}

void FuzzPedal::resetDrive() noexcept
{
    for (auto& c : state)
    {
        c.input.reset();
        c.fizz.reset();
        c.dc.reset();
    }
}

void FuzzPedal::setTargets (bool jump) noexcept
{
    // Fuzz: 12 to 48 dB into the first transistor. Bias 10 sits it in the
    // middle of its range; lower pushes the second stage's operating point
    // into the bend, where small signals barely get through (the gating)
    gain1.setTarget (dbToGain (12.0f + 3.6f * value (fuzz)));
    offset.setTarget (0.12f * (10.0f - value (bias)));
    // Levels fitted to measurements (rs-render, a DI take) so the defaults sit
    // with the Overdrive's, the Fuzz knob stays roughly even, and the gated
    // low-bias sound isn't much quieter
    const float evenOut = 6.1f * (1.0f - std::exp (-value (fuzz) / 2.0f));
    levelGain.setTarget (dbToGain (value (level) - 30.2f - evenOut + 0.6f * (10.0f - value (bias))));
    if (jump)
        for (auto* s : { &gain1, &offset, &levelGain })
            s->reset (s->getTarget());
}

void FuzzPedal::processOversampled (float* const* x, int numChannels, int n) noexcept
{
    fill (gain1, g1.data(), n);
    fill (offset, off.data(), n);
    fill (levelGain, lv.data(), n);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& c = state[static_cast<size_t> (ch)];
        float* s = x[ch];
        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<size_t> (i);
            // First transistor: asymmetric soft clip (it saturates sooner one way)
            const float a = g1[k] * c.input.processSample (s[i]);
            const float s1 = a > 0.0f ? std::tanh (a) : 0.8f * std::tanh (1.25f * a);

            // Second: biased; the static part is taken off so it adds no DC
            const float b = off[k];
            const float s2 = std::tanh (3.0f * (s1 + b)) - std::tanh (3.0f * b);

            s[i] = lv[k] * c.fizz.processSample (c.dc.processSample (s2)) * 6.0f;
        }
    }
}

//==============================================================================
// Big Fuzz

const PedalInfo& BigFuzzPedal::staticInfo()
{
    static const PedalInfo i { "bigfuzz", "Big Fuzz", {
        { "sustain", "Sustain", 0.0f, 10.0f, 6.0f },
        { "tone",    "Tone",    0.0f, 10.0f, 5.0f },
        { "level",   "Level",   -24.0f, 12.0f, 0.0f, "dB" },
    } };
    return i;
}

void BigFuzzPedal::prepareDrive (double osRate, int maxOs, int numChannels)
{
    for (auto* s : { &gain1, &toneMix, &levelGain })
        s->prepare (osRate, 0.02);
    for (auto* v : { &g1, &tm, &lv })
        v->assign (static_cast<size_t> (maxOs), 0.0f);
    state.assign (static_cast<size_t> (numChannels), {});
    for (auto& c : state)
    {
        c.input.setup (OnePole::Mode::highPass, osRate, 100.0);
        c.stage1.setup (OnePole::Mode::lowPass, osRate, 5000.0);   // each stage's feedback cap rolls off the top
        c.stage2.setup (OnePole::Mode::lowPass, osRate, 4000.0);
        // Tone stack: a lowpass side and a highpass side, far enough apart
        // that around 1 kHz they partly cancel: the scoop
        c.bass.setup (OnePole::Mode::lowPass, osRate, 400.0);
        c.treble.setup (OnePole::Mode::highPass, osRate, 2500.0);
        c.dc.setup (osRate, 15.0);
    }
}

void BigFuzzPedal::resetDrive() noexcept
{
    for (auto& c : state)
    {
        c.input.reset();
        c.stage1.reset();
        c.stage2.reset();
        c.bass.reset();
        c.treble.reset();
        c.dc.reset();
    }
}

void BigFuzzPedal::setTargets (bool jump) noexcept
{
    gain1.setTarget (dbToGain (10.0f + 4.0f * value (sustain)));   // 10 to 50 dB
    toneMix.setTarget (value (tone) / 10.0f);
    levelGain.setTarget (dbToGain (value (level) - 10.5f));   // fitted so the default sits with the Overdrive
    if (jump)
        for (auto* s : { &gain1, &toneMix, &levelGain })
            s->reset (s->getTarget());
}

void BigFuzzPedal::processOversampled (float* const* x, int numChannels, int n) noexcept
{
    fill (gain1, g1.data(), n);
    fill (toneMix, tm.data(), n);
    fill (levelGain, lv.data(), n);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& c = state[static_cast<size_t> (ch)];
        float* s = x[ch];
        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<size_t> (i);
            // Two clipping stages in a row: the second squares off what the
            // first rounded, which is where the endless sustain comes from
            float v = std::tanh (g1[k] * c.input.processSample (s[i]));
            v = c.stage1.processSample (v);
            v = std::tanh (12.0f * v);
            v = c.stage2.processSample (v);

            // Tone: blend of a lowpass and a highpass; the middle dips between
            const float t = tm[k];
            const float lo = c.bass.processSample (v), hi = c.treble.processSample (v);
            const float toned = (1.0f - t) * lo + t * hi * 1.3f;

            s[i] = lv[k] * c.dc.processSample (toned);
        }
    }
}

//==============================================================================
// Clean Boost Drive

const PedalInfo& CleanDrivePedal::staticInfo()
{
    static const PedalInfo i { "cleandrive", "Clean Drive", {
        { "gain",   "Gain",   0.0f, 10.0f, 4.0f },
        { "treble", "Treble", 0.0f, 10.0f, 5.0f },
        { "level",  "Level",  -24.0f, 12.0f, 0.0f, "dB" },
    } };
    return i;
}

void CleanDrivePedal::prepareDrive (double osRate, int maxOs, int numChannels)
{
    rate = osRate;
    for (auto* s : { &driveGain, &dirtyMix, &levelGain })
        s->prepare (osRate, 0.02);
    trebleDb.prepare (osRate, 0.03);
    for (auto* v : { &dg, &dm, &lv })
        v->assign (static_cast<size_t> (maxOs), 0.0f);
    state.assign (static_cast<size_t> (numChannels), {});
    for (auto& c : state)
    {
        c.driveLowCut.setup (OnePole::Mode::highPass, osRate, 350.0);   // the clipped side leans on the mids
        c.driveTop.setup (OnePole::Mode::lowPass, osRate, 4500.0);
        c.dc.setup (osRate, 15.0);
    }
    lastTrebleDb = 1000.0f;
}

void CleanDrivePedal::resetDrive() noexcept
{
    for (auto& c : state)
    {
        c.driveLowCut.reset();
        c.driveTop.reset();
        c.treble.reset();
        c.dc.reset();
    }
}

void CleanDrivePedal::setTargets (bool jump) noexcept
{
    const float g = value (gain) / 10.0f;
    driveGain.setTarget (dbToGain (6.0f + 30.0f * g));
    dirtyMix.setTarget (0.15f + 0.85f * g);           // the clean side stays; Gain brings in more of the clipped one
    levelGain.setTarget (dbToGain (value (level) - 14.2f * std::pow (g, 1.2f)));   // fitted: about +6 dB from gain 0 to 10
    trebleDb.setTarget (-6.0f + 1.5f * value (treble)); // -6 .. +9 dB
    if (jump)
    {
        for (auto* s : { &driveGain, &dirtyMix, &levelGain })
            s->reset (s->getTarget());
        trebleDb.reset (trebleDb.getTarget());
    }
}

void CleanDrivePedal::processOversampled (float* const* x, int numChannels, int n) noexcept
{
    fill (driveGain, dg.data(), n);
    fill (dirtyMix, dm.data(), n);
    fill (levelGain, lv.data(), n);

    // Treble: a shelf, updated per block (its smoother is slow)
    for (int i = 0; i < n; ++i)
        trebleDb.next();
    if (std::abs (trebleDb.getCurrent() - lastTrebleDb) > 0.01f)
    {
        lastTrebleDb = trebleDb.getCurrent();
        const auto k = BiquadCoeffs::highShelf (rate, 1800.0, 0.6, lastTrebleDb);
        for (auto& c : state)
            c.treble.setCoefficients (k);
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& c = state[static_cast<size_t> (ch)];
        float* s = x[ch];
        for (int i = 0; i < n; ++i)
        {
            const auto k = static_cast<size_t> (i);
            const float in = s[i];

            // Clipped side: mid-focused, into soft germanium-like clipping
            const float d = c.driveTop.processSample (shapers::hardKnee (dg[k] * c.driveLowCut.processSample (in) * 0.5f));
            const float mixed = in + dm[k] * (d * 0.9f);

            s[i] = lv[k] * c.treble.processSample (c.dc.processSample (mixed));
        }
    }
}

} // namespace fx
