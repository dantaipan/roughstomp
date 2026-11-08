#pragma once

#include <algorithm>
#include <cmath>

namespace fx
{

/** Noise gate for a guitar input: silences hiss and hum between notes.

    Opens instantly when the signal's peak rises past the threshold, stays
    open while it's within 6 dB below (hysteresis, so a decaying note doesn't
    chatter), holds a moment after that, then fades out. Mono; real-time
    safe after prepare(). */
class NoiseGate
{
public:
    static constexpr float hysteresisDb = 6.0f;
    static constexpr double holdSeconds = 0.05;
    static constexpr double openSeconds = 0.0005;    // fade in when the gate opens
    static constexpr double closeSeconds = 0.08;     // fade out when it closes
    static constexpr double detectorRelease = 0.02;  // the peak follower's fall time

    void prepare (double sampleRate) noexcept
    {
        detectorCoeff = coeff (sampleRate, detectorRelease);
        openCoeff = coeff (sampleRate, openSeconds);
        closeCoeff = coeff (sampleRate, closeSeconds);
        holdSamples = static_cast<int> (holdSeconds * sampleRate);
        reset();
    }

    void reset() noexcept
    {
        envelope = 0.0f;
        gain = enabled ? 0.0f : 1.0f;
        open = false;
        holdLeft = 0;
    }

    void setThresholdDb (float dB) noexcept
    {
        openLevel = std::pow (10.0f, dB / 20.0f);
        closeLevel = std::pow (10.0f, (dB - hysteresisDb) / 20.0f);
    }

    /** Off: the gain fades back to unity and stays there. */
    void setEnabled (bool shouldBeEnabled) noexcept { enabled = shouldBeEnabled; }

    bool isOpen() const noexcept { return open || ! enabled; }

    float processSample (float x) noexcept
    {
        const float level = std::abs (x);
        envelope = std::max (level, envelope * detectorCoeff);

        if (envelope >= openLevel)
        {
            open = true;
            holdLeft = holdSamples;
        }
        else if (envelope < closeLevel)
        {
            if (holdLeft > 0)
                --holdLeft;
            else
                open = false;
        }

        const float target = (open || ! enabled) ? 1.0f : 0.0f;
        const float c = target > gain ? openCoeff : closeCoeff;
        gain = target + c * (gain - target);
        return x * gain;
    }

    void process (float* samples, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
            samples[i] = processSample (samples[i]);
    }

private:
    static float coeff (double sampleRate, double seconds) noexcept
    {
        return static_cast<float> (std::exp (-1.0 / std::max (1.0, seconds * sampleRate)));
    }

    float openLevel = 0.001f, closeLevel = 0.0005f;
    float detectorCoeff = 0.0f, openCoeff = 0.0f, closeCoeff = 0.0f;
    float envelope = 0.0f, gain = 1.0f;
    int holdSamples = 0, holdLeft = 0;
    bool open = false, enabled = false;
};

} // namespace fx
