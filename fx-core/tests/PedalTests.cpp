#include <catch2/catch_test_macros.hpp>

#include "TestUtils.h"
#include "fx/Pedal.h"
#include "fx/effects/Overdrive.h"

#include <set>
#include <string>

namespace
{
constexpr double fs = 48000.0;
constexpr int block = 256;

std::vector<float> runPedal (fx::Pedal& p, std::vector<float> x)
{
    p.prepare (fs, block, 1);
    for (size_t pos = 0; pos < x.size(); pos += block)
    {
        float* ch[] { x.data() + pos };
        p.process (ch, 1, static_cast<int> (std::min<size_t> (block, x.size() - pos)));
    }
    return x;
}
} // namespace

TEST_CASE ("Registry lists every pedal once and builds each one", "[pedal]")
{
    const auto all = fx::registry::all();
    REQUIRE (all.size() >= 2);

    std::set<std::string> ids;
    for (const auto* info : all)
    {
        CAPTURE (info->id);
        REQUIRE (ids.insert (std::string (info->id)).second);

        auto p = fx::registry::create (info->id);
        REQUIRE (p != nullptr);
        REQUIRE (&p->info() == info);
        REQUIRE (fx::registry::find (info->id) == info);

        std::set<std::string> paramIds;
        for (const auto& param : info->params)
        {
            CAPTURE (param.id);
            REQUIRE (paramIds.insert (std::string (param.id)).second);
            REQUIRE (param.minValue <= param.defaultValue);
            REQUIRE (param.defaultValue <= param.maxValue);
            if (param.kind == fx::ParamInfo::Kind::choice)
                REQUIRE (param.choices.size() == static_cast<size_t> (param.maxValue - param.minValue) + 1);
        }
    }

    REQUIRE (fx::registry::create ("no-such-pedal") == nullptr);
    REQUIRE (fx::registry::find ("no-such-pedal") == nullptr);
}

TEST_CASE ("Overdrive pedal sounds the same as fx::Overdrive with plugin units", "[pedal]")
{
    auto pedal = fx::registry::create ("overdrive");
    const auto& info = pedal->info();
    pedal->setParameter (info.indexOf ("drive"), 7.0f);
    pedal->setParameter (info.indexOf ("tone"), 3.0f);
    pedal->setParameter (info.indexOf ("level"), -3.0f);
    pedal->setParameter (info.indexOf ("mix"), 80.0f);
    pedal->setParameter (info.indexOf ("mode"), 2.0f);

    fx::Overdrive direct;
    direct.setParameters ({ 0.7f, 0.3f, -3.0f, 0.8f, false, fx::Overdrive::Mode::asymmetric });

    const auto in = test::sine (220.0, fs, 9600, 0.3);
    const auto a = runPedal (*pedal, in);

    auto b = in;
    direct.prepare (fs, block, 1);
    for (size_t pos = 0; pos < b.size(); pos += block)
    {
        float* ch[] { b.data() + pos };
        direct.process (ch, 1, block);
    }

    REQUIRE (a == b);
}

TEST_CASE ("Out-of-range parameter values are clamped, bad indices ignored", "[pedal]")
{
    auto boost = fx::registry::create ("boost");
    boost->setParameter (0, 1000.0f);   // clamps to +24 dB
    boost->setParameter (-1, 5.0f);
    boost->setParameter (99, 5.0f);

    const auto y = runPedal (*boost, std::vector<float> (4800, 0.01f));
    REQUIRE (std::abs (test::toDb (y.back() / 0.01f) - 24.0) < 0.01);
}

TEST_CASE ("Bypassed pedals pass the signal through, delayed by their latency", "[pedal]")
{
    for (const auto* info : fx::registry::all())
    {
        CAPTURE (info->id);
        auto p = fx::registry::create (info->id);
        p->setBypassed (true);

        const auto in = test::sine (440.0, fs, 9600, 0.5);
        const auto y = runPedal (*p, in);
        const auto lat = static_cast<size_t> (p->getLatencySamples());

        // Level unchanged...
        const double gainDb = test::toDb (test::amplitudeAt (y, 440.0, fs, 4800, 4800)
                                          / test::amplitudeAt (in, 440.0, fs, 4800, 4800));
        REQUIRE (std::abs (gainDb) < 0.05);

        // ...and timing matches the reported latency to within half a sample
        // (the 8x oversampler's true latency is fractional)
        double worst = 0.0;
        for (size_t i = 4800; i < in.size(); ++i)
            worst = std::max (worst, static_cast<double> (std::abs (y[i] - in[i - lat])));
        REQUIRE (worst < 0.5 * 0.5 * 2.0 * 3.1416 * 440.0 / fs * 1.05);
    }
}
