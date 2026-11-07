#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "fx/Smoother.h"

using Catch::Matchers::WithinAbs;

TEST_CASE ("LinearSmoother ramps linearly and lands exactly", "[smoother]")
{
    fx::LinearSmoother s { 0.0f };
    s.prepare (1000.0, 0.1); // 100-sample ramp
    s.setTarget (1.0f);

    for (int i = 0; i < 49; ++i)
        s.next();
    REQUIRE_THAT (s.next(), WithinAbs (0.5, 1e-5));   // halfway at sample 50

    for (int i = 0; i < 50; ++i)
        s.next();
    REQUIRE (s.getCurrent() == 1.0f);                 // exact, not just close
    REQUIRE_FALSE (s.isSmoothing());
}

TEST_CASE ("LinearSmoother retargets mid-ramp from where it is", "[smoother]")
{
    fx::LinearSmoother s { 0.0f };
    s.prepare (1000.0, 0.1);
    s.setTarget (1.0f);
    s.skip (50);
    REQUIRE_THAT (s.getCurrent(), WithinAbs (0.5, 1e-5));

    s.setTarget (0.0f);
    REQUIRE_THAT (s.next(), WithinAbs (0.495, 1e-5)); // no jump
    s.skip (1000);
    REQUIRE (s.getCurrent() == 0.0f);
}

TEST_CASE ("OnePoleSmoother converges and snaps to the target", "[smoother]")
{
    fx::OnePoleSmoother s { 0.0f };
    s.prepare (1000.0, 0.01); // tau = 10 samples
    s.setTarget (1.0f);

    for (int i = 0; i < 10; ++i)
        s.next();
    REQUIRE_THAT (s.getCurrent(), WithinAbs (1.0 - std::exp (-1.0), 1e-3)); // 63% after one tau

    for (int i = 0; i < 60; ++i)
        s.next();
    REQUIRE_THAT (s.getCurrent(), WithinAbs (1.0, 1e-3)); // within 0.1% by 7 tau

    for (int i = 0; i < 1000; ++i)
        s.next();
    REQUIRE (s.getCurrent() == 1.0f);
    REQUIRE_FALSE (s.isSmoothing());
}
