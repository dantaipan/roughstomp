#include "fx/Oversampler.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numbers>

namespace fx
{

namespace
{
/** Zeroth-order modified Bessel function of the first kind, for the Kaiser window. */
double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    const double halfX = x / 2.0;

    for (int k = 1; k < 50; ++k)
    {
        const double t = halfX / k;
        term *= t * t;
        sum += term;
        if (term < sum * 1.0e-12)
            break;
    }
    return sum;
}

/** Fixed-size history where the last `size` values are always contiguous:
    at(i) is the value pushed i samples ago. */
class History
{
public:
    void resize (int newSize)
    {
        size = newSize;
        data.assign (static_cast<size_t> (2 * size), 0.0f);
        pos = 0;
    }

    void clear() noexcept { std::fill (data.begin(), data.end(), 0.0f); pos = 0; }

    void push (float x) noexcept
    {
        pos = (pos == 0 ? size : pos) - 1;
        data[static_cast<size_t> (pos)] = x;
        data[static_cast<size_t> (pos + size)] = x;
    }

    const float* window() const noexcept { return data.data() + pos; }

private:
    std::vector<float> data;
    int size = 1, pos = 0;
};
} // namespace

//==============================================================================
/* One 2x stage. The halfband prototype h has N = 4K + 3 taps centred on
   c = 2K + 1. Every other tap is zero except the centre (0.5), which gives
   the polyphase split used below:

     upsample   y[2n]   = sum_i 2 h[2i] x[n - i]
                y[2n+1] = x[n - K]
     downsample v[n]    = sum_i h[2i] s[2(n - i)] + 0.5 s[2(n - K - 1) + 1]

   so each output costs M = 2K + 2 multiplies instead of N. */
struct Oversampler::Stage
{
    int K = 0;
    std::vector<float> taps;                     // h[2i], i = 0 .. M-1
    std::vector<History> upHist, downEven, downOdd;
    std::vector<std::vector<float>> buffer;      // output at this stage's (higher) rate

    Stage (int halfLength, double kaiserBeta, int numChannels, int maxOutputSamples)
        : K (halfLength)
    {
        const int N = 4 * K + 3;
        const int M = 2 * K + 2;
        const double c = 2 * K + 1;

        taps.resize (static_cast<size_t> (M));
        double sum = 0.0;

        for (int i = 0; i < M; ++i)
        {
            const int    j    = 2 * i;
            const double x    = (j - c) / 2.0;                               // half-integer, never 0
            const double sinc = std::sin (std::numbers::pi * x) / (std::numbers::pi * x);
            const double r    = 2.0 * j / (N - 1) - 1.0;
            const double win  = besselI0 (kaiserBeta * std::sqrt (std::max (0.0, 1.0 - r * r)))
                              / besselI0 (kaiserBeta);
            const double h    = 0.5 * sinc * win;
            taps[static_cast<size_t> (i)] = static_cast<float> (h);
            sum += h;
        }

        // Both polyphase branches must have exactly 0.5 DC gain, or a tone at
        // the new Nyquist leaks through. Normalise the even taps to sum to 0.5.
        for (auto& t : taps)
            t = static_cast<float> (t * (0.5 / sum));

        upHist.resize (static_cast<size_t> (numChannels));
        downEven.resize (static_cast<size_t> (numChannels));
        downOdd.resize (static_cast<size_t> (numChannels));

        for (int ch = 0; ch < numChannels; ++ch)
        {
            upHist[static_cast<size_t> (ch)].resize (M);
            downEven[static_cast<size_t> (ch)].resize (M);
            downOdd[static_cast<size_t> (ch)].resize (M);
        }

        buffer.assign (static_cast<size_t> (numChannels),
                       std::vector<float> (static_cast<size_t> (maxOutputSamples), 0.0f));
    }

    void reset() noexcept
    {
        for (auto& h : upHist)   h.clear();
        for (auto& h : downEven) h.clear();
        for (auto& h : downOdd)  h.clear();
    }

    float dot (const float* x) const noexcept
    {
        float acc = 0.0f;
        for (size_t i = 0; i < taps.size(); ++i)
            acc += taps[i] * x[i];
        return acc;
    }

    void up (int ch, const float* in, float* out, int numIn) noexcept
    {
        auto& hist = upHist[static_cast<size_t> (ch)];

        for (int n = 0; n < numIn; ++n)
        {
            hist.push (in[n]);
            out[2 * n]     = 2.0f * dot (hist.window());
            out[2 * n + 1] = hist.window()[K];
        }
    }

    void down (int ch, const float* in, float* out, int numOut) noexcept
    {
        auto& even = downEven[static_cast<size_t> (ch)];
        auto& odd  = downOdd[static_cast<size_t> (ch)];

        for (int n = 0; n < numOut; ++n)
        {
            even.push (in[2 * n]);
            odd.push (in[2 * n + 1]);
            out[n] = dot (even.window()) + 0.5f * odd.window()[K + 1];
        }
    }

    /** Round-trip delay of this stage, in samples at its input (lower) rate. */
    double roundTripLatency() const noexcept { return 2 * K + 1; }
};

//==============================================================================
Oversampler::Oversampler() = default;
Oversampler::~Oversampler() = default;

void Oversampler::prepare (int numChannels, int maxBlockSize, int factorLog2)
{
    assert (factorLog2 >= 1 && factorLog2 <= 3);
    factorLog2 = std::clamp (factorLog2, 1, 3);

    numStages = factorLog2;
    stages.clear();
    stagePointers.clear();

    for (int s = 0; s < factorLog2; ++s)
    {
        // The first stage does the real anti-aliasing work and needs a sharp
        // transition just above 20 kHz. Later stages only have to reject
        // content that is already far above the audio band, so they're shorter.
        const int halfLength = (s == 0) ? 15 : 7;    // 63 or 31 taps
        stages.emplace_back (halfLength, 8.0, numChannels, maxBlockSize << (s + 1));

        auto& ptrs = stagePointers.emplace_back();
        for (auto& ch : stages.back().buffer)
            ptrs.push_back (ch.data());
    }
}

void Oversampler::reset() noexcept
{
    for (auto& s : stages)
        s.reset();
}

float* const* Oversampler::upsample (const float* const* input, int numChannels, int numSamples) noexcept
{
    int n = numSamples;

    for (size_t s = 0; s < stages.size(); ++s)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* in = (s == 0) ? input[ch] : stagePointers[s - 1][static_cast<size_t> (ch)];
            stages[s].up (ch, in, stagePointers[s][static_cast<size_t> (ch)], n);
        }
        n *= 2;
    }

    return stagePointers.back().data();
}

void Oversampler::downsample (float* const* output, int numChannels, int numSamples) noexcept
{
    for (size_t s = stages.size(); s-- > 0;)
    {
        const int numOut = numSamples << s;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* out = (s == 0) ? output[ch] : stagePointers[s - 1][static_cast<size_t> (ch)];
            stages[s].down (ch, stagePointers[s][static_cast<size_t> (ch)], out, numOut);
        }
    }
}

double Oversampler::getLatencySamplesExact() const noexcept
{
    double latency = 0.0;
    double rate = 1.0; // stage input rate relative to the base rate

    for (const auto& s : stages)
    {
        latency += s.roundTripLatency() / rate;
        rate *= 2.0;
    }
    return latency;
}

int Oversampler::getLatencySamples() const noexcept
{
    return static_cast<int> (std::lround (getLatencySamplesExact()));
}

} // namespace fx
