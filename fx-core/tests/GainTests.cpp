#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "fx/effects/Gain.h"

#include <vector>

using Catch::Matchers::WithinAbs;

namespace
{
struct Buffer
{
    Buffer (int numChannels, int numSamples, float value)
        : data (static_cast<size_t> (numChannels), std::vector<float> (static_cast<size_t> (numSamples), value))
    {
        for (auto& ch : data)
            ptrs.push_back (ch.data());
    }

    std::vector<std::vector<float>> data;
    std::vector<float*> ptrs;
};
} // namespace

TEST_CASE ("Gain at 0 dB is a bit-exact pass-through", "[gain]")
{
    fx::Gain gain;
    gain.prepare (48000.0, 512, 2);

    Buffer buf (2, 512, 0.5f);
    gain.process (buf.ptrs.data(), 2, 512);

    for (auto& ch : buf.data)
        for (float s : ch)
            REQUIRE (s == 0.5f);
}

TEST_CASE ("Gain change ramps instead of jumping", "[gain]")
{
    fx::Gain gain;
    gain.prepare (48000.0, 512, 1);
    gain.setGainDecibels (-6.0f);

    Buffer buf (1, 64, 1.0f);
    gain.process (buf.ptrs.data(), 1, 64);

    // First sample has barely moved, so there is no click
    REQUIRE_THAT (buf.data[0][0], WithinAbs (1.0, 0.01));
    // Still ramping after 64 samples (ramp is 20 ms = 960 samples)
    REQUIRE (buf.data[0][63] < buf.data[0][0]);
    REQUIRE (buf.data[0][63] > 0.5f);
}

TEST_CASE ("Gain settles exactly on the target", "[gain]")
{
    fx::Gain gain;
    gain.prepare (48000.0, 2048, 1);
    gain.setGainDecibels (-6.0f);

    Buffer buf (1, 2048, 1.0f);
    gain.process (buf.ptrs.data(), 1, 2048);

    REQUIRE_THAT (buf.data[0][2047], WithinAbs (0.501187, 1e-5));
    REQUIRE_THAT (gain.getCurrentGain(), WithinAbs (0.501187, 1e-5));
}

TEST_CASE ("Gain below -100 dB is silence", "[gain]")
{
    fx::Gain gain;
    gain.setGainDecibels (-120.0f);
    gain.prepare (48000.0, 16, 1); // prepare resets, so no ramp

    Buffer buf (1, 16, 1.0f);
    gain.process (buf.ptrs.data(), 1, 16);

    REQUIRE (buf.data[0][15] == 0.0f);
}
