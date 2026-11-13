#include "RigProcessor.h"

#include "CapturePedal.h"
#include "RigEditor.h"
#include "Presets.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <rs_t3k/Service.h>

#include <cmath>

using rig::Section;
using rig::AmpStage;

namespace rig
{
juce::String sectionKey (Section s) { return s == Section::pre ? "pre" : "loop"; }

juce::String fileSlotKey (FileSlot s)
{
    switch (s.kind)
    {
        case FileSlot::Kind::amp:   return "amp";
        case FileSlot::Kind::power: return "power";
        case FileSlot::Kind::cab:   return "cab";
        case FileSlot::Kind::pedal: break;
    }
    return sectionKey (s.section) + juce::String (s.index + 1);
}

std::optional<FileSlot> fileSlotFromKey (const juce::String& key)
{
    if (key == "amp")   return FileSlot::amp;
    if (key == "power") return FileSlot::power;
    if (key == "cab")   return FileSlot::cab;
    for (auto s : { Section::pre, Section::loop })
        if (key.startsWith (sectionKey (s)))
        {
            const int i = key.fromFirstOccurrenceOf (sectionKey (s), false, false).getIntValue() - 1;
            if (i >= 0 && i < slotsPerSection)
                return FileSlot::pedal (s, i);
        }
    return std::nullopt;
}

Tone3000Source Tone3000Source::from (const rs::t3k::Tone& t, const rs::t3k::Model& m)
{
    Tone3000Source s;
    s.toneId = t.id;
    s.modelId = m.id;
    s.toneTitle = t.title;
    s.modelName = m.name;
    s.creator = t.creator.name();
    s.gear = t.gear;
    s.format = t.format;
    s.license = t.license;
    s.imageUrl = t.image();
    s.toneUrl = t.url;
    return s;
}

rs::t3k::Tone Tone3000Source::tone() const
{
    rs::t3k::Tone t;
    t.id = toneId;
    t.title = toneTitle;
    t.format = format;
    t.gear = gear;
    t.license = license;
    t.url = toneUrl;
    if (imageUrl.isNotEmpty())
        t.images.add (imageUrl);
    t.creator.displayName = creator;
    return t;
}

juce::ParameterID slotOnID (Section s, int i)
{
    return { sectionKey (s) + juce::String (i + 1) + "_on", 1 };
}

juce::ParameterID slotKnobID (Section s, int i, int k)
{
    return { sectionKey (s) + juce::String (i + 1) + "_k" + juce::String (k + 1), 1 };
}

float toPlain (const fx::ParamInfo& p, float normalised) noexcept { return p.fromNormalised (normalised); }

float toNormalised (const fx::ParamInfo& p, float plain) noexcept { return p.toNormalised (plain); }

juce::String valueText (const fx::ParamInfo& p, float plain)
{
    using K = fx::ParamInfo::Kind;
    if (p.kind == K::choice)
    {
        const auto i = static_cast<size_t> (juce::jmax (0.0f, plain - p.minValue));
        return i < p.choices.size() ? juce::String (std::string (p.choices[i])) : juce::String();
    }
    if (p.kind == K::toggle)
        return plain > 0.5f ? "On" : "Off";

    const auto unit = juce::String (std::string (p.unit));
    if (unit == "Hz" && plain >= 1000.0f)
        return juce::String (plain / 1000.0f, plain < 10000.0f ? 2 : 1) + " kHz";
    if (unit == "Hz" && p.maxValue <= 20.0f)
        return juce::String (plain, 2) + " Hz";   // LFO rates
    if (unit == "s")
        return juce::String (plain, plain < 1.0f ? 2 : 1) + " s";
    if (unit == "dB")
        return juce::String (plain, 1) + " dB";

    // (juce::String (x, 0) would mean "all the digits", not none)
    const bool whole = (p.maxValue - p.minValue) > 20.0f;
    const auto number = whole ? juce::String (juce::roundToInt (plain)) : juce::String (plain, 1);
    return number + (unit.isEmpty() ? "" : " " + unit);
}
} // namespace rig

namespace
{
constexpr int maxLoopLatency = 8192;     // samples of dry delay available to the loop
constexpr int maxAmpLatency = 4096;      // resampling latency the amps' dry paths can match
constexpr double longestCabinetSeconds = 0.2;
constexpr float cabinetMakeup = 4.0f;    // at 48 kHz. JUCE normalises IRs to 0.125 energy; this brings a typical cab back to about the amp's level

/** Equal energy at a higher rate means a slightly louder cab (its gain goes
    with the square root of the rate), so the makeup follows the rate. */
float cabinetMakeupAt (double sampleRate) noexcept
{
    return cabinetMakeup * static_cast<float> (std::sqrt (48000.0 / sampleRate));
}

float dbToGain (float dB) noexcept { return dB <= -100.0f ? 0.0f : std::pow (10.0f, dB / 20.0f); }

/** Applies a smoothed gain to every channel. */
void applyGain (fx::LinearSmoother& g, float* const* ch, int numChannels, int n) noexcept
{
    if (! g.isSmoothing())
    {
        const float v = g.getCurrent();
        if (! juce::approximatelyEqual (v, 1.0f))
            for (int c = 0; c < numChannels; ++c)
                juce::FloatVectorOperations::multiply (ch[c], v, n);
        return;
    }

    for (int i = 0; i < n; ++i)
    {
        const float v = g.next();
        for (int c = 0; c < numChannels; ++c)
            ch[c][i] *= v;
    }
}
} // namespace

namespace
{
/** Copies x into dry, delayed by `latency` samples through `line` (which is
    fed every call so it's ready whenever it's needed). */
void alignedDry (fx::DelayLine& line, const float* x, float* dry, int n, int latency) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        dry[i] = latency > 0 ? line.readInteger (latency) : x[i];
        line.write (x[i]);
    }
}

/** ch = dry + blend * (ch - dry); a no-op once the blend has settled on 1. */
void blendWithDry (fx::LinearSmoother& blend, float* const* ch, float* const* dry, int numChannels, int n) noexcept
{
    if (! blend.isSmoothing() && blend.getCurrent() >= 1.0f)
        return;
    for (int i = 0; i < n; ++i)
    {
        const float b = blend.next();
        for (int c = 0; c < numChannels; ++c)
            ch[c][i] = dry[c][i] + b * (ch[c][i] - dry[c][i]);
    }
}

} // namespace

//==============================================================================
RigProcessor::RigProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::mono(),   true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "RoughStomp", createLayout())
{
    auto raw = [this] (const juce::ParameterID& id) { return apvts.getRawParameterValue (id.getParamID()); };
    inputDb      = raw (ParamIDs::input);
    ampOn        = raw (ParamIDs::ampOn);
    ampGainDb    = raw (ParamIDs::ampGain);
    ampLevelDb   = raw (ParamIDs::ampLevel);
    loopOn       = raw (ParamIDs::loopOn);
    loopSendDb   = raw (ParamIDs::loopSend);
    loopReturnDb = raw (ParamIDs::loopReturn);
    loopMix      = raw (ParamIDs::loopMix);
    powerOn      = raw (ParamIDs::powerOn);
    cabOn        = raw (ParamIDs::cabOn);
    outputDb     = raw (ParamIDs::output);
    gateOn       = raw (ParamIDs::gateOn);
    gateThresholdDb = raw (ParamIDs::gateThreshold);
    tunerOn      = raw (ParamIDs::tuner);

    for (auto s : { Section::pre, Section::loop })
        for (int i = 0; i < rig::slotsPerSection; ++i)
        {
            auto& sl = slot (s, i);
            sl.on = raw (rig::slotOnID (s, i));
            for (int k = 0; k < rig::knobsPerSlot; ++k)
                sl.knobs[static_cast<size_t> (k)] = raw (rig::slotKnobID (s, i, k));
        }

    for (auto s : { Section::pre, Section::loop })
        publishLayout (s);

    // A new rig starts with an overdrive in front of the amp
    setSlotPedal (Section::pre, 0, "overdrive");

    presets = std::make_unique<PresetManager> (*this);
    startTimerHz (20);
}

RigProcessor::~RigProcessor()
{
    stopTimer();
}

juce::AudioProcessorValueTreeState::ParameterLayout RigProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto dB = [] (float v, int) { return String (v, 1) + " dB"; };
    auto addDb = [&] (const ParameterID& id, const String& name, float lo, float hi, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (id, name, NormalisableRange<float> (lo, hi, 0.1f), def,
                                                           AudioParameterFloatAttributes().withLabel ("dB")
                                                               .withStringFromValueFunction (dB)));
    };

    addDb (ParamIDs::input, "Input", -24.0f, 24.0f, 0.0f);
    layout.add (std::make_unique<AudioParameterBool> (ParamIDs::ampOn, "Amp On", true));
    addDb (ParamIDs::ampGain, "Amp Gain", -24.0f, 24.0f, 0.0f);
    addDb (ParamIDs::ampLevel, "Amp Level", -24.0f, 12.0f, 0.0f);
    layout.add (std::make_unique<AudioParameterBool> (ParamIDs::loopOn, "Loop On", true));
    addDb (ParamIDs::loopSend, "Loop Send", -24.0f, 12.0f, 0.0f);
    addDb (ParamIDs::loopReturn, "Loop Return", -24.0f, 12.0f, 0.0f);
    layout.add (std::make_unique<AudioParameterFloat> (ParamIDs::loopMix, "Loop Mix", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
                                                       AudioParameterFloatAttributes().withLabel ("%")
                                                           .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParamIDs::powerOn, "Power Amp On", true));
    layout.add (std::make_unique<AudioParameterBool> (ParamIDs::cabOn, "Cab On", true));
    addDb (ParamIDs::output, "Output", -24.0f, 12.0f, 0.0f);
    layout.add (std::make_unique<AudioParameterBool> (ParamIDs::gateOn, "Gate On", false));
    addDb (ParamIDs::gateThreshold, "Gate Threshold", -90.0f, -20.0f, -60.0f);
    layout.add (std::make_unique<AudioParameterBool> (ParamIDs::tuner, "Tuner", false,
                                                      AudioParameterBoolAttributes().withAutomatable (false)));

    // Slot controls: generic, because any pedal can sit in any slot. Their
    // text shows whatever the current pedal's knob means.
    for (auto s : { Section::pre, Section::loop })
    {
        const String sectionName = s == Section::pre ? "Pre" : "Loop";
        for (int i = 0; i < rig::slotsPerSection; ++i)
        {
            const String prefix = sectionName + " " + String (i + 1);
            layout.add (std::make_unique<AudioParameterBool> (rig::slotOnID (s, i), prefix + " On", true));

            for (int k = 0; k < rig::knobsPerSlot; ++k)
            {
                auto text = [this, s, i, k] (float v, int)
                {
                    if (const auto* info = getSlotInfo (s, i); info != nullptr && k < static_cast<int> (info->params.size()))
                    {
                        const auto& p = info->params[static_cast<size_t> (k)];
                        return rig::valueText (p, rig::toPlain (p, v));
                    }
                    return String ("-");
                };
                layout.add (std::make_unique<AudioParameterFloat> (rig::slotKnobID (s, i, k), prefix + " Knob " + String (k + 1),
                                                                   NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                                   AudioParameterFloatAttributes().withStringFromValueFunction (text)));
            }
        }
    }

    return layout;
}

//==============================================================================
const fx::PedalInfo* RigProcessor::getSlotInfo (Section s, int i) const noexcept
{
    if (i < 0 || i >= rig::slotsPerSection)
        return nullptr;
    return slots[index (s)][static_cast<size_t> (i)].info.load();
}

void RigProcessor::setSlotPedal (Section s, int i, juce::StringRef pedalId, bool resetKnobs)
{
    if (i < 0 || i >= rig::slotsPerSection)
        return;

    const std::string id = juce::String (pedalId).toStdString();
    std::shared_ptr<fx::Pedal> pedal;
    if (id == CapturePedal::staticInfo().id)
        pedal = std::make_shared<CapturePedal>();   // NAM lives in the plugin, so this one isn't in fx-core's registry
    else if (! id.empty())
        pedal = fx::registry::create (id);

    if (resetKnobs && pedal != nullptr)
    {
        const auto& params = pedal->info().params;
        for (int k = 0; k < rig::knobsPerSlot && k < static_cast<int> (params.size()); ++k)
            if (auto* p = apvts.getParameter (rig::slotKnobID (s, i, k).getParamID()))
                p->setValueNotifyingHost (rig::toNormalised (params[static_cast<size_t> (k)],
                                                             params[static_cast<size_t> (k)].defaultValue));
    }

    {
        const std::scoped_lock lock (messageLock);
        auto& sl = slot (s, i);
        sl.pedal = std::move (pedal);
        sl.info = sl.pedal != nullptr ? &sl.pedal->info() : nullptr;
        // A new pedal: any capture this slot held is gone with the old one
        ++sl.captureGeneration;
        sl.captureFile = sl.captureLoading = juce::File();
        sl.captureProblem = {};
        tone3000Sources.erase (rig::fileSlotKey (rig::FileSlot::pedal (s, i)));
        publishLayout (s);
    }
    updateLatency();
}

void RigProcessor::swapSlots (Section sa, int a, Section sb, int b)
{
    if (a < 0 || b < 0 || a >= rig::slotsPerSection || b >= rig::slotsPerSection || (sa == sb && a == b))
        return;

    // Settings first: each slot's controls take the other's values
    struct Settings
    {
        std::array<float, rig::knobsPerSlot> knobs {};
        float on = 1.0f;
        juce::String id;
    };
    auto read = [this] (Section s, int i)
    {
        Settings st;
        for (int k = 0; k < rig::knobsPerSlot; ++k)
            st.knobs[static_cast<size_t> (k)] = apvts.getParameter (rig::slotKnobID (s, i, k).getParamID())->getValue();
        st.on = apvts.getParameter (rig::slotOnID (s, i).getParamID())->getValue();
        if (const auto* info = getSlotInfo (s, i))
            st.id = juce::String (std::string (info->id));
        return st;
    };
    auto write = [this] (Section s, int i, const Settings& st)
    {
        for (int k = 0; k < rig::knobsPerSlot; ++k)
            apvts.getParameter (rig::slotKnobID (s, i, k).getParamID())->setValueNotifyingHost (st.knobs[static_cast<size_t> (k)]);
        apvts.getParameter (rig::slotOnID (s, i).getParamID())->setValueNotifyingHost (st.on);
    };
    const auto first = read (sa, a), second = read (sb, b);
    write (sa, a, second);
    write (sb, b, first);

    // Captures (and where they came from) travel with their pedals
    const auto keyA = rig::fileSlotKey (rig::FileSlot::pedal (sa, a)), keyB = rig::fileSlotKey (rig::FileSlot::pedal (sb, b));
    juce::File fileA, fileB;
    std::optional<rig::Tone3000Source> sourceA, sourceB;
    {
        const std::scoped_lock lock (messageLock);
        auto current = [] (const Slot& sl) { return sl.captureLoading != juce::File() ? sl.captureLoading : sl.captureFile; };
        fileA = current (slot (sa, a));   // one still loading counts too
        fileB = current (slot (sb, b));
        if (auto it = tone3000Sources.find (keyA); it != tone3000Sources.end()) sourceA = it->second;
        if (auto it = tone3000Sources.find (keyB); it != tone3000Sources.end()) sourceB = it->second;
    }
    auto placeSources = [&]
    {
        const std::scoped_lock lock (messageLock);
        tone3000Sources.erase (keyA);
        tone3000Sources.erase (keyB);
        if (sourceA) tone3000Sources[keyB] = *sourceA;
        if (sourceB) tone3000Sources[keyA] = *sourceB;
    };

    if (sa == sb)
    {
        // The pedals themselves move, loaded captures and all
        {
            const std::scoped_lock lock (messageLock);
            auto& x = slot (sa, a);
            auto& y = slot (sb, b);
            std::swap (x.pedal, y.pedal);
            x.info = x.pedal != nullptr ? &x.pedal->info() : nullptr;
            y.info = y.pedal != nullptr ? &y.pedal->info() : nullptr;
            std::swap (x.captureFile, y.captureFile);
            std::swap (x.captureLoading, y.captureLoading);
            std::swap (x.captureProblem, y.captureProblem);
            ++x.captureGeneration;   // a load in flight would land in the wrong slot
            ++y.captureGeneration;
            publishLayout (sa);
        }
        placeSources();
        updateLatency();
    }
    else
    {
        // Between rows the channel count differs: fresh pedals, captures reloaded
        setSlotPedal (sa, a, second.id, false);
        setSlotPedal (sb, b, first.id, false);
        placeSources();
        if (first.id == CapturePedal::staticInfo().id.data() && fileA != juce::File())
            loadSlotCapture (sb, b, fileA);
        if (second.id == CapturePedal::staticInfo().id.data() && fileB != juce::File())
            loadSlotCapture (sa, a, fileB);
    }
}

void RigProcessor::publishLayout (Section s)
{
    fx::Chain::Slots layout;
    for (int i = 0; i < rig::slotsPerSection; ++i)
        layout.push_back (slot (s, i).pedal);
    chain (s).setLayout (std::move (layout));
}

void RigProcessor::updateLatency()
{
    int total = 0;
    {
        const std::scoped_lock lock (messageLock);
        total = preChain.getLatencySamples() + loopChain.getLatencySamples()
                + mainAmp.latency + powerAmp.latency;
    }
    if (total != getLatencySamples())
        setLatencySamples (total);
}

//==============================================================================
RigProcessor::BuiltAmps RigProcessor::buildAmps (AmpStage stage, const juce::File& file, double rate, int block)
{
    // The power amp runs once per output channel
    const int copies = stage == AmpStage::power ? 2 : 1;
    BuiltAmps built { std::make_unique<Amps>(), {} };
    for (int c = 0; c < copies; ++c)
    {
        auto result = rig::AmpModel::load (file, rate, block);
        if (result.model == nullptr)
            return { nullptr, result.error.isNotEmpty() ? result.error : juce::String ("Couldn't load it.") };
        built.amps->models.push_back (std::move (result.model));
    }
    return built;
}

void RigProcessor::publishAmps (AmpStage stage, const juce::File& file, std::unique_ptr<Amps> amps)
{
    {
        const std::scoped_lock lock (messageLock);
        auto& a = amp (stage);
        a.expectedRate = amps->models.front()->getExpectedSampleRate();
        a.handoff.publish (std::move (amps));
        a.file = file;
        a.loadingFile = juce::File();
        a.problem = {};
        updateAmpRate (a);
    }
    updateLatency();
}

juce::String RigProcessor::loadAmp (AmpStage stage, const juce::File& file)
{
    double rate;
    int block;
    {
        const std::scoped_lock lock (messageLock);
        rate = currentSampleRate;
        block = maxBlock;
        auto& a = amp (stage);
        ++a.generation;   // supersedes any background load
        a.loadingFile = juce::File();
    }

    auto built = buildAmps (stage, file, rate, block);
    if (built.amps == nullptr)
        return built.error;
    publishAmps (stage, file, std::move (built.amps));
    return {};
}

void RigProcessor::loadAmpAsync (AmpStage stage, const juce::File& file, AmpLoad mode,
                                 std::function<void (const juce::String&)> onDone)
{
    double rate;
    int block, generation;
    {
        const std::scoped_lock lock (messageLock);
        rate = currentSampleRate;
        block = maxBlock;
        auto& a = amp (stage);
        generation = ++a.generation;
        a.loadingFile = file;
        if (mode == AmpLoad::showNewNow)
        {
            a.file = file;
            a.problem = {};
        }
    }

    juce::WeakReference<RigProcessor> self (this);
    loader.addJob ([self, stage, file, mode, rate, block, generation, done = std::move (onDone)]
    {
        auto built = std::make_shared<BuiltAmps> (buildAmps (stage, file, rate, block));

        juce::MessageManager::callAsync ([self, stage, file, mode, rate, block, generation, built, done]
        {
            auto* p = self.get();
            if (p == nullptr)
                return;

            double nowRate;
            int nowBlock;
            {
                const std::scoped_lock lock (p->messageLock);
                if (p->amp (stage).generation != generation)
                    return;   // a later load or clear took over
                p->amp (stage).loadingFile = juce::File();
                nowRate = p->currentSampleRate;
                nowBlock = p->maxBlock;
            }

            if (built->amps == nullptr)
            {
                if (mode == AmpLoad::showNewNow)
                {
                    // Like a restored session: keep the path, say why, no amp
                    const std::scoped_lock lock (p->messageLock);
                    auto& a = p->amp (stage);
                    a.handoff.publish (std::make_unique<Amps>());
                    a.expectedRate = 0.0;
                    a.latency = 0;
                    a.problem = file.existsAsFile() ? built->error : juce::String ("File not found");
                }
                p->updateLatency();
                if (done)
                    done (built->error);
                return;
            }

            // The audio settings changed while it loaded: prepare for the new ones
            // (safe: nothing plays these models yet)
            if (! juce::approximatelyEqual (nowRate, rate) || nowBlock != block)
                for (auto& m : built->amps->models)
                    m->prepare (nowRate, nowBlock);

            p->publishAmps (stage, file, std::move (built->amps));
            if (done)
                done ({});
        });
    });
}

juce::File RigProcessor::getLoadingAmpFile (AmpStage stage) const
{
    const std::scoped_lock lock (messageLock);
    return amp (stage).loadingFile;
}

void RigProcessor::updateAmpRate (AmpState& a)
{
    // Called with messageLock held, after a load or a sample rate change.
    // Must match what the models' own resamplers do.
    a.latency = fx::RateBridge::latencyFor (currentSampleRate, a.expectedRate);
}

void RigProcessor::clearAmp (AmpStage stage)
{
    {
        const std::scoped_lock lock (messageLock);
        auto& a = amp (stage);
        ++a.generation;   // cancels a background load
        a.loadingFile = juce::File();
        a.handoff.publish (std::make_unique<Amps>());
        a.file = juce::File();
        a.expectedRate = 0.0;
        a.latency = 0;
        a.problem = {};
        tone3000Sources.erase (rig::fileSlotKey (rig::fileSlot (stage)));
    }
    updateLatency();
}

juce::File RigProcessor::getAmpFile (AmpStage stage) const
{
    const std::scoped_lock lock (messageLock);
    return amp (stage).file;
}

juce::String RigProcessor::getAmpProblem (AmpStage stage) const
{
    const std::scoped_lock lock (messageLock);
    const auto& a = amp (stage);
    return a.loadingFile != juce::File() ? juce::String ("Loading...") : a.problem;
}

juce::String RigProcessor::loadCabinet (const juce::File& file)
{
    // On failure, keep the path so saving again doesn't lose it, and say why
    auto fail = [&] (const juce::String& why)
    {
        const std::scoped_lock lock (messageLock);
        cabinetLoaded = false;
        cabinetSource.setSize (0, 0);
        cabinetFile = file;
        cabinetProblem = why;
        return cabinetProblem;
    };

    if (! file.existsAsFile())
        return fail ("File not found");

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return fail ("It isn't an audio file this can read.");

    // Mono: the first channel, applied to each output channel
    const auto length = static_cast<int> (std::min<juce::int64> (reader->lengthInSamples,
                                                                  static_cast<juce::int64> (reader->sampleRate * longestCabinetSeconds) + 1));
    juce::AudioBuffer<float> ir (1, length);
    reader->read (&ir, 0, length, 0, true, false);

    // A long IR cut short would end in a step, which sounds like a faint hiss:
    // fade its last 20 ms out instead
    if (length < reader->lengthInSamples)
    {
        const int fade = juce::jmin (length, static_cast<int> (reader->sampleRate * 0.02));
        auto* s = ir.getWritePointer (0, length - fade);
        for (int i = 0; i < fade; ++i)
            s[i] *= 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * static_cast<float> (i + 1) / static_cast<float> (fade));
    }

    const std::scoped_lock lock (messageLock);
    cabinetSource = std::move (ir);
    cabinetSourceRate = reader->sampleRate;
    cabinetFile = file;
    cabinetProblem = {};
    applyCabinet();
    cabinetLoaded = true;
    return {};
}

void RigProcessor::applyCabinet()
{
    if (cabinetSource.getNumSamples() == 0)
        return;

    const auto* src = cabinetSource.getReadPointer (0);
    const int srcLength = cabinetSource.getNumSamples();
    juce::AudioBuffer<float> ir;

    if (fx::RateBridge::latencyFor (currentSampleRate, cabinetSourceRate) == 0)
        ir.makeCopyOf (cabinetSource);
    else
    {
        fx::RateConverter converter;
        const auto from = static_cast<std::int64_t> (std::llround (cabinetSourceRate));
        const auto to = static_cast<std::int64_t> (std::llround (currentSampleRate));
        const auto tail = static_cast<int> (std::ceil (fx::RateConverter::halfWidth (from, to))) + 2;
        converter.prepare (from, to, 0, srcLength + tail + 16);
        converter.push (src, srcLength);
        const std::vector<float> silence (static_cast<size_t> (tail), 0.0f);
        converter.push (silence.data(), tail);

        const auto outLength = static_cast<int> (std::ceil (srcLength * currentSampleRate / cabinetSourceRate));
        ir.setSize (1, outLength + 4);
        ir.setSize (1, converter.pull (ir.getWritePointer (0), ir.getNumSamples()), true);
    }

    // Already at the right rate and length, so JUCE neither resamples nor trims it
    cabinet.loadImpulseResponse (std::move (ir), currentSampleRate, juce::dsp::Convolution::Stereo::no,
                                 juce::dsp::Convolution::Trim::yes, juce::dsp::Convolution::Normalise::yes);
    cabinetRate = currentSampleRate;
}

void RigProcessor::clearCabinet()
{
    const std::scoped_lock lock (messageLock);
    cabinetLoaded = false;
    cabinetFile = juce::File();
    cabinetProblem = {};
    cabinetSource.setSize (0, 0);
    tone3000Sources.erase (rig::fileSlotKey (rig::FileSlot::cab));
}

//==============================================================================
// TONE3000

rs::t3k::Service& RigProcessor::tone3000()
{
    if (tone3000Service == nullptr)
        tone3000Service = std::make_unique<juce::SharedResourcePointer<rs::t3k::Service>>();
    return **tone3000Service;
}

rig::Tone3000Source RigProcessor::getTone3000Source (rig::FileSlot s) const
{
    const std::scoped_lock lock (messageLock);
    const auto it = tone3000Sources.find (rig::fileSlotKey (s));
    return it != tone3000Sources.end() ? it->second : rig::Tone3000Source();
}

void RigProcessor::forgetTone3000Source (rig::FileSlot s)
{
    const std::scoped_lock lock (messageLock);
    tone3000Sources.erase (rig::fileSlotKey (s));
}

juce::File RigProcessor::fileOf (rig::FileSlot s) const
{
    if (s == rig::FileSlot::cab)
        return getCabinetFile();
    if (s.isPedal())
    {
        // One still loading counts: a save meanwhile mustn't lose it
        const std::scoped_lock lock (messageLock);
        const auto& sl = slot (s.section, s.index);
        return sl.captureLoading != juce::File() ? sl.captureLoading : sl.captureFile;
    }
    return getAmpFile (s == rig::FileSlot::amp ? AmpStage::amp : AmpStage::power);
}

void RigProcessor::loadFileInto (rig::FileSlot s, const juce::File& file, bool showNewNow,
                                 std::function<void (const juce::String&)> done)
{
    if (s == rig::FileSlot::cab)
    {
        const auto error = loadCabinet (file);
        if (done)
            done (error);
    }
    else if (s.isPedal())
        loadSlotCapture (s.section, s.index, file, std::move (done));
    else
        loadAmpAsync (s == rig::FileSlot::amp ? AmpStage::amp : AmpStage::power, file,
                      showNewNow ? AmpLoad::showNewNow : AmpLoad::keepOld, std::move (done));
}

//==============================================================================
// NAM captures in pedal slots

void RigProcessor::loadSlotCapture (Section s, int i, const juce::File& file,
                                    std::function<void (const juce::String&)> done, bool now)
{
    double rate;
    int block, generation;
    const int channels = s == Section::pre ? 1 : loopChannels;
    {
        const std::scoped_lock lock (messageLock);
        rate = currentSampleRate;
        block = maxBlock;
        auto& sl = slot (s, i);
        generation = ++sl.captureGeneration;
        sl.captureLoading = file;
        sl.captureProblem = {};
    }

    struct Built
    {
        std::unique_ptr<CapturePedal::Captures> captures;
        juce::String error;
    };
    auto build = [file, rate, block, channels]
    {
        auto b = std::make_shared<Built>();
        b->captures = std::make_unique<CapturePedal::Captures>();
        for (int c = 0; c < channels; ++c)
        {
            auto result = rig::AmpModel::load (file, rate, block);
            if (result.model == nullptr)
            {
                b->error = result.error.isNotEmpty() ? result.error : juce::String ("Couldn't load it.");
                b->captures.reset();
                return b;
            }
            b->captures->models.push_back (std::move (result.model));
        }
        b->captures->latency = b->captures->models.front()->getLatencySamples();
        return b;
    };

    juce::WeakReference<RigProcessor> self (this);
    auto finish = [self, s, i, file, generation, rate, block, done] (std::shared_ptr<Built> b)
    {
        auto* p = self.get();
        if (p == nullptr)
            return;
        double nowRate;
        int nowBlock;
        CapturePedal* pedal = nullptr;
        {
            const std::scoped_lock lock (p->messageLock);
            auto& sl = p->slot (s, i);
            if (sl.captureGeneration != generation)
                return;   // a later load, clear or pedal change took over
            sl.captureLoading = juce::File();
            sl.captureFile = file;
            pedal = dynamic_cast<CapturePedal*> (sl.pedal.get());
            nowRate = p->currentSampleRate;
            nowBlock = p->maxBlock;
            if (b->captures == nullptr)
                sl.captureProblem = file.existsAsFile() ? b->error : juce::String ("File not found");
        }

        if (pedal != nullptr && b->captures != nullptr)
        {
            if (! juce::approximatelyEqual (nowRate, rate) || nowBlock != block)
            {
                for (auto& m : b->captures->models)
                    m->prepare (nowRate, nowBlock);
                b->captures->latency = b->captures->models.front()->getLatencySamples();
            }
            pedal->setCaptures (std::move (b->captures));
            {
                const std::scoped_lock lock (p->messageLock);
                p->publishLayout (s);   // the capture's latency now counts
            }
            p->updateLatency();
        }
        else if (pedal != nullptr)
            pedal->setCaptures (nullptr);
        if (done)
            done (b->error);
    };

    if (now)
        return finish (build());
    loader.addJob ([build, finish]
    {
        auto b = build();
        juce::MessageManager::callAsync ([finish, b] { finish (b); });
    });
}

void RigProcessor::clearSlotCapture (Section s, int i)
{
    CapturePedal* pedal = nullptr;
    {
        const std::scoped_lock lock (messageLock);
        auto& sl = slot (s, i);
        ++sl.captureGeneration;
        sl.captureFile = sl.captureLoading = juce::File();
        sl.captureProblem = {};
        tone3000Sources.erase (rig::fileSlotKey (rig::FileSlot::pedal (s, i)));
        pedal = dynamic_cast<CapturePedal*> (sl.pedal.get());
    }
    if (pedal != nullptr)
    {
        pedal->setCaptures (nullptr);
        {
            const std::scoped_lock lock (messageLock);
            publishLayout (s);
        }
        updateLatency();
    }
}

juce::File RigProcessor::getSlotCaptureFile (Section s, int i) const
{
    const std::scoped_lock lock (messageLock);
    const auto& sl = slot (s, i);
    return sl.captureLoading != juce::File() ? sl.captureLoading : sl.captureFile;
}

juce::String RigProcessor::getSlotCaptureProblem (Section s, int i) const
{
    const std::scoped_lock lock (messageLock);
    const auto& sl = slot (s, i);
    return sl.captureLoading != juce::File() ? juce::String ("Loading...") : sl.captureProblem;
}

void RigProcessor::loadTone3000 (rig::FileSlot slotId, const rs::t3k::Tone& tone, const rs::t3k::Model& model,
                                 std::function<void (const juce::String&)> done)
{
    juce::WeakReference<RigProcessor> self (this);
    const auto source = rig::Tone3000Source::from (tone, model);

    tone3000().download (tone, model, [self, slotId, source, done] (const juce::File& file, const juce::String& error)
    {
        auto* p = self.get();
        if (p == nullptr)
            return;
        if (! file.existsAsFile())
        {
            if (done)
                done (error.isNotEmpty() ? error : juce::String ("The download failed."));
            return;
        }

        auto loaded = [self, slotId, source, done] (const juce::String& loadError)
        {
            if (auto* q = self.get(); q != nullptr && loadError.isEmpty())
            {
                const std::scoped_lock lock (q->messageLock);
                q->tone3000Sources[rig::fileSlotKey (slotId)] = source;
            }
            if (done)
                done (loadError);
        };
        p->loadFileInto (slotId, file, false, loaded);
    });
}

void RigProcessor::setSlotProblem (rig::FileSlot s, const juce::File& file, const juce::String& problem)
{
    if (s.isPedal())
    {
        CapturePedal* pedal = nullptr;
        {
            const std::scoped_lock lock (messageLock);
            auto& sl = slot (s.section, s.index);
            ++sl.captureGeneration;
            sl.captureLoading = juce::File();
            sl.captureFile = file;
            sl.captureProblem = problem;
            pedal = dynamic_cast<CapturePedal*> (sl.pedal.get());
        }
        if (pedal != nullptr)
            pedal->setCaptures (nullptr);
        return;
    }
    if (s == rig::FileSlot::cab)
    {
        const std::scoped_lock lock (messageLock);
        cabinetLoaded = false;
        cabinetSource.setSize (0, 0);
        cabinetFile = file;
        cabinetProblem = problem;
        return;
    }
    {
        const std::scoped_lock lock (messageLock);
        auto& a = amp (s == rig::FileSlot::amp ? AmpStage::amp : AmpStage::power);
        ++a.generation;
        a.handoff.publish (std::make_unique<Amps>());
        a.file = file;
        a.loadingFile = juce::File();
        a.expectedRate = 0.0;
        a.latency = 0;
        a.problem = problem;
    }
    updateLatency();
}

void RigProcessor::redownload (rig::FileSlot s, const rig::Tone3000Source& source, const juce::File& file)
{
    {
        const std::scoped_lock lock (messageLock);
        tone3000Sources[rig::fileSlotKey (s)] = source;
    }

    auto& service = tone3000();
    if (! service.isAvailable())
        return setSlotProblem (s, file, "From TONE3000, which this build can't reach");
    if (! service.isSignedIn())
        return setSlotProblem (s, file, "Sign in to TONE3000 to download it again");

    setSlotProblem (s, file, "Downloading from TONE3000...");

    // Still waiting for this? (The user may have loaded something else since.)
    juce::WeakReference<RigProcessor> self (this);
    auto stillWanted = [self, s, file, source]
    {
        auto* p = self.get();
        if (p == nullptr)
            return false;
        const auto current = p->getTone3000Source (s);
        return current.modelId == source.modelId && p->fileOf (s) == file;
    };

    service.model (source.modelId, [self, s, file, source, stillWanted] (bool ok, const rs::t3k::Model& model, const juce::String& error)
    {
        if (! stillWanted())
            return;
        auto* p = self.get();
        if (! ok)
            return p->setSlotProblem (s, file, rs::t3k::Service::isUnavailable (p->tone3000().lastStatus())
                                                   ? juce::String ("No longer available on TONE3000")
                                                   : error);

        p->tone3000().download (source.tone(), model, [self, s, file, stillWanted] (const juce::File& got, const juce::String& e)
        {
            if (! stillWanted())
                return;
            auto* q = self.get();
            if (! got.existsAsFile())
                return q->setSlotProblem (s, file, e);
            q->loadFileInto (s, got, true, {});
        });
    });
}

void RigProcessor::retryTone3000Downloads()
{
    std::map<juce::String, rig::Tone3000Source> sources;
    {
        const std::scoped_lock lock (messageLock);
        sources = tone3000Sources;
    }
    for (const auto& [key, source] : sources)
        if (const auto s = rig::fileSlotFromKey (key))
        {
            const auto file = fileOf (*s);
            if (source.valid() && file != juce::File() && ! file.existsAsFile())
                redownload (*s, source, file);
        }
}

juce::String RigProcessor::getCabinetProblem() const
{
    const std::scoped_lock lock (messageLock);
    return cabinetProblem;
}

juce::File RigProcessor::getCabinetFile() const
{
    const std::scoped_lock lock (messageLock);
    return cabinetFile;
}

RigProcessor::TunerReading RigProcessor::readTuner()
{
    double rate;
    {
        const std::scoped_lock lock (messageLock);
        rate = currentSampleRate;
    }

    // About 85 ms of audio: two periods of the lowest string, with room
    if (! juce::approximatelyEqual (rate, pitchDetectorRate))
    {
        const int window = juce::nextPowerOfTwo (static_cast<int> (rate * 0.085));
        pitchDetector.prepare (rate, window);
        tunerWindow.assign (static_cast<size_t> (window), 0.0f);
        pitchDetectorRate = rate;
    }

    // Slide what's arrived into the window
    const int ready = tunerFifo.getNumReady();
    if (ready == 0)
        return {};
    const auto scope = tunerFifo.read (ready);
    auto append = [this] (int start, int count)
    {
        if (count <= 0)
            return;
        const auto size = static_cast<int> (tunerWindow.size());
        const int keep = std::max (0, size - count);
        std::copy (tunerWindow.end() - keep, tunerWindow.end(), tunerWindow.begin());
        const int take = std::min (count, size);
        const auto* src = tunerFifoData.data() + start + (count - take);
        std::copy (src, src + take, tunerWindow.begin() + keep);
    };
    append (scope.startIndex1, scope.blockSize1);
    append (scope.startIndex2, scope.blockSize2);

    const auto r = pitchDetector.detect (tunerWindow.data());
    return { r.hz, r.confidence };
}

void RigProcessor::timerCallback()
{
    // Once a second: note whether the rig has drifted from its preset, so a
    // saved session remembers it (getStateInformation may run on any thread)
    if (++ticks % 20 == 0 && presets != nullptr)
        presets->noteModifiedState();

    {
        const std::scoped_lock lock (messageLock);
        preChain.collectGarbage();
        loopChain.collectGarbage();
        mainAmp.handoff.collect();
        powerAmp.handoff.collect();
        for (auto& row : slots)
            for (auto& sl : row)
                if (auto* capture = dynamic_cast<CapturePedal*> (sl.pedal.get()))
                    capture->collect();
    }
    updateLatency();
}

//==============================================================================
bool RigProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    auto monoOrStereo = [] (const juce::AudioChannelSet& set)
    {
        return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
    };
    return monoOrStereo (in) && monoOrStereo (out);
}

void RigProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int numOut = juce::jlimit (1, 2, getTotalNumOutputChannels());
    {
        const std::scoped_lock lock (messageLock);
        currentSampleRate = sampleRate;
        maxBlock = samplesPerBlock;
        loopChannels = numOut;

        preChain.prepare (sampleRate, samplesPerBlock, 1);
        loopChain.prepare (sampleRate, samplesPerBlock, numOut);

        for (auto* a : { &mainAmp, &powerAmp })
        {
            for (auto& m : a->handoff.takeOver().models)
                m->prepare (sampleRate, samplesPerBlock);
            updateAmpRate (*a);
        }
    }

    // Hand over the IR at the new rate first: prepare() installs whatever is
    // queued straight away, whereas a load after it waits for JUCE's
    // background thread (too late for an offline render that starts now)
    {
        const std::scoped_lock lock (messageLock);
        if (! juce::approximatelyEqual (cabinetRate, sampleRate))
            applyCabinet();
    }
    cabinet.prepare ({ sampleRate, static_cast<juce::uint32> (samplesPerBlock), 2 });
    cabinetGain = cabinetMakeupAt (sampleRate);

    for (auto* g : { &inputGain, &ampGain, &ampLevel, &sendGain, &returnGain, &outputGain })
        g->prepare (sampleRate, 0.02);
    mix.prepare (sampleRate, 0.02);

    gate.prepare (sampleRate);
    tunerFifo.reset();

    for (auto* b : { &ampBlend, &powerBlend, &cabBlend })
        b->prepare (sampleRate, 0.02);
    ampBlend.reset (ampOn->load() > 0.5f ? 1.0f : 0.0f);
    powerBlend.reset (powerOn->load() > 0.5f ? 1.0f : 0.0f);
    cabBlend.reset (cabOn->load() > 0.5f ? 1.0f : 0.0f);

    ampDryLine.prepare (maxAmpLatency);
    for (auto& l : powerDryLines)
        l.prepare (maxAmpLatency);
    ampDry.assign (static_cast<size_t> (samplesPerBlock), 0.0f);
    for (auto* v : { &powerDry[0], &powerDry[1], &cabDry[0], &cabDry[1] })
        v->assign (static_cast<size_t> (samplesPerBlock), 0.0f);

    inputGain.reset (dbToGain (inputDb->load()));
    ampGain.reset (dbToGain (ampGainDb->load()));
    ampLevel.reset (dbToGain (ampLevelDb->load()));
    sendGain.reset (dbToGain (loopSendDb->load()));
    returnGain.reset (dbToGain (loopReturnDb->load()));
    outputGain.reset (tunerOn->load() > 0.5f ? 0.0f : dbToGain (outputDb->load()));
    mix.reset (loopOn->load() > 0.5f ? loopMix->load() / 100.0f : 0.0f);

    for (auto& d : dryDelay)
        d.assign (static_cast<size_t> (maxLoopLatency), 0.0f);
    for (auto& d : dry)
        d.assign (static_cast<size_t> (samplesPerBlock), 0.0f);
    dryWrite = 0;

    updateLatency();
}

void RigProcessor::pushParameters (Section s, std::span<fx::Pedal* const> pedals) noexcept
{
    const auto n = std::min (pedals.size(), static_cast<size_t> (rig::slotsPerSection));
    for (size_t i = 0; i < n; ++i)
    {
        auto* pedal = pedals[i];
        if (pedal == nullptr)
            continue;

        auto& sl = slots[index (s)][i];
        const auto& params = pedal->info().params;
        const auto numKnobs = std::min (params.size(), static_cast<size_t> (rig::knobsPerSlot));
        for (size_t k = 0; k < numKnobs; ++k)
            pedal->setParameter (static_cast<int> (k), rig::toPlain (params[k], sl.knobs[k]->load()));
        pedal->setBypassed (sl.on->load() < 0.5f);
        pedal->setTempo (tempo);
        pedal->setPlayPosition (quarterNotes, playing);
    }
}

void RigProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = juce::jlimit (1, 2, getTotalNumOutputChannels());
    const int n = buffer.getNumSamples();

    // Guitar is mono: fold a stereo input down
    if (numIn > 1)
    {
        buffer.addFrom (0, 0, buffer, 1, 0, n);
        buffer.applyGain (0, 0, n, 0.5f);
    }

    // The tuner listens to the raw input; while it's on, the output is muted
    const bool tuning = tunerOn->load() > 0.5f;
    if (tuning)
    {
        const auto scope = tunerFifo.write (std::min (n, tunerFifo.getFreeSpace()));
        const float* in = buffer.getReadPointer (0);
        if (scope.blockSize1 > 0)
            std::copy (in, in + scope.blockSize1, tunerFifoData.begin() + scope.startIndex1);
        if (scope.blockSize2 > 0)
            std::copy (in + scope.blockSize1, in + scope.blockSize1 + scope.blockSize2, tunerFifoData.begin() + scope.startIndex2);
    }

    gate.setEnabled (gateOn->load() > 0.5f);
    gate.setThresholdDb (gateThresholdDb->load());

    // The host's tempo, for synced pedals (standalone has none: 120 BPM)
    tempo = 120.0;
    playing = false;
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
        {
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 0.0)
                tempo = *bpm;
            if (const auto ppq = position->getPpqPosition(); ppq.hasValue())
            {
                quarterNotes = *ppq;
                playing = position->getIsPlaying();
            }
        }

    // Block-rate targets; the smoothers ramp to them
    inputGain.setTarget (dbToGain (inputDb->load()));
    ampGain.setTarget (dbToGain (ampGainDb->load()));
    ampLevel.setTarget (dbToGain (ampLevelDb->load()));
    sendGain.setTarget (dbToGain (loopSendDb->load()));
    returnGain.setTarget (dbToGain (loopReturnDb->load()));
    outputGain.setTarget (tuning ? 0.0f : dbToGain (outputDb->load()));
    ampBlend.setTarget (ampOn->load() > 0.5f ? 1.0f : 0.0f);
    powerBlend.setTarget (powerOn->load() > 0.5f ? 1.0f : 0.0f);
    cabBlend.setTarget (cabOn->load() > 0.5f ? 1.0f : 0.0f);
    mix.setTarget (loopOn->load() > 0.5f ? loopMix->load() / 100.0f : 0.0f);

    const double blockStart = quarterNotes;
    for (int pos = 0; pos < n; pos += maxBlock)
    {
        // Pedals read the play position at the start of each chunk
        quarterNotes = blockStart + pos * tempo / (60.0 * currentSampleRate);
        float* ch[2] { buffer.getWritePointer (0, pos), numOut > 1 ? buffer.getWritePointer (1, pos) : nullptr };
        processChunk (ch, numOut, std::min (maxBlock, n - pos));
    }
}

void RigProcessor::processChunk (float* const* ch, int numOut, int n) noexcept
{
    float* mono = ch[0];

    // Input, gate and pre-amp pedals (mono)
    applyGain (inputGain, &mono, 1, n);
    gate.process (mono, n);
    pushParameters (Section::pre, preChain.beginBlock());
    preChain.process (&mono, 1, n);

    // Amp. On/off crossfades with the dry signal, delayed to match the
    // model's resampling latency (so the latency never changes and the fade
    // never comb-filters). The model keeps running while off, so switching
    // back on doesn't replay stale state.
    auto& amps = mainAmp.handoff.acquire();
    if (amps.loaded())
    {
        auto& model = *amps.models.front();
        alignedDry (ampDryLine, mono, ampDry.data(), n, model.getLatencySamples());
        applyGain (ampGain, &mono, 1, n);
        model.process (mono, n, true);
        applyGain (ampLevel, &mono, 1, n);
        blendWithDry (ampBlend, &mono, std::array { ampDry.data() }.data(), 1, n);
    }
    else
        ampBlend.skip (n);

    if (numOut > 1)
        juce::FloatVectorOperations::copy (ch[1], mono, n);

    // FX loop. The dry path is delayed by the loop pedals' latency so the
    // blend never comb-filters, and so latency stays the same with the loop off.
    const auto loopPedals = loopChain.beginBlock();
    pushParameters (Section::loop, loopPedals);

    int loopLatency = 0;
    for (auto* p : loopPedals)
        if (p != nullptr)
            loopLatency += p->getLatencySamples();
    loopLatency = std::min (loopLatency, maxLoopLatency - 1);

    const int mask = maxLoopLatency - 1;
    for (int c = 0; c < numOut; ++c)
    {
        auto& line = dryDelay[static_cast<size_t> (c)];
        auto& out = dry[static_cast<size_t> (c)];
        int w = dryWrite;
        for (int i = 0; i < n; ++i)
        {
            line[static_cast<size_t> (w)] = ch[c][i];
            out[static_cast<size_t> (i)] = line[static_cast<size_t> ((w - loopLatency) & mask)];
            w = (w + 1) & mask;
        }
    }
    dryWrite = (dryWrite + n) & mask;

    applyGain (sendGain, ch, numOut, n);
    loopChain.process (ch, numOut, n);
    applyGain (returnGain, ch, numOut, n);

    for (int i = 0; i < n; ++i)
    {
        const float m = mix.next();
        for (int c = 0; c < numOut; ++c)
            ch[c][i] = dry[static_cast<size_t> (c)][static_cast<size_t> (i)] + m * (ch[c][i] - dry[static_cast<size_t> (c)][static_cast<size_t> (i)]);
    }

    // Power amp, one model per channel
    auto& power = powerAmp.handoff.acquire();
    if (power.loaded() && static_cast<int> (power.models.size()) >= numOut)
    {
        std::array<float*, 2> dryPtrs { powerDry[0].data(), powerDry[1].data() };
        for (int c = 0; c < numOut; ++c)
        {
            auto& model = *power.models[static_cast<size_t> (c)];
            alignedDry (powerDryLines[static_cast<size_t> (c)], ch[c], dryPtrs[static_cast<size_t> (c)], n, model.getLatencySamples());
            model.process (ch[c], n, true);
        }
        blendWithDry (powerBlend, ch, dryPtrs.data(), numOut, n);
    }
    else
        powerBlend.skip (n);

    // Cabinet (no latency), crossfaded on and off the same way
    if (cabinetLoaded.load())
    {
        std::array<float*, 2> dryPtrs { cabDry[0].data(), cabDry[1].data() };
        for (int c = 0; c < numOut; ++c)
            juce::FloatVectorOperations::copy (dryPtrs[static_cast<size_t> (c)], ch[c], n);
        juce::dsp::AudioBlock<float> block (ch, static_cast<size_t> (numOut), static_cast<size_t> (n));
        cabinet.process (juce::dsp::ProcessContextReplacing<float> (block));
        for (int c = 0; c < numOut; ++c)
            juce::FloatVectorOperations::multiply (ch[c], cabinetGain, n);
        blendWithDry (cabBlend, ch, dryPtrs.data(), numOut, n);
    }
    else
        cabBlend.skip (n);

    applyGain (outputGain, ch, numOut, n);
}

//==============================================================================
std::unique_ptr<juce::XmlElement> RigProcessor::captureState()
{
    auto state = apvts.copyState();
    juce::ValueTree rigTree ("Rig");

    for (auto s : { Section::pre, Section::loop })
        for (int i = 0; i < rig::slotsPerSection; ++i)
        {
            const auto* info = getSlotInfo (s, i);
            rigTree.setProperty (rig::sectionKey (s) + juce::String (i + 1),
                                 info != nullptr ? juce::String (std::string (info->id)) : juce::String(), nullptr);
        }

    rigTree.setProperty ("ampFile",   getAmpFile (AmpStage::amp).getFullPathName(), nullptr);
    rigTree.setProperty ("powerFile", getAmpFile (AmpStage::power).getFullPathName(), nullptr);
    rigTree.setProperty ("cabFile",   getCabinetFile().getFullPathName(), nullptr);

    // Captures in pedal slots
    for (auto s : { Section::pre, Section::loop })
        for (int i = 0; i < rig::slotsPerSection; ++i)
            if (const auto f = fileOf (rig::FileSlot::pedal (s, i)); f != juce::File())
                rigTree.setProperty (rig::fileSlotKey (rig::FileSlot::pedal (s, i)) + "File", f.getFullPathName(), nullptr);

    // Files from TONE3000: what they were, to show and to download again
    std::map<juce::String, rig::Tone3000Source> sources;
    {
        const std::scoped_lock lock (messageLock);
        sources = tone3000Sources;
    }
    for (const auto& [key, t] : sources)
        if (t.valid())
        {
            juce::ValueTree node ("Tone3000");
            node.setProperty ("slot", key, nullptr);
            node.setProperty ("toneId", t.toneId, nullptr);
            node.setProperty ("modelId", t.modelId, nullptr);
            node.setProperty ("toneTitle", t.toneTitle, nullptr);
            node.setProperty ("modelName", t.modelName, nullptr);
            node.setProperty ("creator", t.creator, nullptr);
            node.setProperty ("gear", t.gear, nullptr);
            node.setProperty ("format", t.format, nullptr);
            node.setProperty ("license", t.license, nullptr);
            node.setProperty ("imageUrl", t.imageUrl, nullptr);
            node.setProperty ("toneUrl", t.toneUrl, nullptr);
            rigTree.appendChild (node, nullptr);
        }

    {
        const std::scoped_lock lock (messageLock);
        rigTree.setProperty ("preset", presetName, nullptr);
        rigTree.setProperty ("presetModified", presetModified, nullptr);
    }

    state.removeChild (state.getChildWithName ("Rig"), nullptr);
    state.appendChild (rigTree, nullptr);
    return state.createXml();
}

void RigProcessor::restoreState (const juce::XmlElement& xml, Files files)
{
    if (! xml.hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (xml);
    const auto rigTree = state.getChildWithName ("Rig").createCopy();
    state.removeChild (state.getChildWithName ("Rig"), nullptr);

    // Parameters missing from older states keep their defaults rather than
    // whatever was set before
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (! state.getChildWithProperty ("id", withId->paramID).isValid())
                withId->setValueNotifyingHost (withId->getDefaultValue());
    apvts.replaceState (state);

    // Never come back muted in the tuner
    apvts.getParameter (ParamIDs::tuner.getParamID())->setValueNotifyingHost (0.0f);

    // Knob values came with the parameters, so don't reset them
    for (auto s : { Section::pre, Section::loop })
        for (int i = 0; i < rig::slotsPerSection; ++i)
            setSlotPedal (s, i, rigTree.getProperty (rig::sectionKey (s) + juce::String (i + 1)).toString(), false);

    {
        const std::scoped_lock lock (messageLock);
        presetName = rigTree.getProperty ("preset").toString();
        presetModified = static_cast<bool> (rigTree.getProperty ("presetModified", false));
    }

    if (files == Files::keepCurrent)
        return;

    std::map<juce::String, rig::Tone3000Source> sources;
    for (const auto& node : rigTree)
    {
        if (! node.hasType ("Tone3000"))
            continue;
        if (const auto s = rig::fileSlotFromKey (node.getProperty ("slot").toString()))
            {
                auto& t = sources[rig::fileSlotKey (*s)];
                t.toneId = node.getProperty ("toneId");
                t.modelId = node.getProperty ("modelId");
                t.toneTitle = node.getProperty ("toneTitle").toString();
                t.modelName = node.getProperty ("modelName").toString();
                t.creator = node.getProperty ("creator").toString();
                t.gear = node.getProperty ("gear").toString();
                t.format = node.getProperty ("format").toString();
                t.license = node.getProperty ("license").toString();
                t.imageUrl = node.getProperty ("imageUrl").toString();
                t.toneUrl = node.getProperty ("toneUrl").toString();
            }
    }

    // A TONE3000 file that's gone (a new Mac, a cleared folder) is downloaded again
    auto missingTone3000 = [&] (rig::FileSlot s, const juce::String& path)
    {
        const auto it = sources.find (rig::fileSlotKey (s));
        if (it == sources.end() || ! it->second.valid() || path.isEmpty() || juce::File (path).existsAsFile())
            return false;
        redownload (s, it->second, juce::File (path));
        return true;
    };

    auto restore = [&] (AmpStage stage, const juce::String& path)
    {
        if (missingTone3000 (rig::fileSlot (stage), path))
            return;
        if (path.isEmpty())
            clearAmp (stage);
        else if (files == Files::restoreInBackground)
            loadAmpAsync (stage, juce::File (path), AmpLoad::showNewNow);
        else if (const auto error = loadAmp (stage, juce::File (path)); error.isNotEmpty())
        {
            // Keep the path so saving again doesn't lose it, and say why
            const std::scoped_lock lock (messageLock);
            auto& a = amp (stage);
            a.handoff.publish (std::make_unique<Amps>());
            a.file = juce::File (path);
            a.expectedRate = 0.0;
            a.latency = 0;
            a.problem = juce::File (path).existsAsFile() ? error : "File not found";
        }
    };
    restore (AmpStage::amp, rigTree.getProperty ("ampFile").toString());
    restore (AmpStage::power, rigTree.getProperty ("powerFile").toString());

    if (const auto cab = rigTree.getProperty ("cabFile").toString(); missingTone3000 (rig::FileSlot::cab, cab))
        ;
    else if (cab.isNotEmpty())
        loadCabinet (juce::File (cab));
    else
        clearCabinet();

    // Captures in pedal slots (the pedals themselves were set above)
    for (auto s : { Section::pre, Section::loop })
        for (int i = 0; i < rig::slotsPerSection; ++i)
        {
            const auto fs = rig::FileSlot::pedal (s, i);
            const auto path = rigTree.getProperty (rig::fileSlotKey (fs) + "File").toString();
            if (path.isEmpty() || dynamic_cast<CapturePedal*> (slot (s, i).pedal.get()) == nullptr)
                continue;
            if (! missingTone3000 (fs, path))
                loadSlotCapture (s, i, juce::File (path), {}, files == Files::restore);
        }

    // The files are in; now what they came from (local files have none)
    {
        const std::scoped_lock lock (messageLock);
        tone3000Sources = sources;
    }
}

void RigProcessor::setPresetName (const juce::String& name, bool modified)
{
    const std::scoped_lock lock (messageLock);
    presetName = name;
    presetModified = modified;
}

juce::String RigProcessor::getPresetName() const
{
    const std::scoped_lock lock (messageLock);
    return presetName;
}

bool RigProcessor::wasPresetModified() const
{
    const std::scoped_lock lock (messageLock);
    return presetModified;
}

void RigProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = captureState())
        copyXmlToBinary (*xml, destData);
}

void RigProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        restoreState (*xml, Files::restore);
        if (presets != nullptr)
            presets->stateRestored();
    }
}

juce::AudioProcessorEditor* RigProcessor::createEditor()
{
    return new RigEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RigProcessor();
}
