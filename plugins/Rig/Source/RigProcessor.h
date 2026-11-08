#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "AmpModel.h"
#include "fx/Chain.h"
#include "fx/Handoff.h"
#include "fx/Smoother.h"

#include <array>
#include <atomic>
#include <mutex>

namespace rig
{
enum class Section { pre, loop };
constexpr int numSections     = 2;
constexpr int slotsPerSection = 6;
constexpr int knobsPerSlot    = 8;

/** Amps run before the loop, the power amp after it. */
enum class AmpStage { amp, power };

juce::String sectionKey (Section);                         // "pre", "loop"
juce::ParameterID slotOnID   (Section, int slot);           // "pre1_on"
juce::ParameterID slotKnobID (Section, int slot, int knob); // "pre1_k1"

/** Slot knobs are stored normalised (0..1) because the pedal in a slot can
    change; these map them to and from the pedal's own units. */
float toPlain (const fx::ParamInfo&, float normalised) noexcept;
float toNormalised (const fx::ParamInfo&, float plain) noexcept;
juce::String valueText (const fx::ParamInfo&, float plain);
} // namespace rig

namespace ParamIDs
{
    // Stable once released: sessions and automation depend on these
    inline const juce::ParameterID input      { "input",      1 };
    inline const juce::ParameterID ampOn      { "ampOn",      1 };
    inline const juce::ParameterID ampGain    { "ampGain",    1 };
    inline const juce::ParameterID ampLevel   { "ampLevel",   1 };
    inline const juce::ParameterID loopOn     { "loopOn",     1 };
    inline const juce::ParameterID loopSend   { "loopSend",   1 };
    inline const juce::ParameterID loopReturn { "loopReturn", 1 };
    inline const juce::ParameterID loopMix    { "loopMix",    1 };
    inline const juce::ParameterID powerOn    { "powerOn",    1 };
    inline const juce::ParameterID cabOn      { "cabOn",      1 };
    inline const juce::ParameterID output     { "output",     1 };
}

/** The whole rig:

      in -> input -> [pre-amp pedals] -> AMP (NAM) -> to stereo
         -> FX loop: send -> [loop pedals] -> return, blended with the
            latency-aligned dry signal by mix
         -> POWER AMP (optional NAM, one per channel) -> CAB (IR) -> output

    Pedals, amps and the cabinet are swapped on the message thread and handed
    to the audio thread without locks (fx::Handoff, fx::Chain). */
class RigProcessor final : public juce::AudioProcessor,
                           private juce::Timer
{
public:
    RigProcessor();
    ~RigProcessor() override;

    // Audio
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    // Editor
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // Info
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.25; }   // the cabinet's ring

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Message thread ----------------------------------------------------------

    /** Puts a pedal in a slot ("" empties it). With resetKnobs, the slot's
        knobs jump to the pedal's defaults. */
    void setSlotPedal (rig::Section, int slot, juce::StringRef pedalId, bool resetKnobs = true);

    /** Any thread. nullptr for an empty slot. */
    const fx::PedalInfo* getSlotInfo (rig::Section, int slot) const noexcept;

    /** Returns an error message, or an empty string on success. */
    juce::String loadAmp (rig::AmpStage, const juce::File&);
    void clearAmp (rig::AmpStage);
    juce::File getAmpFile (rig::AmpStage) const;

    /** A problem worth showing (e.g. the file is missing), or empty. */
    juce::String getAmpProblem (rig::AmpStage) const;

    juce::String loadCabinet (const juce::File&);
    void clearCabinet();
    juce::File getCabinetFile() const;
    juce::String getCabinetProblem() const;

private:
    struct Slot
    {
        std::shared_ptr<fx::Pedal> pedal;                   // message thread
        std::atomic<const fx::PedalInfo*> info { nullptr };
        std::atomic<float>* on = nullptr;
        std::array<std::atomic<float>*, rig::knobsPerSlot> knobs {};
    };

    /** One model per channel it runs on: 1 for the amp, 2 for the power amp. */
    struct Amps
    {
        std::vector<std::unique_ptr<rig::AmpModel>> models;
        bool loaded() const noexcept { return ! models.empty(); }
    };

    struct AmpState
    {
        fx::Handoff<Amps> handoff;
        juce::File file;
        double expectedRate = 0.0;   // the capture's sample rate, 0 if any
        int latency = 0;             // from resampling to expectedRate
        juce::String problem;
    };

    static constexpr size_t index (rig::Section s) noexcept { return static_cast<size_t> (s); }
    Slot& slot (rig::Section s, int i) noexcept { return slots[index (s)][static_cast<size_t> (i)]; }
    AmpState& amp (rig::AmpStage s) noexcept { return s == rig::AmpStage::amp ? mainAmp : powerAmp; }
    const AmpState& amp (rig::AmpStage s) const noexcept { return s == rig::AmpStage::amp ? mainAmp : powerAmp; }
    fx::Chain& chain (rig::Section s) noexcept { return s == rig::Section::pre ? preChain : loopChain; }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void publishLayout (rig::Section);
    void updateLatency();
    void updateAmpRate (AmpState&);
    void timerCallback() override;

    void processChunk (float* const* channels, int numChannels, int numSamples) noexcept;
    void pushParameters (rig::Section, std::span<fx::Pedal* const>) noexcept;

    // Declared before apvts: parameter text functions read the slots
    std::array<std::array<Slot, rig::slotsPerSection>, rig::numSections> slots;

public:
    juce::AudioProcessorValueTreeState apvts;

private:
    mutable std::mutex messageLock;   // message-side state; the audio thread never takes it

    fx::Chain preChain, loopChain;
    AmpState mainAmp, powerAmp;

    juce::dsp::Convolution cabinet { juce::dsp::Convolution::NonUniform { 1024 } };
    std::atomic<bool> cabinetLoaded { false };
    juce::File cabinetFile;
    juce::String cabinetProblem;
    juce::AudioBuffer<float> cabinetSource;   // the IR as read, at its own rate
    double cabinetSourceRate = 0.0;
    double cabinetRate = 0.0;                 // the rate it was last handed to `cabinet` at
    float cabinetGain = 4.0f;                 // audio thread; set in prepareToPlay

    /** Resamples the IR to the current rate (with fx's resampler: JUCE's own
        lets through highs the cab should cut) and hands it to the convolver.
        messageLock held. */
    void applyCabinet();

    std::atomic<float>* inputDb = nullptr;
    std::atomic<float>* ampOn = nullptr;
    std::atomic<float>* ampGainDb = nullptr;
    std::atomic<float>* ampLevelDb = nullptr;
    std::atomic<float>* loopOn = nullptr;
    std::atomic<float>* loopSendDb = nullptr;
    std::atomic<float>* loopReturnDb = nullptr;
    std::atomic<float>* loopMix = nullptr;
    std::atomic<float>* powerOn = nullptr;
    std::atomic<float>* cabOn = nullptr;
    std::atomic<float>* outputDb = nullptr;

    fx::LinearSmoother inputGain { 1.0f }, ampGain { 1.0f }, ampLevel { 1.0f },
                       sendGain { 1.0f }, returnGain { 1.0f }, mix { 1.0f }, outputGain { 1.0f };

    // The loop's dry path, delayed to line up with its pedals' latency
    std::array<std::vector<float>, 2> dryDelay, dry;
    int dryWrite = 0;

    double currentSampleRate = 48000.0;
    int maxBlock = 512;
    double tempo = 120.0;   // audio thread: the host's transport this block
    double quarterNotes = 0.0;
    bool playing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigProcessor)
};
