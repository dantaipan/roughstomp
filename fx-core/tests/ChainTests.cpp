#include <catch2/catch_test_macros.hpp>

#include "TestUtils.h"
#include "fx/Chain.h"

#include <atomic>
#include <thread>

namespace
{
constexpr double fs = 48000.0;
constexpr int block = 256;

/** Counts how it is used, to check what the chain does with it. */
class ProbePedal final : public fx::Pedal
{
public:
    explicit ProbePedal (float g = 1.0f, int lat = 0) : gain (g), latency (lat) {}

    const fx::PedalInfo& info() const noexcept override
    {
        static const fx::PedalInfo i { "probe", "Probe", {} };
        return i;
    }

    void setParameter (int, float) noexcept override {}
    void setBypassed (bool) noexcept override {}
    void prepare (double, int, int) override { ++prepares; }
    void reset() override {}
    void process (float* const* ch, int n, int len) override
    {
        ++processes;
        for (int c = 0; c < n; ++c)
            for (int i = 0; i < len; ++i)
                ch[c][i] *= gain;
    }
    int getLatencySamples() const override { return latency; }

    std::atomic<int> prepares { 0 }, processes { 0 };
    float gain;
    int latency;
};

float runBlock (fx::Chain& chain, float value)
{
    std::vector<float> x (block, value);
    float* ch[] { x.data() };
    chain.beginBlock();
    chain.process (ch, 1, block);
    return x[0];
}
} // namespace

TEST_CASE ("Chain runs pedals in order and sums their latency", "[chain]")
{
    fx::Chain chain;
    REQUIRE (runBlock (chain, 0.5f) == 0.5f);   // empty chain passes through

    auto a = std::make_shared<ProbePedal> (2.0f, 10);
    auto b = std::make_shared<ProbePedal> (3.0f, 32);
    chain.setLayout ({ a, nullptr, b });
    chain.prepare (fs, block, 1);

    REQUIRE (chain.getLatencySamples() == 42);
    REQUIRE (chain.getLayout().size() == 3);    // empty slots keep their place
    REQUIRE (runBlock (chain, 0.5f) == 3.0f);
    REQUIRE (a->prepares == 1);
    REQUIRE (b->prepares == 1);

    // Real pedals: overdrive latency is the oversampler's, boost has none
    fx::Chain real;
    std::shared_ptr<fx::Pedal> od = fx::registry::create ("overdrive"), boost = fx::registry::create ("boost");
    real.setLayout ({ od, boost, fx::registry::create ("overdrive") });
    real.prepare (fs, block, 2);
    REQUIRE (real.getLatencySamples() == 2 * od->getLatencySamples());
    REQUIRE (od->getLatencySamples() > 0);
}

TEST_CASE ("Reordering keeps pedals running without re-preparing them", "[chain]")
{
    fx::Chain chain;
    auto a = std::make_shared<ProbePedal> (2.0f);
    auto b = std::make_shared<ProbePedal> (3.0f);
    chain.setLayout ({ a, b });
    chain.prepare (fs, block, 1);
    runBlock (chain, 1.0f);

    chain.setLayout ({ b, a });
    auto c = std::make_shared<ProbePedal> (5.0f);
    chain.setLayout ({ b, a, c });
    REQUIRE (a->prepares == 1);
    REQUIRE (b->prepares == 1);
    REQUIRE (c->prepares == 1);   // new pedals are prepared before the audio thread sees them

    REQUIRE (runBlock (chain, 1.0f) == 30.0f);   // only the newest layout is applied

    // Removing a pedal and putting it back before the audio thread caught up
    // must not re-prepare it: the audio thread may still be running it
    chain.setLayout ({ b });
    chain.setLayout ({ a, b });
    REQUIRE (a->prepares == 1);
}

TEST_CASE ("Old layouts are freed on the message thread, not the audio thread", "[chain]")
{
    fx::Chain chain;
    chain.prepare (fs, block, 1);

    std::weak_ptr<ProbePedal> gone;
    {
        auto a = std::make_shared<ProbePedal> (2.0f);
        gone = a;
        chain.setLayout ({ a });
    }
    runBlock (chain, 1.0f);

    chain.setLayout ({});
    runBlock (chain, 1.0f);           // audio thread retires the old layout...
    REQUIRE (! gone.expired());       // ...but doesn't delete it
    chain.collectGarbage();
    REQUIRE (gone.expired());

}

TEST_CASE ("Layout changes while audio runs on another thread", "[chain][threads]")
{
    fx::Chain chain;
    std::vector<std::shared_ptr<fx::Pedal>> pool;
    for (int i = 0; i < 4; ++i)
        pool.push_back (fx::registry::create (i % 2 == 0 ? "overdrive" : "boost"));

    chain.setLayout ({ pool[0] });
    chain.prepare (fs, block, 2);

    std::atomic<bool> stop { false };
    std::atomic<int> blocks { 0 };
    bool finite = true;

    std::thread audio ([&]
    {
        test::Buffer buf (2, block);
        const auto sine = test::sine (110.0, fs, block, 0.3);
        while (! stop)
        {
            for (auto& ch : buf.data)
                ch = sine;
            for (auto* pedal : chain.beginBlock())
                if (pedal != nullptr)
                    pedal->setParameter (0, 5.0f);
            chain.process (buf.ptrs.data(), 2, block);
            for (auto& ch : buf.data)
                for (float s : ch)
                    finite = finite && std::isfinite (s);
            ++blocks;
        }
    });

    for (int i = 0; i < 400; ++i)
    {
        fx::Chain::Slots slots;
        for (int k = 0; k < 4; ++k)
            slots.push_back ((i >> k) & 1 ? pool[static_cast<size_t> ((k + i) % 4)] : nullptr);
        if (i % 7 == 0)
            slots.push_back (fx::registry::create ("boost"));   // fresh pedals come and go
        chain.setLayout (std::move (slots));
        chain.collectGarbage();
        std::this_thread::yield();
    }

    while (blocks < 50)
        std::this_thread::yield();
    stop = true;
    audio.join();

    REQUIRE (finite);
}
