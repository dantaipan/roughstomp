#include "LoopPedals.h"

#include <algorithm>
#include <cmath>

namespace fx
{

namespace
{
struct Voicing
{
    std::array<double, 8> linesMs;      // at size 50%
    std::array<double, 4> diffusersMs;
    float diffusion;
    double modulationMs;
};

// Mutually prime-ish lengths so the echoes don't line up
constexpr std::array<Voicing, 3> voicings {{
    { { 13.1, 15.9, 17.5, 19.9, 22.3, 24.9, 28.1, 30.9 }, { 4.77, 3.59, 12.73, 9.31 }, 0.65f, 0.15 },   // room
    { { 31.3, 37.9, 41.7, 47.3, 53.1, 59.3, 67.1, 73.7 }, { 4.77, 3.59, 12.73, 9.31 }, 0.70f, 0.30 },   // hall
    { { 17.9, 21.1, 23.3, 26.9, 29.3, 32.7, 35.9, 39.1 }, { 3.11, 2.29, 7.93, 5.87 },  0.78f, 0.10 },   // plate: dense, quick build-up
}};

constexpr double maxScale = 1.5;   // size 100%
constexpr double longestLineMs = 73.7 * maxScale + 1.0;
constexpr double maxPreDelayMs = 200.0;

// Rows of an 8x8 Hadamard matrix: how lines are fed and summed to each side
constexpr std::array<float, 8> inputSigns { 1, -1, 1, 1, -1, 1, -1, -1 };
// Chosen so that lines next to each other in length (similar energy) land
// in phase on one side and out of phase on the other: the sides decorrelate
constexpr std::array<float, 8> leftSigns  { 1, 1, -1, -1, 1, 1, -1, -1 };
constexpr std::array<float, 8> rightSigns { 1, -1, -1, 1, 1, -1, -1, 1 };
constexpr float outputScale = 0.3f;

double scaleFor (float sizePercent) noexcept { return 0.5 + sizePercent / 100.0; }

double meanLineMs (const Voicing& v) noexcept
{
    double sum = 0.0;
    for (double ms : v.linesMs)
        sum += ms;
    return sum / static_cast<double> (v.linesMs.size());
}

/** For a given decay time, shorter lines ring louder (power goes as
    1 / sqrt (mean length)), so the output is scaled to keep every mode and
    size at the level of the hall at 50%. */
float loudnessFor (const Voicing& v, double scale) noexcept
{
    const double reference = meanLineMs (voicings[1]);
    return static_cast<float> (std::pow (meanLineMs (v) * scale / reference, 0.25));
}
} // namespace

const PedalInfo& ReverbPedal::staticInfo()
{
    using K = ParamInfo::Kind;
    using T = ParamInfo::Taper;
    static const PedalInfo i { "reverb", "Reverb", {
        { "decay",    "Decay",     0.2f,  10.0f,   1.8f,  "s",  K::continuous, {}, T::log },
        { "size",     "Size",      0.0f,  100.0f,  50.0f, "%" },
        { "preDelay", "Pre-delay", 0.0f,  200.0f,  10.0f, "ms" },
        { "tone",     "Tone",      1000.0f, 16000.0f, 6000.0f, "Hz", K::continuous, {}, T::log },
        { "mix",      "Mix",       0.0f,  100.0f,  25.0f, "%" },
        { "mode",     "Mode",      0.0f,  2.0f,    1.0f,  {}, K::choice, { "Room", "Hall", "Plate" } },
    } };
    return i;
}

void ReverbPedal::prepare (double newSampleRate, int, int)
{
    sampleRate = newSampleRate;
    const auto ms = [this] (double t) { return static_cast<int> (std::ceil (t * 0.001 * sampleRate)); };

    preDelayLine.prepare (ms (maxPreDelayMs) + 4);
    for (auto& d : diffusers)
        d.line.prepare (ms (13.0) + 4);
    for (auto& l : lines)
        l.prepare (ms (longestLineMs + 1.0) + 4);

    lfo.prepare (sampleRate);
    lfo.setRate (0.45);
    sizeScale.prepare (sampleRate, 0.1);
    preDelaySamples.prepare (sampleRate, 0.05);
    send.prepare (sampleRate, 0.02);
    wetGain.prepare (sampleRate, 0.02);

    primed = false;
    currentMode = -1;
    setMode (choice (mode));
    reset();
}

void ReverbPedal::reset()
{
    preDelayLine.reset();
    for (auto& d : diffusers)
        d.line.reset();
    for (auto& l : lines)
        l.reset();
    for (auto& f : damping)
        f.reset();
    lfo.reset();
}

void ReverbPedal::setMode (int m) noexcept
{
    m = std::clamp (m, 0, 2);
    if (m == currentMode)
        return;

    // The lines' lengths jump, so start from silence rather than glitch
    if (currentMode >= 0)
    {
        for (auto& l : lines)
            l.reset();
        for (auto& d : diffusers)
            d.line.reset();
    }

    currentMode = m;
    const auto& v = voicings[static_cast<size_t> (m)];
    for (size_t i = 0; i < diffusers.size(); ++i)
        diffusers[i].length = std::max (1, static_cast<int> (v.diffusersMs[i] * 0.001 * sampleRate));
}

void ReverbPedal::updateLines() noexcept
{
    // Each line loses 60 dB over the decay time, whatever its length
    const auto& v = voicings[static_cast<size_t> (currentMode)];
    const double scale = sizeScale.getTarget();
    const double rt60 = value (decay);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const double seconds = v.linesMs[i] * 0.001 * scale;
        gains[i] = static_cast<float> (std::pow (10.0, -3.0 * seconds / rt60));
    }
}

void ReverbPedal::process (float* const* channels, int numChannels, int numSamples)
{
    setMode (choice (mode));
    const auto& v = voicings[static_cast<size_t> (currentMode)];

    sizeScale.setTarget (static_cast<float> (scaleFor (value (size))));
    preDelaySamples.setTarget (static_cast<float> (value (preDelay) * 0.001 * sampleRate));
    send.setTarget (isBypassed() ? 0.0f : 1.0f);   // bypassed: the tail rings out
    wetGain.setTarget (value (mix) / 100.0f);
    if (! primed)
    {
        sizeScale.reset (sizeScale.getTarget());
        preDelaySamples.reset (preDelaySamples.getTarget());
        send.reset (send.getTarget());
        wetGain.reset (wetGain.getTarget());
        primed = true;
    }
    updateLines();

    const double toneHz = std::min (static_cast<double> (value (tone)), sampleRate * 0.45);
    for (auto& f : damping)
        f.setup (OnePole::Mode::lowPass, sampleRate, toneHz);

    const auto msToSamples = static_cast<float> (0.001 * sampleRate);
    const auto modDepth = static_cast<float> (v.modulationMs) * msToSamples;
    const float g = v.diffusion;
    const float maxRead = lines[0].getMaxDelay();

    for (int i = 0; i < numSamples; ++i)
    {
        float in = channels[0][i];
        if (numChannels > 1)
            in = 0.5f * (in + channels[1][i]);

        // Pre-delay, then smear the input into a dense cloud
        float x = preDelayLine.readLinear (std::max (1.0f, preDelaySamples.next()));
        preDelayLine.write (send.next() * in);
        for (auto& d : diffusers)
            x = d.process (x, g);

        // The network: read every line (slowly modulated, so it doesn't ring
        // metallic), damp, mix through a Householder matrix, feed back
        const float scale = sizeScale.next();
        std::array<float, numLines> y {};
        float sum = 0.0f;
        for (size_t l = 0; l < lines.size(); ++l)
        {
            const float len = static_cast<float> (v.linesMs[l]) * msToSamples * scale
                              + modDepth * lfo.valueAt (static_cast<double> (l) / numLines);
            y[l] = damping[l].processSample (lines[l].readCubic (std::min (len, maxRead)));
            sum += y[l];
        }
        lfo.advance (1);

        const float householder = 2.0f / numLines * sum;
        float left = 0.0f, right = 0.0f;
        for (size_t l = 0; l < lines.size(); ++l)
        {
            lines[l].write (inputSigns[l] * x + gains[l] * (y[l] - householder));
            left  += leftSigns[l] * y[l];
            right += rightSigns[l] * y[l];
        }

        const float w = wetGain.next() * outputScale * loudnessFor (v, scale);
        channels[0][i] += w * (numChannels > 1 ? left : 0.5f * (left + right));
        if (numChannels > 1)
            channels[1][i] += w * right;
    }
}

} // namespace fx
