#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "fx/NoiseGate.h"
#include "fx/Pedal.h"
#include "fx/PitchDetector.h"
#include "TestUtils.h"
#include "pedals/LoopPedals.h"

#include <algorithm>
#include <random>

using Catch::Matchers::WithinAbs;

namespace
{
constexpr double fs = 48000.0;

std::vector<float> noise (int length, float amplitude, unsigned seed = 5)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-amplitude, amplitude);
    std::vector<float> x (static_cast<size_t> (length));
    for (auto& s : x)
        s = d (rng);
    return x;
}

double peakDb (const std::vector<float>& y, size_t from, size_t to)
{
    float p = 0.0f;
    for (size_t i = from; i < to; ++i)
        p = std::max (p, std::abs (y[i]));
    return test::toDb (p + 1e-12);
}

/** Runs a mono signal through a pedal from the registry. */
std::vector<float> runPedal (fx::Pedal& p, std::vector<float> x, int block = 256)
{
    for (size_t pos = 0; pos < x.size(); pos += static_cast<size_t> (block))
    {
        float* ch[] { x.data() + pos };
        p.process (ch, 1, static_cast<int> (std::min (static_cast<size_t> (block), x.size() - pos)));
    }
    return x;
}

void set (fx::Pedal& p, std::string_view id, float v)
{
    const int i = p.info().indexOf (id);
    REQUIRE (i >= 0);
    p.setParameter (i, v);
}
} // namespace

//==============================================================================
TEST_CASE ("Noise gate silences hiss and lets notes through", "[gate]")
{
    fx::NoiseGate gate;
    gate.prepare (fs);
    gate.setThresholdDb (-50.0f);
    gate.setEnabled (true);
    gate.reset();

    // Hiss at -60 dB: shut
    auto hiss = noise (48000, 0.001f);
    gate.process (hiss.data(), static_cast<int> (hiss.size()));
    CHECK (peakDb (hiss, 24000, 48000) < -100.0);

    // A note arrives: it opens at once, so the attack isn't lost
    auto note = test::sine (196.0, fs, 48000, 0.3);
    gate.process (note.data(), static_cast<int> (note.size()));
    CHECK (peakDb (note, 0, 96) > -12.0);   // within 2 ms
    CHECK_THAT (peakDb (note, 24000, 48000), WithinAbs (test::toDb (0.3), 0.01));

    // The note decays to just under the threshold: hysteresis keeps it open
    auto tail = test::sine (196.0, fs, 24000, 0.002);   // -54 dB
    gate.process (tail.data(), static_cast<int> (tail.size()));
    CHECK_THAT (peakDb (tail, 12000, 24000), WithinAbs (test::toDb (0.002), 0.05));

    // Then the hiss again: it fades out over the close time, not a click
    auto after = noise (48000, 0.001f, 9);
    const auto dry = after;
    gate.process (after.data(), static_cast<int> (after.size()));
    double worstStep = 0.0;
    for (size_t i = 1; i < 9600; ++i)
        worstStep = std::max (worstStep, std::abs (static_cast<double> (after[i] - dry[i])));
    CHECK (worstStep < 0.002);
    CHECK (peakDb (after, 24000, 48000) < -100.0);

    // Off: everything passes
    gate.setEnabled (false);
    auto x = noise (24000, 0.001f, 11);
    const auto copy = x;
    gate.process (x.data(), static_cast<int> (x.size()));
    CHECK_THAT (x[23999], WithinAbs (copy[23999], 1e-9));
}

//==============================================================================
TEST_CASE ("Compressor static curve and timing", "[compressor]")
{
    auto comp = fx::registry::create ("compressor");
    REQUIRE (comp != nullptr);
    comp->prepare (fs, 256, 1);
    set (*comp, "sustain", 5.0f);   // threshold -27 dB, ratio 5:1, make-up 10.8 dB

    // Gain computer: above the knee, every 5 dB in is 1 dB out
    using C = fx::CompressorPedal;
    CHECK_THAT (C::gainDb (-40.0f, -27.0f, 5.0f), WithinAbs (0.0, 1e-6));
    CHECK_THAT (C::gainDb (-7.0f, -27.0f, 5.0f), WithinAbs (-16.0, 1e-4));

    // Steady tones: a 20 dB louder input comes out about 4 dB louder
    auto levelOut = [&] (double amplitude)
    {
        comp->reset();
        const auto y = runPedal (*comp, test::sine (500.0, fs, 48000, amplitude));
        return peakDb (y, 36000, 48000);
    };
    const double quiet = levelOut (0.05), loud = levelOut (0.5);   // -26 and -6 dBFS
    CHECK_THAT (loud - quiet, WithinAbs (20.0 / 5.0, 1.5));
    CHECK_THAT (loud, WithinAbs (-6.0 - 16.8 + 10.8, 1.0));   // gain computer + make-up

    // Attack: a sudden loud note is squeezed within a few attack times
    set (*comp, "attack", 5.0f);
    comp->reset();
    auto x = test::sine (500.0, fs, 24000, 0.02);
    const auto burst = test::sine (500.0, fs, 24000, 0.5);
    x.insert (x.end(), burst.begin(), burst.end());
    const auto y = runPedal (*comp, x);
    const double settled = peakDb (y, 40000, 48000);
    CHECK (peakDb (y, 24000 + 2400, 24000 + 3360) < settled + 1.0);   // 50-70 ms in: done
    CHECK (peakDb (y, 24000, 24000 + 96) > settled + 6.0);           // the first 2 ms: the pick comes through

    // Bypassed: exactly dry
    comp->setBypassed (true);
    runPedal (*comp, std::vector<float> (4800, 0.1f));
    const auto xs = noise (4800, 0.3f);
    const auto ys = runPedal (*comp, xs);
    for (size_t i = 0; i < xs.size(); ++i)
        REQUIRE (ys[i] == xs[i]);
}

//==============================================================================
TEST_CASE ("Wah sweeps its peak with the position", "[wah]")
{
    auto gainAt = [] (fx::Pedal& p, double freq)
    {
        p.reset();
        const auto y = runPedal (p, test::sine (freq, fs, 24000 + 4800));
        return test::toDb (test::amplitudeAt (y, freq, fs, 24000, 4800));
    };

    auto wah = fx::registry::create ("wah");
    REQUIRE (wah != nullptr);
    wah->prepare (fs, 256, 1);

    for (const float pos : { 0.0f, 50.0f, 100.0f })
    {
        CAPTURE (pos);
        set (*wah, "position", pos);
        runPedal (*wah, std::vector<float> (4800, 0.0f));   // let the sweep settle
        const double centre = fx::WahPedal::frequencyFor (pos / 100.0);
        const double atPeak = gainAt (*wah, centre);
        CHECK (atPeak > gainAt (*wah, centre * 2.5) + 10.0);   // a clear peak
        CHECK (atPeak > gainAt (*wah, centre / 2.5) + 6.0);
    }

    // Auto: louder playing opens it up (more highs)
    auto autoWah = fx::registry::create ("wah");
    autoWah->prepare (fs, 256, 1);
    set (*autoWah, "mode", 1.0f);
    set (*autoWah, "position", 0.0f);
    set (*autoWah, "sensitivity", 8.0f);
    auto quiet = [&] (float amp)
    {
        autoWah->reset();
        const auto y = runPedal (*autoWah, test::sine (1800.0, fs, 24000, amp));
        return test::toDb (test::amplitudeAt (y, 1800.0, fs, 12000, 4800) / amp);
    };
    CHECK (quiet (0.5f) > quiet (0.01f) + 6.0);
}

//==============================================================================
TEST_CASE ("Pitch detector finds guitar notes", "[tuner]")
{
    fx::PitchDetector detector;
    detector.prepare (fs, 4096);

    // Open strings, sine and a harmonic-rich sawtooth (where octave errors
    // happen), slightly out of tune
    const double hz = GENERATE (82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 1046.5);
    const double detune = GENERATE (-13.0, 0.0, 7.0);   // cents
    const bool saw = GENERATE (false, true);
    CAPTURE (hz, detune, saw);

    const double f = hz * std::exp2 (detune / 1200.0);
    // The sawtooth is band-limited (harmonics up to Nyquist) like a real
    // string: a naive one aliases into tones that aren't harmonics at all
    std::vector<float> x (4096);
    const int harmonics = saw ? static_cast<int> (fs / 2.0 / f) : 1;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double phase = 2.0 * std::numbers::pi * f * static_cast<double> (i) / fs;
        double v = 0.0;
        for (int k = 1; k <= harmonics; ++k)
            v += std::sin (k * phase) / k;
        x[i] = static_cast<float> (0.3 * v);
    }

    const auto r = detector.detect (x.data());
    CHECK (r.confidence > 0.8f);
    const double centsOff = 1200.0 * std::log2 (r.hz / f);
    CHECK (std::abs (centsOff) < 0.5);

    const auto note = fx::Note::fromHz (r.hz);
    CHECK_THAT (note.cents, WithinAbs (detune, 0.6));
}

TEST_CASE ("Pitch detector stays quiet on silence and noise; note names", "[tuner]")
{
    fx::PitchDetector detector;
    detector.prepare (fs, 4096);

    std::vector<float> silence (4096, 0.0f);
    CHECK (detector.detect (silence.data()).hz == 0.0f);

    const auto n = noise (4096, 0.3f);
    CHECK (detector.detect (n.data()).confidence < 0.6f);

    const auto e2 = fx::Note::fromHz (82.41);
    CHECK (e2.name == "E");
    CHECK (e2.octave == 2);
    const auto a4 = fx::Note::fromHz (440.0);
    CHECK (a4.name == "A");
    CHECK (a4.octave == 4);
    CHECK_THAT (fx::Note::fromHz (466.16).cents, WithinAbs (0.0, 0.1));   // A#4
    CHECK (fx::Note::fromHz (466.16).name == "A#");
}
