#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <optional>

/** Which MIDI CC moves which control. A preference, not part of a sound: your
    controller is the same in every session, so it's one setting for every
    RoughStomp (the app and the plugin in any host), kept with the view
    preference in ~/Library/Application Support/RoughStomp. Message thread
    only; use it through a juce::SharedResourcePointer. It broadcasts a change
    whenever a mapping changes. */
class MidiMappings final : public juce::ChangeBroadcaster
{
public:
    struct Mapping
    {
        int cc = 0;
        juce::String paramId;
        bool toggle = false;   // a switch flips on each press (CC >= 64), for momentary footswitches
    };

    MidiMappings();

    const std::vector<Mapping>& get() const noexcept { return mappings; }
    std::optional<Mapping> forParameter (const juce::String& paramId) const;

    /** Maps the CC to the parameter, replacing whatever either had. */
    void assign (int cc, const juce::String& paramId, bool toggle = false);
    void forget (const juce::String& paramId);
    void setToggle (const juce::String& paramId, bool);

    /** false: changes last only as long as this process (e.g. for pictures
        of the editor), leaving the saved preference alone. */
    void setRemembered (bool r) noexcept { remembered = r; }

    /** CCs that never map: bank select, and the channel mode messages. */
    static bool isMappable (int cc) noexcept { return cc >= 0 && cc < 120 && cc != 0 && cc != 32; }

private:
    void save();

    std::unique_ptr<juce::PropertiesFile> file;
    std::vector<Mapping> mappings;
    bool remembered = true;
};

/** A rig's MIDI input: mapped CCs move their controls, program changes pick
    presets, and MIDI Learn listens for the next CC.

    CCs are applied on the audio thread (as host automation is), so an
    expression pedal on a control moves it smoothly and at once; the host
    and the editor hear about it as they do for automation. Program changes
    and learned CCs are handed to the message thread, which polls for them. */
class MidiControl final : private juce::ChangeListener
{
public:
    explicit MidiControl (juce::AudioProcessorValueTreeState&);
    ~MidiControl() override;

    /** Audio thread. */
    void process (const juce::MidiBuffer&) noexcept;

    // Message thread ---------------------------------------------------------

    /** The program number most recently received, or -1 (once per message). */
    int takeProgramChange() noexcept { return program.exchange (-1); }

    /** The next CC received maps to this parameter (the CC doesn't move
        anything meanwhile). "" stops listening. */
    void learn (const juce::String& paramId);
    juce::String getLearning() const { return learning; }

    /** Call regularly: completes a learn shortly after its CC has come in
        (long enough to tell a momentary footswitch, which sends a press and a
        release, from a latching one). Returns true if a mapping was made. */
    bool poll();

    MidiMappings& getMappings() noexcept { return *mappings; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuild();   // the audio thread's table, from the mappings

    juce::AudioProcessorValueTreeState& apvts;
    juce::SharedResourcePointer<MidiMappings> mappings;

    std::array<std::atomic<juce::RangedAudioParameter*>, 128> targets {};
    std::array<std::atomic<bool>, 128> toggles {};
    std::array<int, 128> lastValue {};   // audio thread: for toggles, a press is a rise through 64

    std::atomic<int> program { -1 }, learnedCc { -1 };
    std::atomic<int> learnedValues { 0 };   // bit 0: the learned CC went below 64, bit 1: to 64 or above
    std::atomic<bool> listening { false };
    juce::String learning;   // message thread
    double learnedAt = 0.0;  // ms: when its CC first came in

    JUCE_DECLARE_NON_COPYABLE (MidiControl)
};
