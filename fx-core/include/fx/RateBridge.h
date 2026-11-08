#pragma once

#include <cstdint>
#include <vector>

namespace fx
{

/** Streams audio from one sample rate to another with a windowed-sinc filter.

    Output sample j is the band-limited input signal at input position
    (j - delay) * inRate / outRate. Positions step exactly (integer rates,
    rational stepping), so two converters running in opposite directions stay
    locked forever and a round trip has a fixed whole-sample latency.

    push() and pull() are real-time safe; prepare() allocates. */
class RateConverter
{
public:
    static constexpr int zeroCrossings = 32;    // each side of the centre, at the lower rate
    static constexpr double cutoff = 0.925;     // of the lower rate's Nyquist (passband to ~0.85)
    static constexpr double kaiserBeta = 7.5;   // ~75 dB stopband

    /** Input samples each side of the centre that one output reads. */
    static double halfWidth (std::int64_t inRate, std::int64_t outRate) noexcept;

    /** delayOut: output samples of extra delay (the first output reads
        input position -delayOut * inRate / outRate, i.e. silence). */
    void prepare (std::int64_t inRate, std::int64_t outRate, std::int64_t delayOut, int historyCapacity);
    void reset() noexcept;

    void push (const float* input, int numSamples) noexcept;

    /** Produces outputs while their input is all here, up to maxOutputs.
        Returns how many. */
    int pull (float* output, int maxOutputs) noexcept;

    /** Produces exactly numOutputs; any input not here yet reads as silence
        and counts as an underrun. */
    void pullExactly (float* output, int numOutputs) noexcept;

    std::int64_t getUnderruns() const noexcept { return underruns; }

private:
    float outputAt() const noexcept;
    bool available() const noexcept;
    void advance() noexcept;

    std::int64_t inRate = 1, outRate = 1, delay = 0;
    double scale = 1.0;      // min (1, outRate / inRate)
    double width = 0.0;      // halfWidth, in input samples
    std::vector<float> table;
    std::vector<float> history;
    std::int64_t mask = 0;
    std::int64_t written = 0;
    std::int64_t position = 0, remainder = 0;   // next output reads position + remainder / outRate
    std::int64_t underruns = 0;
};

/** Runs a process (e.g. an amp capture) at its own sample rate inside a host
    running at another: resample in, process, resample back. Rates that match
    pass straight through with no latency.

    The latency is constant and a whole number of host samples, so it can be
    reported to the host and compensated. */
class RateBridge
{
public:
    /** Host-rate samples of delay the round trip adds; 0 if the rates match. */
    static int latencyFor (double outerRate, double innerRate) noexcept;

    /** innerRate <= 0 means the process runs at any rate. Allocates. */
    void prepare (double outerRate, double innerRate, int maxOuterBlock);
    void reset() noexcept;

    bool isActive() const noexcept { return active; }
    int getLatencySamples() const noexcept { return latency; }

    /** The most samples one call of the process can be given. */
    int getMaxInnerBlock() const noexcept { return maxInner; }

    /** Underruns since prepare: always 0 unless something is wrong. */
    std::int64_t getUnderruns() const noexcept { return fromInner.getUnderruns(); }

    /** Replaces samples[0..n) with process applied at the inner rate.
        n must be at most maxOuterBlock. process (float*, int) works in place. */
    template <typename Process>
    void process (float* samples, int n, Process&& inner)
    {
        if (! active)
        {
            inner (samples, n);
            return;
        }

        toInner.push (samples, n);
        const int m = toInner.pull (buffer.data(), maxInner);
        if (m > 0)
        {
            inner (buffer.data(), m);
            fromInner.push (buffer.data(), m);
        }
        fromInner.pullExactly (samples, n);
    }

private:
    bool active = false;
    int latency = 0;
    int maxInner = 0;
    RateConverter toInner, fromInner;
    std::vector<float> buffer;
};

} // namespace fx
