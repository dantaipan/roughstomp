#pragma once

#include "fx/Effect.h"
#include "fx/Filters.h"
#include "fx/Oversampler.h"
#include "fx/Smoother.h"

#include <vector>

namespace fx
{

/** Overdrive v1: a Tube Screamer-style mid-focused soft clipper.

    Everything runs at 8x oversampling, including the clean blend and the
    bypass crossfade, so dry and driven signals stay perfectly time-aligned
    (no comb filtering when blending) and latency is constant even when
    bypassed:

      in -> [up x8] -> drive (boost above ~720 Hz) -> pre-clip LPF
         -> soft clip -> tone LPF -> fizz LPF -> DC blocker -> level
         -> clean blend -> bypass crossfade -> [down x8] -> out

    The clipper starts working around -12 dBFS, about where a DI guitar
    peaks, so drive 0 is "edge of breakup". Clipping makes the signal denser
    and louder as drive rises, so a makeup gain of -1 dB per 4 dB of drive
    keeps level 0 dB close to bypass loudness at every drive setting.

    Every parameter is smoothed; call setParameters() at the start of each
    block from the audio thread. */
class Overdrive final : public Effect
{
public:
    struct Parameters
    {
        float drive    = 0.5f;   // 0..1, maps to 0..40 dB of gain on a log taper
        float tone     = 0.5f;   // 0..1, maps to a 600 Hz..8 kHz lowpass
        float levelDb  = 0.0f;   // output level
        float mix      = 1.0f;   // 0 = clean, 1 = fully driven
        bool  bypassed = false;  // crossfades over 10 ms
    };

    static constexpr int   oversamplingLog2 = 3;       // 8x: aliasing ~10 dB lower than 4x for <1% CPU
    static constexpr float maxDriveDb       = 40.0f;
    static constexpr float clipThreshold    = 0.25f;   // -12 dBFS
    static constexpr float toneMinHz        = 600.0f;
    static constexpr float toneMaxHz        = 8000.0f;
    static constexpr float driveFocusHz     = 720.0f;  // drive mostly boosts above this
    static constexpr float preClipLowPassHz = 7000.0f; // tames fizz and aliasing
    static constexpr float postLowPassHz    = 10000.0f;
    static constexpr float makeupPerDriveDb = -0.25f;

    static float driveDecibels (float drive) noexcept;
    static float toneCutoffHz  (float tone) noexcept;

    void setParameters (const Parameters& p) noexcept;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;
    int  getLatencySamples() const override { return oversampler.getLatencySamples(); }

private:
    void processChunk (float* const* channels, int numChannels, int numSamples) noexcept;

    struct Channel
    {
        OnePole   focus;   // highpass: the part of the signal drive acts on
        OnePole   preClip; // lowpass
        OnePole   tone;    // lowpass, swept
        Biquad    post;    // lowpass
        DcBlocker dc;
    };

    Oversampler oversampler;
    double osRate = 8 * 44100.0;
    int maxBlock = 0;

    LinearSmoother  driveDb, level { 1.0f }, mix { 1.0f }, bypass;
    OnePoleSmoother tone { 0.5f };

    // Per-sample parameter values at the oversampled rate, shared by all channels
    std::vector<float>  driveGains, makeups, levels, mixes, bypasses;
    std::vector<double> toneCoeffs;

    std::vector<Channel> channelState;
};

} // namespace fx
