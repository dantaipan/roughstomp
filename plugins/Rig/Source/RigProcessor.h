#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "AmpModel.h"
#include "rs_t3k/Types.h"
#include "fx/Chain.h"
#include "fx/DelayLine.h"
#include "fx/Handoff.h"
#include "fx/NoiseGate.h"
#include "fx/PitchDetector.h"
#include "fx/Smoother.h"

#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <optional>

class PresetManager;
namespace rs::t3k { class Service; }

namespace rig
{
enum class Section { pre, loop };
constexpr int numSections     = 2;
constexpr int slotsPerSection = 6;
constexpr int knobsPerSlot    = 8;

/** The two amps, side by side: each feeds its own cab, then they're panned
    into stereo before the loop. */
enum class AmpStage { amp, ampB };

/** The two cabs: A after Amp A, B after Amp B (B with no IR of its own uses A's). */
enum class Cab { a, b };

/** Where a file goes: the two amps, the cab, or a pedal slot holding a NAM
    capture. */
struct FileSlot
{
    enum class Kind { amp, ampB, cab, cabB, pedal };
    Kind kind = Kind::amp;
    Section section = Section::pre;   // for pedal slots
    int index = 0;

    static const FileSlot amp, ampB, cab, cabB;
    static FileSlot pedal (Section s, int i) noexcept { return { Kind::pedal, s, i }; }

    bool isPedal() const noexcept { return kind == Kind::pedal; }
    bool operator== (const FileSlot& o) const noexcept
    {
        return kind == o.kind && (kind != Kind::pedal || (section == o.section && index == o.index));
    }
    bool operator!= (const FileSlot& o) const noexcept { return ! (*this == o); }
};
inline constexpr FileSlot FileSlot::amp   { FileSlot::Kind::amp,   Section::pre, 0 };
inline constexpr FileSlot FileSlot::ampB  { FileSlot::Kind::ampB,  Section::pre, 0 };
inline constexpr FileSlot FileSlot::cab   { FileSlot::Kind::cab,   Section::pre, 0 };
inline constexpr FileSlot FileSlot::cabB  { FileSlot::Kind::cabB,  Section::pre, 0 };

inline FileSlot fileSlot (AmpStage s) noexcept { return s == AmpStage::amp ? FileSlot::amp : FileSlot::ampB; }
inline FileSlot fileSlot (Cab c) noexcept { return c == Cab::a ? FileSlot::cab : FileSlot::cabB; }
juce::String fileSlotKey (FileSlot);                       // "amp", "ampB", "cab", "cabB", "pre3", "loop1"...
std::optional<FileSlot> fileSlotFromKey (const juce::String&);

/** Where a slot's file came from when it's a TONE3000 download: what to show
    for it, and what to download again if the file has gone. */
struct Tone3000Source
{
    int toneId = 0, modelId = 0;
    juce::String toneTitle, modelName, creator, gear, format, license, imageUrl, toneUrl;

    bool valid() const noexcept { return toneId > 0 && modelId > 0; }
    static Tone3000Source from (const rs::t3k::Tone&, const rs::t3k::Model&);
    rs::t3k::Tone tone() const;   // enough of one to find or make its download
};

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
    inline const juce::ParameterID powerOn    { "powerOn",    1 };   // Amp B's On (once the power amp's)
    inline const juce::ParameterID ampBGain   { "ampBGain",   1 };
    inline const juce::ParameterID ampBLevel  { "ampBLevel",  1 };
    inline const juce::ParameterID ampPan     { "ampPan",     1 };
    inline const juce::ParameterID ampBPan    { "ampBPan",    1 };
    inline const juce::ParameterID cabOn      { "cabOn",      1 };   // Cab A's On (once the only cab's)
    inline const juce::ParameterID cabBOn     { "cabBOn",     1 };
    inline const juce::ParameterID output     { "output",     1 };
    inline const juce::ParameterID gateOn     { "gateOn",     1 };
    inline const juce::ParameterID gateThreshold { "gateThreshold", 1 };
    inline const juce::ParameterID tuner      { "tuner",      1 };
}

/** The whole rig:

      in -> input -> gate -> [pre-amp pedals] -> AMP (NAM) -> to stereo
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

    // Whole-rig state (message thread) ------------------------------------------

    /** Everything: parameters, pedals, file paths, preset name. */
    std::unique_ptr<juce::XmlElement> captureState();

    /** keepCurrent leaves the amps and cabs as they are (factory
        presets don't know which captures you have). */
    enum class Files { restore, restoreInBackground, keepCurrent };
    void restoreState (const juce::XmlElement&, Files);

    /** The preset the rig came from ("" if none), and whether it had been
        changed when the state was saved. Kept with sessions. */
    void setPresetName (const juce::String&, bool modified);
    juce::String getPresetName() const;
    bool wasPresetModified() const;

    PresetManager& getPresets() noexcept { return *presets; }

    // Message thread ----------------------------------------------------------

    /** Puts a pedal in a slot ("" empties it). With resetKnobs, the slot's
        knobs jump to the pedal's defaults. */
    void setSlotPedal (rig::Section, int slot, juce::StringRef pedalId, bool resetKnobs = true);

    /** Swaps two slots, pedals and settings (into an empty slot: a move).
        Within a section the pedals themselves move, tails and all; between
        sections fresh ones are made (the chains run different channel counts). */
    void swapSlots (rig::Section, int slot, rig::Section, int otherSlot);

    /** Any thread. nullptr for an empty slot. */
    const fx::PedalInfo* getSlotInfo (rig::Section, int slot) const noexcept;

    /** Loads now (blocking). Returns an error message, or empty on success. */
    juce::String loadAmp (rig::AmpStage, const juce::File&);

    /** Loads on a background thread so the GUI doesn't freeze; the current
        amp keeps playing until the new one is ready. keepOld: the panel shows
        the old file until then, and a failed load changes nothing.
        showNewNow: it shows the new file (loading) at once, and a failed
        load leaves it there with the problem (as a restored session does).
        A later load or clear cancels an earlier one. `done` runs on the
        message thread with an error message, or empty on success. */
    enum class AmpLoad { keepOld, showNewNow };
    void loadAmpAsync (rig::AmpStage, const juce::File&, AmpLoad, std::function<void (const juce::String&)> done = {});

    /** The file being loaded in the background, or none. */
    juce::File getLoadingAmpFile (rig::AmpStage) const;
    void clearAmp (rig::AmpStage);
    juce::File getAmpFile (rig::AmpStage) const;

    /** A problem worth showing (e.g. the file is missing), or empty. */
    juce::String getAmpProblem (rig::AmpStage) const;

    /** The tuner's latest reading. Call a few times a second while the tuner
        is on (the output is muted then); it reads what the audio thread has
        captured since the last call. */
    struct TunerReading
    {
        float hz = 0.0f;           // 0: no clear note
        float confidence = 0.0f;
    };
    TunerReading readTuner();

    juce::String loadCabinet (const juce::File&, rig::Cab = rig::Cab::a);
    void clearCabinet (rig::Cab = rig::Cab::a);
    juce::File getCabinetFile (rig::Cab = rig::Cab::a) const;
    juce::String getCabinetProblem (rig::Cab = rig::Cab::a) const;

    // NAM captures in pedal slots (message thread) -------------------------------

    /** Loads a capture into a slot holding the "NAM Capture" pedal: in the
        background, unless now (restoring a session for an offline bounce).
        done gets an error, or empty on success. */
    void loadSlotCapture (rig::Section, int slot, const juce::File&,
                          std::function<void (const juce::String&)> done = {}, bool now = false);
    void clearSlotCapture (rig::Section, int slot);
    /** The slot's capture (or the one loading), and any problem ("Loading..." while it loads). */
    juce::File getSlotCaptureFile (rig::Section, int slot) const;
    juce::String getSlotCaptureProblem (rig::Section, int slot) const;

    // TONE3000 (message thread) ------------------------------------------------

    /** Downloads the model (unless it already has) and loads it into the
        slot, keeping where it came from with the session. done gets an
        error, or empty on success. */
    void loadTone3000 (rig::FileSlot, const rs::t3k::Tone&, const rs::t3k::Model&,
                       std::function<void (const juce::String&)> done = {});

    /** What a slot's file came from on TONE3000 (invalid for a local file).
        Loading a local file or clearing the slot forgets it. */
    rig::Tone3000Source getTone3000Source (rig::FileSlot) const;
    void forgetTone3000Source (rig::FileSlot);

    /** Downloads again any TONE3000 file a restored session needed but
        couldn't find (e.g. once the user has signed in). */
    void retryTone3000Downloads();

    /** The process's TONE3000 connection, made the first time it's needed. */
    rs::t3k::Service& tone3000();

private:
    struct Slot
    {
        std::shared_ptr<fx::Pedal> pedal;                   // message thread
        std::atomic<const fx::PedalInfo*> info { nullptr };
        std::atomic<float>* on = nullptr;
        std::array<std::atomic<float>*, rig::knobsPerSlot> knobs {};

        // A NAM capture pedal's file (messageLock)
        juce::File captureFile, captureLoading;
        juce::String captureProblem;
        int captureGeneration = 0;   // bumped by every load, clear or pedal change: stale loads are dropped
    };

    /** An amp's model (a vector, as the handoff swaps whole objects). */
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
        juce::File loadingFile;      // a background load in progress
        int generation = 0;          // bumped by every load or clear: stale loads are dropped
    };

    struct BuiltAmps
    {
        std::unique_ptr<Amps> amps;
        juce::String error;
    };
    static BuiltAmps buildAmps (rig::AmpStage, const juce::File&, double rate, int block);
    void publishAmps (rig::AmpStage, const juce::File&, std::unique_ptr<Amps>);

    static constexpr size_t index (rig::Section s) noexcept { return static_cast<size_t> (s); }
    Slot& slot (rig::Section s, int i) noexcept { return slots[index (s)][static_cast<size_t> (i)]; }
    const Slot& slot (rig::Section s, int i) const noexcept { return slots[index (s)][static_cast<size_t> (i)]; }
    AmpState& amp (rig::AmpStage s) noexcept { return s == rig::AmpStage::amp ? mainAmp : ampBState; }
    const AmpState& amp (rig::AmpStage s) const noexcept { return s == rig::AmpStage::amp ? mainAmp : ampBState; }
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
    AmpState mainAmp, ampBState;

    /** A cab: its IR as read (at its own rate), handed to a mono convolver. */
    struct CabState
    {
        juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 1024 } };
        std::atomic<bool> loaded { false };   // an IR is in (B: its own or A's)
        juce::File file;
        juce::String problem;
        juce::AudioBuffer<float> source;
        double sourceRate = 0.0;
        double rate = 0.0;                    // the rate it was last handed over at
    };
    CabState cabA, cabB;
    CabState& cabState (rig::Cab c) noexcept { return c == rig::Cab::a ? cabA : cabB; }
    const CabState& cabState (rig::Cab c) const noexcept { return c == rig::Cab::a ? cabA : cabB; }
    float cabinetGain = 4.0f;                 // audio thread; set in prepareToPlay

    std::map<juce::String, rig::Tone3000Source> tone3000Sources;   // by fileSlotKey; messageLock
    int loopChannels = 2;

    /** The file a slot holds, and loading one into it (any kind of slot). */
    juce::File fileOf (rig::FileSlot) const;
    void loadFileInto (rig::FileSlot, const juce::File&, bool showNewNow, std::function<void (const juce::String&)> done);
    std::unique_ptr<juce::SharedResourcePointer<rs::t3k::Service>> tone3000Service;

    /** The slot's TONE3000 file is missing: download it again (or say why
        it can't) and load it. */
    void redownload (rig::FileSlot, const rig::Tone3000Source&, const juce::File&);
    void setSlotProblem (rig::FileSlot, const juce::File&, const juce::String& problem);

    /** Resamples a cab's IR to the current rate (with fx's resampler: JUCE's
        own lets through highs the cab should cut) and hands it to its
        convolver. Cab B with no IR of its own takes A's. messageLock held. */
    void applyCabinet (rig::Cab);

    std::atomic<float>* inputDb = nullptr;
    std::atomic<float>* ampOn = nullptr;
    std::atomic<float>* ampGainDb = nullptr;
    std::atomic<float>* ampLevelDb = nullptr;
    std::atomic<float>* loopOn = nullptr;
    std::atomic<float>* loopSendDb = nullptr;
    std::atomic<float>* loopReturnDb = nullptr;
    std::atomic<float>* loopMix = nullptr;
    std::atomic<float>* powerOn = nullptr;   // Amp B's On
    std::atomic<float>* ampBGainDb = nullptr;
    std::atomic<float>* ampBLevelDb = nullptr;
    std::atomic<float>* ampPanValue = nullptr;
    std::atomic<float>* ampBPanValue = nullptr;
    std::atomic<float>* cabOn = nullptr;   // Cab A's On
    std::atomic<float>* cabBOn = nullptr;
    std::atomic<float>* outputDb = nullptr;
    std::atomic<float>* gateOn = nullptr;
    std::atomic<float>* gateThresholdDb = nullptr;
    std::atomic<float>* tunerOn = nullptr;

    fx::NoiseGate gate;

    juce::String presetName;       // messageLock
    bool presetModified = false;   // messageLock
    std::unique_ptr<PresetManager> presets;
    int ticks = 0;

    // Tuner: the audio thread copies the raw input here while tuning; the
    // message thread reads it into a window and finds the pitch
    static constexpr int tunerFifoSize = 32768;
    juce::AbstractFifo tunerFifo { tunerFifoSize };
    std::vector<float> tunerFifoData = std::vector<float> (static_cast<size_t> (tunerFifoSize));
    std::vector<float> tunerWindow;
    fx::PitchDetector pitchDetector;
    double pitchDetectorRate = 0.0;

    fx::LinearSmoother inputGain { 1.0f }, ampGain { 1.0f }, ampLevel { 1.0f }, ampBGain { 1.0f }, ampBLevel { 1.0f },
                       sendGain { 1.0f }, returnGain { 1.0f }, mix { 1.0f }, outputGain { 1.0f },
                       panA { 0.0f }, panB { 0.0f };

    // Amp A and the cabs switch on and off by crossfading with their dry
    // signal (Amp A's delayed to match its resampling latency); Amp B fades
    // to silence. The shorter amp path is delayed to line up with the longer
    fx::LinearSmoother ampBlend { 1.0f }, ampBOnGain { 1.0f }, cabBlendA { 1.0f }, cabBlendB { 1.0f };
    fx::DelayLine ampDryLine, alignA, alignB;
    std::vector<float> ampDry, pathB, pathBOn, cabDryA, cabDryB;

    // The loop's dry path, delayed to line up with its pedals' latency
    std::array<std::vector<float>, 2> dryDelay, dry;
    int dryWrite = 0;

    double currentSampleRate = 48000.0;
    int maxBlock = 512;
    double tempo = 120.0;   // audio thread: the host's transport this block
    double quarterNotes = 0.0;
    bool playing = false;

    // Background loading of captures. Last, so it's destroyed (waiting for
    // any load in progress) before everything else.
    juce::ThreadPool loader { juce::ThreadPoolOptions{}.withThreadName ("RoughStomp loader").withNumberOfThreads (1) };

    JUCE_DECLARE_WEAK_REFERENCEABLE (RigProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigProcessor)
};
