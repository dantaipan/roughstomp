#pragma once

// fx-core's pedals beyond the Overdrive and Boost (all handle stereo).
// Private to fx-core: hosts reach them through fx::registry.

#include "fx/DelayLine.h"
#include "fx/Filters.h"
#include "fx/GraphicEq.h"
#include "fx/Lfo.h"
#include "fx/Oversampler.h"
#include "fx/Pedal.h"
#include "fx/Smoother.h"

#include <array>
#include <cmath>
#include <vector>

namespace fx
{

/** What the host's transport says, as a pedal sees it. */
struct HostTransport
{
    double tempo = 120.0, position = 0.0;
    bool playing = false;
};

/** Tempo sync for LFO rates: the choices a Sync menu offers, and the rate
    each gives (index 0 is Free: the pedal's own Rate knob). */
namespace lfoSync
{
    inline const std::vector<std::string_view> choices { "Free rate", "1 bar", "1/2", "1/4", "1/4 trip", "1/8", "1/8 trip", "1/16" };

    inline double beatsPerCycle (int c) noexcept
    {
        static constexpr std::array<double, 8> beats { 0.0, 4.0, 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25 };
        return c >= 0 && c < static_cast<int> (beats.size()) ? beats[static_cast<size_t> (c)] : 0.0;
    }

    inline double rate (int c, double freeHz, double bpm) noexcept
    {
        const double beats = beatsPerCycle (c);
        return beats > 0.0 ? bpm / 60.0 / beats : freeHz;
    }

    /** Sets the LFO's rate, and while the host plays a synced LFO is locked to
        the bar: cycle n starts on beat n * beatsPerCycle. Call at the start of
        each block. */
    inline void apply (Lfo& lfo, int c, double freeHz, const HostTransport& host) noexcept
    {
        lfo.setRate (rate (c, freeHz, host.tempo));
        if (const double beats = beatsPerCycle (c); beats > 0.0 && host.playing)
            lfo.setPosition (host.position / beats);
    }
}

/** Graphic EQ: seven octave bands (fx::graphicEq) and a level. */
class EqPedal final : public BasicPedal
{
public:
    enum Index { level = graphicEq::numBands };
    static const PedalInfo& staticInfo();

    EqPedal() : BasicPedal (staticInfo()) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;

private:
    static constexpr int numBands = graphicEq::numBands;
    static constexpr int updateInterval = 16;   // samples between coefficient updates while a slider moves

    void setTargets() noexcept;
    void updateCoefficients() noexcept;

    double sampleRate = 48000.0;
    bool primed = false;
    std::array<OnePoleSmoother, numBands> gains;   // dB
    LinearSmoother levelGain { 1.0f }, wet { 1.0f };
    std::vector<std::array<Biquad, numBands>> filters;   // per channel
};

/** Chorus: 1-3 voices of a modulated 8-16 ms delay, blended with the dry
    signal. Width spreads the voices' modulation across the stereo field. */
class ChorusPedal final : public BasicPedal
{
public:
    enum Index { rate, depth, mix, width, voices, sync };
    static const PedalInfo& staticInfo();

    ChorusPedal() : BasicPedal (staticInfo()) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;

private:
    static constexpr int maxVoices = 3;
    void setTargets() noexcept;

    double sampleRate = 48000.0;
    bool primed = false;
    Lfo lfo;
    LinearSmoother depthAmount { 0.5f }, wet { 0.5f }, spread { 1.0f };
    std::array<LinearSmoother, maxVoices> voiceGains;   // voices fade in and out
    std::vector<DelayLine> lines;   // per channel
};

/** Delay: digital, tape or ping-pong; free time or synced to the host
    tempo. Tails ring out when bypassed. Tape is modelled on a tape echo
    whose time is set by the tape's speed: short times run the tape fast,
    so a slap-back is bright and steady, while long echoes are darker and
    wobble more. It saturates as it records, so the repeats compress as
    they build up. */
class DelayPedal final : public BasicPedal
{
public:
    enum Index { time, sync, feedback, mix, tone, mode };
    enum class Mode { digital, tape, pingPong };
    static const PedalInfo& staticInfo();

    /** Beats per sync choice (index 0 is Off). */
    static double syncBeats (int choice) noexcept;
    static constexpr double maxSeconds = 3.0;

    DelayPedal() : BasicPedal (staticInfo()) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;

    /** The delay time it's heading for, in seconds. */
    double targetSeconds() const noexcept;

private:
    double sampleRate = 48000.0;
    OnePoleSmoother delaySamples { 1.0f };   // tape: glides (and bends pitch)
    float fromDelay = 1.0f, toDelay = 1.0f;  // digital: crossfades between two read points
    int fadeRemaining = 0, fadeLength = 1;
    LinearSmoother send { 1.0f }, wetGain { 0.35f }, feedbackGain { 0.35f };
    Lfo wow, flutter, drift;
    std::vector<DelayLine> lines;
    std::vector<OnePole> toneFilters, lowCuts;
    std::vector<Biquad> tapeBandwidth;   // the playback head's top end: wider at higher tape speeds
    float bandwidthDelay = -1.0f;        // the delay the bandwidth filters were set for
    bool primed = false;
};

/** Reverb: an 8-line feedback delay network behind four allpass diffusers.
    Room, hall and plate differ in their delay lengths and diffusion. Tails
    ring out when bypassed. */
class ReverbPedal final : public BasicPedal
{
public:
    enum Index { decay, size, preDelay, tone, mix, mode };
    enum class Mode { room, hall, plate };
    static const PedalInfo& staticInfo();

    ReverbPedal() : BasicPedal (staticInfo()) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;

private:
    static constexpr int numLines = 8, numDiffusers = 4;

    struct Allpass
    {
        DelayLine line;
        int length = 1;
        float process (float x, float g) noexcept
        {
            const float d = line.readInteger (length);
            const float v = x + g * d;
            line.write (v);
            return d - g * v;
        }
    };

    double sampleRate = 48000.0;
    bool primed = false;
    int currentMode = -1;
    DelayLine preDelayLine;
    std::array<Allpass, numDiffusers> diffusers;
    std::array<DelayLine, numLines> lines;
    std::array<OnePole, numLines> damping;
    std::array<float, numLines> gains {};
    Lfo lfo;
    OnePoleSmoother sizeScale { 1.0f }, preDelaySamples { 0.0f };
    LinearSmoother send { 1.0f }, wetGain { 0.25f };

    void setMode (int mode) noexcept;
    void updateLines() noexcept;
};

/** Tremolo: volume pulsing, sine to choppy square; Spread pans the pulses
    across the stereo field (100% = auto-pan). */
class TremoloPedal final : public BasicPedal
{
public:
    enum Index { rate, depth, spread, shape, sync };
    static const PedalInfo& staticInfo();

    TremoloPedal() : BasicPedal (staticInfo()) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override { lfo.reset(); }
    void process (float* const* channels, int numChannels, int numSamples) override;

private:
    bool primed = false;
    Lfo lfo;
    LinearSmoother depthAmount { 0.6f }, spreadAmount { 0.0f };
};

/** Vibrato: pitch wobble from a swept delay, fully wet. */
class VibratoPedal final : public BasicPedal
{
public:
    enum Index { rate, depth, sync };
    static const PedalInfo& staticInfo();

    VibratoPedal() : BasicPedal (staticInfo()) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;

private:
    double sampleRate = 48000.0;
    bool primed = false;
    Lfo lfo;
    LinearSmoother depthAmount { 0.4f }, wet { 1.0f };
    std::vector<DelayLine> lines;
};

/** Flanger: a very short delay swept over +-2 octaves around Delay, with
    positive or negative feedback. The sides sweep a quarter cycle apart. */
class FlangerPedal final : public BasicPedal
{
public:
    enum Index { rate, depth, delay, feedback, mix, sync };
    static const PedalInfo& staticInfo();

    FlangerPedal() : BasicPedal (staticInfo()) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;

private:
    double sampleRate = 48000.0;
    bool primed = false;
    Lfo lfo;
    OnePoleSmoother centreMs { 1.5f };
    LinearSmoother depthAmount { 0.7f }, feedbackGain { 0.5f }, wet { 0.5f };
    std::vector<DelayLine> lines;
};

/** Phaser: 4, 8 or 12 first-order allpass stages swept between 200 Hz and
    4 kHz, with feedback. The sides sweep a quarter cycle apart. */
class PhaserPedal final : public BasicPedal
{
public:
    enum Index { rate, depth, feedback, mix, stages, sync };
    static const PedalInfo& staticInfo();

    PhaserPedal() : BasicPedal (staticInfo()) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;

private:
    static constexpr int maxStages = 12;

    double sampleRate = 48000.0;
    bool primed = false;
    Lfo lfo;
    LinearSmoother depthAmount { 0.7f }, feedbackGain { 0.3f }, wet { 0.5f };
    std::array<LinearSmoother, maxStages> stageGains;   // stages fade in and out when the count changes
    std::vector<std::array<float, maxStages>> states;   // per channel
    std::vector<float> lastOut;
};

/** Spring reverb, after Valimaki, Parker and Abel's parametric spring model:
    three springs of different lengths, each a loop of a stretched-allpass
    chain (the chirp: higher frequencies arrive later, up to the spring's
    cut-off near 4.4 kHz), a steep low-pass at that cut-off, gentle damping,
    and a delay whose length wanders a little at random (no metallic flutter).
    Summed mostly to mono, as a tank is. Dwell drives the springs harder;
    Tone is the brightness of what comes out. Tails ring out when bypassed. */
class SpringPedal final : public BasicPedal
{
public:
    enum Index { decay, dwell, tone, mix };
    static const PedalInfo& staticInfo();

    SpringPedal() : BasicPedal (staticInfo()) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;

private:
    static constexpr int numSprings = 3;
    static constexpr int numStages = 100;

    struct Spring
    {
        DelayLine line;
        float length = 1.0f;              // samples, before the wander
        float dispersion = 0.75f;         // the allpasses' coefficient
        std::vector<float> chirpIn, chirpOut;   // numStages x stretch: each stage's last inputs and outputs
        std::array<Biquad, 2> cutOff;     // 4th-order low-pass at the spring's cut-off
        OnePole damping, lowCut;
        Lfo wander;
        float last = 0.0f;
    };

    double sampleRate = 48000.0;
    bool primed = false;
    int stretch = 5;      // samples per allpass delay: puts the dispersion below the cut-off
    int chirpPos = 0;     // shared ring position in every stage's history
    float wanderDepth = 8.0f;
    std::array<Spring, numSprings> springs;
    OnePole inputLowCut;
    std::array<OnePole, 2> brightness;   // Tone, per output side
    LinearSmoother send { 1.0f }, wetGain { 0.3f }, drive { 1.0f };
};

/** Compressor: Sustain sets how hard it squeezes (threshold and ratio
    together), with automatic make-up gain; Blend mixes the dry signal back
    in (parallel compression). The sides share one gain, so the image holds. */
class CompressorPedal final : public BasicPedal
{
public:
    enum Index { sustain, attack, level, blend };
    static const PedalInfo& staticInfo();

    CompressorPedal() : BasicPedal (staticInfo()) {}

    /** The gain computer: dB of gain (<= 0) for an input level in dB. */
    static float gainDb (float inputDb, float thresholdDb, float ratio) noexcept;
    static float thresholdFor (float sustainValue) noexcept { return -12.0f - 3.0f * sustainValue; }
    static float ratioFor (float sustainValue) noexcept     { return 2.0f + 0.6f * sustainValue; }
    static float makeupFor (float sustainValue) noexcept;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;

private:
    double sampleRate = 48000.0;
    bool primed = false;
    float peak = 0.0f, peakRelease = 0.0f;   // level detector
    float reductionDb = 0.0f;                // smoothed gain reduction
    LinearSmoother levelGain { 1.0f }, wet { 1.0f }, makeup { 1.0f };
};

/** Wah: a resonant bandpass swept 350 Hz - 2.2 kHz by Position, which is a
    host parameter, so an expression pedal or automation can rock it. Auto
    mode follows the playing dynamics instead (an envelope filter), starting
    from Position. */
class WahPedal final : public BasicPedal
{
public:
    enum Index { position, resonance, level, sensitivity, mode };
    enum class Mode { manual, autoWah };
    static const PedalInfo& staticInfo();

    static constexpr double lowestHz = 350.0, highestHz = 2200.0;
    static double frequencyFor (double position01) noexcept { return lowestHz * std::pow (highestHz / lowestHz, position01); }

    WahPedal() : BasicPedal (staticInfo()) {}

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;

private:
    double sampleRate = 48000.0;
    bool primed = false;
    OnePoleSmoother sweep { 0.5f };   // 0..1, smoothed so stepped automation doesn't zipper
    float envelope = 0.0f, envAttack = 0.0f, envRelease = 0.0f;
    LinearSmoother levelGain { 1.0f }, wet { 1.0f };
    struct State { float ic1 = 0.0f, ic2 = 0.0f; };
    std::vector<State> states;   // per channel
};

/** Shared by the drive pedals: 8x oversampling around the nonlinear part
    (as the Overdrive), chunking to the prepared block size, and bypass as a
    crossfade with the dry signal taken through the same oversampler, so the
    latency never changes. A drive fills in the oversampled processing. */
class DrivePedal : public BasicPedal
{
public:
    using BasicPedal::BasicPedal;

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;
    int getLatencySamples() const override { return oversampler.getLatencySamples(); }

protected:
    static constexpr int oversamplingLog2 = 3;

    /** Allocate and set up for this (oversampled) rate. */
    virtual void prepareDrive (double osRate, int maxOversampledBlock, int numChannels) = 0;
    virtual void resetDrive() noexcept = 0;
    /** Start of a block: point the smoothers at the current settings. With
        jump, go straight there (the first block after prepare). */
    virtual void setTargets (bool jump) noexcept = 0;
    /** Process n oversampled samples per channel, in place. */
    virtual void processOversampled (float* const* x, int numChannels, int n) noexcept = 0;

    /** Fills out[0..n) from a smoother (cheaply when it's idle). */
    template <typename Smoother>
    static void fill (Smoother& s, float* out, int n) noexcept
    {
        if (! s.isSmoothing())
            std::fill (out, out + n, s.getCurrent());
        else
            for (int i = 0; i < n; ++i)
                out[i] = s.next();
    }

private:
    Oversampler oversampler;
    int maxBlock = 512;
    bool primed = false;
    LinearSmoother wet { 1.0f };
    std::vector<float> wetCurve;
    std::vector<std::vector<float>> dry;
};

/** Fuzz: a vintage two-transistor style. Fuzz sets the gain, Bias the
    transistors' operating point: lower starves them into a gated, sputtering
    sound. Raspy and asymmetric; softer playing (or the guitar's volume)
    clips less. */
class FuzzPedal final : public DrivePedal
{
public:
    enum Index { fuzz, bias, level };
    static const PedalInfo& staticInfo();
    FuzzPedal() : DrivePedal (staticInfo()) {}

private:
    void prepareDrive (double osRate, int maxOversampledBlock, int numChannels) override;
    void resetDrive() noexcept override;
    void setTargets (bool jump) noexcept override;
    void processOversampled (float* const* x, int numChannels, int n) noexcept override;

    LinearSmoother gain1 { 1.0f }, offset { 0.0f }, levelGain { 1.0f };
    std::vector<float> g1, off, lv;
    struct Channel { OnePole input, fizz; DcBlocker dc; };
    std::vector<Channel> state;
};

/** Big Fuzz: the cascaded-stage, sustaining style. Two clipping stages for
    long sustain, then a tone control that sweeps from bass through a scooped
    middle to treble. */
class BigFuzzPedal final : public DrivePedal
{
public:
    enum Index { sustain, tone, level };
    static const PedalInfo& staticInfo();
    BigFuzzPedal() : DrivePedal (staticInfo()) {}

private:
    void prepareDrive (double osRate, int maxOversampledBlock, int numChannels) override;
    void resetDrive() noexcept override;
    void setTargets (bool jump) noexcept override;
    void processOversampled (float* const* x, int numChannels, int n) noexcept override;

    LinearSmoother gain1 { 1.0f }, toneMix { 0.5f }, levelGain { 1.0f };
    std::vector<float> g1, tm, lv;
    struct Channel { OnePole input, stage1, stage2, bass, treble; DcBlocker dc; };
    std::vector<Channel> state;
};

/** Clean Boost Drive: the transparent style. Soft clipping blended with a
    clean path that stays, so the guitar's own sound comes through; Gain
    brings in more of the clipped side; an active Treble control. */
class CleanDrivePedal final : public DrivePedal
{
public:
    enum Index { gain, treble, level };
    static const PedalInfo& staticInfo();
    CleanDrivePedal() : DrivePedal (staticInfo()) {}

private:
    void prepareDrive (double osRate, int maxOversampledBlock, int numChannels) override;
    void resetDrive() noexcept override;
    void setTargets (bool jump) noexcept override;
    void processOversampled (float* const* x, int numChannels, int n) noexcept override;

    double rate = 384000.0;
    LinearSmoother driveGain { 1.0f }, dirtyMix { 0.3f }, levelGain { 1.0f };
    OnePoleSmoother trebleDb { 0.0f };
    std::vector<float> dg, dm, lv;
    struct Channel { OnePole driveLowCut, driveTop; Biquad treble; DcBlocker dc; };
    std::vector<Channel> state;
    float lastTrebleDb = 1000.0f;
};

/** Blues Drive: the touch-sensitive, amp-like style. Two clipping stages,
    each keeping its clean signal under the clipped part (diodes in the
    feedback), so it cleans up with picking and the guitar's volume; full
    lows, no strong mid hump. Gain goes from barely broken up to near fuzz,
    Tone is an active treble cut and boost. */
class BluesDrivePedal final : public DrivePedal
{
public:
    enum Index { gain, tone, level };
    static const PedalInfo& staticInfo();
    BluesDrivePedal() : DrivePedal (staticInfo()) {}

private:
    void prepareDrive (double osRate, int maxOversampledBlock, int numChannels) override;
    void resetDrive() noexcept override;
    void setTargets (bool jump) noexcept override;
    void processOversampled (float* const* x, int numChannels, int n) noexcept override;

    double rate = 384000.0;
    LinearSmoother gain1 { 1.0f }, gain2 { 1.0f }, levelGain { 1.0f };
    OnePoleSmoother toneDb { 0.0f };
    std::vector<float> g1, g2, lv;
    struct Channel { OnePole input, leg1, leg2, top1, top2; Biquad tone, fizz; DcBlocker dc; };
    std::vector<Channel> state;
    float lastToneDb = 1000.0f;
};

} // namespace fx
