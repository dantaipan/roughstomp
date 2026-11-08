#include "LoopPedals.h"

#include <cmath>

namespace fx
{

namespace
{
constexpr double baseDelayMs = 8.0;     // the first voice's centre delay
constexpr double voiceSpacingMs = 3.0;  // each further voice sits this much later
constexpr double maxSweepMs = 4.0;      // peak-to-peak sweep at full depth
} // namespace

const PedalInfo& ChorusPedal::staticInfo()
{
    using K = ParamInfo::Kind;
    using T = ParamInfo::Taper;
    static const PedalInfo i { "chorus", "Chorus", {
        { "rate",   "Rate",   0.1f, 5.0f,   0.8f,  "Hz", K::continuous, {}, T::log },
        { "depth",  "Depth",  0.0f, 100.0f, 50.0f, "%" },
        { "mix",    "Mix",    0.0f, 100.0f, 50.0f, "%" },
        { "width",  "Width",  0.0f, 100.0f, 100.0f, "%" },
        { "voices", "Voices", 0.0f, 2.0f,   1.0f,  {}, K::choice, { "1 voice", "2 voices", "3 voices" } },
        { "sync",   "Sync",   0.0f, 7.0f,   0.0f,  {}, K::choice, lfoSync::choices },
    } };
    return i;
}

void ChorusPedal::prepare (double newSampleRate, int, int numChannels)
{
    sampleRate = newSampleRate;
    const double longestMs = baseDelayMs + 2.0 * voiceSpacingMs + maxSweepMs + 1.0;
    lines.resize (static_cast<size_t> (numChannels));
    for (auto& l : lines)
        l.prepare (static_cast<int> (std::ceil (longestMs * 0.001 * sampleRate)) + 4);

    lfo.prepare (sampleRate);
    lfo.reset();
    for (auto* s : { &depthAmount, &wet, &spread })
        s->prepare (sampleRate, 0.05);
    for (auto& g : voiceGains)
        g.prepare (sampleRate, 0.05);
    primed = false;
}

void ChorusPedal::setTargets() noexcept
{
    lfoSync::apply (lfo, choice (sync), value (rate), { getTempo(), getPlayPosition(), isHostPlaying() });
    depthAmount.setTarget (value (depth) / 100.0f);
    wet.setTarget (isBypassed() ? 0.0f : value (mix) / 100.0f);
    spread.setTarget (value (width) / 100.0f);
    const int numVoices = choice (voices) + 1;
    for (int v = 0; v < maxVoices; ++v)
        voiceGains[static_cast<size_t> (v)].setTarget (v < numVoices ? 1.0f : 0.0f);
}

void ChorusPedal::reset()
{
    for (auto& l : lines)
        l.reset();
    lfo.reset();
}

void ChorusPedal::process (float* const* channels, int numChannels, int numSamples)
{
    setTargets();
    if (! primed)
    {
        for (auto* s : { &depthAmount, &wet, &spread })
            s->reset (s->getTarget());
        for (auto& g : voiceGains)
            g.reset (g.getTarget());
        primed = true;
    }

    numChannels = std::min (numChannels, static_cast<int> (lines.size()));
    const auto msToSamples = static_cast<float> (0.001 * sampleRate);

    for (int i = 0; i < numSamples; ++i)
    {
        const float d = depthAmount.next(), m = wet.next(), w = spread.next();

        // Voices at fixed places around the cycle, so adding one never moves
        // the others; the total is kept at about the same loudness
        std::array<float, maxVoices> g {};
        float total = 0.0f;
        for (size_t v = 0; v < g.size(); ++v)
            total += (g[v] = voiceGains[v].next());
        const float norm = 1.0f / std::sqrt (std::max (total, 1.0f));

        for (int c = 0; c < numChannels; ++c)
        {
            auto& line = lines[static_cast<size_t> (c)];
            const float in = channels[c][i];

            // Voices spread evenly around the cycle; the right channel runs a
            // quarter cycle ahead at full width
            float sum = 0.0f;
            for (int v = 0; v < maxVoices; ++v)
            {
                const float gain = g[static_cast<size_t> (v)];
                if (gain <= 0.0f)
                    continue;
                const double offset = static_cast<double> (v) / maxVoices + (c == 1 ? 0.25 * w : 0.0);
                const float mod = lfo.valueAt (offset);
                const float ms = static_cast<float> (baseDelayMs + v * voiceSpacingMs) + 0.5f * static_cast<float> (maxSweepMs) * d * mod;
                sum += gain * line.readCubic (ms * msToSamples);
            }
            line.write (in);

            channels[c][i] = in + m * (sum * norm - in);
        }
        lfo.advance (1);
    }
}

} // namespace fx
