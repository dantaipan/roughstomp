#pragma once

namespace fx
{

/** Common interface for every effect in fx-core.

    Rules for implementers:
    - prepare() runs on a non-audio thread; allocate everything here.
    - process() runs on the audio thread: no allocation, locks, I/O or logging.
    - Effects know nothing about JUCE plugins, parameters trees or GUIs. The
      wrapper (plugin or RoughTrack) pushes parameter values in each block.
*/
struct Effect
{
    virtual ~Effect() = default;

    virtual void prepare (double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void reset() = 0;
    virtual void process (float* const* channels, int numChannels, int numSamples) = 0;

    virtual int getLatencySamples() const { return 0; }
};

} // namespace fx
