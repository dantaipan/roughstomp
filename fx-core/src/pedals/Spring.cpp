#include "LoopPedals.h"

#include <algorithm>
#include <cmath>

namespace fx
{

namespace
{
using K = ParamInfo::Kind;
using T = ParamInfo::Taper;

/** First-order allpass, transposed: y = a x + s, s' = x - a y. */
inline float allpass (float x, float a, float& s) noexcept
{
    const float y = a * x + s;
    s = x - a * y;
    return y;
}
} // namespace

namespace
{
constexpr std::array<double, 2> springMs { 37.0, 43.0 };   // a slightly different spring per side
constexpr std::array<float, 2> springDispersion { -0.62f, -0.58f };
constexpr double springLowCutHz = 180.0;   // springs don't do bass
constexpr float springOutput = 0.6f;

/** Group delay in samples of a first-order allpass at DC: (1 - a) / (1 + a). */
double allpassDelay (float a) noexcept { return (1.0 - a) / (1.0 + a); }
} // namespace

const PedalInfo& SpringPedal::staticInfo()
{
    static const PedalInfo i { "spring", "Spring", {
        { "decay", "Decay", 0.5f,  5.0f,   2.0f,   "s",  K::continuous, {}, T::log },
        { "dwell", "Dwell", 0.0f,  10.0f,  5.0f },
        { "tone",  "Tone",  1000.0f, 8000.0f, 3500.0f, "Hz", K::continuous, {}, T::log },
        { "mix",   "Mix",   0.0f,  100.0f, 30.0f,  "%" },
    } };
    return i;
}

void SpringPedal::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    for (size_t s = 0; s < springs.size(); ++s)
    {
        auto& sp = springs[s];
        sp.length = static_cast<int> (springMs[s] * 0.001 * sampleRate);
        sp.line.prepare (sp.length + 4);
        sp.lowCut.setup (OnePole::Mode::highPass, sampleRate, 60.0);
    }
    inputLowCut.setup (OnePole::Mode::highPass, sampleRate, springLowCutHz);
    for (auto* s : { &send, &wetGain, &drive })
        s->prepare (sampleRate, 0.03);
    primed = false;
    reset();
}

void SpringPedal::reset()
{
    for (auto& sp : springs)
    {
        sp.line.reset();
        sp.allpass.fill (0.0f);
        sp.damping.reset();
        sp.lowCut.reset();
        sp.last = 0.0f;
    }
    inputLowCut.reset();
}

void SpringPedal::process (float* const* channels, int numChannels, int numSamples)
{
    send.setTarget (isBypassed() ? 0.0f : 1.0f);   // bypassed: the tail rings out
    wetGain.setTarget (value (mix) / 100.0f);
    drive.setTarget (0.5f * std::pow (8.0f, value (dwell) / 10.0f));   // 0.5 .. 4
    if (! primed)
    {
        for (auto* s : { &send, &wetGain, &drive })
            s->reset (s->getTarget());
        primed = true;
    }

    // Each spring loses 60 dB over the decay time; its loop is the line plus
    // the dispersion chain's delay (taken at low frequencies)
    std::array<float, 2> feedback {};
    const double toneHz = std::min (static_cast<double> (value (tone)), sampleRate * 0.45);
    for (size_t s = 0; s < springs.size(); ++s)
    {
        const double loopSeconds = (springs[s].length + numStages * allpassDelay (springDispersion[s])) / sampleRate;
        feedback[s] = static_cast<float> (std::pow (10.0, -3.0 * loopSeconds / value (decay)));
        springs[s].damping.setup (OnePole::Mode::lowPass, sampleRate, toneHz);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float in = channels[0][i];
        if (numChannels > 1)
            in = 0.5f * (in + channels[1][i]);

        // Dwell: how hard the springs are driven (they saturate)
        const float g = drive.next();
        const float x = std::tanh (g * inputLowCut.processSample (in) * send.next()) / std::sqrt (g);

        std::array<float, 2> out {};
        for (size_t s = 0; s < springs.size(); ++s)
        {
            auto& sp = springs[s];
            const float a = springDispersion[s];

            float v = x + feedback[s] * sp.last;
            for (auto& state : sp.allpass)
                v = allpass (v, a, state);

            const float y = sp.lowCut.processSample (sp.damping.processSample (sp.line.readInteger (sp.length)));
            sp.line.write (v);
            sp.last = y;
            out[s] = y;
        }

        const float w = wetGain.next() * springOutput;
        if (numChannels > 1)
        {
            channels[0][i] += w * out[0];
            channels[1][i] += w * out[1];
        }
        else
            channels[0][i] += w * 0.5f * (out[0] + out[1]);
    }
}

} // namespace fx
