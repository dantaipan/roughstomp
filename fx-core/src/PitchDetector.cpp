#include "fx/PitchDetector.h"

#include <algorithm>
#include <array>

namespace fx
{

namespace
{
constexpr float yinThreshold = 0.12f;
} // namespace

void PitchDetector::prepare (double newSampleRate, int windowSize)
{
    sampleRate = newSampleRate;
    window = windowSize;
    maxLag = std::min (window / 2, static_cast<int> (sampleRate / lowestHz) + 2);
    minLag = std::max (2, static_cast<int> (sampleRate / highestHz));
    difference.assign (static_cast<size_t> (maxLag + 1), 0.0f);
    raw.assign (static_cast<size_t> (maxLag + 1), 0.0);
}

PitchDetector::Result PitchDetector::detect (const float* x) noexcept
{
    const int w = window - maxLag;   // samples compared at each lag
    if (w <= 0)
        return {};

    // Quiet input: no pitch worth showing
    double energy = 0.0;
    for (int i = 0; i < window; ++i)
        energy += static_cast<double> (x[i]) * x[i];
    if (energy / window < 1e-8)
        return {};

    // Difference function and its cumulative-mean normalisation (YIN steps 2-3)
    difference[0] = 1.0f;
    double runningSum = 0.0;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double d = 0.0;
        for (int i = 0; i < w; ++i)
        {
            const double diff = static_cast<double> (x[i]) - x[i + lag];
            d += diff * diff;
        }
        raw[static_cast<size_t> (lag)] = d;
        runningSum += d;
        difference[static_cast<size_t> (lag)] = runningSum > 0.0 ? static_cast<float> (d * lag / runningSum) : 1.0f;
    }

    // The first dip under the threshold, followed down to its bottom (step 4);
    // failing that, the deepest dip
    int best = -1;
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (difference[static_cast<size_t> (lag)] < yinThreshold)
        {
            while (lag + 1 <= maxLag && difference[static_cast<size_t> (lag + 1)] < difference[static_cast<size_t> (lag)])
                ++lag;
            best = lag;
            break;
        }
    if (best < 0)
    {
        best = minLag;
        for (int lag = minLag; lag <= maxLag; ++lag)
            if (difference[static_cast<size_t> (lag)] < difference[static_cast<size_t> (best)])
                best = lag;
    }

    // Parabolic interpolation around the dip for sub-sample accuracy (step 5),
    // on the plain difference function: the normalised one is skewed
    double lag = best;
    if (best > 1 && best < maxLag)
    {
        const double a = raw[static_cast<size_t> (best - 1)];
        const double b = raw[static_cast<size_t> (best)];
        const double c = raw[static_cast<size_t> (best + 1)];
        const double denom = a - 2.0 * b + c;
        if (denom > 0.0)
            lag += 0.5 * (a - c) / denom;
    }

    // Refine on the furthest whole multiple of the period in range: the
    // same interpolation error divided by k (it matters for high notes,
    // which have few samples per period)
    if (const int k = static_cast<int> ((maxLag - 2) / lag); k >= 2)
    {
        int m = static_cast<int> (std::lround (k * lag));
        for (int step = 0; step < 3; ++step)   // walk to the bottom of that dip
        {
            if (m > 1 && raw[static_cast<size_t> (m - 1)] < raw[static_cast<size_t> (m)])
                --m;
            else if (m + 1 < maxLag && raw[static_cast<size_t> (m + 1)] < raw[static_cast<size_t> (m)])
                ++m;
        }
        if (m > 1 && m < maxLag)
        {
            const double a = raw[static_cast<size_t> (m - 1)], b = raw[static_cast<size_t> (m)], c = raw[static_cast<size_t> (m + 1)];
            const double denom = a - 2.0 * b + c;
            const double refined = (m + (denom > 0.0 ? 0.5 * (a - c) / denom : 0.0)) / k;
            if (std::abs (refined - lag) < 0.5)   // only if it's really the same period
                lag = refined;
        }
    }

    Result r;
    r.confidence = std::clamp (1.0f - difference[static_cast<size_t> (best)], 0.0f, 1.0f);
    r.hz = static_cast<float> (sampleRate / lag);
    return r;
}

Note Note::fromHz (double hz, double a4) noexcept
{
    static constexpr std::array<std::string_view, 12> names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    Note n;
    if (hz <= 0.0)
        return n;

    const double midi = 69.0 + 12.0 * std::log2 (hz / a4);
    n.midi = static_cast<int> (std::lround (midi));
    n.cents = static_cast<float> ((midi - n.midi) * 100.0);
    n.name = names[static_cast<size_t> (((n.midi % 12) + 12) % 12)];
    n.octave = n.midi / 12 - 1;
    return n;
}

} // namespace fx
