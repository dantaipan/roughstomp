#include "LoopPedals.h"

#include <cmath>

namespace fx
{

namespace
{
float dbToGain (float dB) noexcept { return std::pow (10.0f, dB / 20.0f); }
} // namespace

const PedalInfo& EqPedal::staticInfo()
{
    constexpr float r = graphicEq::rangeDb;
    static const PedalInfo i { "eq", "EQ", {
        { "b100",  "100",  -r, r, 0.0f, "dB" },
        { "b200",  "200",  -r, r, 0.0f, "dB" },
        { "b400",  "400",  -r, r, 0.0f, "dB" },
        { "b800",  "800",  -r, r, 0.0f, "dB" },
        { "b1k6",  "1.6k", -r, r, 0.0f, "dB" },
        { "b3k2",  "3.2k", -r, r, 0.0f, "dB" },
        { "b6k4",  "6.4k", -r, r, 0.0f, "dB" },
        { "level", "Level", -r, r, 0.0f, "dB" },
    }, PedalInfo::Style::graphicEq };
    return i;
}

void EqPedal::setTargets() noexcept
{
    for (int b = 0; b < numBands; ++b)
        gains[static_cast<size_t> (b)].setTarget (value (b));
    levelGain.setTarget (dbToGain (value (level)));
    wet.setTarget (isBypassed() ? 0.0f : 1.0f);
}

void EqPedal::updateCoefficients() noexcept
{
    std::array<BiquadCoeffs, numBands> k;
    for (int b = 0; b < numBands; ++b)
        k[static_cast<size_t> (b)] = graphicEq::band (sampleRate, b, gains[static_cast<size_t> (b)].getCurrent());

    for (auto& channel : filters)
        for (size_t b = 0; b < k.size(); ++b)
            channel[b].setCoefficients (k[b]);
}

void EqPedal::prepare (double newSampleRate, int, int numChannels)
{
    sampleRate = newSampleRate;
    filters.assign (static_cast<size_t> (numChannels), {});
    for (auto& s : gains)
        s.prepare (sampleRate, 0.05);
    levelGain.prepare (sampleRate, 0.02);
    wet.prepare (sampleRate, 0.02);
    primed = false;
}

void EqPedal::reset()
{
    for (auto& channel : filters)
        for (auto& f : channel)
            f.reset();
}

void EqPedal::process (float* const* channels, int numChannels, int numSamples)
{
    setTargets();
    if (! primed)
    {
        // Start on the current settings rather than gliding from the defaults
        for (auto& s : gains)
            s.reset (s.getTarget());
        levelGain.reset (levelGain.getTarget());
        wet.reset (wet.getTarget());
        updateCoefficients();
        primed = true;
    }

    numChannels = std::min (numChannels, static_cast<int> (filters.size()));

    for (int start = 0; start < numSamples; start += updateInterval)
    {
        const int n = std::min (updateInterval, numSamples - start);

        bool moving = false;
        for (auto& s : gains)
        {
            moving = moving || s.isSmoothing();
            for (int i = 0; i < n; ++i)
                s.next();
        }
        if (moving)
            updateCoefficients();

        // The level and bypass ramps are shared by every channel: each
        // channel steps a copy, and the last one's state is kept
        const auto levelAt = levelGain;
        const auto wetAt = wet;
        for (int c = 0; c < numChannels; ++c)
        {
            auto lv = levelAt, w = wetAt;
            float* x = channels[c] + start;
            auto& f = filters[static_cast<size_t> (c)];
            for (int i = 0; i < n; ++i)
            {
                const float in = x[i];
                float y = in;
                for (auto& band : f)
                    y = band.processSample (y);
                y *= lv.next();
                x[i] = in + w.next() * (y - in);
            }
            levelGain = lv;
            wet = w;
        }
        if (numChannels == 0)
        {
            levelGain.skip (n);
            wet.skip (n);
        }
    }
}

} // namespace fx
