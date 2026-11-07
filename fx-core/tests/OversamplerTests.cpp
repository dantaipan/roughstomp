#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "fx/Oversampler.h"
#include "TestUtils.h"

#include <algorithm>
#include <numbers>

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double fs = 48000.0;

/** Runs a mono signal up and back down in blocks of blockSize. */
std::vector<float> roundTrip (fx::Oversampler& os, std::vector<float> x, int blockSize)
{
    for (size_t pos = 0; pos < x.size(); pos += static_cast<size_t> (blockSize))
    {
        const int n = static_cast<int> (std::min (static_cast<size_t> (blockSize), x.size() - pos));
        float* ch[] = { x.data() + pos };
        os.upsample (ch, 1, n);
        os.downsample (ch, 1, n);
    }
    return x;
}
} // namespace

TEST_CASE ("Oversampler latency", "[oversampler]")
{
    fx::Oversampler os;

    os.prepare (2, 512, 1);
    REQUIRE (os.getFactor() == 2);
    REQUIRE (os.getLatencySamplesExact() == 31.0);
    REQUIRE (os.getLatencySamples() == 31);

    os.prepare (2, 512, 2);
    REQUIRE (os.getFactor() == 4);
    REQUIRE (os.getLatencySamplesExact() == 38.5);

    os.prepare (2, 512, 3);
    REQUIRE (os.getFactor() == 8);
    REQUIRE (os.getLatencySamplesExact() == 42.25);
}

TEST_CASE ("Oversampler round trip is a pure delay", "[oversampler]")
{
    const int factorLog2 = GENERATE (1, 2, 3);
    const int blockSize  = GENERATE (512, 37, 1);
    CAPTURE (factorLog2, blockSize);

    fx::Oversampler os;
    os.prepare (1, 512, factorLog2);
    const double latency = os.getLatencySamplesExact();

    for (double freq : { 100.0, 1000.0, 10000.0 })
    {
        CAPTURE (freq);
        os.reset();
        const auto out = roundTrip (os, test::sine (freq, fs, 4000), blockSize);

        // Output equals the input delayed by the (possibly fractional) latency
        double maxErr = 0.0;
        for (size_t i = 200; i < out.size(); ++i)
        {
            const double expected = std::sin (2.0 * std::numbers::pi * freq * (static_cast<double> (i) - latency) / fs);
            maxErr = std::max (maxErr, std::abs (out[i] - expected));
        }
        REQUIRE (maxErr < 2.0e-3);
    }
}

TEST_CASE ("Oversampler rejects images when upsampling", "[oversampler]")
{
    fx::Oversampler os;
    os.prepare (1, 4800, 1);

    // A 15 kHz tone at 48 kHz, upsampled to 96 kHz, leaves an image at 33 kHz
    auto x = test::sine (15000.0, fs, 4800);
    const float* in[] = { x.data() };
    os.upsample (in, 1, 4800);                     // warm up the filter
    float* const* up = os.upsample (in, 1, 4800);

    std::vector<float> hi (up[0], up[0] + 9600);
    const double wanted = test::amplitudeAt (hi, 15000.0, 2 * fs, 0, 9600);
    const double image  = test::amplitudeAt (hi, 33000.0, 2 * fs, 0, 9600);

    REQUIRE_THAT (wanted, WithinAbs (1.0, 0.01));
    REQUIRE (test::toDb (image / wanted) < -70.0);
}

TEST_CASE ("Oversampler removes content above the base Nyquist when downsampling", "[oversampler]")
{
    fx::Oversampler os;
    os.prepare (1, 4800, 1);
    std::vector<float> out (4800);
    float* outCh[] = { out.data() };
    const std::vector<float> silence (4800, 0.0f);
    const float* in[] = { silence.data() };

    // A 30 kHz tone at 96 kHz would alias to 18 kHz at 48 kHz
    const auto tone = test::sine (30000.0, 2 * fs, 9600);
    for (int pass = 0; pass < 2; ++pass)
    {
        float* const* up = os.upsample (in, 1, 4800);
        std::copy (tone.begin(), tone.end(), up[0]);
        os.downsample (outCh, 1, 4800);
    }

    REQUIRE (test::toDb (test::amplitudeAt (out, 18000.0, fs, 0, 4800)) < -70.0);
}
