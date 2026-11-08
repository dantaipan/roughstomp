#pragma once

#include "fx/Effect.h"
#include "fx/Filters.h"
#include "fx/Oversampler.h"
#include "fx/Smoother.h"

#include <vector>

namespace fx
{

/** Overdrive v2: a mid-focused drive with three clipping modes.

    Everything runs at 8x oversampling, including the clean blend and the
    bypass crossfade, so dry and driven signals stay perfectly time-aligned
    (no comb filtering when blending) and latency is constant even when
    bypassed:

      in -> [up x8] -> drive (boost above the mode's focus frequency)
         -> pre-clip LPF -> + touch bias -> clipper (mode) -> tone LPF
         -> fizz LPFs -> DC blocker -> makeup + level -> clean blend
         -> bypass crossfade -> [down x8] -> out

    Modes:
      soft        tanh, Tube Screamer-style; odd harmonics, smooth
      hard        rounded hard clip with fuller lows, 6 dB more gain and
                  extra treble roll-off; distortion-pedal style
      asymmetric  diode-style, clips harder on one side; even harmonics,
                  warmer and more tube-like

    Touch: an envelope follower biases the clipper by how hard you play, so
    strong picking blooms with even harmonics and backing off the guitar's
    volume cleans up.

    Tone: a first-order lowpass swept from 600 Hz to 8 kHz on a log taper
    (about 2.2 kHz at noon). It keeps rolling off the top octaves, which is
    what tames clipper fizz. A 700 Hz shelf was tried in v2 and lost the
    blind A/B to this: its flat top end let fizz through above 3 kHz.

    The clipper starts working around -12 dBFS, about where a DI guitar
    peaks. Clipping makes the signal denser and louder as drive rises, so a
    makeup gain of -1 dB per 4 dB of drive, plus a per-mode trim, keeps
    level 0 dB close to bypass loudness at every setting.

    Every parameter is smoothed, and changing mode crossfades the clippers;
    call setParameters() at the start of each block from the audio thread. */
class Overdrive final : public Effect
{
public:
    enum class Mode { soft, hard, asymmetric };
    static constexpr int numModes = 3;

    struct Parameters
    {
        float drive    = 0.5f;   // 0..1, maps to 0..40 dB of gain on a log taper
        float tone     = 0.5f;   // 0..1, maps to a 600 Hz..8 kHz lowpass
        float levelDb  = 0.0f;   // output level
        float mix      = 1.0f;   // 0 = clean, 1 = fully driven
        bool  bypassed = false;  // crossfades over 10 ms
        Mode  mode     = Mode::soft;
    };

    /** What each mode changes besides the clipping curve. */
    struct Voicing
    {
        float focusHz;        // drive mostly boosts above this
        float driveOffsetDb;  // extra gain on top of the drive knob
        float trimDb;         // evens out loudness between modes
        float fizzHz;         // extra one-pole treble roll-off after the clipper
    };

    static const Voicing& voicing (Mode) noexcept;

    static constexpr int   oversamplingLog2 = 3;       // 8x: aliasing ~10 dB lower than 4x for <1% CPU
    static constexpr float maxDriveDb       = 40.0f;
    static constexpr float clipThreshold    = 0.25f;   // -12 dBFS
    static constexpr float preClipLowPassHz = 7000.0f; // tames fizz and aliasing
    static constexpr float toneMinHz        = 600.0f;
    static constexpr float toneMaxHz        = 8000.0f;
    static constexpr float postLowPassHz    = 10000.0f;
    static constexpr float makeupPerDriveDb = -0.25f;
    static constexpr float touchDepth       = 0.3f;    // bias, as a fraction of the threshold, at full strength
    static constexpr double touchAttackSeconds  = 0.002;
    static constexpr double touchReleaseSeconds = 0.08;

    static float driveDecibels (float drive) noexcept;
    static float toneCutoffHz  (float tone) noexcept;

    /** The clipping curve for a mode, with its threshold at 1. */
    static float shape (Mode, float x) noexcept;

    void setParameters (const Parameters& p) noexcept;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;
    int  getLatencySamples() const override { return oversampler.getLatencySamples(); }

private:
    void processChunk (float* const* channels, int numChannels, int numSamples) noexcept;

    struct Channel
    {
        OnePole   focus;     // highpass: the part of the signal drive acts on
        OnePole   preClip;   // lowpass
        OnePole   tone;      // lowpass, swept by the tone knob
        Biquad    post;      // lowpass
        OnePole   fizz;      // lowpass, per mode
        DcBlocker dc;
        float     envelope = 0.0f;
    };

    Oversampler oversampler;
    double osRate = 8 * 44100.0;
    int maxBlock = 0;

    LinearSmoother  driveDb, level { 1.0f }, mix { 1.0f }, bypass;
    LinearSmoother  modeFade { 1.0f };  // 0 = fromMode, 1 = toMode
    OnePoleSmoother tone { 0.5f }, focusHz { 720.0f }, fizzHz { 40000.0f };

    Mode fromMode = Mode::soft, toMode = Mode::soft;
    float touchAttack = 0.0f, touchRelease = 0.0f;

    // Per-sample parameter values at the oversampled rate, shared by all channels
    std::vector<float>  driveGains, makeups, levels, mixes, bypasses, fades;
    std::vector<double> focusCoeffs, toneCoeffs, fizzCoeffs;

    std::vector<Channel> channelState;
};

} // namespace fx
