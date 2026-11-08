#pragma once

#include <cmath>
#include <string_view>
#include <vector>

namespace fx
{

/** Finds the pitch of a monophonic signal (a guitar string) with the YIN
    algorithm. Not for the audio thread: detect() takes ~1 ms on a 4096-sample
    window, so a tuner runs it on the message thread a few times a second.
    prepare() allocates; detect() doesn't. */
class PitchDetector
{
public:
    struct Result
    {
        float hz = 0.0f;           // 0 when there's no clear pitch
        float confidence = 0.0f;   // 0..1: how periodic the signal is
    };

    static constexpr double lowestHz = 60.0;     // below a drop-C low string
    static constexpr double highestHz = 1400.0;  // above the 24th fret, top string

    /** windowSize: samples given to detect(); at least two periods of the
        lowest note. */
    void prepare (double sampleRate, int windowSize);

    int getWindowSize() const noexcept { return window; }

    /** samples: the most recent getWindowSize() samples. */
    Result detect (const float* samples) noexcept;

private:
    double sampleRate = 48000.0;
    int window = 4096, maxLag = 0, minLag = 0;
    std::vector<float> difference;   // cumulative-mean normalised
    std::vector<double> raw;         // the plain difference function
};

/** A frequency as the nearest note of equal temperament. */
struct Note
{
    int midi = 0;           // 69 = A4
    float cents = 0.0f;     // -50..50 off that note
    std::string_view name;  // "E", "F#", ...
    int octave = 0;

    static Note fromHz (double hz, double a4 = 440.0) noexcept;
};

} // namespace fx
