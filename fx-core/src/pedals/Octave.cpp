#include "LoopPedals.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fx
{

namespace
{
// The bands: quarter octaves from 55 Hz, two complex one-poles each
constexpr double lowestBandHz = 55.0;
constexpr double bandWidth = 0.26;       // each pole's, of the band's frequency: two in a row make about a quarter octave

// A guitar's low notes have weak fundamentals under loud harmonics. The
// octaves are tilted towards the lows (3 dB an octave from here) so the
// octave of a low note has weight, like a bass
constexpr double tiltFromHz = 500.0;

// Gates: a band's own level (about -66 to -54 dBFS), and its level against
// the loudest band's (so another note's skirt, or noise under a note, stays out)
constexpr float gateFloor = 0.0005f, gateOpen = 0.002f;
constexpr float relFloor = 0.03f, relOpen = 0.12f;

// Vintage: a steep tracking filter (4-pole at 160 Hz), so a low string's
// fundamental makes the zero crossings even under louder harmonics; the
// trigger works relative to its envelope (which falls within a few ms after a
// loud note, so the next one triggers at once), so higher notes, attenuated by
// the filter, still track. The subs are the flip-flops' squares, shaped by the
// low end's envelope and smoothed. (Cutoff, hysteresis and release chosen by
// sweeping them over a guitar take with fast passages: fewest frames out of tune)
constexpr double trackHighPassHz = 40.0, trackLowPassHz = 160.0;
constexpr float hysteresis = 0.25f;
constexpr double trackReleaseSeconds = 0.005;
constexpr double envHighPassHz = 50.0, envLowPassHz = 6000.0;   // the whole guitar's envelope shapes the sub, so every pick shows
constexpr double subReleaseSeconds = 0.002;                      // and it falls between notes (smooth enough to be this quick: no ripple from the analytic signal)

// The phase splitter's two all-pass chains (Olli Niemitalo's coefficients,
// squared): their outputs are 90 degrees apart across the guitar's range
constexpr std::array<float, 4> splitA { 0.4794008f, 0.8762184f, 0.9765975f, 0.9974999f };
constexpr std::array<float, 4> splitB { 0.1617584f, 0.7330289f, 0.9453497f, 0.9905991f };
constexpr double subSmoothHz = 2000.0;   // takes the edge off the squares; Tone does the rest
constexpr double upInputHz = 300.0, upHighPassHz = 80.0, upSmoothHz = 2500.0;   // the rectifier octave-up
constexpr float vintageGate = 0.0015f, vintageOpen = 0.006f;

// The bands respond faster when wider: the low ones get at least this (per pole)
constexpr double minBandWidthHz = 30.0;

// Tone: a 2-pole low-pass on the octaves down, from 120 Hz (a round, bass-like
// octave) to 4 kHz (the whole shifted spectrum), on a log scale
constexpr double toneLowHz = 120.0, toneHighHz = 4000.0;

// The octaves down get a low shelf: at 40-80 Hz the ear needs more level to
// hear the same loudness, so equal energy would sound quiet beside the guitar
constexpr double subShelfHz = 100.0, subShelfDb = 6.0;

// Levels, fitted with rs-render on a guitar take: 100% of an octave down
// about 4 dB above the dry note (shelf included), the octave up level with it
constexpr float vintageSub1 = 0.76f, vintageSub2 = 0.63f, vintageUp = 3.0f;
constexpr float polySub1 = 1.77f, polySub2 = 1.42f, polyUp = 1.88f;

/** The principal square root of (re, im). */
inline void complexSqrt (float re, float im, float& outRe, float& outIm) noexcept
{
    const float mag = std::sqrt (re * re + im * im);
    outRe = std::sqrt (std::max (0.0f, 0.5f * (mag + re)));
    const float i = std::sqrt (std::max (0.0f, 0.5f * (mag - re)));
    outIm = im < 0.0f ? -i : i;
}

/** Keeps a square root continuous: the sign nearer the last one. */
inline void continueRoot (float& r, float& i, float& lastR, float& lastI) noexcept
{
    if (r * lastR + i * lastI < 0.0f)
    {
        r = -r;
        i = -i;
    }
    lastR = r;
    lastI = i;
}
} // namespace

const PedalInfo& OctavePedal::staticInfo()
{
    using K = ParamInfo::Kind;
    using T = ParamInfo::Taper;
    static const PedalInfo i { "octave", "Octave", {
        { "direct", "Direct", 0.0f, 100.0f, 100.0f, "%" },
        { "sub1",   "Oct -1", 0.0f, 100.0f, 60.0f,  "%" },
        { "sub2",   "Oct -2", 0.0f, 100.0f, 0.0f,   "%" },
        { "up",     "Oct Up", 0.0f, 100.0f, 0.0f,   "%" },
        { "range",  "Range",  100.0f, 2000.0f, 2000.0f, "Hz", K::continuous, {}, T::log },
        { "mode",   "Mode",   0.0f, 1.0f, 1.0f, {}, K::choice, { "Vintage", "Poly" } },
        { "tone",   "Tone",   0.0f, 10.0f, 4.0f },
    } };
    return i;
}

void OctavePedal::prepare (double newSampleRate, int, int numChannels)
{
    sampleRate = newSampleRate;

    for (int k = 0; k < numBands; ++k)
    {
        const auto i = static_cast<size_t> (k);
        const double f = std::min (lowestBandHz * std::exp2 (k / 4.0), sampleRate * 0.4);
        const double r = std::exp (-std::numbers::pi * std::max (bandWidth * f, minBandWidthHz) / sampleRate);
        const double w = 2.0 * std::numbers::pi * f / sampleRate;
        poleRe[i] = r * std::cos (w);
        poleIm[i] = r * std::sin (w);
        inputScale[i] = 1.0 - r;   // unity gain at the centre, per pole
        tilt[i] = static_cast<float> (std::sqrt (tiltFromHz / f));
    }

    channels.assign (static_cast<size_t> (numChannels), {});
    for (auto& c : channels)
    {
        c.polyUpLowPass.setCoefficients (BiquadCoeffs::lowPass (sampleRate, std::min (4500.0, sampleRate * 0.4), 0.707));
        c.subShelf.setCoefficients (BiquadCoeffs::lowShelf (sampleRate, subShelfHz, 0.7, subShelfDb));
        c.subTone.reset();
        c.trackHighPass.setup (OnePole::Mode::highPass, sampleRate, trackHighPassHz);
        c.track1.setCoefficients (BiquadCoeffs::lowPass (sampleRate, trackLowPassHz, 0.5412));   // 4-pole Butterworth
        c.track2.setCoefficients (BiquadCoeffs::lowPass (sampleRate, trackLowPassHz, 1.3066));
        c.envHighPass.setup (OnePole::Mode::highPass, sampleRate, envHighPassHz);
        c.envLowPass.setup (OnePole::Mode::lowPass, sampleRate, envLowPassHz);
        c.sub1Smooth.setCoefficients (BiquadCoeffs::lowPass (sampleRate, subSmoothHz, 0.6));
        c.sub2Smooth.setCoefficients (BiquadCoeffs::lowPass (sampleRate, subSmoothHz, 0.6));
        c.upInput.setup (OnePole::Mode::lowPass, sampleRate, upInputHz);
        c.upHighPass.setup (OnePole::Mode::highPass, sampleRate, upHighPassHz);
        c.upSmooth.setup (OnePole::Mode::lowPass, sampleRate, upSmoothHz);
        c.upDc.setup (sampleRate, 15.0);
        c.reset();
    }

    turnSmoothing = 1.0 - std::exp (-1.0 / (0.004 * sampleRate));
    envAttack = 1.0f - std::exp (-1.0f / static_cast<float> (0.001 * sampleRate));
    envRelease = 1.0f - std::exp (-1.0f / static_cast<float> (0.05 * sampleRate));
    trackRelease = 1.0f - std::exp (-1.0f / static_cast<float> (trackReleaseSeconds * sampleRate));
    subRelease = 1.0f - std::exp (-1.0f / static_cast<float> (subReleaseSeconds * sampleRate));
    weightedRange = -1.0f;

    for (auto* s : { &directGain, &sub1Gain, &sub2Gain, &upGain, &polyAmount })
        s->prepare (sampleRate, 0.02);
    toneHz.prepare (sampleRate, 0.03);
    lastToneHz = -1.0f;
    primed = false;
}

void OctavePedal::reset()
{
    for (auto& c : channels)
        c.reset();
}

void OctavePedal::Channel::reset() noexcept
{
    for (auto* a : { &re, &im, &re1, &im1, &turnIm })
        a->fill (0.0);
    turnRe.fill (1.0);
    w1r.fill (1.0f);
    w1i.fill (0.0f);
    w2r.fill (1.0f);
    w2i.fill (0.0f);
    polyUpLowPass.reset();
    subTone.reset();
    subShelf.reset();
    for (auto* f : { &trackHighPass, &envHighPass, &envLowPass, &upInput, &upHighPass, &upSmooth })
        f->reset();
    for (auto* f : { &track1, &track2, &sub1Smooth, &sub2Smooth })
        f->reset();
    upDc.reset();
    for (auto* a : { &hx1, &hx2, &hy1, &hy2, &gx1, &gx2, &gy1, &gy2 })
        a->fill (0.0f);
    gDelay = 0.0f;
    trackEnvelope = envelope = 0.0f;
    armed = ff1 = ff2 = false;
}

OctavePedal::Voices OctavePedal::runPoly (Channel& c, float x) noexcept
{
    constexpr auto n = static_cast<size_t> (numBands);
    std::array<float, n> mag {};
    float loudest = 0.0f;
    Voices v;

    // The bands' analytic signals, and how fast each turns (smoothed)
    for (size_t k = 0; k < n; ++k)
    {
        const double ar = poleRe[k] * c.re1[k] - poleIm[k] * c.im1[k] + inputScale[k] * x;
        const double ai = poleRe[k] * c.im1[k] + poleIm[k] * c.re1[k];
        c.re1[k] = ar;
        c.im1[k] = ai;
        const double lastRe = c.re[k], lastIm = c.im[k];
        const double zr = poleRe[k] * lastRe - poleIm[k] * lastIm + inputScale[k] * ar;
        const double zi = poleRe[k] * lastIm + poleIm[k] * lastRe + inputScale[k] * ai;
        c.re[k] = zr;
        c.im[k] = zi;

        c.turnRe[k] += turnSmoothing * ((zr * lastRe + zi * lastIm) - c.turnRe[k]);
        c.turnIm[k] += turnSmoothing * ((zi * lastRe - zr * lastIm) - c.turnIm[k]);

        mag[k] = static_cast<float> (std::sqrt (zr * zr + zi * zi));
        loudest = std::max (loudest, mag[k]);
    }

    for (size_t k = 0; k < n; ++k)
    {
        // Gates only (smooth): every band that's part of what's playing is
        // shifted, so a note's whole spectrum moves down an octave, its
        // timbre intact, and nothing depends on deciding what's a note
        const float gate = std::clamp ((mag[k] - gateFloor) / (gateOpen - gateFloor), 0.0f, 1.0f)
                         * std::clamp ((mag[k] - relFloor * loudest) / ((relOpen - relFloor) * loudest + 1.0e-20f), 0.0f, 1.0f);

        // A band that's dropped out starts its square roots afresh, from the
        // same place as every other: when bands pick up the same note they
        // then choose the same sign, and add up
        if (gate <= 0.0f)
        {
            c.w1r[k] = c.w2r[k] = 1.0f;
            c.w1i[k] = c.w2i[k] = 0.0f;
            continue;
        }

        // Undo the band filter's own phase shift at the frequency it's ringing
        // at, so bands carrying the same note agree on its phase. Each pole is
        // (1-r)/(1 - pole e^{-jw}), whose phase is -arg of that denominator;
        // there are two, so turn by twice it
        auto re = static_cast<float> (c.re[k]), im = static_cast<float> (c.im[k]);
        {
            const double er = c.turnRe[k], ei = c.turnIm[k];
            const double emag = std::sqrt (er * er + ei * ei);
            if (emag > 1.0e-30)
            {
                const double dr = 1.0 - (poleRe[k] * er + poleIm[k] * ei) / emag;
                const double di = -(poleIm[k] * er - poleRe[k] * ei) / emag;
                const double dmag = std::sqrt (dr * dr + di * di);
                if (dmag > 1.0e-30)
                {
                    const double ur = dr / dmag, ui = di / dmag;
                    const double cr = ur * ur - ui * ui, ci = 2.0 * ur * ui;
                    re = static_cast<float> (c.re[k] * cr - c.im[k] * ci);
                    im = static_cast<float> (c.re[k] * ci + c.im[k] * cr);
                }
            }
        }

        // Octave up: the square, back to the band's level
        v.up += gate * (re * re - im * im) / mag[k];

        // Octaves down: square roots, each continuing the last, back to the
        // band's level (|z| Re(w)/|w| = |w| Re(w), as |w|^2 = |z|; |w|^1.5 Re(v)
        // for the fourth root v), tilted towards the lows
        float w1r, w1i, w2r, w2i;
        complexSqrt (re, im, w1r, w1i);
        continueRoot (w1r, w1i, c.w1r[k], c.w1i[k]);
        complexSqrt (w1r, w1i, w2r, w2i);
        continueRoot (w2r, w2i, c.w2r[k], c.w2i[k]);

        const float w1mag = std::sqrt (w1r * w1r + w1i * w1i);
        const float weight = rangeWeight[k] * gate * tilt[k];
        v.sub1 += weight * w1mag * w1r;
        v.sub2 += weight * w1mag * std::sqrt (w1mag) * w2r;
    }
    v.up = c.polyUpLowPass.processSample (v.up);
    return v;
}

OctavePedal::Voices OctavePedal::runVintage (Channel& c, float x) noexcept
{
    // The tracking filter, and its envelope (fast up, quick down) for the trigger
    const float t = c.track2.processSample (c.track1.processSample (c.trackHighPass.processSample (x)));
    const float tl = std::abs (t);
    c.trackEnvelope += (tl > c.trackEnvelope ? envAttack : trackRelease) * (tl - c.trackEnvelope);

    // The guitar's envelope: the subs' level, and the gate. From the analytic
    // signal (two all-pass chains 90 degrees apart), so it's smooth without
    // being slow: it follows each pick and falls between notes
    const float e = c.envLowPass.processSample (c.envHighPass.processSample (x));
    float a = e, b = e;
    for (size_t s = 0; s < 4; ++s)
    {
        const float ya = splitA[s] * (a + c.hy2[s]) - c.hx2[s];
        c.hx2[s] = c.hx1[s]; c.hx1[s] = a; c.hy2[s] = c.hy1[s]; c.hy1[s] = ya; a = ya;
        const float yb = splitB[s] * (b + c.gy2[s]) - c.gx2[s];
        c.gx2[s] = c.gx1[s]; c.gx1[s] = b; c.gy2[s] = c.gy1[s]; c.gy1[s] = yb; b = yb;
    }
    const float bDelayed = c.gDelay;
    c.gDelay = b;
    const float level = std::sqrt (a * a + bDelayed * bDelayed);
    c.envelope += (level > c.envelope ? envAttack : subRelease) * (level - c.envelope);
    const float open = std::clamp ((c.envelope - vintageGate) / (vintageOpen - vintageGate), 0.0f, 1.0f);

    // The Schmitt trigger: dipping below -h arms it, the next upward zero
    // crossing toggles the first flip-flop (whose rising edge toggles the
    // second). It answers within a cycle, as the analog ones do
    if (open > 0.0f)
    {
        if (t < -hysteresis * c.trackEnvelope)
            c.armed = true;
        else if (c.armed && t >= 0.0f)
        {
            c.armed = false;
            c.ff1 = ! c.ff1;
            if (c.ff1)
                c.ff2 = ! c.ff2;
        }
    }

    // The flip-flops' square waves, at half and a quarter of the pitch,
    // shaped by the envelope and smoothed into a round, solid sub
    const float amount = open * c.envelope;
    Voices v;
    v.sub1 = c.sub1Smooth.processSample (c.ff1 ? amount : -amount);
    v.sub2 = c.sub2Smooth.processSample (c.ff2 ? amount : -amount);
    // The octave up: the low end, full-wave rectified
    const float rectified = std::abs (c.upInput.processSample (e));
    v.up = c.upSmooth.processSample (c.upHighPass.processSample (c.upDc.processSample (open * rectified)));
    return v;
}

void OctavePedal::process (float* const* data, int numChannels, int numSamples)
{
    const bool bypassed = isBypassed();
    directGain.setTarget (bypassed ? 1.0f : value (direct) / 100.0f);   // bypass: the dry signal, faded to
    sub1Gain.setTarget (bypassed ? 0.0f : value (sub1) / 100.0f);
    sub2Gain.setTarget (bypassed ? 0.0f : value (sub2) / 100.0f);
    upGain.setTarget (bypassed ? 0.0f : value (up) / 100.0f);
    polyAmount.setTarget (static_cast<Mode> (choice (mode)) == Mode::poly ? 1.0f : 0.0f);
    toneHz.setTarget (static_cast<float> (toneLowHz * std::pow (toneHighHz / toneLowHz, value (tone) / 10.0)));
    if (! primed)
    {
        for (auto* s : { &directGain, &sub1Gain, &sub2Gain, &upGain, &polyAmount })
            s->reset (s->getTarget());
        toneHz.reset (toneHz.getTarget());
        primed = true;
    }

    // Tone: per block, from its smoother
    for (int i = 0; i < numSamples; ++i)
        toneHz.next();
    if (std::abs (toneHz.getCurrent() - lastToneHz) > 0.5f)
    {
        lastToneHz = toneHz.getCurrent();
        const auto k = BiquadCoeffs::lowPass (sampleRate, std::min (static_cast<double> (lastToneHz), sampleRate * 0.4), 0.6);
        for (auto& c : channels)
            c.subTone.setCoefficients (k);
    }

    // Range: bands above it fade out of the octave-down over half an octave
    if (const float r = value (range); ! (std::abs (r - weightedRange) < 0.01f))
    {
        weightedRange = r;
        for (int k = 0; k < numBands; ++k)
        {
            const double f = lowestBandHz * std::exp2 (k / 4.0);
            const double t = std::log2 (f / r) / 0.5;   // 0 at Range, 1 half an octave above
            rangeWeight[static_cast<size_t> (k)] = t <= 0.0 ? 1.0f
                                                 : t >= 1.0 ? 0.0f
                                                            : static_cast<float> (0.5 + 0.5 * std::cos (std::numbers::pi * t));
        }
    }

    numChannels = std::min (numChannels, static_cast<int> (channels.size()));
    for (int i = 0; i < numSamples; ++i)
    {
        const float d = directGain.next(), g1 = sub1Gain.next(), g2 = sub2Gain.next(), gu = upGain.next();
        const float p = polyAmount.next();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float x = data[ch][i];
            auto& c = channels[static_cast<size_t> (ch)];
            // Only the mode in use runs, except while switching (a 20 ms crossfade)
            const Voices poly = p > 0.0f ? runPoly (c, x) : Voices {};
            const Voices vintage = p < 1.0f ? runVintage (c, x) : Voices {};

            const float sub1 = p * polySub1 * poly.sub1 + (1.0f - p) * vintageSub1 * vintage.sub1;
            const float sub2 = p * polySub2 * poly.sub2 + (1.0f - p) * vintageSub2 * vintage.sub2;
            const float upV  = p * polyUp * poly.up + (1.0f - p) * vintageUp * vintage.up;
            data[ch][i] = d * x + c.subShelf.processSample (c.subTone.processSample (g1 * sub1 + g2 * sub2)) + gu * upV;
        }
    }
}

} // namespace fx
