#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace fx
{

/** Low-frequency oscillator, output -1..1. Real-time safe throughout.

    Shapes are made to modulate without clicks: the square has short slopes
    (1/16 of a cycle) and random glides smoothly from one value to the next
    once per cycle. valueAt() reads the same waveform at a phase offset, for
    stereo spread or several voices from one oscillator. */
class Lfo
{
public:
    enum class Shape { sine, triangle, square, random };

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        setRate (rateHz);
    }

    /** Back to the start of a cycle (and the same random sequence). */
    void reset (double startPhase = 0.0) noexcept
    {
        cycle = 0;
        phase = wrap (startPhase);
    }

    /** Jumps to a position in cycles (whole cycles count, for random). */
    void setPosition (double cycles) noexcept
    {
        const double whole = std::floor (cycles);
        cycle = static_cast<std::int64_t> (whole);
        phase = cycles - whole;
    }

    /** The position in cycles since reset(). */
    double getPosition() const noexcept { return static_cast<double> (cycle) + phase; }

    void setRate (double hz) noexcept
    {
        rateHz = std::max (0.0, hz);
        increment = sampleRate > 0.0 ? rateHz / sampleRate : 0.0;
    }

    void setShape (Shape s) noexcept { shape = s; }

    double getRate()  const noexcept { return rateHz; }
    double getPhase() const noexcept { return phase; }

    /** The current value, then advances one sample. */
    float next() noexcept
    {
        const float v = valueAt (0.0);
        advance (1);
        return v;
    }

    void advance (int numSamples) noexcept
    {
        phase += increment * numSamples;
        if (phase >= 1.0)
        {
            const auto whole = static_cast<std::int64_t> (phase);
            cycle += whole;
            phase -= static_cast<double> (whole);
        }
    }

    /** The current value at a phase offset (in cycles, e.g. 0.25 = 90°),
        without advancing. */
    float valueAt (double offset) const noexcept
    {
        double p = phase + offset;
        const auto whole = static_cast<std::int64_t> (std::floor (p));
        p -= static_cast<double> (whole);

        switch (shape)
        {
            case Shape::sine:     return static_cast<float> (std::sin (2.0 * std::numbers::pi * p));
            case Shape::triangle: return triangle (p);
            case Shape::square:   return std::clamp (8.0f * triangle (p), -1.0f, 1.0f);
            case Shape::random:
            {
                // Raised-cosine glide between one random value per cycle
                const auto n = cycle + whole;
                const double t = 0.5 - 0.5 * std::cos (std::numbers::pi * p);
                const double a = randomAt (n), b = randomAt (n + 1);
                return static_cast<float> (a + t * (b - a));
            }
        }
        return 0.0f;
    }

private:
    static double wrap (double p) noexcept { return p - std::floor (p); }

    /** Sine-phased triangle: 0 at p = 0, peaks at 0.25. */
    static float triangle (double p) noexcept
    {
        const double t = p < 0.25 ? 4.0 * p : (p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
        return static_cast<float> (t);
    }

    /** A repeatable random value in -1..1 for cycle n. */
    static double randomAt (std::int64_t n) noexcept
    {
        auto x = static_cast<std::uint64_t> (n) + 0x9e3779b97f4a7c15ull;   // splitmix64
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
        x ^= x >> 31;
        return static_cast<double> (x >> 11) / static_cast<double> (1ull << 52) - 1.0;
    }

    double sampleRate = 48000.0, rateHz = 1.0, increment = 0.0;
    double phase = 0.0;
    std::int64_t cycle = 0;
    Shape shape = Shape::sine;
};

} // namespace fx
