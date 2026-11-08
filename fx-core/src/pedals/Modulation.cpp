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

/** First-order allpass, transposed: y = a x + s, s' = x - a y. */
inline float allpass (float x, float a, float& s) noexcept
{
    const float y = a * x + s;
    s = x - a * y;
    return y;
}
} // namespace

//==============================================================================
const PedalInfo& TremoloPedal::staticInfo()
{
    static const PedalInfo i { "tremolo", "Tremolo", {
        { "rate",   "Rate",   0.5f, 15.0f,  5.0f,  "Hz", K::continuous, {}, T::log },
        { "depth",  "Depth",  0.0f, 100.0f, 60.0f, "%" },
        { "spread", "Spread", 0.0f, 100.0f, 0.0f,  "%" },
        { "shape",  "Shape",  0.0f, 2.0f,   0.0f,  {}, K::choice, { "Sine", "Triangle", "Square" } },
        { "sync",   "Sync",   0.0f, 7.0f,   0.0f,  {}, K::choice, lfoSync::choices },
    } };
    return i;
}

void TremoloPedal::prepare (double sampleRate, int, int)
{
    lfo.prepare (sampleRate);
    lfo.reset();
    depthAmount.prepare (sampleRate, 0.03);
    spreadAmount.prepare (sampleRate, 0.03);
    primed = false;
}

void TremoloPedal::process (float* const* channels, int numChannels, int numSamples)
{
    static constexpr std::array shapes { Lfo::Shape::sine, Lfo::Shape::triangle, Lfo::Shape::square };
    lfo.setShape (shapes[static_cast<size_t> (std::clamp (choice (shape), 0, 2))]);
    lfoSync::apply (lfo, choice (sync), value (rate), { getTempo(), getPlayPosition(), isHostPlaying() });
    depthAmount.setTarget (isBypassed() ? 0.0f : value (depth) / 100.0f);   // bypass: the pulsing fades out
    spreadAmount.setTarget (value (spread) / 100.0f);
    if (! primed)
    {
        depthAmount.reset (depthAmount.getTarget());
        spreadAmount.reset (spreadAmount.getTarget());
        primed = true;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const float d = depthAmount.next(), s = spreadAmount.next();
        for (int c = 0; c < numChannels && c < 2; ++c)
        {
            // Peaks at unity, dips by the depth; the right side runs up to
            // half a cycle behind
            const float mod = lfo.valueAt (c == 1 ? 0.5 * s : 0.0);
            channels[c][i] *= 1.0f - d * (0.5f - 0.5f * mod);
        }
        lfo.advance (1);
    }
}

//==============================================================================
namespace
{
constexpr double vibratoCentreMs = 4.0, vibratoMaxSwingMs = 2.0;
}

const PedalInfo& VibratoPedal::staticInfo()
{
    static const PedalInfo i { "vibrato", "Vibrato", {
        { "rate",  "Rate",  0.5f, 10.0f,  5.0f,  "Hz", K::continuous, {}, T::log },
        { "depth", "Depth", 0.0f, 100.0f, 40.0f, "%" },
        { "sync",  "Sync",  0.0f, 7.0f,   0.0f,  {}, K::choice, lfoSync::choices },
    } };
    return i;
}

void VibratoPedal::prepare (double newSampleRate, int, int numChannels)
{
    sampleRate = newSampleRate;
    lines.resize (static_cast<size_t> (numChannels));
    for (auto& l : lines)
        l.prepare (static_cast<int> (std::ceil ((vibratoCentreMs + vibratoMaxSwingMs + 1.0) * 0.001 * sampleRate)) + 4);
    lfo.prepare (sampleRate);
    lfo.reset();
    depthAmount.prepare (sampleRate, 0.05);
    wet.prepare (sampleRate, 0.03);
    primed = false;
}

void VibratoPedal::reset()
{
    for (auto& l : lines)
        l.reset();
    lfo.reset();
}

void VibratoPedal::process (float* const* channels, int numChannels, int numSamples)
{
    lfoSync::apply (lfo, choice (sync), value (rate), { getTempo(), getPlayPosition(), isHostPlaying() });
    depthAmount.setTarget (value (depth) / 100.0f);
    wet.setTarget (isBypassed() ? 0.0f : 1.0f);
    if (! primed)
    {
        depthAmount.reset (depthAmount.getTarget());
        wet.reset (wet.getTarget());
        primed = true;
    }

    numChannels = std::min (numChannels, static_cast<int> (lines.size()));
    const auto msToSamples = static_cast<float> (0.001 * sampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        const float d = depthAmount.next(), m = wet.next();
        const float ms = static_cast<float> (vibratoCentreMs + vibratoMaxSwingMs * d * lfo.next());
        for (int c = 0; c < numChannels; ++c)
        {
            auto& line = lines[static_cast<size_t> (c)];
            const float in = channels[c][i];
            const float y = line.readCubic (ms * msToSamples);
            line.write (in);
            channels[c][i] = in + m * (y - in);
        }
    }
}

//==============================================================================
namespace
{
constexpr double flangerOctaves = 2.0;   // sweep each way at full depth
constexpr double flangerMaxMs = 12.0;
}

const PedalInfo& FlangerPedal::staticInfo()
{
    static const PedalInfo i { "flanger", "Flanger", {
        { "rate",     "Rate",     0.05f, 5.0f,   0.3f, "Hz", K::continuous, {}, T::log },
        { "depth",    "Depth",    0.0f,  100.0f, 70.0f, "%" },
        { "delay",    "Delay",    0.3f,  4.0f,   1.2f, "ms", K::continuous, {}, T::log },
        { "feedback", "Feedback", -95.0f, 95.0f, 50.0f, "%" },
        { "mix",      "Mix",      0.0f,  100.0f, 50.0f, "%" },
        { "sync",     "Sync",     0.0f,  7.0f,   0.0f,  {}, K::choice, lfoSync::choices },
    } };
    return i;
}

void FlangerPedal::prepare (double newSampleRate, int, int numChannels)
{
    sampleRate = newSampleRate;
    lines.resize (static_cast<size_t> (numChannels));
    for (auto& l : lines)
        l.prepare (static_cast<int> (std::ceil (flangerMaxMs * 0.001 * sampleRate)) + 4);
    lfo.prepare (sampleRate);
    lfo.reset();
    // Slow glides: a short delay moved quickly sweeps the comb fast enough
    // to "zip", especially with feedback
    centreMs.prepare (sampleRate, 0.2);
    depthAmount.prepare (sampleRate, 0.2);
    feedbackGain.prepare (sampleRate, 0.1);
    wet.prepare (sampleRate, 0.03);
    primed = false;
}

void FlangerPedal::reset()
{
    for (auto& l : lines)
        l.reset();
    lfo.reset();
}

void FlangerPedal::process (float* const* channels, int numChannels, int numSamples)
{
    lfoSync::apply (lfo, choice (sync), value (rate), { getTempo(), getPlayPosition(), isHostPlaying() });
    centreMs.setTarget (value (delay));
    depthAmount.setTarget (value (depth) / 100.0f);
    feedbackGain.setTarget (value (feedback) / 100.0f);
    wet.setTarget (isBypassed() ? 0.0f : value (mix) / 100.0f);
    if (! primed)
    {
        centreMs.reset (centreMs.getTarget());
        for (auto* s : { &depthAmount, &feedbackGain, &wet })
            s->reset (s->getTarget());
        primed = true;
    }

    numChannels = std::min (numChannels, static_cast<int> (lines.size()));
    const auto msToSamples = static_cast<float> (0.001 * sampleRate);
    const float shortest = 2.5f, longest = static_cast<float> (flangerMaxMs) * msToSamples;

    for (int i = 0; i < numSamples; ++i)
    {
        const float centre = centreMs.next(), d = depthAmount.next(), fb = feedbackGain.next(), m = wet.next();
        for (int c = 0; c < numChannels; ++c)
        {
            // Swept on a log scale, so the sweep sounds even
            const float mod = lfo.valueAt (c == 1 ? 0.25 : 0.0);
            const float samples = centre * msToSamples * std::exp2 (static_cast<float> (flangerOctaves) * d * mod);

            auto& line = lines[static_cast<size_t> (c)];
            const float in = channels[c][i];
            const float y = line.readCubic (std::clamp (samples, shortest, longest));
            line.write (in + fb * y);
            channels[c][i] = in + m * (y - in);
        }
        lfo.advance (1);
    }
}

//==============================================================================
namespace
{
constexpr double phaserCentreHz = 894.0;    // geometric middle of 200 Hz - 4 kHz
constexpr double phaserOctaves = 2.16;      // each way at full depth
}

const PedalInfo& PhaserPedal::staticInfo()
{
    static const PedalInfo i { "phaser", "Phaser", {
        { "rate",     "Rate",     0.05f, 5.0f,   0.5f, "Hz", K::continuous, {}, T::log },
        { "depth",    "Depth",    0.0f,  100.0f, 70.0f, "%" },
        { "feedback", "Feedback", 0.0f,  90.0f,  30.0f, "%" },
        { "mix",      "Mix",      0.0f,  100.0f, 50.0f, "%" },
        { "stages",   "Stages",   0.0f,  2.0f,   0.0f,  {}, K::choice, { "4 stages", "8 stages", "12 stages" } },
        { "sync",     "Sync",     0.0f,  7.0f,   0.0f,  {}, K::choice, lfoSync::choices },
    } };
    return i;
}

void PhaserPedal::prepare (double newSampleRate, int, int numChannels)
{
    sampleRate = newSampleRate;
    states.assign (static_cast<size_t> (numChannels), {});
    lastOut.assign (static_cast<size_t> (numChannels), 0.0f);
    lfo.prepare (sampleRate);
    lfo.reset();
    for (auto* s : { &depthAmount, &feedbackGain, &wet })
        s->prepare (sampleRate, 0.03);
    for (auto& g : stageGains)
        g.prepare (sampleRate, 0.05);
    primed = false;
}

void PhaserPedal::reset()
{
    for (auto& s : states)
        s.fill (0.0f);
    std::fill (lastOut.begin(), lastOut.end(), 0.0f);
    lfo.reset();
}

void PhaserPedal::process (float* const* channels, int numChannels, int numSamples)
{
    lfoSync::apply (lfo, choice (sync), value (rate), { getTempo(), getPlayPosition(), isHostPlaying() });
    depthAmount.setTarget (value (depth) / 100.0f);
    feedbackGain.setTarget (value (feedback) / 100.0f);
    wet.setTarget (isBypassed() ? 0.0f : value (mix) / 100.0f);
    const int numStages = 4 * (choice (stages) + 1);
    for (int k = 0; k < maxStages; ++k)
        stageGains[static_cast<size_t> (k)].setTarget (k < numStages ? 1.0f : 0.0f);
    if (! primed)
    {
        for (auto* s : { &depthAmount, &feedbackGain, &wet })
            s->reset (s->getTarget());
        for (auto& g : stageGains)
            g.reset (g.getTarget());
        primed = true;
    }

    numChannels = std::min (numChannels, static_cast<int> (states.size()));
    const double piOverFs = std::numbers::pi / sampleRate;
    const double highest = sampleRate * 0.45;

    for (int i = 0; i < numSamples; ++i)
    {
        const float d = depthAmount.next(), fb = feedbackGain.next(), m = wet.next();
        std::array<float, maxStages> g {};
        for (size_t k = 0; k < g.size(); ++k)
            g[k] = stageGains[k].next();

        for (int c = 0; c < numChannels; ++c)
        {
            const double hz = std::min (highest, phaserCentreHz * std::exp2 (phaserOctaves * d * lfo.valueAt (c == 1 ? 0.25 : 0.0)));
            const double t = std::tan (piOverFs * hz);
            const auto a = static_cast<float> ((t - 1.0) / (t + 1.0));

            auto& s = states[static_cast<size_t> (c)];
            auto& last = lastOut[static_cast<size_t> (c)];
            const float in = channels[c][i];

            // A stage fading out blends towards a straight wire
            float y = in + fb * last;
            for (size_t k = 0; k < g.size(); ++k)
            {
                const float ap = allpass (y, a, s[k]);
                y += g[k] * (ap - y);
            }
            last = y;
            channels[c][i] = in + m * (y - in);
        }
        lfo.advance (1);
    }
}

} // namespace fx
