#pragma once

#include "fx/Effect.h"

#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace fx
{

/** One knob or switch on a pedal, in the units the user sees. */
struct ParamInfo
{
    enum class Kind { continuous, toggle, choice };

    std::string_view id;        // stable once released: saved chains depend on it
    std::string_view name;
    float minValue = 0.0f, maxValue = 1.0f, defaultValue = 0.0f;
    std::string_view unit {};   // "dB", "%", or empty
    Kind kind = Kind::continuous;
    std::vector<std::string_view> choices {};   // for Kind::choice: value is the index
};

/** What a pedal is called and which controls it has. */
struct PedalInfo
{
    std::string_view id;        // stable once released, e.g. "overdrive"
    std::string_view name;      // shown to the user
    std::vector<ParamInfo> params;

    /** Index of a parameter by id, or -1. */
    int indexOf (std::string_view paramId) const noexcept;
};

/** An effect that can sit in a pedalboard slot.

    Hosts (the pedalboard plugin, RoughTrack) know pedals only through this
    interface: they read info() to build controls and push values in with
    setParameter() at the start of each block, on the audio thread.
    Values are plain (e.g. dB, 0..10), not normalised. */
class Pedal : public Effect
{
public:
    virtual const PedalInfo& info() const noexcept = 0;

    /** Audio thread, start of a block. Out-of-range values are clamped. */
    virtual void setParameter (int index, float value) noexcept = 0;

    /** Click-free bypass. Latency stays the same when bypassed, so a chain's
        total latency never changes while you stomp. */
    virtual void setBypassed (bool shouldBeBypassed) noexcept = 0;
};

/** Every pedal fx-core knows how to build, by id. */
namespace registry
{
    std::span<const PedalInfo* const> all() noexcept;
    const PedalInfo* find (std::string_view id) noexcept;

    /** Message thread only (allocates). Returns nullptr for an unknown id.
        The pedal starts on its default parameter values. */
    std::unique_ptr<Pedal> create (std::string_view id);
}

} // namespace fx
