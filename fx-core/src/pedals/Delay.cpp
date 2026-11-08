#include "LoopPedals.h"

#include <algorithm>
#include <cmath>

namespace fx
{

namespace
{
constexpr double glideSeconds = 0.15;   // tape: time constant when the delay time changes (a pitch bend)
constexpr double fadeSeconds = 0.05;    // digital: crossfade to a new time
constexpr double lowCutHz = 80.0;       // keeps repeats from piling up mud
constexpr double wowHz = 0.7, flutterHz = 6.3;
constexpr double wowSeconds = 0.0008, flutterSeconds = 0.00012;   // peak deviation
} // namespace

const PedalInfo& DelayPedal::staticInfo()
{
    using K = ParamInfo::Kind;
    using T = ParamInfo::Taper;
    static const PedalInfo i { "delay", "Delay", {
        { "time",     "Time",     20.0f, 2000.0f, 400.0f, "ms", K::continuous, {}, T::log },
        { "sync",     "Sync",     0.0f,  8.0f,    0.0f,   {}, K::choice,
          { "Free time", "1/2", "1/4", "1/4 dot", "1/4 trip", "1/8", "1/8 dot", "1/8 trip", "1/16" } },
        { "feedback", "Feedback", 0.0f,  95.0f,   35.0f,  "%" },
        { "mix",      "Mix",      0.0f,  100.0f,  35.0f,  "%" },
        { "tone",     "Tone",     1000.0f, 12000.0f, 5000.0f, "Hz", K::continuous, {}, T::log },
        { "mode",     "Mode",     0.0f,  2.0f,    0.0f,   {}, K::choice, { "Digital", "Tape", "Ping-Pong" } },
    } };
    return i;
}

double DelayPedal::syncBeats (int c) noexcept
{
    static constexpr std::array<double, 9> beats { 0.0, 2.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25 };
    return c >= 0 && c < static_cast<int> (beats.size()) ? beats[static_cast<size_t> (c)] : 0.0;
}

double DelayPedal::targetSeconds() const noexcept
{
    const double beats = syncBeats (choice (sync));
    const double seconds = beats > 0.0 ? beats * 60.0 / getTempo() : value (time) / 1000.0;
    return std::clamp (seconds, 0.001, maxSeconds - 0.01);
}

void DelayPedal::prepare (double newSampleRate, int, int numChannels)
{
    sampleRate = newSampleRate;
    const auto n = static_cast<size_t> (numChannels);

    lines.resize (n);
    for (auto& l : lines)
        l.prepare (static_cast<int> (std::ceil (maxSeconds * sampleRate)) + 8);

    toneFilters.assign (n, {});
    lowCuts.assign (n, {});
    for (auto& f : lowCuts)
        f.setup (OnePole::Mode::highPass, sampleRate, lowCutHz);

    delaySamples.prepare (sampleRate, glideSeconds);
    fadeLength = std::max (1, static_cast<int> (fadeSeconds * sampleRate));
    for (auto* s : { &send, &wetGain, &feedbackGain })
        s->prepare (sampleRate, 0.02);

    wow.prepare (sampleRate);
    wow.setRate (wowHz);
    flutter.prepare (sampleRate);
    flutter.setRate (flutterHz);
    primed = false;
}

void DelayPedal::reset()
{
    for (auto& l : lines)
        l.reset();
    for (auto& f : toneFilters)
        f.reset();
    for (auto& f : lowCuts)
        f.reset();
    wow.reset();
    flutter.reset (0.3);
}

void DelayPedal::process (float* const* channels, int numChannels, int numSamples)
{
    const auto m = static_cast<Mode> (choice (mode));
    const bool tape = m == Mode::tape;

    // Tape repeats are darker
    const double toneHz = std::min (static_cast<double> (value (tone)) * (tape ? 0.6 : 1.0), sampleRate * 0.45);
    for (auto& f : toneFilters)
        f.setup (OnePole::Mode::lowPass, sampleRate, toneHz);

    delaySamples.setTarget (static_cast<float> (targetSeconds() * sampleRate));
    send.setTarget (isBypassed() ? 0.0f : 1.0f);   // bypassed: no new input, the tail rings out
    wetGain.setTarget (value (mix) / 100.0f);
    feedbackGain.setTarget (value (feedback) / 100.0f);

    if (! primed)
    {
        // Start on the current settings rather than gliding from nothing
        delaySamples.reset (delaySamples.getTarget());
        fromDelay = toDelay = delaySamples.getTarget();
        fadeRemaining = 0;
        send.reset (send.getTarget());
        wetGain.reset (wetGain.getTarget());
        feedbackGain.reset (feedbackGain.getTarget());
        primed = true;
    }

    numChannels = std::min (numChannels, static_cast<int> (lines.size()));
    const bool pingPong = m == Mode::pingPong && numChannels == 2;
    const auto wowDepth = static_cast<float> (wowSeconds * sampleRate);
    const auto flutterDepth = static_cast<float> (flutterSeconds * sampleRate);

    const float target = delaySamples.getTarget();

    for (int i = 0; i < numSamples; ++i)
    {
        // Tape glides to a new time; digital fades over to it, keeping the pitch
        float d = delaySamples.next(), d2 = d, fade = 0.0f;
        if (tape)
            d = d2 += wowDepth * wow.next() + flutterDepth * flutter.next();
        else
        {
            if (fadeRemaining == 0 && std::abs (target - toDelay) >= 1.0f)
            {
                fromDelay = toDelay;
                toDelay = target;
                fadeRemaining = fadeLength;
            }
            if (fadeRemaining > 0)
            {
                --fadeRemaining;
                fade = 1.0f - static_cast<float> (fadeRemaining) / static_cast<float> (fadeLength);
                d = fromDelay;
                d2 = toDelay;
            }
            else
                d = d2 = toDelay;
        }

        const float s = send.next(), w = wetGain.next(), fb = feedbackGain.next();

        // Each channel's repeat, filtered: what you hear and what feeds back
        std::array<float, 2> repeat {};
        for (int c = 0; c < numChannels && c < 2; ++c)
        {
            auto& line = lines[static_cast<size_t> (c)];
            float y = line.readCubic (d);
            if (fade > 0.0f)
                y += fade * (line.readCubic (d2) - y);
            y = lowCuts[static_cast<size_t> (c)].processSample (toneFilters[static_cast<size_t> (c)].processSample (y));
            if (tape)
                y = std::tanh (y);
            repeat[static_cast<size_t> (c)] = y;
        }

        if (pingPong)
        {
            // The input starts on the left; each repeat crosses over
            const float in = 0.5f * (channels[0][i] + channels[1][i]);
            lines[0].write (s * in + fb * repeat[1]);
            lines[1].write (fb * repeat[0]);
        }
        else
        {
            for (int c = 0; c < numChannels && c < 2; ++c)
                lines[static_cast<size_t> (c)].write (s * channels[c][i] + fb * repeat[static_cast<size_t> (c)]);
        }

        for (int c = 0; c < numChannels && c < 2; ++c)
            channels[c][i] += w * repeat[static_cast<size_t> (c)];
    }
}

} // namespace fx
