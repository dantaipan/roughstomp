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

    // Up to 16 kHz: the audible band. Above it the downsampler's transition
    // band lets a little ultrasonic content fold to ~22 kHz, which nobody hears.
    for (int bin = 1; bin < 1600; ++bin)
    {
        if (bin % 101 == 0)
            continue; // a harmonic
        worst = std::max (worst, test::amplitudeAt (y, bin * 10.0, fs, start, len));
    }

    const double worstDb = test::toDb (worst / fundamental);
    INFO ("worst alias: " << worstDb << " dB");
    REQUIRE (worstDb < -65.0); // measured -69.7 at 8x (-60.2 at 4x)
}

//==============================================================================
// Phase 3: modes, touch and tone stack

namespace
{
using Mode = fx::Overdrive::Mode;
constexpr Mode allModes[] { Mode::soft, Mode::hard, Mode::asymmetric };

double harmonicDb (const std::vector<float>& y, double f, int k)
{
    const size_t start = y.size() - 4800, len = 4800;
    return test::toDb (test::amplitudeAt (y, f * k, fs, start, len) / test::amplitudeAt (y, f, fs, start, len));
}

double rmsDb (const std::vector<float>& x, size_t from)
{
    double sum = 0.0;
    for (size_t i = from; i < x.size(); ++i)
        sum += static_cast<double> (x[i]) * x[i];
    return 10.0 * std::log10 (sum / static_cast<double> (x.size() - from));
}
} // namespace

TEST_CASE ("Every mode is bounded and keeps aliasing low", "[overdrive][modes]")
{
    const double f = 1010.0; // see the aliasing test above for why 1010 Hz

    // Worst case: full drive, tone up, a pure tone. Measured at 8x: soft -93,
    // hard -61, asymmetric -69 dB. Hard's harmonics fall off slowest, so a few
    // fold back from above the 384 kHz internal rate (here at 1010 +/- 200 Hz).
    const double limits[] { -85.0, -59.0, -65.0 };

    for (Mode m : allModes)
    {
        CAPTURE (static_cast<int> (m));
        const auto y = run ({ 1.0f, 1.0f, 0.0f, 1.0f, false, m }, test::sine (f, fs, 24000, 0.25));
        REQUIRE (peak (y, 2400) < 0.6);

        const size_t start = y.size() - 4800, len = 4800;
        const double fundamental = test::amplitudeAt (y, f, fs, start, len);
        double worst = 0.0;
        for (int bin = 1; bin < 1600; ++bin) // audible band, as above
            if (bin % 101 != 0)
                worst = std::max (worst, test::amplitudeAt (y, bin * 10.0, fs, start, len));

        const double worstDb = test::toDb (worst / fundamental);
        INFO ("worst alias: " << worstDb << " dB");
        REQUIRE (worstDb < limits[static_cast<int> (m)]);
    }
}

TEST_CASE ("Asymmetric mode adds far more even harmonics than soft", "[overdrive][modes]")
{
    const double f = 220.0;
    const auto in = test::sine (f, fs, 24000, 0.25);

    const double softH2 = harmonicDb (run ({ 0.4f, 0.5f, 0.0f, 1.0f, false, Mode::soft }, in), f, 2);
    const double asymH2 = harmonicDb (run ({ 0.4f, 0.5f, 0.0f, 1.0f, false, Mode::asymmetric }, in), f, 2);
    CAPTURE (softH2, asymH2);

    // Soft gets some even harmonics from the touch bias too, by design
    REQUIRE (asymH2 > -22.0);
    REQUIRE (asymH2 > softH2 + 5.0);
}

TEST_CASE ("Hard mode clips harder than soft at the same drive", "[overdrive][modes]")
{
    const double f = 220.0;
    const auto in = test::sine (f, fs, 24000, 0.25);

    const double soft = thd (run ({ 0.3f, 0.5f, 0.0f, 1.0f, false, Mode::soft }, in), f);
    const double hard = thd (run ({ 0.3f, 0.5f, 0.0f, 1.0f, false, Mode::hard }, in), f);
    CAPTURE (soft, hard);
    REQUIRE (hard > soft);
}

TEST_CASE ("Modes sit at similar loudness", "[overdrive][modes]")
{
    // A harmonic-rich 110 Hz tone at a DI-ish level
    std::vector<float> in (24000);
    for (size_t i = 0; i < in.size(); ++i)
    {
        double s = 0.0;
        for (int k = 1; k <= 5; ++k)
            s += std::sin (2.0 * std::numbers::pi * 110.0 * k * static_cast<double> (i) / fs) / k;
        in[i] = static_cast<float> (0.12 * s);
    }

    const double softDb = rmsDb (run ({ 0.5f, 0.5f, 0.0f, 1.0f, false, Mode::soft }, in), 4800);
    for (Mode m : { Mode::hard, Mode::asymmetric })
    {
        const double db = rmsDb (run ({ 0.5f, 0.5f, 0.0f, 1.0f, false, m }, in), 4800);
        CAPTURE (static_cast<int> (m), db - softDb);
        REQUIRE (std::abs (db - softDb) < 3.0);
    }
}

TEST_CASE ("Changing mode crossfades instead of clicking", "[overdrive][modes]")
{
    const double f = 220.0;
    auto x = test::sine (f, fs, 19200, 0.25);
    const fx::Overdrive::Parameters soft { 0.3f, 0.5f, 0.0f, 1.0f, false, Mode::soft };
    auto hard = soft;
    hard.mode = Mode::hard;

    // The largest step each mode makes on its own, steady
    auto steadyStep = [&] (const fx::Overdrive::Parameters& p)
    {
        const auto y = run (p, x);
        double m = 0.0;
        for (size_t i = 4800; i + 1 < y.size(); ++i)
            m = std::max (m, static_cast<double> (std::abs (y[i + 1] - y[i])));
        return m;
    };
    const double limit = 1.25 * std::max (steadyStep (soft), steadyStep (hard));

    fx::Overdrive od;
    od.setParameters (soft);
    od.prepare (fs, block, 1);

    for (size_t pos = 0; pos < x.size(); pos += block)
    {
        if (pos == 9600)
            od.setParameters (hard);

        float* ch[] = { x.data() + pos };
        od.process (ch, 1, block);
    }

    double maxStep = 0.0;
    for (size_t i = 4800; i + 1 < x.size(); ++i)
        maxStep = std::max (maxStep, static_cast<double> (std::abs (x[i + 1] - x[i])));

    CAPTURE (maxStep, limit);
    REQUIRE (maxStep < limit);
}

TEST_CASE ("Touch: harder picking blooms, softer playing cleans up", "[overdrive][touch]")
{
    const double f = 220.0;
    const fx::Overdrive::Parameters p { 0.25f, 0.5f, 0.0f, 1.0f, false, Mode::soft };

    const auto loud  = run (p, test::sine (f, fs, 24000, 0.25));  // dug-in pick, -12 dBFS
    const auto quiet = run (p, test::sine (f, fs, 24000, 0.03));  // guitar volume rolled back, ~-30 dBFS

    // Even harmonics come from the touch bias, so they rise with playing strength
    CAPTURE (harmonicDb (loud, f, 2), harmonicDb (quiet, f, 2));
    REQUIRE (harmonicDb (loud, f, 2) > harmonicDb (quiet, f, 2) + 10.0);

    // Rolled back, the same settings are nearly clean
    CAPTURE (thd (loud, f), thd (quiet, f));
    REQUIRE (thd (quiet, f) < 0.05);
    REQUIRE (thd (loud, f) > 4.0 * thd (quiet, f));
}

TEST_CASE ("Tone knob: lowpass sweep, dark cuts the treble, lows stay put", "[overdrive][tone]")
{
    // At low drive the pedal is nearly linear, so test tones show the filter
    auto gainAt = [] (double f, float tone)
    {
        const auto y = run ({ 0.0f, tone, 0.0f, 1.0f, false, Mode::soft }, test::sine (f, fs, 24000, 0.01));
        return test::toDb (test::amplitudeAt (y, f, fs, y.size() - 4800, 4800) / 0.01);
    };

    const double lowDark = gainAt (100.0, 0.0), lowBright = gainAt (100.0, 1.0);
    const double hiDark  = gainAt (4000.0, 0.0), hiNoon = gainAt (4000.0, 0.5), hiBright = gainAt (4000.0, 1.0);
    const double topNoon = gainAt (8000.0, 0.5);
    CAPTURE (lowDark, lowBright, hiDark, hiNoon, hiBright, topNoon);

    REQUIRE (std::abs (lowDark - lowBright) < 1.0);   // lows barely move
    REQUIRE (hiBright - hiDark > 12.0);               // the knob has real range at 4 kHz
    REQUIRE (hiNoon < hiBright - 3.0);
    REQUIRE (topNoon < hiNoon - 3.0);                 // keeps rolling off: this is what tames fizz
}
