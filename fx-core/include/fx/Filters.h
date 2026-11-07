#pragma once

#include <cmath>
#include <complex>
#include <numbers>

namespace fx
{

/* All filters here process ONE channel. An effect keeps one instance per
   channel (sized in prepare()). Coefficients are computed in double and the
   state is kept in double, which keeps low cutoffs clean; audio in and out
   is float. Changing coefficients is real-time safe but not smoothed, so
   smooth the cutoff yourself if it is automated. */

//==============================================================================
/** First-order lowpass/highpass using the topology-preserving transform
    (Zavalishin). Stable and well-behaved even when the cutoff is modulated. */
class OnePole
{
public:
    enum class Mode { lowPass, highPass };

    void setup (Mode newMode, double sampleRate, double cutoffHz) noexcept
    {
        mode = newMode;
        const double g = std::tan (std::numbers::pi * cutoffHz / sampleRate);
        G = g / (1.0 + g);
    }

    void reset() noexcept { s = 0.0; }

    float processSample (float in) noexcept
    {
        const double x  = in;
        const double v  = (x - s) * G;
        const double lp = v + s;
        s = lp + v;
        return static_cast<float> (mode == Mode::lowPass ? lp : x - lp);
    }

    void processBlock (float* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            data[i] = processSample (data[i]);
    }

private:
    Mode   mode = Mode::lowPass;
    double G = 0.0, s = 0.0;
};

//==============================================================================
/** Normalised biquad coefficients (a0 == 1), from Robert Bristow-Johnson's
    Audio EQ Cookbook. */
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    static BiquadCoeffs lowPass  (double sampleRate, double freq, double q = 0.70710678) noexcept;
    static BiquadCoeffs highPass (double sampleRate, double freq, double q = 0.70710678) noexcept;
    /** Bandpass with 0 dB gain at the centre frequency. */
    static BiquadCoeffs bandPass (double sampleRate, double freq, double q) noexcept;
    static BiquadCoeffs peak      (double sampleRate, double freq, double q, double gainDb) noexcept;
    static BiquadCoeffs lowShelf  (double sampleRate, double freq, double q, double gainDb) noexcept;
    static BiquadCoeffs highShelf (double sampleRate, double freq, double q, double gainDb) noexcept;

    /** Linear magnitude response at a frequency (for tests and GUIs). */
    double magnitudeAt (double sampleRate, double freq) const noexcept
    {
        const auto z1 = std::polar (1.0, -2.0 * std::numbers::pi * freq / sampleRate); // z^-1
        const auto z2 = z1 * z1;
        return std::abs ((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
    }

private:
    static BiquadCoeffs normalise (double b0, double b1, double b2,
                                   double a0, double a1, double a2) noexcept
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    struct Prototype { double cosW, alpha; };

    static Prototype prototype (double sampleRate, double freq, double q) noexcept
    {
        const double w = 2.0 * std::numbers::pi * freq / sampleRate;
        return { std::cos (w), std::sin (w) / (2.0 * q) };
    }
};

inline BiquadCoeffs BiquadCoeffs::lowPass (double sampleRate, double freq, double q) noexcept
{
    const auto [c, alpha] = prototype (sampleRate, freq, q);
    return normalise ((1.0 - c) / 2.0, 1.0 - c, (1.0 - c) / 2.0,
                      1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

inline BiquadCoeffs BiquadCoeffs::highPass (double sampleRate, double freq, double q) noexcept
{
    const auto [c, alpha] = prototype (sampleRate, freq, q);
    return normalise ((1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0,
                      1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

inline BiquadCoeffs BiquadCoeffs::bandPass (double sampleRate, double freq, double q) noexcept
{
    const auto [c, alpha] = prototype (sampleRate, freq, q);
    return normalise (alpha, 0.0, -alpha,
                      1.0 + alpha, -2.0 * c, 1.0 - alpha);
}

inline BiquadCoeffs BiquadCoeffs::peak (double sampleRate, double freq, double q, double gainDb) noexcept
{
    const auto [c, alpha] = prototype (sampleRate, freq, q);
    const double A = std::pow (10.0, gainDb / 40.0);
    return normalise (1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A,
                      1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
}

inline BiquadCoeffs BiquadCoeffs::lowShelf (double sampleRate, double freq, double q, double gainDb) noexcept
{
    const auto [c, alpha] = prototype (sampleRate, freq, q);
    const double A  = std::pow (10.0, gainDb / 40.0);
    const double sq = 2.0 * std::sqrt (A) * alpha;
    return normalise (A * ((A + 1.0) - (A - 1.0) * c + sq),
                      2.0 * A * ((A - 1.0) - (A + 1.0) * c),
                      A * ((A + 1.0) - (A - 1.0) * c - sq),
                      (A + 1.0) + (A - 1.0) * c + sq,
                      -2.0 * ((A - 1.0) + (A + 1.0) * c),
                      (A + 1.0) + (A - 1.0) * c - sq);
}

inline BiquadCoeffs BiquadCoeffs::highShelf (double sampleRate, double freq, double q, double gainDb) noexcept
{
    const auto [c, alpha] = prototype (sampleRate, freq, q);
    const double A  = std::pow (10.0, gainDb / 40.0);
    const double sq = 2.0 * std::sqrt (A) * alpha;
    return normalise (A * ((A + 1.0) + (A - 1.0) * c + sq),
                      -2.0 * A * ((A - 1.0) + (A + 1.0) * c),
                      A * ((A + 1.0) + (A - 1.0) * c - sq),
                      (A + 1.0) - (A - 1.0) * c + sq,
                      2.0 * ((A - 1.0) - (A + 1.0) * c),
                      (A + 1.0) - (A - 1.0) * c - sq);
}

//==============================================================================
/** Biquad in transposed direct form II. */
class Biquad
{
public:
    void setCoefficients (const BiquadCoeffs& c) noexcept { k = c; }
    const BiquadCoeffs& getCoefficients() const noexcept  { return k; }

    void reset() noexcept { z1 = z2 = 0.0; }

    float processSample (float in) noexcept
    {
        const double x = in;
        const double y = k.b0 * x + z1;
        z1 = k.b1 * x - k.a1 * y + z2;
        z2 = k.b2 * x - k.a2 * y;
        return static_cast<float> (y);
    }

    void processBlock (float* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            data[i] = processSample (data[i]);
    }

private:
    BiquadCoeffs k;
    double z1 = 0.0, z2 = 0.0;
};

//==============================================================================
/** Removes DC offset: y[n] = x[n] - x[n-1] + R * y[n-1].
    Needed after asymmetric clipping, which adds DC. */
class DcBlocker
{
public:
    void setup (double sampleRate, double cutoffHz = 10.0) noexcept
    {
        R = std::exp (-2.0 * std::numbers::pi * cutoffHz / sampleRate);
    }

    void reset() noexcept { x1 = y1 = 0.0; }

    float processSample (float in) noexcept
    {
        const double x = in;
        const double y = x - x1 + R * y1;
        x1 = x;
        y1 = y;
        return static_cast<float> (y);
    }

    void processBlock (float* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            data[i] = processSample (data[i]);
    }

private:
    double R = 0.9987, x1 = 0.0, y1 = 0.0;
};

} // namespace fx
