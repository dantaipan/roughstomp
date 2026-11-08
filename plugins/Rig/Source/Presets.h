#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class RigProcessor;

/** Whole-rig presets. Message thread only.

    - User presets are files (.rsrig, the rig's own state as XML) in
      ~/Library/Application Support/RoughStomp/Presets (or the platform's
      equivalent). They include the amp, power amp and cab file paths.
    - Factory presets are built in. They set pedals and settings but keep the
      current amp and cab, since they can't know which captures you have.

    Browsing order: factory presets, then user presets by name. */
class PresetManager
{
public:
    explicit PresetManager (RigProcessor&);

    struct Entry
    {
        juce::String name;
        bool factory = false;
        int factoryIndex = -1;
        juce::File file;   // user presets
    };

    static juce::File getFolder();
    static constexpr const char* extension = ".rsrig";

    /** Every preset, in browsing order (scans the folder). */
    std::vector<Entry> list() const;

    /** Returns an error message, or empty on success. */
    juce::String load (const Entry&);

    /** Saves the rig as a user preset (overwriting one of the same name).
        Returns an error message, or empty on success. */
    juce::String save (const juce::String& name);

    bool remove (const Entry&);

    /** Loads the previous or next preset in browsing order. */
    void step (int delta);

    juce::String getCurrentName() const;
    bool isCurrentFactory() const;

    /** Has the rig changed since the preset was loaded or saved? Cheap
        enough to call a couple of times a second. */
    bool isModified();

    // From the processor
    void stateRestored();       // a session was restored: that's the new baseline
    void noteModifiedState();   // the processor's timer: record isModified() for sessions

private:
    juce::String fingerprint();   // the rig's state, without the preset bookkeeping
    void loaded (const juce::String& name, bool factory);

    RigProcessor& processor;
    juce::String baseline;
    bool currentIsFactory = false;
};
