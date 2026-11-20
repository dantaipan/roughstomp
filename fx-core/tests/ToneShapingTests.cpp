#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "fx/ToneShaping.h"
#include "TestUtils.h"

#include <algorithm>

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double fs = 48000.0;

/** Steady-state gain in dB of a tone stack at one frequency. */
double toneDb (float bass, float middle, float treble, double freq)
{
    fx::ToneStack t;
    t.prepare (fs);
    t.setGains (bass, middle, treble);
    t.reset();
    return test::measureGainDb ([&] (float x) { t.process (&x, 1); return x; }, freq, fs);
}

double cutDb (float lowHz, float highHz, double freq)
{
    fx::CutFilters c;
    c.prepare (fs);
    c.setCutoffs (lowHz, highHz);
    c.reset();
    return test::measureGainDb ([&] (float x) { c.process (&x, 1); return x; }, freq, fs);
}

std::vector<float> noise (int n)
{
    std::vector<float> x (static_cast<size_t> (n));
    unsigned s = 12345;
    for (auto& v : x)
    {
        s = s * 1664525u + 1013904223u;
        v = static_cast<float> (s >> 8) / static_cast<float> (1u << 24) * 2.0f - 1.0f;
    }
    return x;
}

float maxStep (const std::vector<float>& x)
{
    float m = 0.0f;
    for (size_t i = 1; i < x.size(); ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}
} // namespace

TEST_CASE ("Tone stack flat is the input exactly", "[tone]")
{
    fx::ToneStack t;
    t.prepare (fs);
    t.setGains (0.0f, 0.0f, 0.0f);
    const auto in = noise (4096);
    auto x = in;
    t.process (x.data(), static_cast<int> (x.size()));
    REQUIRE (x == in);
    REQUIRE (t.isIdle());

    // As a host's 0 dB can come back from its normalised form
    t.setGains (1.8e-7f, -1.8e-7f, 1.0e-4f);
    x = in;
    t.process (x.data(), static_cast<int> (x.size()));
    REQUIRE (x == in);
}

TEST_CASE ("Tone stack bands land where they should", "[tone]")
{
    // Each band at full boost: its own region rises, the others stay put
    REQUIRE_THAT (toneDb (12.0f, 0.0f, 0.0f, 40.0),   WithinAbs (11.5, 0.6));
    REQUIRE_THAT (toneDb (12.0f, 0.0f, 0.0f, 3000.0), WithinAbs (0.0, 0.3));
    REQUIRE_THAT (toneDb (0.0f, 12.0f, 0.0f, 650.0),  WithinAbs (12.0, 0.1));
    REQUIRE_THAT (toneDb (0.0f, 12.0f, 0.0f, 40.0),   WithinAbs (0.0, 0.6));
    REQUIRE_THAT (toneDb (0.0f, 0.0f, 12.0f, 10000.0), WithinAbs (12.0, 0.6));
    REQUIRE_THAT (toneDb (0.0f, 0.0f, 12.0f, 100.0),  WithinAbs (0.0, 0.1));

    // Cuts mirror boosts
    REQUIRE_THAT (toneDb (-12.0f, 0.0f, 0.0f, 40.0), WithinAbs (-11.5, 0.6));
    REQUIRE_THAT (toneDb (0.0f, -6.0f, 0.0f, 650.0), WithinAbs (-6.0, 0.1));

    // As run matches its own analytic response (what a GUI would draw)
    for (double f : { 80.0, 400.0, 1200.0, 5000.0 })
        REQUIRE_THAT (toneDb (4.0f, -3.0f, 6.0f, f), WithinAbs (fx::ToneStack::responseDb (4.0f, -3.0f, 6.0f, f, fs), 0.05));
}

TEST_CASE ("Tone stack glides without clicks and comes back to exact", "[tone]")
{
    fx::ToneStack t;
    t.prepare (fs);
    auto x = test::sine (440.0, fs, 48000, 0.5);
    const auto dryStep = maxStep (x);

    // Turned hard mid-signal, every 1024 samples
    const int block = 512;
    for (int pos = 0, i = 0; pos < static_cast<int> (x.size()); pos += block, ++i)
    {
        const float g = (i / 2) % 2 == 0 ? 12.0f : -12.0f;
        t.setGains (g, -g, g);
        t.process (x.data() + pos, block);
    }
    // A 440 Hz sine at up to +12 dB moves at most ~4x as far per sample; a click would jump far more
    REQUIRE (maxStep (x) < dryStep * 4.5f);

    // Back to flat: after the filters have died away it's the input again, bit for bit
    t.setGains (0.0f, 0.0f, 0.0f);
    std::vector<float> settle (static_cast<size_t> (fs * 0.5), 0.1f);   // the glide snaps to 0 dB after ~250 ms, then 100 ms more
    t.process (settle.data(), static_cast<int> (settle.size()));
    REQUIRE (t.isIdle());
    const auto in = noise (1024);
    auto y = in;
    t.process (y.data(), static_cast<int> (y.size()));
    REQUIRE (y == in);
}

TEST_CASE ("Cut filters off at the ends of their ranges", "[tone]")
{
    fx::CutFilters c;
    c.prepare (fs);
    c.setCutoffs (fx::CutFilters::lowCutMinHz, fx::CutFilters::highCutMaxHz);
    c.reset();
    const auto in = noise (4096);
    auto x = in;
    c.process (x.data(), static_cast<int> (x.size()));
    REQUIRE (x == in);

    c.setCutoffs (fx::CutFilters::lowCutMinHz + 0.0001f, fx::CutFilters::highCutMaxHz - 0.01f);
    x = in;
    c.process (x.data(), static_cast<int> (x.size()));
    REQUIRE (x == in);
}

TEST_CASE ("Cut filters' slopes", "[tone]")
{
    REQUIRE_THAT (cutDb (100.0f, 20000.0f, 100.0), WithinAbs (-3.01, 0.1));
    REQUIRE (cutDb (100.0f, 20000.0f, 25.0) < -22.0);          // two octaves down, 12 dB/oct
    REQUIRE_THAT (cutDb (100.0f, 20000.0f, 2000.0), WithinAbs (0.0, 0.05));
    REQUIRE_THAT (cutDb (20.0f, 5000.0f, 5000.0), WithinAbs (-3.01, 0.1));
    REQUIRE (cutDb (20.0f, 5000.0f, 20000.0) < -22.0);
    REQUIRE_THAT (cutDb (20.0f, 5000.0f, 300.0), WithinAbs (0.0, 0.05));
    REQUIRE_THAT (cutDb (150.0f, 6000.0f, 1000.0), WithinAbs (fx::CutFilters::responseDb (150.0f, 6000.0f, 1000.0, fs), 0.05));
}

TEST_CASE ("Cut filters switch on and off without clicks", "[tone]")
{
    fx::CutFilters c;
    c.prepare (fs);
    auto x = test::sine (220.0, fs, 48000, 0.5);
    const auto dryStep = maxStep (x);
    const int block = 256;
    for (int pos = 0, i = 0; pos < static_cast<int> (x.size()); pos += block, ++i)
    {
        const bool on = (i / 8) % 2 == 1;
        c.setCutoffs (on ? 400.0f : 20.0f, on ? 1500.0f : 20000.0f);
        c.process (x.data() + pos, block);
    }
    REQUIRE (maxStep (x) < dryStep * 1.5f);
}
