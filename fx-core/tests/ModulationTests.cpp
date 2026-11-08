#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "fx/DelayLine.h"
#include "fx/Lfo.h"
#include "TestUtils.h"

#include <algorithm>

using Catch::Matchers::WithinAbs;

TEST_CASE ("Lfo shapes stay in range and repeat at the rate", "[lfo]")
{
    using S = fx::Lfo::Shape;
    const auto shape = GENERATE (S::sine, S::triangle, S::square, S::random);
    CAPTURE (static_cast<int> (shape));

    fx::Lfo lfo;
    lfo.prepare (48000.0);
    lfo.setRate (2.0);
    lfo.setShape (shape);
    lfo.reset();

    float lo = 1.0f, hi = -1.0f, biggestStep = 0.0f, last = lfo.valueAt (0.0);
    for (int i = 0; i < 48000; ++i)
    {
        const float v = lfo.next();
        lo = std::min (lo, v);
        hi = std::max (hi, v);
        biggestStep = std::max (biggestStep, std::abs (v - last));
        last = v;
    }

    CHECK (lo >= -1.0f);
    CHECK (hi <= 1.0f);
    CHECK (hi - lo > 0.5f);   // it actually moves (random included)

    // No jumps: the steepest shape (square slopes, 1/16 cycle) moves at most
    // 2 / (24000 / 16) per sample at 2 Hz
    CHECK (biggestStep < 2.0f / (24000.0f / 16.0f) * 1.01f);

    // Two cycles in a second: back where it started (random excepted)
    if (shape != S::random)
        CHECK_THAT (lfo.valueAt (0.0), WithinAbs (fx::Lfo{}.valueAt (0.0), 1e-3));
}

TEST_CASE ("Lfo phase offsets", "[lfo]")
{
    fx::Lfo lfo;
    lfo.prepare (48000.0);
    lfo.setRate (1.0);
    lfo.reset();
    CHECK_THAT (lfo.valueAt (0.25), WithinAbs (1.0, 1e-6));    // sine peaks a quarter cycle in
    CHECK_THAT (lfo.valueAt (0.75), WithinAbs (-1.0, 1e-6));

    lfo.setShape (fx::Lfo::Shape::triangle);
    CHECK_THAT (lfo.valueAt (0.25), WithinAbs (1.0, 1e-6));
    CHECK_THAT (lfo.valueAt (-0.25), WithinAbs (-1.0, 1e-6));  // negative offsets wrap

    // Random: an offset of whole cycles reads that later cycle's value
    lfo.setShape (fx::Lfo::Shape::random);
    const float ahead = lfo.valueAt (3.0);
    lfo.advance (3 * 48000);
    CHECK_THAT (lfo.valueAt (0.0), WithinAbs (ahead, 1e-6));
}

TEST_CASE ("DelayLine integer and fractional reads", "[delayline]")
{
    fx::DelayLine d;
    d.prepare (100);

    // Feed a ramp: sample n is n
    for (int n = 0; n < 50; ++n)
        d.write (static_cast<float> (n));

    // The next sample would be 50, so delay k reads 50 - k
    CHECK (d.readInteger (1) == 49.0f);
    CHECK (d.readInteger (10) == 40.0f);
    CHECK_THAT (d.readLinear (10.25f), WithinAbs (39.75, 1e-5));
    CHECK_THAT (d.readCubic (10.25f), WithinAbs (39.75, 1e-4));   // exact on a ramp

    // Out-of-range delays are clamped, never read outside the buffer
    CHECK (d.readInteger (0) == 49.0f);
    CHECK (d.readInteger (1000) == d.readInteger (100));
}

TEST_CASE ("DelayLine interpolation keeps the highs", "[delayline]")
{
    // A 10 kHz tone through a half-sample delay (the worst case)
    constexpr double fs = 48000.0;
    auto through = [&] (bool cubic)
    {
        fx::DelayLine d;
        d.prepare (64);
        const auto x = test::sine (10000.0, fs, 9600);
        std::vector<float> y (x.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            y[i] = cubic ? d.readCubic (20.5f) : d.readLinear (20.5f);
            d.write (x[i]);
        }
        return test::toDb (test::amplitudeAt (y, 10000.0, fs, 4800, 4800));
    };

    CHECK (through (false) < -1.5);   // linear dulls it
    CHECK (through (true) > -0.6);    // cubic barely does
}

TEST_CASE ("DelayLine modulated read has no zipper noise", "[delayline]")
{
    // Sweeping the delay smoothly through a 1 kHz tone: the output's
    // second difference should never be far above the tone's own
    constexpr double fs = 48000.0;
    fx::DelayLine d;
    d.prepare (2000);
    fx::Lfo lfo;
    lfo.prepare (fs);
    lfo.setRate (3.0);

    const auto x = test::sine (1000.0, fs, 48000, 0.5);
    std::vector<float> y (x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        y[i] = d.readCubic (480.0f + 120.0f * lfo.next());
        d.write (x[i]);
    }

    double worst = 0.0, reference = 0.0;
    for (size_t i = 2000; i < y.size(); ++i)
    {
        worst = std::max (worst, std::abs (static_cast<double> (y[i]) - 2.0 * y[i - 1] + y[i - 2]));
        reference = std::max (reference, std::abs (static_cast<double> (x[i]) - 2.0 * x[i - 1] + x[i - 2]));
    }
    CHECK (worst < reference * 1.2);   // the Doppler shift allows a little more
}
