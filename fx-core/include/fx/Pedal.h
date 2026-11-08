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

    /** How a 0..1 control (a host parameter, a knob's travel) maps onto the
        range. log suits frequencies and times; it needs minValue > 0. */
    enum class Taper { linear, log };

    std::string_view id;        // stable once released: saved chains depend on it
    std::string_view name;
    float minValue = 0.0f, maxValue = 1.0f, defaultValue = 0.0f;
    std::string_view unit {};   // "dB", "%", or empty
    Kind kind = Kind::continuous;
    std::vector<std::string_view> choices {};   // for Kind::choice: value is the index
    Taper taper = Taper::linear;

    /** 0..1 to plain, rounded for toggles and choices. */
    float fromNormalised (float normalised) const noexcept;
    float toNormalised (float plain) const noexcept;
};

/** What a pedal is called and which controls it has. */
struct PedalInfo
{
    /** How a GUI should lay the controls out. graphicEq: every continuous
        control is a vertical slider, the first ones being fx::graphicEq's
        bands, so the sliders draw the curve. */
    enum class Style { knobs, graphicEq };

    std::string_view id;        // stable once released, e.g. "overdrive"
    std::string_view name;      // shown to the user
    std::vector<ParamInfo> params;
    Style style = Style::knobs;

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
        total latency never changes while you stomp. Delays and reverbs let
        their tails ring out. */
    virtual void setBypassed (bool shouldBeBypassed) noexcept = 0;

    /** Audio thread, start of a block: the host's tempo, for synced times. */
    virtual void setTempo (double /*bpm*/) noexcept {}

    /** Audio thread, start of a block: where the host's playhead is, in
        quarter notes, so synced modulation lines up with the beat. */
    virtual void setPlayPosition (double /*quarterNotes*/, bool /*isPlaying*/) noexcept {}
};

/** A Pedal that keeps its parameter values (plain, clamped) for its
    process() to read. Starts on the defaults. */
class BasicPedal : public Pedal
{
public:
    explicit BasicPedal (const PedalInfo&);

    const PedalInfo& info() const noexcept override { return pedalInfo; }
    void setParameter (int index, float value) noexcept override;
    void setBypassed (bool b) noexcept override { bypassed = b; }
    void setTempo (double bpm) noexcept override { if (bpm > 0.0) tempo = bpm; }
    void setPlayPosition (double quarterNotes, bool isPlaying) noexcept override
    {
        position = quarterNotes;
        playing = isPlaying;
    }

protected:
    float value (int index) const noexcept { return values[static_cast<size_t> (index)]; }
    int choice (int index) const noexcept { return static_cast<int> (value (index) + 0.5f); }
    bool isBypassed() const noexcept { return bypassed; }
    double getTempo() const noexcept { return tempo; }
    bool isHostPlaying() const noexcept { return playing; }
    double getPlayPosition() const noexcept { return position; }   // quarter notes

private:
    const PedalInfo& pedalInfo;
    std::vector<float> values;
    bool bypassed = false;
    double tempo = 120.0;
    double position = 0.0;
    bool playing = false;
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
