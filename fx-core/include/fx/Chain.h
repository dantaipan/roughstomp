#pragma once

#include "fx/Handoff.h"
#include "fx/Pedal.h"

#include <memory>
#include <span>
#include <vector>

namespace fx
{

/** A series of pedal slots that can be rearranged while audio runs.

    Threads:
    - Message thread: prepare(), setLayout(), getLatencySamples(),
      collectGarbage(). setLayout() builds and prepares the new layout,
      then hands it to the audio thread (see Handoff).
    - Audio thread: beginBlock() picks up the newest layout, then push
      parameters into its pedals, then process().

    Call collectGarbage() regularly (a 10-30 Hz timer is plenty); until it
    runs, a later layout waits instead of being applied.

    Slots may be empty (nullptr), so slot numbers stay put: entry i of
    beginBlock() is always slot i.

    Pedals are shared between layouts, so moving or adding a pedal keeps
    the others running untouched (no reset, no click). A moved pedal keeps
    its state, e.g. a delay keeps its echoes. */
class Chain
{
public:
    static constexpr int maxSlots = 16;

    using Slots = std::vector<std::shared_ptr<Pedal>>;

    // Message thread ----------------------------------------------------------

    /** Prepares every pedal in the newest layout. Like the host's
        prepareToPlay, call it only while process() isn't running. */
    void prepare (double sampleRate, int maxBlockSize, int numChannels);

    /** Replaces the layout. Pedals not in any earlier layout are prepared
        here (if prepare() has run). Slots past maxSlots are dropped. */
    void setLayout (Slots slots);

    /** The newest layout, as last passed to setLayout(). */
    const Slots& getLayout() const noexcept { return latest; }

    /** Sum of the pedals' latencies in the newest layout. Bypassed pedals
        count too, since they keep their latency. Valid once prepare() has
        run: some pedals only know their latency after preparing. */
    int getLatencySamples() const noexcept { return latency; }

    /** Deletes layouts the audio thread has finished with. */
    void collectGarbage();

    // Audio thread ------------------------------------------------------------

    /** Switches to the newest layout if one is waiting, and returns its
        slots, in order. Empty slots are nullptr. */
    std::span<Pedal* const> beginBlock() noexcept;

    /** Runs the pedals from the last beginBlock(), in place. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

private:
    struct Layout
    {
        Slots owners;              // keeps pedals alive
        std::vector<Pedal*> order; // what the audio thread walks
    };

    bool isPrepared (const std::shared_ptr<Pedal>&) const;
    void sumLatency();

    Handoff<Layout> layouts;

    Slots latest;
    std::vector<std::weak_ptr<Pedal>> preparedPedals; // prepared at the current settings and still alive
    int latency = 0;
    bool prepared = false;
    double sampleRate = 0.0;
    int maxBlock = 0, channels = 0;
};

} // namespace fx
