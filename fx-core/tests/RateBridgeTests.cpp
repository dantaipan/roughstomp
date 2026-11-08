#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "fx/RateBridge.h"
#include "TestUtils.h"

#include <algorithm>
#include <random>

namespace
{
/** Runs x through the bridge in random block sizes, with `inner` applied at
    the inner rate. Every inner sample is also appended to `seen`. */
template <typename Inner>
std::vector<float> run (fx::RateBridge& bridge, std::vector<float> x, int maxBlock, Inner&& inner,
                        std::vector<float>* seen = nullptr, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::uniform_int_distribution<int> size (1, maxBlock);

    for (size_t pos = 0; pos < x.size();)
    {
        const int n = static_cast<int> (std::min (static_cast<size_t> (size (rng)), x.size() - pos));
        bridge.process (x.data() + pos, n, [&] (float* s, int m)
        {
            REQUIRE (m <= bridge.getMaxInnerBlock());
            inner (s, m);
            if (seen != nullptr)
                seen->insert (seen->end(), s, s + m);
        });
        pos += static_cast<size_t> (n);
    }
    return x;
}

auto identity = [] (float*, int) {};

/** Largest difference between y and x delayed by `latency`, ignoring the
    first `skip` samples (the filters starting up). */
double worstError (const std::vector<float>& x, const std::vector<float>& y, int latency, size_t skip)
{
    double worst = 0.0;
    for (size_t i = skip; i < y.size(); ++i)
        worst = std::max (worst, std::abs (static_cast<double> (y[i]) - x[i - static_cast<size_t> (latency)]));
    return worst;
}
} // namespace

TEST_CASE ("RateBridge passes through when the rates match", "[ratebridge]")
{
    fx::RateBridge bridge;
    bridge.prepare (48000.0, 48000.0, 256);
    REQUIRE_FALSE (bridge.isActive());
    REQUIRE (bridge.getLatencySamples() == 0);

    bridge.prepare (44100.0, 0.0, 256);   // the process runs at any rate
    REQUIRE_FALSE (bridge.isActive());

    const auto x = test::sine (1000.0, 48000.0, 2000);
    bridge.prepare (48000.0, 48000.0, 256);
    REQUIRE (run (bridge, x, 256, identity) == x);
}

TEST_CASE ("RateBridge round trip is a fixed whole-sample delay", "[ratebridge]")
{
    const auto [outer, inner] = GENERATE (std::pair { 44100.0, 48000.0 }, std::pair { 96000.0, 48000.0 },
                                          std::pair { 88200.0, 48000.0 }, std::pair { 48000.0, 44100.0 },
                                          std::pair { 192000.0, 48000.0 });
    CAPTURE (outer, inner);

    const int maxBlock = GENERATE (1, 64, 512);
    CAPTURE (maxBlock);

    fx::RateBridge bridge;
    bridge.prepare (outer, inner, maxBlock);
    REQUIRE (bridge.isActive());
    REQUIRE (bridge.getLatencySamples() == fx::RateBridge::latencyFor (outer, inner));
    REQUIRE (bridge.getLatencySamples() < static_cast<int> (outer * 0.002));   // under 2 ms

    // Tones well inside the passband come back unchanged, just later
    const int length = static_cast<int> (outer * 0.25);
    auto x = test::sine (1000.0, outer, length, 0.5);
    const auto y2 = test::sine (7300.0, outer, length, 0.3);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] += y2[i];

    const auto y = run (bridge, x, maxBlock, identity);
    CHECK (bridge.getUnderruns() == 0);
    CHECK (test::toDb (worstError (x, y, bridge.getLatencySamples(), 2000)) < -60.0);

    // reset() starts over with the same timing
    bridge.reset();
    const auto again = run (bridge, x, maxBlock, identity, nullptr, 7);
    CHECK (bridge.getUnderruns() == 0);
    CHECK (test::toDb (worstError (x, again, bridge.getLatencySamples(), 2000)) < -60.0);
}

TEST_CASE ("RateBridge runs the process at the inner rate", "[ratebridge]")
{
    constexpr double outer = 44100.0, inner = 48000.0;
    fx::RateBridge bridge;
    bridge.prepare (outer, inner, 512);

    const auto x = test::sine (1000.0, outer, 44100);
    std::vector<float> seen;
    run (bridge, x, 512, identity, &seen);

    // One second in is about one second of inner samples
    CHECK (std::abs (static_cast<double> (seen.size()) - inner) < 100.0);

    // And a 1 kHz tone is still 1 kHz there
    CHECK (std::abs (test::toDb (test::amplitudeAt (seen, 1000.0, inner, 4800, 24000))) < 0.05);
}

TEST_CASE ("RateBridge passband is flat and the process sees it", "[ratebridge]")
{
    constexpr double outer = 44100.0, inner = 48000.0;
    const double freq = GENERATE (100.0, 5000.0, 15000.0, 18000.0);
    CAPTURE (freq);

    fx::RateBridge bridge;
    bridge.prepare (outer, inner, 512);
    const auto y = run (bridge, test::sine (freq, outer, 88200), 512, identity);
    CHECK (std::abs (test::toDb (test::amplitudeAt (y, freq, outer, 44100, 44100))) < 0.1);
}

TEST_CASE ("RateBridge keeps images and aliases out", "[ratebridge]")
{
    SECTION ("Upsampling 44.1 -> 48 kHz: a 20 kHz tone's image at 23.9 kHz")
    {
        fx::RateBridge bridge;
        bridge.prepare (44100.0, 48000.0, 512);
        std::vector<float> seen;
        run (bridge, test::sine (20000.0, 44100.0, 44100), 512, identity, &seen);
        CHECK (test::toDb (test::amplitudeAt (seen, 23900.0, 48000.0, 4800, 24000)) < -70.0);
    }

    SECTION ("Downsampling 96 -> 48 kHz: a 30 kHz tone would alias to 18 kHz")
    {
        fx::RateBridge bridge;
        bridge.prepare (96000.0, 48000.0, 512);
        std::vector<float> seen;
        run (bridge, test::sine (30000.0, 96000.0, 96000), 512, identity, &seen);
        CHECK (test::toDb (test::amplitudeAt (seen, 18000.0, 48000.0, 4800, 24000)) < -70.0);
    }

    SECTION ("Coming back 48 -> 44.1 kHz: what the process makes above 22.05 kHz")
    {
        // The process adds a 23 kHz tone; at 44.1 kHz it would alias to 21.1 kHz
        fx::RateBridge bridge;
        bridge.prepare (44100.0, 48000.0, 512);
        std::int64_t t = 0;
        const auto y = run (bridge, std::vector<float> (44100, 0.0f), 512, [&] (float* s, int m)
        {
            for (int i = 0; i < m; ++i, ++t)
                s[i] += static_cast<float> (std::sin (2.0 * std::numbers::pi * 23000.0 * static_cast<double> (t) / 48000.0));
        });
        CHECK (test::toDb (test::amplitudeAt (y, 21100.0, 44100.0, 4410, 22050)) < -70.0);
    }
}

TEST_CASE ("RateBridge latency figures", "[ratebridge]")
{
    CHECK (fx::RateBridge::latencyFor (48000.0, 48000.0) == 0);
    CHECK (fx::RateBridge::latencyFor (48000.0, 0.0) == 0);
    CHECK (fx::RateBridge::latencyFor (44100.0, 48000.0) == 66);
    CHECK (fx::RateBridge::latencyFor (96000.0, 48000.0) == 131);
}
