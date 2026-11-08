#include "fx/RateBridge.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fx
{

namespace
{
constexpr int tableResolution = 512;   // table points per zero crossing

double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 50; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}

std::int64_t floorDiv (std::int64_t a, std::int64_t b) noexcept
{
    const auto q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

int nextPowerOfTwo (int n) noexcept
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

std::int64_t roundRate (double rate) noexcept { return static_cast<std::int64_t> (std::llround (rate)); }
} // namespace

//==============================================================================
double RateConverter::halfWidth (std::int64_t in, std::int64_t out) noexcept
{
    const double s = std::min (1.0, static_cast<double> (out) / static_cast<double> (in));
    return zeroCrossings / s;
}

void RateConverter::prepare (std::int64_t newInRate, std::int64_t newOutRate, std::int64_t delayOut, int historyCapacity)
{
    inRate = newInRate;
    outRate = newOutRate;
    delay = delayOut;
    scale = std::min (1.0, static_cast<double> (outRate) / static_cast<double> (inRate));
    width = halfWidth (inRate, outRate);

    // g(t) = cutoff * sinc (cutoff * t) * kaiser (t / zeroCrossings), t >= 0,
    // in units of the lower rate's samples. One extra point for interpolation.
    const int points = zeroCrossings * tableResolution + 2;
    table.assign (static_cast<size_t> (points), 0.0f);
    const double norm = besselI0 (kaiserBeta);
    for (int i = 0; i < points; ++i)
    {
        const double t = static_cast<double> (i) / tableResolution;
        const double r = t / zeroCrossings;
        if (r >= 1.0)
            break;
        const double x = std::numbers::pi * cutoff * t;
        const double sinc = t == 0.0 ? 1.0 : std::sin (x) / x;
        const double window = besselI0 (kaiserBeta * std::sqrt (1.0 - r * r)) / norm;
        table[static_cast<size_t> (i)] = static_cast<float> (cutoff * sinc * window);
    }

    const int capacity = nextPowerOfTwo (std::max (historyCapacity, 16));
    history.assign (static_cast<size_t> (capacity), 0.0f);
    mask = capacity - 1;
    reset();
}

void RateConverter::reset() noexcept
{
    std::fill (history.begin(), history.end(), 0.0f);
    written = 0;
    underruns = 0;

    // The first output reads input position -delay * inRate / outRate
    const auto start = -delay * inRate;
    position = floorDiv (start, outRate);
    remainder = start - position * outRate;
}

void RateConverter::push (const float* input, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        history[static_cast<size_t> ((written + i) & mask)] = input[i];
    written += numSamples;
}

bool RateConverter::available() const noexcept
{
    // The highest input sample this output reads: floor (p + width)
    const double p = static_cast<double> (position) + static_cast<double> (remainder) / static_cast<double> (outRate);
    return static_cast<std::int64_t> (std::floor (p + width)) < written;
}

void RateConverter::advance() noexcept
{
    remainder += inRate;
    position += remainder / outRate;
    remainder %= outRate;
}

float RateConverter::outputAt() const noexcept
{
    const double frac = static_cast<double> (remainder) / static_cast<double> (outRate);
    const double p = static_cast<double> (position) + frac;

    // Every input i with |i - p| < width, skipping anything before the start
    // or not yet written (silence)
    auto lo = static_cast<std::int64_t> (std::floor (p - width)) + 1;
    auto hi = static_cast<std::int64_t> (std::floor (p + width));
    lo = std::max<std::int64_t> (lo, 0);
    hi = std::min<std::int64_t> (hi, written - 1);

    const double step = scale * tableResolution;   // table points per input sample
    const auto last = static_cast<double> (table.size() - 2);
    double sum = 0.0;
    for (auto i = lo; i <= hi; ++i)
    {
        const double t = std::min (std::abs (static_cast<double> (i) - p) * step, last);
        const auto k = static_cast<size_t> (t);
        const double f = t - static_cast<double> (k);
        const double g = table[k] + f * (table[k + 1] - table[k]);
        sum += g * history[static_cast<size_t> (i & mask)];
    }
    return static_cast<float> (sum * scale);
}

int RateConverter::pull (float* output, int maxOutputs) noexcept
{
    int n = 0;
    while (n < maxOutputs && available())
    {
        output[n++] = outputAt();
        advance();
    }
    return n;
}

void RateConverter::pullExactly (float* output, int numOutputs) noexcept
{
    for (int i = 0; i < numOutputs; ++i)
    {
        if (! available())
            ++underruns;
        output[i] = outputAt();
        advance();
    }
}

//==============================================================================
int RateBridge::latencyFor (double outerRate, double innerRate) noexcept
{
    const auto outer = roundRate (outerRate), inner = roundRate (innerRate);
    if (inner <= 0 || outer <= 0 || inner == outer)
        return 0;

    // Inner sample k is ready once outer sample k*r + wIn has arrived; outer
    // output j reads inner samples up to (j - L)/r + wOut. Both hold at the
    // end of every block when L >= wOut*r + wIn + r (r = outer/inner).
    const double r = static_cast<double> (outer) / static_cast<double> (inner);
    const double wIn = RateConverter::halfWidth (outer, inner);    // outer samples
    const double wOut = RateConverter::halfWidth (inner, outer);   // inner samples
    return static_cast<int> (std::ceil (wOut * r + wIn + r)) + 1;
}

void RateBridge::prepare (double outerRate, double innerRate, int maxOuterBlock)
{
    latency = latencyFor (outerRate, innerRate);
    active = latency > 0;
    maxInner = maxOuterBlock;

    if (! active)
    {
        buffer.clear();
        return;
    }

    const auto outer = roundRate (outerRate), inner = roundRate (innerRate);
    const double r = static_cast<double> (outer) / static_cast<double> (inner);
    maxInner = static_cast<int> (std::ceil (maxOuterBlock / r)) + 4;
    buffer.assign (static_cast<size_t> (maxInner), 0.0f);

    // History: the filter's span, plus a block, plus (for the way back) the
    // latency held in it
    const auto spanIn = static_cast<int> (2.0 * RateConverter::halfWidth (outer, inner)) + 8;
    const auto spanOut = static_cast<int> (2.0 * RateConverter::halfWidth (inner, outer)) + 8;
    toInner.prepare (outer, inner, 0, spanIn + maxOuterBlock + 8);
    fromInner.prepare (inner, outer, latency, spanOut + maxInner + static_cast<int> (latency / r) + 8);
}

void RateBridge::reset() noexcept
{
    if (! active)
        return;
    toInner.reset();
    fromInner.reset();
}

} // namespace fx
