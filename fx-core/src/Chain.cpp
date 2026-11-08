#include "fx/Chain.h"

#include <algorithm>

namespace fx
{

void Chain::prepare (double newSampleRate, int maxBlockSize, int numChannels)
{
    // Audio isn't running, so we may take the waiting layout ourselves
    auto& layout = layouts.takeOver();

    sampleRate = newSampleRate;
    maxBlock = maxBlockSize;
    channels = numChannels;
    prepared = true;

    preparedPedals.clear();
    for (const auto& pedal : layout.owners)
        if (pedal != nullptr)
        {
            pedal->prepare (sampleRate, maxBlock, channels);
            preparedPedals.push_back (pedal);
        }

    sumLatency();   // some pedals only know theirs once prepared
}

bool Chain::isPrepared (const std::shared_ptr<Pedal>& pedal) const
{
    return std::any_of (preparedPedals.begin(), preparedPedals.end(),
                        [&] (const auto& w) { return w.lock() == pedal; });
}

void Chain::sumLatency()
{
    latency = 0;
    for (const auto& pedal : latest)
        if (pedal != nullptr)
            latency += pedal->getLatencySamples();
}

void Chain::setLayout (Slots slots)
{
    if (slots.size() > static_cast<size_t> (maxSlots))
        slots.resize (static_cast<size_t> (maxSlots));

    // Pedals still alive from any earlier layout may be running on the audio
    // thread right now, so only fresh ones get prepared
    if (prepared)
        for (const auto& pedal : slots)
            if (pedal != nullptr && ! isPrepared (pedal))
            {
                pedal->prepare (sampleRate, maxBlock, channels);
                preparedPedals.push_back (pedal);
            }

    auto layout = std::make_unique<Layout>();
    layout->owners = slots;
    for (const auto& pedal : slots)
        layout->order.push_back (pedal.get());

    latest = std::move (slots);
    sumLatency();

    layouts.publish (std::move (layout));
    collectGarbage();
}

void Chain::collectGarbage()
{
    layouts.collect();
    std::erase_if (preparedPedals, [] (const auto& w) { return w.expired(); });
}

std::span<Pedal* const> Chain::beginBlock() noexcept
{
    return layouts.acquire().order;
}

void Chain::process (float* const* chans, int numChannels, int numSamples) noexcept
{
    for (auto* pedal : layouts.get().order)
        if (pedal != nullptr)
            pedal->process (chans, numChannels, numSamples);
}

} // namespace fx
