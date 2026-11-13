#pragma once

#include "AmpModel.h"
#include "fx/DelayLine.h"
#include "fx/Handoff.h"
#include "fx/Pedal.h"
#include "fx/Smoother.h"

#include <atomic>

/** A NAM capture in a pedal slot (a capture of a real pedal, say): Input
    drives it, Level sets what comes out, Mix blends it with the dry signal.
    One model per channel it runs on (1 in the pre-amp row, 2 in the loop).
    It lives in the plugin rather than fx-core, since it needs NAM.

    The models are loaded off the audio thread and handed over with
    setCaptures(). The dry signal is delayed to match a capture's resampling
    latency, so Mix and bypass never comb-filter and the latency doesn't
    change when you stomp. With no capture it passes the signal through. */
class CapturePedal final : public fx::BasicPedal
{
public:
    enum Index { input, level, mix };
    static const fx::PedalInfo& staticInfo();

    CapturePedal() : BasicPedal (staticInfo()) {}

    struct Captures
    {
        std::vector<std::unique_ptr<rig::AmpModel>> models;   // one per channel
        int latency = 0;
    };

    /** Message thread. Empty captures clear it. The latency changes with the
        capture: re-publish the chain's layout afterwards so it's counted. */
    void setCaptures (std::unique_ptr<Captures>);
    bool hasCapture() const noexcept { return loaded.load(); }

    /** Message thread, now and then: frees what the audio thread let go of. */
    void collect() { handoff.collect(); }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override;
    void reset() override;
    void process (float* const* channels, int numChannels, int numSamples) override;
    int getLatencySamples() const override { return latency.load(); }

    static constexpr int maxLatency = 4096;   // resampling latency the dry path can match

private:
    fx::Handoff<Captures> handoff;
    std::atomic<int> latency { 0 };
    std::atomic<bool> loaded { false };
    double sampleRate = 48000.0;
    int maxBlock = 512;
    bool primed = false;

    fx::LinearSmoother inputGain { 1.0f }, levelGain { 1.0f }, wet { 1.0f };
    std::vector<fx::DelayLine> dryLines;
    std::vector<std::vector<float>> dry;
};
