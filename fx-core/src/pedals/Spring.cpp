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

// Three springs, as in a long amp tank: lengths that don't line up, slightly
// different dispersion, each wandering at its own rate
constexpr std::array<double, 3> springMs     { 36.0, 41.0, 46.5 };
constexpr std::array<float, 3>  springDisp   { 0.75f, 0.72f, 0.77f };
constexpr std::array<double, 3> wanderHz     { 0.8, 1.1, 1.37 };
constexpr double cutOffHz = 4400.0;     // springs pass very little above this
constexpr double dampingHz = 7000.0;    // a little more loss for the highs each trip
constexpr double inputLowCutHz = 180.0; // springs don't do bass
constexpr double loopLowCutHz = 90.0;
constexpr float springOutput = 0.45f;   // wet level: matched (RMS, on a DI) to the first spring, so Mix means the same

// How each side hears the three springs: mostly the same (a tank is mono),
// with a little width
constexpr std::array<float, 3> leftMix  { 1.0f, 0.7f, 0.4f };
constexpr std::array<float, 3> rightMix { 0.4f, 0.7f, 1.0f };
constexpr float sideNorm = 0.778f;      // 1 / sqrt (1 + 0.49 + 0.16)

/** Group delay in samples of a stretched allpass (a + z^-k) / (1 + a z^-k) at w. */
double chirpDelay (float a, int k, double w) noexcept
{
    const double aa = static_cast<double> (a);
    return k * (1.0 - aa * aa) / (1.0 + aa * aa + 2.0 * aa * std::cos (k * w));
}
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
    stretch = std::max (1, static_cast<int> (sampleRate / (2.0 * cutOffHz)));
    wanderDepth = static_cast<float> (8.0 * sampleRate / 48000.0);

    for (size_t s = 0; s < springs.size(); ++s)
    {
        auto& sp = springs[s];
        sp.length = static_cast<float> (springMs[s] * 0.001 * sampleRate);
        sp.dispersion = springDisp[s];
        sp.line.prepare (static_cast<int> (sp.length + wanderDepth) + 8);
        sp.chirpIn.assign (static_cast<size_t> (numStages * stretch), 0.0f);
        sp.chirpOut.assign (static_cast<size_t> (numStages * stretch), 0.0f);
        sp.cutOff[0].setCoefficients (BiquadCoeffs::lowPass (sampleRate, cutOffHz, 0.5412));   // Butterworth, 4th order
        sp.cutOff[1].setCoefficients (BiquadCoeffs::lowPass (sampleRate, cutOffHz, 1.3066));
        sp.damping.setup (OnePole::Mode::lowPass, sampleRate, std::min (dampingHz, sampleRate * 0.45));
        sp.lowCut.setup (OnePole::Mode::highPass, sampleRate, loopLowCutHz);
        sp.wander.prepare (sampleRate);
        sp.wander.setShape (Lfo::Shape::random);
        sp.wander.setRate (wanderHz[s]);
    }
    inputLowCut.setup (OnePole::Mode::highPass, sampleRate, inputLowCutHz);
    for (auto* sm : { &send, &wetGain, &drive })
        sm->prepare (sampleRate, 0.03);
    primed = false;
    reset();
}

void SpringPedal::reset()
{
    for (size_t s = 0; s < springs.size(); ++s)
    {
        auto& sp = springs[s];
        sp.line.reset();
        std::fill (sp.chirpIn.begin(), sp.chirpIn.end(), 0.0f);
        std::fill (sp.chirpOut.begin(), sp.chirpOut.end(), 0.0f);
        for (auto& f : sp.cutOff)
            f.reset();
        sp.damping.reset();
        sp.lowCut.reset();
        sp.wander.reset (0.37 * static_cast<double> (s));   // each starts somewhere different
        sp.last = 0.0f;
    }
    chirpPos = 0;
    inputLowCut.reset();
    for (auto& b : brightness)
        b.reset();
}

void SpringPedal::process (float* const* channels, int numChannels, int numSamples)
{
    send.setTarget (isBypassed() ? 0.0f : 1.0f);   // bypassed: the tail rings out
    wetGain.setTarget (value (mix) / 100.0f);
    drive.setTarget (0.5f * std::pow (8.0f, value (dwell) / 10.0f));   // 0.5 .. 4
    if (! primed)
    {
        for (auto* sm : { &send, &wetGain, &drive })
            sm->reset (sm->getTarget());
        primed = true;
    }

    const double toneHz = std::min (static_cast<double> (value (tone)), sampleRate * 0.45);
    for (auto& b : brightness)
        b.setup (OnePole::Mode::lowPass, sampleRate, toneHz);

    // Each spring loses 60 dB over the decay time. A trip round the loop is
    // the line plus the chirp's delay (taken at 1 kHz, the heart of the sound)
    std::array<float, numSprings> feedback {};
    const double w1k = 2.0 * std::numbers::pi * 1000.0 / sampleRate;
    for (size_t s = 0; s < springs.size(); ++s)
    {
        const double trip = springs[s].length + numStages * chirpDelay (springs[s].dispersion, stretch, w1k);
        feedback[s] = static_cast<float> (std::pow (10.0, -3.0 * (trip / sampleRate) / value (decay)));
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float in = channels[0][i];
        if (numChannels > 1)
            in = 0.5f * (in + channels[1][i]);

        // Dwell: how hard the springs are driven (they saturate)
        const float g = drive.next();
        const float x = std::tanh (g * inputLowCut.processSample (in) * send.next()) / std::sqrt (g);

        std::array<float, numSprings> out {};
        for (size_t s = 0; s < springs.size(); ++s)
        {
            auto& sp = springs[s];

            // What has come back round, then the trip again: through the chirp
            // (each stage: y = a x + x[n-k] - a y[n-k])...
            const float a = sp.dispersion;
            float v = x + feedback[s] * sp.last;
            size_t idx = static_cast<size_t> (chirpPos);
            for (int m = 0; m < numStages; ++m, idx += static_cast<size_t> (stretch))
            {
                const float y = a * v + sp.chirpIn[idx] - a * sp.chirpOut[idx];
                sp.chirpIn[idx] = v;
                sp.chirpOut[idx] = y;
                v = y;
            }

            // ...and the length of the spring, which wanders a little
            const float delay = sp.length + wanderDepth * sp.wander.next();
            float y = sp.line.readCubic (delay);
            sp.line.write (v);

            for (auto& f : sp.cutOff)
                y = f.processSample (y);
            y = sp.lowCut.processSample (sp.damping.processSample (y));
            sp.last = y;
            out[s] = y;
        }
        chirpPos = (chirpPos + 1) % stretch;

        float left = 0.0f, right = 0.0f;
        for (size_t s = 0; s < springs.size(); ++s)
        {
            left  += leftMix[s] * out[s];
            right += rightMix[s] * out[s];
        }

        const float w = wetGain.next() * springOutput * sideNorm;
        if (numChannels > 1)
        {
            channels[0][i] += w * brightness[0].processSample (left);
            channels[1][i] += w * brightness[1].processSample (right);
        }
        else
            channels[0][i] += w * brightness[0].processSample (0.5f * (left + right));
    }
}

} // namespace fx
