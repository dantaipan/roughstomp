#pragma once

#include <vector>

namespace fx
{

/** 2x / 4x / 8x oversampling built from cascaded 2x halfband FIR stages.

    Use it around anything nonlinear (clippers, saturators) so the harmonics
    they create above the original Nyquist are filtered out instead of
    folding back down as aliasing:

        auto* const* up = os.upsample (in, numCh, n);      // n * factor samples
        ... process up[ch][0 .. n * factor) in place ...
        os.downsample (out, numCh, n);                      // back to n samples

    The filters are linear-phase, so the round trip is a pure delay of
    getLatencySamplesExact() samples at the base rate. With 2x that is a whole
    number of samples (31). With 4x/8x it has a fraction (38.5 / 42.25), which
    is normal; report the rounded value to the host.

    prepare() allocates; upsample() and downsample() are real-time safe. */
class Oversampler
{
public:
    Oversampler();
    ~Oversampler();

    /** factorLog2: 1 = 2x, 2 = 4x, 3 = 8x. */
    void prepare (int numChannels, int maxBlockSize, int factorLog2);
    void reset() noexcept;

    int getFactor() const noexcept { return 1 << numStages; }

    /** Upsamples numSamples (<= maxBlockSize) per channel. The returned
        channel pointers hold numSamples * getFactor() samples and stay valid
        until the next call; process them in place, then call downsample(). */
    float* const* upsample (const float* const* input, int numChannels, int numSamples) noexcept;

    /** Filters and decimates the oversampled buffer back to numSamples. */
    void downsample (float* const* output, int numChannels, int numSamples) noexcept;

    /** Round-trip delay in base-rate samples. */
    double getLatencySamplesExact() const noexcept;
    int    getLatencySamples() const noexcept;

private:
    struct Stage;
    std::vector<Stage> stages;
    std::vector<std::vector<float*>> stagePointers; // per stage: channel pointers into its buffer
    int numStages = 0;
};

} // namespace fx
