#include "CapturePedal.h"

#include <cmath>

namespace
{
float dbToGain (float dB) noexcept { return std::pow (10.0f, dB / 20.0f); }
} // namespace

const fx::PedalInfo& CapturePedal::staticInfo()
{
    static const fx::PedalInfo i { "nam", "NAM Capture", {
        { "input", "Input", -24.0f, 24.0f, 0.0f, "dB" },
        { "level", "Level", -24.0f, 12.0f, 0.0f, "dB" },
        { "mix",   "Mix",   0.0f, 100.0f, 100.0f, "%" },
    } };
    return i;
}

void CapturePedal::setCaptures (std::unique_ptr<Captures> next)
{
    if (next == nullptr)
        next = std::make_unique<Captures>();
    const int newLatency = next->models.empty() ? 0 : next->latency;
    loaded = ! next->models.empty();
    handoff.publish (std::move (next));
    latency = newLatency;
}

void CapturePedal::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    sampleRate = newSampleRate;
    maxBlock = maxBlockSize;

    // Not running now (the chain prepares only while process() isn't), so the
    // models can be re-prepared for the new settings
    auto& c = handoff.takeOver();
    for (auto& m : c.models)
        m->prepare (sampleRate, maxBlock);
    if (! c.models.empty())
        c.latency = c.models.front()->getLatencySamples();
    latency = c.models.empty() ? 0 : c.latency;

    dryLines.assign (static_cast<size_t> (numChannels), {});
    for (auto& l : dryLines)
        l.prepare (maxLatency + maxBlock + 8);
    dry.assign (static_cast<size_t> (numChannels), std::vector<float> (static_cast<size_t> (maxBlock), 0.0f));

    for (auto* s : { &inputGain, &levelGain, &wet })
        s->prepare (sampleRate, 0.02);
    primed = false;
}

void CapturePedal::reset()
{
    for (auto& l : dryLines)
        l.reset();
}

void CapturePedal::process (float* const* channels, int numChannels, int numSamples)
{
    auto& c = handoff.acquire();
    numChannels = std::min (numChannels, static_cast<int> (dryLines.size()));
    if (c.models.empty() || static_cast<int> (c.models.size()) < numChannels)
        return;   // no capture: straight through

    inputGain.setTarget (dbToGain (value (input)));
    levelGain.setTarget (dbToGain (value (level)));
    wet.setTarget (isBypassed() ? 0.0f : value (mix) / 100.0f);
    if (! primed)
    {
        for (auto* s : { &inputGain, &levelGain, &wet })
            s->reset (s->getTarget());
        primed = true;
    }

    const int lat = std::min (c.latency, maxLatency);
    for (int pos = 0; pos < numSamples; pos += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - pos);

        // The dry signal, delayed to line up with the capture's output
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& line = dryLines[static_cast<size_t> (ch)];
            float* x = channels[ch] + pos;
            float* d = dry[static_cast<size_t> (ch)].data();
            for (int i = 0; i < n; ++i)
            {
                d[i] = lat > 0 ? line.readInteger (lat) : x[i];   // read, then write: exactly lat samples behind
                line.write (x[i]);
            }
        }

        // Input gain into each channel's model
        for (int i = 0; i < n; ++i)
        {
            const float g = inputGain.next();
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][pos + i] *= g;
        }
        for (int ch = 0; ch < numChannels; ++ch)
            c.models[static_cast<size_t> (ch)]->process (channels[ch] + pos, n, true);

        // Level, then the blend with the aligned dry signal
        for (int i = 0; i < n; ++i)
        {
            const float l = levelGain.next(), w = wet.next();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float d = dry[static_cast<size_t> (ch)][static_cast<size_t> (i)];
                channels[ch][pos + i] = d + w * (l * channels[ch][pos + i] - d);
            }
        }
    }
}
