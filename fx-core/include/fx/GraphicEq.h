#pragma once

#include "fx/Filters.h"

#include <array>
#include <cmath>
#include <span>

namespace fx::graphicEq
{

/** The graphic EQ's bands: octaves from 100 Hz, one peaking filter each.
    Shared by the pedal and anything drawing its curve, so they agree. */
inline constexpr std::array<double, 7> bandsHz { 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 6400.0 };
inline constexpr int numBands = static_cast<int> (bandsHz.size());
inline constexpr double q = 1.2;
inline constexpr float rangeDb = 15.0f;

inline BiquadCoeffs band (double sampleRate, int index, double gainDb) noexcept
{
    const double hz = std::min (bandsHz[static_cast<size_t> (index)], sampleRate * 0.45);
    return BiquadCoeffs::peak (sampleRate, hz, q, gainDb);
}

/** The EQ's gain in dB at a frequency, for band gains in dB. */
inline double responseDb (std::span<const float> gainsDb, double freq, double sampleRate = 48000.0) noexcept
{
    double magnitude = 1.0;
    for (int b = 0; b < numBands && b < static_cast<int> (gainsDb.size()); ++b)
        magnitude *= band (sampleRate, b, gainsDb[static_cast<size_t> (b)]).magnitudeAt (sampleRate, freq);
    return 20.0 * std::log10 (magnitude);
}

} // namespace fx::graphicEq
