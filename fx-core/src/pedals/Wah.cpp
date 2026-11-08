#include "LoopPedals.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fx
{

namespace
{
using K = ParamInfo::Kind;

float dbToGain (float dB) noexcept { return std::pow (10.0f, dB / 20.0f); }
float coeff (double sampleRate, double seconds) noexcept
{
    return static_cast<float> (std::exp (-1.0 / std::max (1.0, seconds * sampleRate)));
}
} // namespace

const PedalInfo& WahPedal::staticInfo()
{
    static const PedalInfo i { "wah", "Wah", {
        { "position",    "Position",    0.0f, 100.0f, 50.0f, "%" },
        { "resonance",   "Resonance",   0.0f, 10.0f,  5.0f },
        { "level",       "Level",       -12.0f, 12.0f, 0.0f, "dB" },
        { "sensitivity", "Sensitivity", 0.0f, 10.0f,  5.0f },
        { "mode",        "Mode",        0.0f, 1.0f,   0.0f, {}, K::choice, { "Pedal", "Auto" } },
    } };
    return i;
}

void WahPedal::prepare (double newSampleRate, int, int numChannels)
{
    sampleRate = newSampleRate;
    states.assign (static_cast<size_t> (numChannels), {});
    sweep.prepare (sampleRate, 0.01);
    envAttack = coeff (sampleRate, 0.005);
    envRelease = coeff (sampleRate, 0.12);
    levelGain.prepare (sampleRate, 0.03);
    wet.prepare (sampleRate, 0.03);
    primed = false;
}

void WahPedal::reset()
{
    for (auto& s : states)
        s = {};
    envelope = 0.0f;
}

void WahPedal::process (float* const* channels, int numChannels, int numSamples)
{
    const bool autoMode = static_cast<Mode> (choice (mode)) == Mode::autoWah;
    const float base = value (position) / 100.0f;
    const float sens = 0.5f * std::pow (2.0f, value (sensitivity) * 0.6f);   // envelope -> sweep

    // Q from 2 (gentle) to 12 (vocal); the bandpass is normalised to unity
    // at its peak, plus a little lowpass for body, like the real circuit
    const float q = 2.0f + value (resonance);
    const float k = 1.0f / q;
    levelGain.setTarget (dbToGain (value (level)));
    wet.setTarget (isBypassed() ? 0.0f : 1.0f);
    if (! autoMode)
        sweep.setTarget (base);
    if (! primed)
    {
        sweep.reset (base);
        levelGain.reset (levelGain.getTarget());
        wet.reset (wet.getTarget());
        primed = true;
    }

    numChannels = std::min (numChannels, static_cast<int> (states.size()));
    const double piOverFs = std::numbers::pi / sampleRate;
    const double highest = sampleRate * 0.45;

    for (int i = 0; i < numSamples; ++i)
    {
        if (autoMode)
        {
            float x = 0.0f;
            for (int c = 0; c < numChannels; ++c)
                x = std::max (x, std::abs (channels[c][i]));
            const float e = x > envelope ? envAttack : envRelease;
            envelope = x + e * (envelope - x);
            sweep.setTarget (std::clamp (base + sens * envelope, 0.0f, 1.0f));
        }

        const auto g = static_cast<float> (std::tan (piOverFs * std::min (highest, frequencyFor (sweep.next()))));
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float lv = levelGain.next(), w = wet.next();

        for (int c = 0; c < numChannels; ++c)
        {
            auto& s = states[static_cast<size_t> (c)];
            const float in = channels[c][i];

            // Topology-preserving state-variable filter (Zavalishin)
            const float v3 = in - s.ic2;
            const float v1 = a1 * s.ic1 + a2 * v3;
            const float v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
            s.ic1 = 2.0f * v1 - s.ic1;
            s.ic2 = 2.0f * v2 - s.ic2;

            const float y = (1.6f * k * v1 + 0.15f * v2) * lv;
            channels[c][i] = in + w * (y - in);
        }
    }
}

} // namespace fx
