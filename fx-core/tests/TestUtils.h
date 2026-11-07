#pragma once

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace test
{

/** Multichannel buffer with the float* const* view effects expect. */
struct Buffer
{
    Buffer (int numChannels, int numSamples, float value = 0.0f)
        : data (static_cast<size_t> (numChannels), std::vector<float> (static_cast<size_t> (numSamples), value))
    {
        for (auto& ch : data)
            ptrs.push_back (ch.data());
    }

    std::vector<std::vector<float>> data;
    std::vector<float*> ptrs;
};

inline std::vector<float> sine (double freq, double sampleRate, int numSamples, double amplitude = 1.0)
{
    std::vector<float> out (static_cast<size_t> (numSamples));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<float> (amplitude * std::sin (2.0 * std::numbers::pi * freq * static_cast<double> (i) / sampleRate));
    return out;
}

/** Amplitude of one frequency over [start, start + length). Pick a length
    holding a whole number of cycles to avoid leakage. */
inline double amplitudeAt (const std::vector<float>& x, double freq, double sampleRate, size_t start, size_t length)
{
    std::complex<double> acc {};
    for (size_t i = 0; i < length; ++i)
        acc += static_cast<double> (x[start + i])
             * std::polar (1.0, -2.0 * std::numbers::pi * freq * static_cast<double> (i) / sampleRate);
    return 2.0 * std::abs (acc) / static_cast<double> (length);
}

inline double toDb (double gain) { return 20.0 * std::log10 (gain); }

/** Runs a 1-channel processor over a unit sine and returns its gain in dB,
    measured after the filter has settled. 4800 samples at 48 kHz holds a
    whole number of cycles for any multiple of 10 Hz. */
template <typename ProcessSample>
double measureGainDb (ProcessSample&& process, double freq, double sampleRate = 48000.0)
{
    constexpr size_t settle = 24000, length = 4800;
    auto x = sine (freq, sampleRate, static_cast<int> (settle + length));
    for (auto& s : x)
        s = process (s);
    return toDb (amplitudeAt (x, freq, sampleRate, settle, length));
}

} // namespace test
