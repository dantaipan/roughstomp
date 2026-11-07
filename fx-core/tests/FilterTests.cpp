#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "fx/Filters.h"
#include "TestUtils.h"

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double fs = 48000.0;

double biquadDb (const fx::BiquadCoeffs& c, double freq)
{
    fx::Biquad f;
    f.setCoefficients (c);
    return test::measureGainDb ([&] (float x) { return f.processSample (x); }, freq, fs);
}

/** The filter as run must match its own analytic response. */
void requireMatchesAnalytic (const fx::BiquadCoeffs& c)
{
    for (double freq : { 50.0, 200.0, 1000.0, 3000.0, 8000.0, 15000.0 })
    {
        INFO ("freq = " << freq);
        REQUIRE_THAT (biquadDb (c, freq), WithinAbs (test::toDb (c.magnitudeAt (fs, freq)), 0.02));
    }
}
} // namespace

TEST_CASE ("Biquad lowpass response", "[filters]")
{
    const auto c = fx::BiquadCoeffs::lowPass (fs, 1000.0);
    REQUIRE_THAT (biquadDb (c, 100.0),  WithinAbs (0.0, 0.05));
    REQUIRE_THAT (biquadDb (c, 1000.0), WithinAbs (-3.01, 0.05));   // Butterworth Q
    REQUIRE (biquadDb (c, 10000.0) < -38.0);                        // ~12 dB/oct
    requireMatchesAnalytic (c);
}

TEST_CASE ("Biquad highpass response", "[filters]")
{
    const auto c = fx::BiquadCoeffs::highPass (fs, 1000.0);
    REQUIRE_THAT (biquadDb (c, 10000.0), WithinAbs (0.0, 0.1));
    REQUIRE_THAT (biquadDb (c, 1000.0),  WithinAbs (-3.01, 0.05));
    REQUIRE (biquadDb (c, 100.0) < -38.0);
    requireMatchesAnalytic (c);
}

TEST_CASE ("Biquad bandpass, peak and shelves", "[filters]")
{
    const auto bp = fx::BiquadCoeffs::bandPass (fs, 1000.0, 2.0);
    REQUIRE_THAT (biquadDb (bp, 1000.0), WithinAbs (0.0, 0.05));
    requireMatchesAnalytic (bp);

    const auto pk = fx::BiquadCoeffs::peak (fs, 1000.0, 1.0, 6.0);
    REQUIRE_THAT (biquadDb (pk, 1000.0), WithinAbs (6.0, 0.05));
    REQUIRE_THAT (biquadDb (pk, 20.0 * 10), WithinAbs (0.0, 1.0)); // well below: near flat
    requireMatchesAnalytic (pk);

    const auto ls = fx::BiquadCoeffs::lowShelf (fs, 300.0, 0.707, -6.0);
    REQUIRE_THAT (test::toDb (ls.magnitudeAt (fs, 10.0)), WithinAbs (-6.0, 0.1));
    REQUIRE_THAT (test::toDb (ls.magnitudeAt (fs, 10000.0)), WithinAbs (0.0, 0.1));
    requireMatchesAnalytic (ls);

    const auto hs = fx::BiquadCoeffs::highShelf (fs, 3000.0, 0.707, 6.0);
    REQUIRE_THAT (test::toDb (hs.magnitudeAt (fs, 20000.0)), WithinAbs (6.0, 0.2));
    REQUIRE_THAT (test::toDb (hs.magnitudeAt (fs, 50.0)), WithinAbs (0.0, 0.1));
    requireMatchesAnalytic (hs);
}

TEST_CASE ("OnePole lowpass and highpass are -3 dB at the cutoff", "[filters]")
{
    fx::OnePole lp, hp;
    lp.setup (fx::OnePole::Mode::lowPass,  fs, 1000.0);
    hp.setup (fx::OnePole::Mode::highPass, fs, 1000.0);

    auto lpDb = [&] (double f) { lp.reset(); return test::measureGainDb ([&] (float x) { return lp.processSample (x); }, f, fs); };
    auto hpDb = [&] (double f) { hp.reset(); return test::measureGainDb ([&] (float x) { return hp.processSample (x); }, f, fs); };

    REQUIRE_THAT (lpDb (1000.0), WithinAbs (-3.01, 0.05));
    REQUIRE_THAT (hpDb (1000.0), WithinAbs (-3.01, 0.05));
    REQUIRE_THAT (lpDb (50.0),   WithinAbs (0.0, 0.05));
    REQUIRE_THAT (hpDb (15000.0), WithinAbs (0.0, 0.3));
    REQUIRE (lpDb (10000.0) < -19.0);  // ~6 dB/oct
    REQUIRE (hpDb (100.0)   < -19.0);
}

TEST_CASE ("DcBlocker removes an offset and passes audio", "[filters]")
{
    fx::DcBlocker dc;
    dc.setup (fs);

    // 1 kHz tone riding on a 0.5 DC offset
    auto x = test::sine (1000.0, fs, 48000, 0.5);
    for (auto& s : x)
        s = dc.processSample (s + 0.5f);

    double mean = 0.0;
    for (size_t i = 43200; i < 48000; ++i)
        mean += x[i];
    mean /= 4800.0;

    REQUIRE (std::abs (mean) < 1.0e-3);
    REQUIRE_THAT (test::amplitudeAt (x, 1000.0, fs, 43200, 4800), WithinAbs (0.5, 0.005));
}
