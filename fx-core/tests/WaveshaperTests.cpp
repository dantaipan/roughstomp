#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "fx/Waveshapers.h"
#include "TestUtils.h"

#include <functional>

using Catch::Matchers::WithinAbs;
namespace sh = fx::shapers;

namespace
{
double harmonicLevel (const std::function<float (float)>& shaper, int harmonic, float drive)
{
    constexpr double fs = 48000.0, f = 100.0;
    auto x = test::sine (f, fs, 4800);
    for (auto& s : x)
        s = shaper (drive * s);
    return test::amplitudeAt (x, f * harmonic, fs, 0, 4800);
}
} // namespace

TEST_CASE ("Symmetric shapers are odd, bounded and unity at small levels", "[shapers]")
{
    const std::function<float (float)> shapers[] = { sh::tanhClip, [] (float x) { return sh::hardClip (x); } };

    for (const auto& f : shapers)
    {
        REQUIRE_THAT (f (0.001f), WithinAbs (0.001, 1e-6));
        for (float x : { 0.1f, 0.5f, 1.0f, 3.0f, 100.0f })
        {
            REQUIRE (f (-x) == -f (x));
            REQUIRE (std::abs (f (x)) <= 1.0f);
        }
    }
}

TEST_CASE ("Cubic soft clip shape", "[shapers]")
{
    REQUIRE_THAT (sh::softClipCubic (0.001f), WithinAbs (0.0015, 1e-6)); // gain 1.5
    REQUIRE (sh::softClipCubic (1.0f) == 1.0f);
    REQUIRE (sh::softClipCubic (5.0f) == 1.0f);
    REQUIRE (sh::softClipCubic (-5.0f) == -1.0f);
}

TEST_CASE ("Asymmetric shaper clips harder on the negative side", "[shapers]")
{
    REQUIRE_THAT (sh::asymmetric (0.001f),  WithinAbs (0.001, 1e-5));
    REQUIRE_THAT (sh::asymmetric (-0.001f), WithinAbs (-0.001, 1e-5));
    REQUIRE_THAT (sh::asymmetric (50.0f),  WithinAbs (1.0, 1e-6));
    REQUIRE_THAT (sh::asymmetric (-50.0f), WithinAbs (-0.6, 1e-6));
}

TEST_CASE ("Only asymmetric clipping makes even harmonics", "[shapers]")
{
    const float drive = 4.0f;

    REQUIRE (harmonicLevel (sh::tanhClip, 3, drive) > 0.05);   // odd: plenty
    REQUIRE (harmonicLevel (sh::tanhClip, 2, drive) < 1e-4);   // even: none

    auto asym = [] (float x) { return sh::asymmetric (x); };
    REQUIRE (harmonicLevel (asym, 2, drive) > 0.05);
}
