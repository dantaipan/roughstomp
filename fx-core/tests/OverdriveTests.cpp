#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "fx/effects/Overdrive.h"
#include "TestUtils.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double fs = 48000.0;
constexpr int block = 256;

/** Runs a mono signal through a freshly prepared Overdrive. */
std::vector<float> run (const fx::Overdrive::Parameters& p, std::vector<float> x)
{
    fx::Overdrive od;
    od.setParameters (p);
    od.prepare (fs, block, 1);

    for (size_t pos = 0; pos < x.size(); pos += block)
    {
        float* ch[] = { x.data() + pos };
        od.process (ch, 1, static_cast<int> (std::min<size_t> (block, x.size() - pos)));
    }
    return x;
}

double peak (const std::vector<float>& x, size_t from = 0)
{
    double m = 0.0;
    for (size_t i = from; i < x.size(); ++i)
        m = std::max (m, static_cast<double> (std::abs (x[i])));
    return m;
}

/** Harmonic distortion: energy in harmonics 2..10 relative to the fundamental. */
double thd (const std::vector<float>& y, double f)
{
    const size_t start = y.size() - 4800, len = 4800;
    const double fund = test::amplitudeAt (y, f, fs, start, len);
    double h = 0.0;
    for (int k = 2; k <= 10; ++k)
        h += std::pow (test::amplitudeAt (y, f * k, fs, start, len), 2.0);
    return std::sqrt (h) / fund;
}
} // namespace

TEST_CASE ("Overdrive reports the oversampler latency", "[overdrive]")
{
    fx::Overdrive od;
    od.prepare (fs, block, 2);
    REQUIRE (od.getLatencySamples() == 42); // 42.25 at 8x, rounded
}

TEST_CASE ("Overdrive is silent on silence", "[overdrive]")
{
    const auto y = run ({ 1.0f, 0.5f, 12.0f, 1.0f, false }, std::vector<float> (9600, 0.0f));
    REQUIRE (peak (y) == 0.0);
}

TEST_CASE ("Bypassed overdrive passes the input, delayed by its latency", "[overdrive]")
{
    const double f = 440.0, latency = 42.25;
    const auto y = run ({ 1.0f, 0.5f, 0.0f, 1.0f, true }, test::sine (f, fs, 9600, 0.5));

    double maxErr = 0.0;
    for (size_t i = 200; i < y.size(); ++i)
        maxErr = std::max (maxErr, std::abs (y[i] - 0.5 * std::sin (2.0 * std::numbers::pi * f * (static_cast<double> (i) - latency) / fs)));
    REQUIRE (maxErr < 1.0e-3);
}

TEST_CASE ("More drive means more distortion", "[overdrive]")
{
    const double f = 220.0;
    const auto in = test::sine (f, fs, 24000, 0.25); // a DI-ish -12 dBFS peak

    double last = 0.0;
    for (float drive : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const double d = thd (run ({ drive, 1.0f, 0.0f, 1.0f, false }, in), f);
        CAPTURE (drive, d);
        REQUIRE (d > last);
        last = d;
    }
    REQUIRE (last > 0.3); // heavily driven at full drive
}

TEST_CASE ("Output stays bounded however hard it is driven", "[overdrive]")
{
    const auto y = run ({ 1.0f, 1.0f, 0.0f, 1.0f, false }, test::sine (220.0, fs, 24000, 4.0)); // +12 dBFS in
    REQUIRE (peak (y, 2400) < 0.5);  // clipper ceiling is 0.25; filters can overshoot a little
}

TEST_CASE ("Tone knob brightens the sound", "[overdrive]")
{
    const double f = 220.0;
    const auto in = test::sine (f, fs, 24000, 0.25);

    auto highs = [&] (float tone)
    {
        const auto y = run ({ 0.7f, tone, 0.0f, 1.0f, false }, in);
        return test::amplitudeAt (y, f * 15, fs, y.size() - 4800, 4800); // 3.3 kHz harmonic
    };

    REQUIRE (test::toDb (highs (1.0f) / highs (0.0f)) > 12.0);
}

TEST_CASE ("Mix at 0 is the clean signal at the output level", "[overdrive]")
{
    const double f = 440.0;
    const auto y = run ({ 1.0f, 0.5f, -6.0f, 0.0f, false }, test::sine (f, fs, 9600, 0.5));
    REQUIRE_THAT (test::amplitudeAt (y, f, fs, 4800, 4800), WithinAbs (0.5 * 0.501187, 2e-3));
    REQUIRE (thd (y, f) < 1e-3);
}

TEST_CASE ("Bypass toggles without clicks", "[overdrive]")
{
    // A steady tone; flip bypass on mid-stream and look for a jump
    const double f = 220.0;
    auto x = test::sine (f, fs, 19200, 0.25);

    fx::Overdrive od;
    od.setParameters ({ 0.3f, 0.5f, 0.0f, 1.0f, false });
    od.prepare (fs, block, 1);

    for (size_t pos = 0; pos < x.size(); pos += block)
    {
        if (pos == 9600)
            od.setParameters ({ 0.3f, 0.5f, 0.0f, 1.0f, true });

        float* ch[] = { x.data() + pos };
        od.process (ch, 1, block);
    }

    // The largest sample-to-sample step stays within what the tone itself
    // produces (2*pi*f/fs * amplitude ~= 0.007 for the clean tone)
    double maxStep = 0.0;
    for (size_t i = 4800; i + 1 < x.size(); ++i)
        maxStep = std::max (maxStep, static_cast<double> (std::abs (x[i + 1] - x[i])));
    REQUIRE (maxStep < 0.02);
}

TEST_CASE ("Aliasing stays far below the signal at full drive", "[overdrive]")
{
    // With fs = 48 kHz and a 4800-sample window, bins are 10 Hz apart. A
    // 1010 Hz tone puts every true harmonic on a multiple of 1010 Hz, so any
    // energy on other bins is aliasing (or noise).
    const double f = 1010.0;
    const auto y = run ({ 1.0f, 1.0f, 0.0f, 1.0f, false }, test::sine (f, fs, 24000, 0.25));
    const size_t start = y.size() - 4800, len = 4800;

    const double fundamental = test::amplitudeAt (y, f, fs, start, len);
    double worst = 0.0;

    for (int bin = 1; bin < 2400; ++bin)
    {
        if (bin % 101 == 0)
            continue; // a harmonic
        worst = std::max (worst, test::amplitudeAt (y, bin * 10.0, fs, start, len));
    }

    const double worstDb = test::toDb (worst / fundamental);
    INFO ("worst alias: " << worstDb << " dB");
    REQUIRE (worstDb < -65.0); // measured -69.7 at 8x (-60.2 at 4x)
}
