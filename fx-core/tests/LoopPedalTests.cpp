#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "fx/Filters.h"
#include "fx/GraphicEq.h"
#include "fx/Pedal.h"
#include "TestUtils.h"

#include <algorithm>
#include <random>

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double fs = 48000.0;
constexpr int block = 256;

struct Rig
{
    explicit Rig (std::string_view id, int channels = 2) : pedal (fx::registry::create (id)), numChannels (channels)
    {
        REQUIRE (pedal != nullptr);
        pedal->prepare (fs, block, numChannels);
    }

    void set (std::string_view param, float v)
    {
        const int i = pedal->info().indexOf (param);
        REQUIRE (i >= 0);
        pedal->setParameter (i, v);
    }

    /** Processes a mono signal copied to every channel; returns each channel. */
    std::vector<std::vector<float>> run (const std::vector<float>& x, int blockSize = block)
    {
        std::vector<std::vector<float>> out (static_cast<size_t> (numChannels), x);
        for (size_t pos = 0; pos < x.size(); pos += static_cast<size_t> (blockSize))
        {
            const int n = static_cast<int> (std::min (static_cast<size_t> (blockSize), x.size() - pos));
            std::vector<float*> ptrs;
            for (auto& ch : out)
                ptrs.push_back (ch.data() + pos);
            pedal->process (ptrs.data(), numChannels, n);
        }
        return out;
    }

    std::unique_ptr<fx::Pedal> pedal;
    int numChannels;
};

std::vector<float> impulse (int length, int at = 0, float height = 1.0f)
{
    std::vector<float> x (static_cast<size_t> (length), 0.0f);
    x[static_cast<size_t> (at)] = height;
    return x;
}

std::vector<float> noise (int length, float amplitude = 0.3f)
{
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> d (-amplitude, amplitude);
    std::vector<float> x (static_cast<size_t> (length));
    for (auto& s : x)
        s = d (rng);
    return x;
}

/** The biggest second difference: a click shows up as a spike in it.
    Compare a transition against the steady signal on both sides of it. */
double jerk (const std::vector<float>& y, size_t from, size_t to)
{
    double worst = 0.0;
    for (size_t i = std::max<size_t> (from, 2); i < std::min (to, y.size()); ++i)
        worst = std::max (worst, std::abs (static_cast<double> (y[i]) - 2.0 * y[i - 1] + y[i - 2]));
    return worst;
}

size_t loudestAfter (const std::vector<float>& y, size_t from)
{
    size_t best = from;
    for (size_t i = from; i < y.size(); ++i)
        if (std::abs (y[i]) > std::abs (y[best]))
            best = i;
    return best;
}

double rmsDb (const std::vector<float>& y, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += static_cast<double> (y[i]) * y[i];
    return test::toDb (std::sqrt (sum / static_cast<double> (to - from)) + 1e-30);
}
} // namespace

TEST_CASE ("Loop pedals are registered and behave", "[looppedals]")
{
    const auto id = GENERATE (std::string_view ("eq"), std::string_view ("chorus"),
                              std::string_view ("delay"), std::string_view ("reverb"),
                              std::string_view ("tremolo"), std::string_view ("vibrato"),
                              std::string_view ("flanger"), std::string_view ("phaser"),
                              std::string_view ("spring"), std::string_view ("compressor"), std::string_view ("wah"),
                              std::string_view ("fuzz"), std::string_view ("bigfuzz"), std::string_view ("cleandrive"));
    CAPTURE (id);
    const int channels = GENERATE (1, 2);
    CAPTURE (channels);

    Rig rig (id, channels);
    const bool oversampled = id == "fuzz" || id == "bigfuzz" || id == "cleandrive";
    CHECK (rig.pedal->getLatencySamples() == (oversampled ? 42 : 0));

    // Loud noise at every setting's extreme stays finite and bounded
    for (int p = 0; p < static_cast<int> (rig.pedal->info().params.size()); ++p)
        rig.pedal->setParameter (p, rig.pedal->info().params[static_cast<size_t> (p)].maxValue);
    const auto y = rig.run (noise (5 * 48000, 0.9f), 97);
    for (const auto& ch : y)
        for (float s : ch)
        {
            REQUIRE (std::isfinite (s));
            REQUIRE (std::abs (s) < 1000.0f);   // EQ maxed out is +54 dB: loud, but not a runaway
        }
}

TEST_CASE ("Knob tapers", "[looppedals]")
{
    const auto* info = fx::registry::find ("delay");
    REQUIRE (info != nullptr);
    const auto& time = info->params[static_cast<size_t> (info->indexOf ("time"))];
    CHECK_THAT (time.fromNormalised (0.0f), WithinAbs (20.0, 1e-3));
    CHECK_THAT (time.fromNormalised (0.5f), WithinAbs (200.0, 0.01));   // log: geometric middle
    CHECK_THAT (time.fromNormalised (1.0f), WithinAbs (2000.0, 0.01));
    CHECK_THAT (time.toNormalised (200.0f), WithinAbs (0.5, 1e-5));

    const auto& sync = info->params[static_cast<size_t> (info->indexOf ("sync"))];
    CHECK (sync.fromNormalised (0.26f) == 2.0f);   // choices round to an index
}

TEST_CASE ("Graphic EQ: flat at its defaults, bands where they should be", "[looppedals][eq]")
{
    auto gainAt = [] (Rig& rig, double freq)
    {
        const auto y = rig.run (test::sine (freq, fs, 24000 + 4800));
        return test::toDb (test::amplitudeAt (y[0], freq, fs, 24000, 4800));
    };

    Rig rig ("eq", 1);
    CHECK (rig.pedal->info().style == fx::PedalInfo::Style::graphicEq);
    for (double f : { 100.0, 1000.0, 5000.0 })
        CHECK (std::abs (gainAt (rig, f)) < 0.05);

    // One band up: that band's centre rises by about the slider amount,
    // two octaves away barely moves
    rig.set ("b800", 12.0f);
    CHECK_THAT (gainAt (rig, 800.0), WithinAbs (12.0, 0.3));
    CHECK (std::abs (gainAt (rig, 200.0)) < 1.0);

    // What it does matches the curve the GUI draws
    rig.set ("b100", -9.0f);
    rig.set ("b3k2", 6.0f);
    const std::array<float, 7> gains { -9.0f, 0.0f, 0.0f, 12.0f, 0.0f, 6.0f, 0.0f };
    for (double f : { 80.0, 300.0, 1200.0, 3200.0, 9000.0 })
    {
        CAPTURE (f);
        CHECK_THAT (gainAt (rig, f), WithinAbs (fx::graphicEq::responseDb (gains, f, fs), 0.1));
    }

    rig.set ("level", -6.0f);
    CHECK_THAT (gainAt (rig, 1200.0), WithinAbs (fx::graphicEq::responseDb (gains, 1200.0, fs) - 6.0, 0.1));

    // Bypassed it's exactly the input again (once the crossfade is done)
    rig.pedal->setBypassed (true);
    const auto x = noise (4800);
    rig.run (x);
    const auto y = rig.run (x);
    for (size_t i = 0; i < x.size(); ++i)
        REQUIRE (y[0][i] == x[i]);
}

TEST_CASE ("Graphic EQ sliders jump without clicks", "[looppedals][eq]")
{
    Rig rig ("eq", 1);
    rig.set ("b400", 15.0f);
    const auto x = test::sine (300.0, fs, 48000, 0.5);
    auto first = rig.run (std::vector<float> (x.begin(), x.begin() + 24000));
    rig.set ("b400", -15.0f);   // top to bottom
    rig.set ("b200", 15.0f);
    auto second = rig.run (std::vector<float> (x.begin() + 24000, x.end()));

    std::vector<float> y = first[0];
    y.insert (y.end(), second[0].begin(), second[0].end());
    CHECK (jerk (y, 24000, 30000) < 1.5 * std::max (jerk (y, 12000, 24000), jerk (y, 36000, 48000)));
}

TEST_CASE ("Chorus: wet, stereo, and smooth when its knobs jump", "[looppedals][chorus]")
{
    Rig rig ("chorus", 2);
    const auto x = test::sine (440.0, fs, 48000, 0.5);
    auto first = rig.run (std::vector<float> (x.begin(), x.begin() + 24000));

    // Width 100%: the sides differ
    double diff = 0.0;
    for (size_t i = 12000; i < 24000; ++i)
        diff = std::max (diff, std::abs (static_cast<double> (first[0][i]) - first[1][i]));
    CHECK (diff > 0.05);

    rig.set ("depth", 100.0f);
    rig.set ("rate", 5.0f);
    rig.set ("mix", 100.0f);
    rig.set ("voices", 2.0f);
    auto second = rig.run (std::vector<float> (x.begin() + 24000, x.end()));

    for (int c = 0; c < 2; ++c)
    {
        auto y = first[static_cast<size_t> (c)];
        y.insert (y.end(), second[static_cast<size_t> (c)].begin(), second[static_cast<size_t> (c)].end());
        CHECK (jerk (y, 24000, 30000) < 1.5 * std::max (jerk (y, 12000, 24000), jerk (y, 36000, 48000)));
    }

    // Width 0: mono
    Rig narrow ("chorus", 2);
    narrow.set ("width", 0.0f);
    const auto m = narrow.run (x);
    for (size_t i = 0; i < x.size(); ++i)
        REQUIRE (m[0][i] == m[1][i]);
}

TEST_CASE ("Delay: echo timing, feedback and tempo sync", "[looppedals][delay]")
{
    SECTION ("Free time")
    {
        Rig rig ("delay", 1);
        rig.set ("time", 250.0f);
        rig.set ("feedback", 50.0f);
        rig.set ("tone", 12000.0f);
        rig.set ("mix", 100.0f);
        const auto y = rig.run (impulse (48000, 100))[0];

        const size_t first = loudestAfter (y, 200);
        CHECK (first == 100 + 12000);
        const size_t second = loudestAfter (y, first + 1000);
        CHECK (second >= 100 + 24000);   // the feedback filters may smear it by a sample
        CHECK (second <= 100 + 24002);
        // Each repeat is about half the last (feedback 50%, a little lost to the filters)
        const double ratio = std::abs (y[second] / y[first]);
        CHECK (ratio > 0.4);
        CHECK (ratio < 0.52);
    }

    SECTION ("Synced to the host tempo")
    {
        const auto [division, beats] = GENERATE (std::pair { 2, 1.0 }, std::pair { 3, 1.5 }, std::pair { 7, 1.0 / 3.0 });
        const double bpm = GENERATE (90.0, 120.0, 137.0);
        CAPTURE (division, bpm);

        Rig rig ("delay", 1);
        rig.set ("sync", static_cast<float> (division));
        rig.set ("feedback", 0.0f);
        rig.set ("mix", 100.0f);
        rig.set ("tone", 12000.0f);
        rig.pedal->setTempo (bpm);

        const auto y = rig.run (impulse (96000, 0))[0];
        const double expected = beats * 60.0 / bpm * fs;
        CHECK (std::abs (static_cast<double> (loudestAfter (y, 10)) - expected) <= 1.0);
    }

    SECTION ("Ping-pong alternates sides")
    {
        Rig rig ("delay", 2);
        rig.set ("mode", 2.0f);
        rig.set ("time", 100.0f);
        rig.set ("feedback", 60.0f);
        rig.set ("mix", 100.0f);
        const auto y = rig.run (impulse (48000, 0));
        // First repeat on the left only, second on the right only
        auto peak = [] (const std::vector<float>& ch, size_t at)
        {
            float p = 0.0f;
            for (size_t i = at - 50; i < at + 50; ++i)
                p = std::max (p, std::abs (ch[i]));
            return p;
        };
        CHECK (peak (y[0], 4800) > 0.1f);
        CHECK (peak (y[1], 4800) < 1e-4f);
        CHECK (peak (y[1], 9600) > 0.02f);
        CHECK (peak (y[0], 9600) < 1e-4f);
    }
}

TEST_CASE ("Digital delay time changes don't click", "[looppedals][delay]")
{
    Rig rig ("delay", 1);
    rig.set ("time", 300.0f);
    rig.set ("feedback", 40.0f);
    const auto x = test::sine (220.0, fs, 96000, 0.5);
    auto first = rig.run (std::vector<float> (x.begin(), x.begin() + 48000));
    rig.set ("time", 120.0f);
    auto second = rig.run (std::vector<float> (x.begin() + 48000, x.end()));

    auto y = first[0];
    y.insert (y.end(), second[0].begin(), second[0].end());
    CHECK (jerk (y, 48000, 60000) < 1.5 * std::max (jerk (y, 30000, 48000), jerk (y, 72000, 96000)));
}

TEST_CASE ("Delay and reverb tails ring out when bypassed", "[looppedals]")
{
    const auto id = GENERATE (std::string_view ("delay"), std::string_view ("reverb"), std::string_view ("spring"));
    CAPTURE (id);

    Rig rig (id, 2);
    rig.set ("mix", 100.0f);
    rig.run (noise (4800));                 // something to ring
    rig.pedal->setBypassed (true);
    const auto tail = rig.run (std::vector<float> (24000, 0.0f));
    CHECK (rmsDb (tail[0], 0, tail[0].size()) > -60.0);   // still ringing

    // New input passes dry and adds nothing once the old tail has gone
    rig.run (std::vector<float> (10 * 48000, 0.0f));
    const auto x = noise (4800);
    const auto y = rig.run (x);
    for (size_t i = 0; i < x.size(); ++i)
        REQUIRE (std::abs (y[0][i] - x[i]) < 1e-4f);
}

TEST_CASE ("Reverb decays at the set time, stays stable, and is stereo", "[looppedals][reverb]")
{
    const auto mode = GENERATE (0.0f, 1.0f, 2.0f);
    const float decay = GENERATE (0.8f, 3.0f);
    CAPTURE (mode, decay);

    Rig rig ("reverb", 2);
    rig.set ("mode", mode);
    rig.set ("decay", decay);
    rig.set ("mix", 100.0f);
    rig.set ("preDelay", 0.0f);
    rig.set ("tone", 16000.0f);

    auto y = rig.run (impulse (static_cast<int> (fs * decay * 1.5), 0, 0.0f));   // warm up empty
    y = rig.run (impulse (static_cast<int> (fs * decay * 1.5)));

    // Energy over 50 ms windows: -60 dB from the early level at about decay
    // seconds (within 35%: the damping filters shorten the highs' decay)
    const size_t window = 2400;
    const double early = rmsDb (y[0], 2400, 2400 + window);
    size_t at = 2400;
    while (at + window < y[0].size() && rmsDb (y[0], at, at + window) > early - 60.0)
        at += window / 4;
    const double seconds = static_cast<double> (at) / fs;
    CHECK (seconds > 0.65 * decay);
    CHECK (seconds < 1.35 * decay);

    // The two sides are different signals
    double dot = 0.0, l2 = 0.0, r2 = 0.0;
    for (size_t i = 1; i < 12000; ++i)   // after the dry impulse
    {
        dot += static_cast<double> (y[0][i]) * y[1][i];
        l2 += static_cast<double> (y[0][i]) * y[0][i];
        r2 += static_cast<double> (y[1][i]) * y[1][i];
    }
    CHECK (std::abs (dot) / std::sqrt (l2 * r2) < 0.5);

    // Longest decay, a minute of noise: no runaway
    Rig longest ("reverb", 2);
    longest.set ("decay", 10.0f);
    longest.set ("mix", 100.0f);
    longest.set ("mode", mode);
    const auto z = longest.run (noise (60 * 48000, 0.5f));
    CHECK (rmsDb (z[0], z[0].size() - 48000, z[0].size()) < 6.0);
}

TEST_CASE ("Reverb level doesn't jump with mode or size", "[looppedals][reverb]")
{
    // Wet level for steady noise, against the hall at size 50%
    auto wetDb = [] (float mode, float size)
    {
        Rig rig ("reverb", 2);
        rig.set ("mode", mode);
        rig.set ("size", size);
        rig.set ("mix", 100.0f);
        const auto x = noise (12 * 48000, 0.2f);
        const auto y = rig.run (x);
        std::vector<float> wet (y[0].size());
        for (size_t i = 0; i < wet.size(); ++i)
            wet[i] = y[0][i] - x[i];
        return rmsDb (wet, 6 * 48000, wet.size());
    };

    const double reference = wetDb (1.0f, 50.0f);
    for (float mode : { 0.0f, 1.0f, 2.0f })
        for (float size : { 0.0f, 50.0f, 100.0f })
        {
            CAPTURE (mode, size);
            CHECK (std::abs (wetDb (mode, size) - reference) < 1.0);
        }
}

namespace
{
/** Sample positions where y crosses its mean going up. */
std::vector<double> upCrossings (const std::vector<float>& y, size_t from)
{
    double mean = 0.0;
    for (size_t i = from; i < y.size(); ++i)
        mean += y[i];
    mean /= static_cast<double> (y.size() - from);

    std::vector<double> at;
    for (size_t i = from + 1; i < y.size(); ++i)
        if (y[i - 1] < mean && y[i] >= mean)
            at.push_back (static_cast<double> (i - 1) + (mean - y[i - 1]) / (y[i] - y[i - 1]));
    return at;
}

double gainAtDb (Rig& rig, double freq)
{
    const auto y = rig.run (test::sine (freq, fs, 24000 + 4800));
    return test::toDb (test::amplitudeAt (y[0], freq, fs, 24000, 4800));
}

/** Jerk of a transition (a knob jump at the halfway point) against the
    steady signal either side. */
template <typename Change>
double transitionRatio (Rig& rig, const std::vector<float>& x, Change&& change)
{
    const size_t half = x.size() / 2;
    auto first = rig.run (std::vector<float> (x.begin(), x.begin() + static_cast<std::ptrdiff_t> (half)));
    change();
    auto second = rig.run (std::vector<float> (x.begin() + static_cast<std::ptrdiff_t> (half), x.end()));

    double worst = 0.0;
    for (size_t c = 0; c < first.size(); ++c)
    {
        auto y = first[c];
        y.insert (y.end(), second[c].begin(), second[c].end());
        const double steady = std::max (jerk (y, half / 2, half), jerk (y, half + half / 2, y.size()));
        worst = std::max (worst, jerk (y, half, half + 4800) / steady);
    }
    return worst;
}
} // namespace

TEST_CASE ("Tremolo depth, tempo sync and auto-pan", "[looppedals][tremolo]")
{
    Rig rig ("tremolo", 2);
    rig.set ("depth", 100.0f);
    rig.set ("spread", 100.0f);
    rig.set ("sync", 3.0f);   // 1/4
    const double bpm = GENERATE (100.0, 120.0, 133.0);
    CAPTURE (bpm);
    rig.pedal->setTempo (bpm);

    // A steady input shows the gain itself
    const auto y = rig.run (std::vector<float> (4 * 48000, 1.0f));
    const auto [lo, hi] = std::minmax_element (y[0].begin(), y[0].end());
    CHECK (*lo < 0.01f);
    CHECK (*hi > 0.99f);

    // One cycle per beat
    const auto ups = upCrossings (y[0], 4800);
    REQUIRE (ups.size() > 3);
    const double period = (ups.back() - ups.front()) / static_cast<double> (ups.size() - 1);
    CHECK (std::abs (period - 60.0 / bpm * fs) < 1.0);

    // Spread 100% with a sine: the sides pan against each other
    for (size_t i = 0; i < y[0].size(); i += 97)
        REQUIRE (std::abs (y[0][i] + y[1][i] - 1.0f) < 1e-3f);
}

TEST_CASE ("Vibrato moves the pitch, not the level", "[looppedals][vibrato]")
{
    Rig rig ("vibrato", 1);
    rig.set ("depth", 100.0f);
    rig.set ("rate", 4.0f);
    const auto y = rig.run (test::sine (1000.0, fs, 2 * 48000, 0.5))[0];

    // Level within a fraction of a dB everywhere (envelope over each cycle)
    float lo = 1.0f, hi = 0.0f;
    for (size_t start = 4800; start + 48 < y.size(); start += 48)
    {
        float peak = 0.0f;
        for (size_t i = start; i < start + 48; ++i)
            peak = std::max (peak, std::abs (y[i]));
        lo = std::min (lo, peak);
        hi = std::max (hi, peak);
    }
    CHECK (test::toDb (hi / lo) < 0.5);

    // Pitch swings: periods vary by a few percent either way
    const auto ups = upCrossings (y, 4800);
    double shortest = 1e9, longest = 0.0;
    for (size_t i = 1; i < ups.size(); ++i)
    {
        shortest = std::min (shortest, ups[i] - ups[i - 1]);
        longest = std::max (longest, ups[i] - ups[i - 1]);
    }
    CHECK (longest / shortest > 1.04);
    CHECK (longest / shortest < 1.2);
}

TEST_CASE ("Flanger and phaser notch where they should", "[looppedals][flanger][phaser]")
{
    SECTION ("Flanger: 1 ms comb, notch at 500 Hz, peak at 1 kHz")
    {
        Rig rig ("flanger", 1);
        rig.set ("depth", 0.0f);
        rig.set ("delay", 1.0f);
        rig.set ("feedback", 0.0f);
        rig.set ("mix", 50.0f);
        CHECK (gainAtDb (rig, 500.0) < -25.0);
        CHECK (std::abs (gainAtDb (rig, 1000.0)) < 0.5);
    }

    SECTION ("Phaser: 4 stages at 894 Hz notch at 370 Hz and 2.16 kHz")
    {
        Rig rig ("phaser", 1);
        rig.set ("depth", 0.0f);
        rig.set ("feedback", 0.0f);
        rig.set ("mix", 50.0f);
        CHECK (gainAtDb (rig, 370.0) < -25.0);
        CHECK (gainAtDb (rig, 2160.0) < -25.0);
        CHECK (gainAtDb (rig, 894.0) > -0.5);   // all stages at 90°: back in phase
    }

    SECTION ("Flanger with full negative feedback stays bounded")
    {
        Rig rig ("flanger", 2);
        rig.set ("feedback", -95.0f);
        rig.set ("depth", 100.0f);
        const auto y = rig.run (noise (10 * 48000, 0.5f));
        CHECK (rmsDb (y[0], y[0].size() - 48000, y[0].size()) < 12.0);
    }
}

TEST_CASE ("Modulation pedals don't click when knobs jump", "[looppedals]")
{
    const auto x = test::sine (330.0, fs, 48000, 0.5);

    SECTION ("Tremolo rate and depth")
    {
        Rig rig ("tremolo", 2);
        rig.set ("depth", 20.0f);
        CHECK (transitionRatio (rig, x, [&] { rig.set ("depth", 100.0f); rig.set ("rate", 12.0f); }) < 1.5);
    }
    SECTION ("Vibrato depth")
    {
        Rig rig ("vibrato", 1);
        rig.set ("depth", 0.0f);
        CHECK (transitionRatio (rig, x, [&] { rig.set ("depth", 100.0f); rig.set ("rate", 9.0f); }) < 1.5);
    }
    SECTION ("Flanger delay and feedback")
    {
        // Its sweep is slow, so the steady parts need to span a whole sweep
        Rig rig ("flanger", 2);
        CHECK (transitionRatio (rig, test::sine (330.0, fs, 8 * 48000, 0.5), [&] { rig.set ("delay", 4.0f); rig.set ("feedback", -80.0f); rig.set ("depth", 100.0f); }) < 1.5);
    }
    SECTION ("Phaser stage count")
    {
        Rig rig ("phaser", 2);
        CHECK (transitionRatio (rig, x, [&] { rig.set ("stages", 2.0f); rig.set ("feedback", 90.0f); }) < 1.5);
    }
}

TEST_CASE ("Spring: chirps, decays, and is mostly mono", "[looppedals][spring]")
{
    Rig rig ("spring", 2);
    rig.set ("mix", 100.0f);
    rig.set ("decay", 2.0f);
    const auto y = rig.run (impulse (3 * 48000));

    // The chirp (as in the published spring model): within the first trip
    // round the springs, higher frequencies arrive later, up to the cut-off.
    // Compare when each band's energy is centred
    auto centre = [&] (fx::BiquadCoeffs k)
    {
        fx::Biquad f;
        f.setCoefficients (k);
        double sum = 0.0, weighted = 0.0;
        for (size_t i = 1; i < static_cast<size_t> (0.06 * fs); ++i)   // skip the dry impulse
        {
            const double v = f.processSample (y[0][i]);
            sum += v * v;
            weighted += v * v * static_cast<double> (i);
        }
        return weighted / sum;
    };
    const double low = centre (fx::BiquadCoeffs::bandPass (fs, 400.0, 2.0));
    const double high = centre (fx::BiquadCoeffs::bandPass (fs, 3000.0, 2.0));
    CHECK (high > low + 0.002 * fs);   // at least 2 ms later

    // Decays: quieter after 2 s by roughly the 60 dB the decay promises
    const double early = rmsDb (y[0], 4800, 9600), late = rmsDb (y[0], 2 * 48000, 2 * 48000 + 4800);
    CHECK (early - late > 40.0);
    CHECK (early - late < 80.0);

    // A tank is mono: the sides are mostly the same, with a little width
    double dot = 0.0, l2 = 0.0, r2 = 0.0;
    for (size_t i = 1; i < 48000; ++i)
    {
        dot += static_cast<double> (y[0][i]) * y[1][i];
        l2 += static_cast<double> (y[0][i]) * y[0][i];
        r2 += static_cast<double> (y[1][i]) * y[1][i];
    }
    const double correlation = dot / std::sqrt (l2 * r2);
    CHECK (correlation > 0.5);
    CHECK (correlation < 0.95);
}

TEST_CASE ("Synced modulation locks to the host's beat while playing", "[looppedals][tremolo]")
{
    // Tremolo synced to 1/4, full depth, sine: a quarter of the way through a
    // beat the LFO is at its peak (gain 1), three quarters through at its dip
    auto gainAt = [] (double quarterNotes, bool playing)
    {
        Rig rig ("tremolo", 1);
        rig.set ("depth", 100.0f);
        rig.set ("sync", 3.0f);
        rig.pedal->setTempo (120.0);
        rig.pedal->setPlayPosition (quarterNotes, playing);
        return rig.run (std::vector<float> (1, 1.0f))[0][0];
    };

    CHECK_THAT (gainAt (8.25, true), WithinAbs (1.0, 1e-4));
    CHECK_THAT (gainAt (8.75, true), WithinAbs (0.0, 1e-4));
    CHECK_THAT (gainAt (3.0, true), WithinAbs (0.5, 1e-4));       // on the beat: half way
    CHECK_THAT (gainAt (8.75, false), WithinAbs (0.5, 1e-4));     // stopped: runs free from its start

    // Playing on in blocks, the lock never jumps: the LFO's own count and
    // the host's position agree block after block
    Rig rig ("tremolo", 1);
    rig.set ("depth", 100.0f);
    rig.set ("sync", 5.0f);   // 1/8
    rig.pedal->setTempo (97.0);
    std::vector<float> y;
    double ppq = 1.3;
    for (int b = 0; b < 400; ++b)
    {
        rig.pedal->setPlayPosition (ppq, true);
        const auto out = rig.run (std::vector<float> (block, 1.0f))[0];
        y.insert (y.end(), out.begin(), out.end());
        ppq += block * 97.0 / (60.0 * fs);
    }
    CHECK (jerk (y, 2, y.size()) < 1e-4);
}

TEST_CASE ("Drives: fuzz gating, the big fuzz's scoop, the clean drive's clean side", "[looppedals][drives]")
{
    auto levelDb = [] (Rig& rig, double freq, double amplitude)
    {
        rig.pedal->reset();
        const auto y = rig.run (test::sine (freq, fs, 24000 + 4800, amplitude));
        return test::toDb (test::amplitudeAt (y[0], freq, fs, 24000, 4800));
    };

    SECTION ("Fuzz: starving the bias gates quiet notes far more than loud ones")
    {
        auto gap = [&] (float bias)
        {
            Rig rig ("fuzz", 1);
            rig.set ("bias", bias);
            return levelDb (rig, 220.0, 0.3) - levelDb (rig, 220.0, 0.003);   // loud minus quiet
        };
        CHECK (gap (2.0f) > gap (10.0f) + 10.0);
    }

    SECTION ("Big Fuzz: tone in the middle scoops the mids")
    {
        Rig rig ("bigfuzz", 1);
        rig.set ("sustain", 0.0f);
        // Tiny input keeps it in the clean region, so this is the tone stack
        const double low = levelDb (rig, 250.0, 0.0005), mid = levelDb (rig, 1000.0, 0.0005), high = levelDb (rig, 3500.0, 0.0005);
        CHECK (mid < low - 3.0);
        CHECK (mid < high - 3.0);
    }

    SECTION ("Clean Drive: at low gain it's mostly the clean signal; up high, much dirtier")
    {
        auto harmonics = [&] (float gainKnob)
        {
            Rig rig ("cleandrive", 1);
            rig.set ("gain", gainKnob);
            rig.set ("treble", 4.0f);   // flat
            rig.pedal->reset();
            const auto y = rig.run (test::sine (200.0, fs, 24000 + 4800, 0.3));
            const double f = test::amplitudeAt (y[0], 200.0, fs, 24000, 4800);
            const double h = test::amplitudeAt (y[0], 400.0, fs, 24000, 4800) + test::amplitudeAt (y[0], 600.0, fs, 24000, 4800);
            return test::toDb (h / f);
        };
        CHECK (harmonics (0.0f) < -20.0);
        CHECK (harmonics (10.0f) > harmonics (0.0f) + 10.0);
    }

    SECTION ("Bypassed, a drive is the dry signal delayed by its latency")
    {
        for (auto id : { "fuzz", "bigfuzz", "cleandrive" })
        {
            CAPTURE (id);
            Rig rig (id, 1);
            rig.pedal->setBypassed (true);
            const auto x = test::sine (440.0, fs, 24000, 0.5);
            const auto y = rig.run (x)[0];
            const int latency = rig.pedal->getLatencySamples();
            double worst = 0.0;
            for (size_t i = 12000; i < y.size(); ++i)
                worst = std::max (worst, std::abs (static_cast<double> (y[i]) - x[i - static_cast<size_t> (latency)]));
            CHECK (test::toDb (worst) < -40.0);
        }
    }
}
