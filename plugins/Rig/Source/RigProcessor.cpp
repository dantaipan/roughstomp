#include "RigProcessor.h"

#include "RigEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

using rig::Section;
using rig::AmpStage;

namespace rig
{
juce::String sectionKey (Section s) { return s == Section::pre ? "pre" : "loop"; }

juce::ParameterID slotOnID (Section s, int i)
{
    return { sectionKey (s) + juce::String (i + 1) + "_on", 1 };
}

juce::ParameterID slotKnobID (Section s, int i, int k)
{
    return { sectionKey (s) + juce::String (i + 1) + "_k" + juce::String (k + 1), 1 };
}

float toPlain (const fx::ParamInfo& p, float normalised) noexcept
{
    const float v = p.minValue + juce::jlimit (0.0f, 1.0f, normalised) * (p.maxValue - p.minValue);
    return p.kind == fx::ParamInfo::Kind::continuous ? v : std::round (v);
}

float toNormalised (const fx::ParamInfo& p, float plain) noexcept
{
    return p.maxValue > p.minValue ? juce::jlimit (0.0f, 1.0f, (plain - p.minValue) / (p.maxValue - p.minValue)) : 0.0f;
}

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
    const int decimals = (p.maxValue - p.minValue) > 20.0f ? 0 : 1;
    return juce::String (plain, unit == "dB" ? 1 : decimals) + (unit.isEmpty() ? "" : " " + unit);
}
} // namespace rig

namespace
{
constexpr int maxLoopLatency = 8192;     // samples of dry delay available to the loop
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
    std::shared_ptr<fx::Pedal> pedal = id.empty() ? nullptr : fx::registry::create (id);

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
        publishLayout (s);
    }
    updateLatency();
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
juce::String RigProcessor::loadAmp (AmpStage stage, const juce::File& file)
{
    double rate;
    int block;
    {
        const std::scoped_lock lock (messageLock);
        rate = currentSampleRate;
        block = maxBlock;
    }

    // The power amp runs once per output channel
    const int copies = stage == AmpStage::power ? 2 : 1;
    auto amps = std::make_unique<Amps>();
    for (int c = 0; c < copies; ++c)
    {
        auto result = rig::AmpModel::load (file, rate, block);
        if (result.model == nullptr)
            return result.error;
        amps->models.push_back (std::move (result.model));
    }

    {
        const std::scoped_lock lock (messageLock);
        auto& a = amp (stage);
        a.expectedRate = amps->models.front()->getExpectedSampleRate();
        a.handoff.publish (std::move (amps));
        a.file = file;
        a.problem = {};
        updateAmpRate (a);
    }
    updateLatency();
    return {};
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
        a.handoff.publish (std::make_unique<Amps>());
        a.file = juce::File();
        a.expectedRate = 0.0;
        a.latency = 0;
        a.problem = {};
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
    return amp (stage).problem;
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

void RigProcessor::timerCallback()
{
    {
        const std::scoped_lock lock (messageLock);
        preChain.collectGarbage();
        loopChain.collectGarbage();
        mainAmp.handoff.collect();
        powerAmp.handoff.collect();
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

    inputGain.reset (dbToGain (inputDb->load()));
    ampGain.reset (dbToGain (ampGainDb->load()));
    ampLevel.reset (dbToGain (ampLevelDb->load()));
    sendGain.reset (dbToGain (loopSendDb->load()));
    returnGain.reset (dbToGain (loopReturnDb->load()));
    outputGain.reset (dbToGain (outputDb->load()));
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

    // Block-rate targets; the smoothers ramp to them
    inputGain.setTarget (dbToGain (inputDb->load()));
    ampGain.setTarget (dbToGain (ampGainDb->load()));
    ampLevel.setTarget (dbToGain (ampLevelDb->load()));
    sendGain.setTarget (dbToGain (loopSendDb->load()));
    returnGain.setTarget (dbToGain (loopReturnDb->load()));
    outputGain.setTarget (dbToGain (outputDb->load()));
    mix.setTarget (loopOn->load() > 0.5f ? loopMix->load() / 100.0f : 0.0f);

    for (int pos = 0; pos < n; pos += maxBlock)
    {
        float* ch[2] { buffer.getWritePointer (0, pos), numOut > 1 ? buffer.getWritePointer (1, pos) : nullptr };
        processChunk (ch, numOut, std::min (maxBlock, n - pos));
    }
}

void RigProcessor::processChunk (float* const* ch, int numOut, int n) noexcept
{
    float* mono = ch[0];

    // Input and pre-amp pedals (mono)
    applyGain (inputGain, &mono, 1, n);
    pushParameters (Section::pre, preChain.beginBlock());
    preChain.process (&mono, 1, n);

    // Amp
    // Amp. Switched off, a resampled capture still passes through its
    // resampler, so the latency stays put.
    auto& amps = mainAmp.handoff.acquire();
    if (amps.loaded())
    {
        const bool on = ampOn->load() > 0.5f;
        if (on)
            applyGain (ampGain, &mono, 1, n);
        amps.models.front()->process (mono, n, on);
        if (on)
            applyGain (ampLevel, &mono, 1, n);
    }

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
        const bool on = powerOn->load() > 0.5f;
        for (int c = 0; c < numOut; ++c)
            power.models[static_cast<size_t> (c)]->process (ch[c], n, on);
    }

    // Cabinet
    if (cabOn->load() > 0.5f && cabinetLoaded.load())
    {
        juce::dsp::AudioBlock<float> block (ch, static_cast<size_t> (numOut), static_cast<size_t> (n));
        cabinet.process (juce::dsp::ProcessContextReplacing<float> (block));
        for (int c = 0; c < numOut; ++c)
            juce::FloatVectorOperations::multiply (ch[c], cabinetGain, n);
    }

    applyGain (outputGain, ch, numOut, n);
}

//==============================================================================
void RigProcessor::getStateInformation (juce::MemoryBlock& destData)
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

    state.removeChild (state.getChildWithName ("Rig"), nullptr);
    state.appendChild (rigTree, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void RigProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const auto rigTree = state.getChildWithName ("Rig").createCopy();
    state.removeChild (state.getChildWithName ("Rig"), nullptr);
    apvts.replaceState (state);

    // Knob values came with the parameters, so don't reset them
    for (auto s : { Section::pre, Section::loop })
        for (int i = 0; i < rig::slotsPerSection; ++i)
            setSlotPedal (s, i, rigTree.getProperty (rig::sectionKey (s) + juce::String (i + 1)).toString(), false);

    auto restore = [&] (AmpStage stage, const juce::String& path)
    {
        if (path.isEmpty())
            clearAmp (stage);
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

    if (const auto cab = rigTree.getProperty ("cabFile").toString(); cab.isNotEmpty())
        loadCabinet (juce::File (cab));
    else
        clearCabinet();
}

juce::AudioProcessorEditor* RigProcessor::createEditor()
{
    return new RigEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RigProcessor();
}
