#include "fx/Pedal.h"

#include "fx/effects/Gain.h"
#include "fx/effects/Overdrive.h"
#include "pedals/LoopPedals.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace fx
{

int PedalInfo::indexOf (std::string_view paramId) const noexcept
{
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].id == paramId)
            return static_cast<int> (i);
    return -1;
}

float ParamInfo::fromNormalised (float normalised) const noexcept
{
    const float t = std::clamp (normalised, 0.0f, 1.0f);
    if (kind != Kind::continuous)
        return std::round (minValue + t * (maxValue - minValue));
    if (taper == Taper::log && minValue > 0.0f)
        return minValue * std::pow (maxValue / minValue, t);
    return minValue + t * (maxValue - minValue);
}

float ParamInfo::toNormalised (float plain) const noexcept
{
    if (! (maxValue > minValue))
        return 0.0f;
    const float v = std::clamp (plain, minValue, maxValue);
    if (kind == Kind::continuous && taper == Taper::log && minValue > 0.0f)
        return std::log (v / minValue) / std::log (maxValue / minValue);
    return (v - minValue) / (maxValue - minValue);
}

BasicPedal::BasicPedal (const PedalInfo& i) : pedalInfo (i)
{
    for (const auto& p : i.params)
        values.push_back (p.defaultValue);
}

void BasicPedal::setParameter (int index, float v) noexcept
{
    if (index < 0 || index >= static_cast<int> (values.size()))
        return;
    const auto& p = pedalInfo.params[static_cast<size_t> (index)];
    values[static_cast<size_t> (index)] = std::clamp (v, p.minValue, p.maxValue);
}

namespace
{
float clampTo (const ParamInfo& p, float v) noexcept { return std::clamp (v, p.minValue, p.maxValue); }

//==============================================================================
/** Overdrive with the same controls and ranges as the standalone plugin. */
class OverdrivePedal final : public Pedal
{
public:
    enum Index { drive, tone, level, mix, mode };

    static const PedalInfo& staticInfo()
    {
        using K = ParamInfo::Kind;
        static const PedalInfo i { "overdrive", "Overdrive", {
            { "drive", "Drive",  0.0f, 10.0f,  5.0f },
            { "tone",  "Tone",   0.0f, 10.0f,  5.0f },
            { "level", "Level", -24.0f, 12.0f, 0.0f, "dB" },
            { "mix",   "Mix",    0.0f, 100.0f, 100.0f, "%" },
            { "mode",  "Mode",   0.0f, 2.0f,   0.0f, {}, K::choice, { "Soft", "Hard", "Asymmetric" } },
        } };
        return i;
    }

    OverdrivePedal()
    {
        for (int k = 0; k < static_cast<int> (staticInfo().params.size()); ++k)
            setParameter (k, staticInfo().params[static_cast<size_t> (k)].defaultValue);
    }

    const PedalInfo& info() const noexcept override { return staticInfo(); }

    void setParameter (int index, float value) noexcept override
    {
        if (index < 0 || index >= static_cast<int> (staticInfo().params.size()))
            return;

        const float v = clampTo (staticInfo().params[static_cast<size_t> (index)], value);
        switch (index)
        {
            case drive: params.drive   = v / 10.0f; break;
            case tone:  params.tone    = v / 10.0f; break;
            case level: params.levelDb = v; break;
            case mix:   params.mix     = v / 100.0f; break;
            case mode:  params.mode    = static_cast<Overdrive::Mode> (static_cast<int> (v + 0.5f)); break;
            default: break;
        }
    }

    void setBypassed (bool b) noexcept override { params.bypassed = b; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        overdrive.setParameters (params);   // start on the current settings
        overdrive.prepare (sampleRate, maxBlockSize, numChannels);
    }

    void reset() override { overdrive.reset(); }

    void process (float* const* channels, int numChannels, int numSamples) override
    {
        overdrive.setParameters (params);
        overdrive.process (channels, numChannels, numSamples);
    }

    int getLatencySamples() const override { return overdrive.getLatencySamples(); }

private:
    Overdrive::Parameters params;
    Overdrive overdrive;
};

//==============================================================================
/** Clean boost: a smoothed level control. Bypassing ramps it to unity. */
class BoostPedal final : public Pedal
{
public:
    static const PedalInfo& staticInfo()
    {
        static const PedalInfo i { "boost", "Boost", {
            { "level", "Level", -12.0f, 24.0f, 6.0f, "dB" },
        } };
        return i;
    }

    const PedalInfo& info() const noexcept override { return staticInfo(); }

    void setParameter (int index, float value) noexcept override
    {
        if (index == 0)
            levelDb = clampTo (staticInfo().params[0], value);
    }

    void setBypassed (bool b) noexcept override { bypassed = b; }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        gain.setGainDecibels (bypassed ? 0.0f : levelDb);
        gain.prepare (sampleRate, maxBlockSize, numChannels);
    }

    void reset() override { gain.reset(); }

    void process (float* const* channels, int numChannels, int numSamples) override
    {
        gain.setGainDecibels (bypassed ? 0.0f : levelDb);
        gain.process (channels, numChannels, numSamples);
    }

private:
    float levelDb = 6.0f;
    bool bypassed = false;
    Gain gain;
};

//==============================================================================
struct Entry
{
    const PedalInfo& (*info)();
    std::unique_ptr<Pedal> (*make)();
};

template <typename P>
constexpr Entry entry() { return { &P::staticInfo, [] () -> std::unique_ptr<Pedal> { return std::make_unique<P>(); } }; }

// Append new pedals at the end; the order is what pedal menus show
constexpr std::array entries { entry<OverdrivePedal>(), entry<BoostPedal>(),
                               entry<EqPedal>(), entry<ChorusPedal>(), entry<DelayPedal>(), entry<ReverbPedal>(),
                               entry<TremoloPedal>(), entry<VibratoPedal>(), entry<FlangerPedal>(), entry<PhaserPedal>(),
                               entry<SpringPedal>() };
} // namespace

namespace registry
{
std::span<const PedalInfo* const> all() noexcept
{
    static const auto infos = []
    {
        std::array<const PedalInfo*, entries.size()> a {};
        for (size_t i = 0; i < entries.size(); ++i)
            a[i] = &entries[i].info();
        return a;
    }();
    return infos;
}

const PedalInfo* find (std::string_view id) noexcept
{
    for (const auto* i : all())
        if (i->id == id)
            return i;
    return nullptr;
}

std::unique_ptr<Pedal> create (std::string_view id)
{
    for (const auto& e : entries)
        if (e.info().id == id)
            return e.make();
    return nullptr;
}
} // namespace registry

} // namespace fx
