#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

/** How the rig shows its pedals: as hardware (knobs, footswitch) or as plain
    sliders. A preference, not part of a sound: one setting for every
    RoughStomp (the app and the plugin in any host), kept in
    ~/Library/Application Support/RoughStomp. Use it through a
    juce::SharedResourcePointer; it broadcasts a change when switched. */
class ViewSettings final : public juce::ChangeBroadcaster
{
public:
    enum class Style { pedals, sliders };

    ViewSettings();

    Style getStyle() const noexcept { return style; }

    /** remember: save it as the preference (false: just this process, e.g.
        for pictures of the editor). */
    void setStyle (Style, bool remember = true);

private:
    std::unique_ptr<juce::PropertiesFile> file;
    Style style = Style::pedals;
};
