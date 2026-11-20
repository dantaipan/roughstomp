#pragma once

#include "fx/Filters.h"
#include "fx/Smoother.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace fx
{

/* Tone shaping around an amp capture, which has no tone controls of its own.
   Both classes process ONE channel (the rig's amp paths are mono until
   they're panned) and are real-time safe once prepared. */

//==============================================================================
/** Bass, Middle and Treble after a capture: a low shelf, a broad mid peak and
    a high shelf, each +-rangeDb. All at 0 dB it's exactly the input (not
    merely close), so a rig that never touches it sounds as it did. Changes
    glide; coefficients follow every `updateInterval` samples. */
class ToneStack
{
public:
    static constexpr float rangeDb = 12.0f;
    static constexpr double bassHz = 120.0, middleHz = 650.0, trebleHz = 2800.0;
    static constexpr double shelfQ = 0.7071, middleQ = 0.7;
    static constexpr float flatWithinDb = 0.005f;   // gains this close to 0 are 0

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        for (auto& s : gains)
            s.prepare (sampleRate, 0.015);
        applied = { 1.0e9f, 1.0e9f, 1.0e9f };   // recompute on the next block
        reset();
    }

    void reset() noexcept
    {
        for (auto& f : filters)
            f.reset();
        for (auto& s : gains)
            s.reset (s.getTarget());
        idleSamples = idleAfter();
    }

    /** Targets in dB (clamped to +-rangeDb); call once per block. After
        prepare(), set the gains then reset() to start on them without a glide. */
    void setGains (float bassDb, float middleDb, float trebleDb) noexcept
    {
        // A host's "0 dB" can come back from its normalised form as 2e-7:
        // that's flat, so the stack can stand aside exactly
        auto target = [] (float dB) { return std::abs (dB) < flatWithinDb ? 0.0f : std::clamp (dB, -rangeDb, rangeDb); };
        gains[0].setTarget (target (bassDb));
        gains[1].setTarget (target (middleDb));
        gains[2].setTarget (target (trebleDb));
    }

    /** True once it's flat and has settled: process() then leaves the signal alone. */
    bool isIdle() const noexcept { return idleSamples >= idleAfter(); }

    void process (float* x, int n) noexcept
    {
        if (isIdle() && isFlatTarget())
            return;

        for (int start = 0; start < n; start += updateInterval)
        {
            // Back to flat: the filters keep running (as an exact pass-through)
            // until their state has died away, then stop with nothing to click
            if (isFlatTarget() && ! isGliding())
            {
                if (isIdle())
                {
                    for (auto& f : filters)
                        f.reset();
                    return;
                }
                idleSamples += updateInterval;
            }
            else
                idleSamples = 0;

            const int len = std::min (updateInterval, n - start);
            for (int i = 0; i < len; ++i)
                for (auto& s : gains)
                    s.next();
            updateCoefficients();
            for (auto& f : filters)
                f.processBlock (x + start, len);
        }
    }

    /** The response in dB at a frequency, for given gains (for tests and GUIs). */
    static double responseDb (float bassDb, float middleDb, float trebleDb, double freq, double sampleRate = 48000.0) noexcept
    {
        const auto c = coefficients (sampleRate, bassDb, middleDb, trebleDb);
        double m = 1.0;
        for (const auto& k : c)
            m *= k.magnitudeAt (sampleRate, freq);
        return 20.0 * std::log10 (m);
    }

private:
    static constexpr int updateInterval = 16;

    static std::array<BiquadCoeffs, 3> coefficients (double sr, float bass, float middle, float treble) noexcept
    {
        // Within the band's reach at low rates; an unused band (0 dB) is the identity
        auto at = [sr] (double hz) { return std::min (hz, sr * 0.45); };
        return { BiquadCoeffs::lowShelf  (sr, at (bassHz),   shelfQ,  bass),
                 BiquadCoeffs::peak      (sr, at (middleHz), middleQ, middle),
                 BiquadCoeffs::highShelf (sr, at (trebleHz), shelfQ,  treble) };
    }

    void updateCoefficients() noexcept
    {
        const std::array<float, 3> now { gains[0].getCurrent(), gains[1].getCurrent(), gains[2].getCurrent() };
        bool changed = false;
        for (size_t b = 0; b < 3; ++b)
            changed = changed || now[b] < applied[b] || now[b] > applied[b];
        if (! changed)
            return;
        applied = now;
        const auto c = coefficients (sampleRate, now[0], now[1], now[2]);
        for (size_t b = 0; b < 3; ++b)
            filters[b].setCoefficients (c[b]);
    }

    bool isFlatTarget() const noexcept
    {
        return std::all_of (gains.begin(), gains.end(), [] (const OnePoleSmoother& s) { return ! (std::abs (s.getTarget()) > 0.0f); });
    }

    bool isGliding() const noexcept
    {
        return std::any_of (gains.begin(), gains.end(), [] (const OnePoleSmoother& s) { return s.isSmoothing(); });
    }

    /** How long flat filters run before they stop: their state has gone by then. */
    int idleAfter() const noexcept { return static_cast<int> (sampleRate * 0.1); }

    double sampleRate = 48000.0;
    std::array<OnePoleSmoother, 3> gains;
    std::array<Biquad, 3> filters;
    std::array<float, 3> applied { 0.0f, 0.0f, 0.0f };
    int idleSamples = 1 << 30;
};

//==============================================================================
/** Low cut and high cut (12 dB/octave), e.g. after a cabinet IR. Each is off
    at the end of its range (low cut fully down, high cut fully up); switching
    one on or off fades over 20 ms, and the cutoff glides. */
class CutFilters
{
public:
    static constexpr float lowCutMinHz = 20.0f, lowCutMaxHz = 500.0f;
    static constexpr float highCutMinHz = 1500.0f, highCutMaxHz = 20000.0f;

    // At the end of its range (within a hair, as a host's value can come back)
    static bool lowCutOff (float hz) noexcept  { return hz < lowCutMinHz + 0.5f; }
    static bool highCutOff (float hz) noexcept { return hz > highCutMaxHz - 1.0f; }

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        for (auto* s : { &low, &high })
        {
            s->logHz.prepare (sampleRate, 0.02);
            s->wet.prepare (sampleRate, 0.02);
            s->appliedLogHz = -1.0f;
        }
        reset();
    }

    void reset() noexcept
    {
        for (auto* s : { &low, &high })
        {
            s->filter.reset();
            s->logHz.reset (s->logHz.getTarget());
            s->wet.reset (s->wet.getTarget());
        }
    }

    /** Call once per block. After prepare(), set the cutoffs then reset() to
        start on them without a fade. */
    void setCutoffs (float lowCutHz, float highCutHz) noexcept
    {
        low.set (! lowCutOff (lowCutHz), std::clamp (lowCutHz, lowCutMinHz, lowCutMaxHz));
        high.set (! highCutOff (highCutHz), std::clamp (highCutHz, highCutMinHz, highCutMaxHz));
    }

    void process (float* x, int n) noexcept
    {
        low.process (x, n, sampleRate, true);
        high.process (x, n, sampleRate, false);
    }

    /** The response in dB at a frequency, with both stages fully on or off as
        their settings say (for tests and GUIs). */
    static double responseDb (float lowCutHz, float highCutHz, double freq, double sampleRate = 48000.0) noexcept
    {
        double m = 1.0;
        if (! lowCutOff (lowCutHz))
            m *= BiquadCoeffs::highPass (sampleRate, std::clamp (lowCutHz, lowCutMinHz, lowCutMaxHz)).magnitudeAt (sampleRate, freq);
        if (! highCutOff (highCutHz))
            m *= BiquadCoeffs::lowPass (sampleRate, std::min (static_cast<double> (std::clamp (highCutHz, highCutMinHz, highCutMaxHz)), sampleRate * 0.45))
                     .magnitudeAt (sampleRate, freq);
        return 20.0 * std::log10 (m);
    }

private:
    static constexpr int updateInterval = 16;

    struct Stage
    {
        Biquad filter;
        OnePoleSmoother logHz;    // the cutoff, smoothed as heard (log)
        LinearSmoother wet;       // 0: off, 1: fully filtered
        float appliedLogHz = -1.0f;

        void set (bool on, float hz) noexcept
        {
            const float target = std::log (hz);
            // Switching on starts at the new cutoff, rather than sweeping from the old one
            if (on && ! (wet.getTarget() > 0.0f) && ! wet.isSmoothing())
                logHz.reset (target);
            else
                logHz.setTarget (target);
            wet.setTarget (on ? 1.0f : 0.0f);
        }

        void process (float* x, int n, double sr, bool isLowCut) noexcept
        {
            if (! wet.isSmoothing() && ! (wet.getCurrent() > 0.0f))
            {
                filter.reset();   // off: ready to start clean
                return;
            }

            for (int start = 0; start < n; start += updateInterval)
            {
                const int len = std::min (updateInterval, n - start);
                float lh = logHz.getCurrent();
                for (int i = 0; i < len; ++i)
                    lh = logHz.next();
                if (lh < appliedLogHz || lh > appliedLogHz)
                {
                    appliedLogHz = lh;
                    const double hz = std::min (static_cast<double> (std::exp (lh)), sr * 0.45);
                    filter.setCoefficients (isLowCut ? BiquadCoeffs::highPass (sr, hz) : BiquadCoeffs::lowPass (sr, hz));
                }

                float* p = x + start;
                if (! wet.isSmoothing() && wet.getCurrent() >= 1.0f)
                    filter.processBlock (p, len);
                else
                    for (int i = 0; i < len; ++i)
                    {
                        const float dry = p[i];
                        const float w = wet.next();
                        p[i] = dry + w * (filter.processSample (dry) - dry);
                    }
            }
        }
    };

    double sampleRate = 48000.0;
    Stage low, high;
};

} // namespace fx
