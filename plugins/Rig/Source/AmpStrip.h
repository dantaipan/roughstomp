#pragma once

#include "RigProcessor.h"
#include "rs_ui/Family.h"

/** A file a strip holds (an amp's capture, a cab's IR): its name (a TONE3000
    tone's title, with the T3K mark), or a problem, and Load / TONE3000 /
    clear. */
class FileRow final : public juce::Component
{
public:
    explicit FileRow (const juce::String& loadTooltip);

    /** emptyText shows (dimmed) when there's no file. */
    void show (const juce::File&, const juce::String& problem, const rig::Tone3000Source&, const juce::String& emptyText);
    void setTone3000Available (bool);

    std::function<void()> onLoad, onClear, onTone3000, onShowTone3000;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Label name;
    juce::TextButton load { "Load..." }, tone3000 { "TONE3000" }, clear { juce::String::fromUTF8 ("\xc3\x97") };
    bool fromTone3000 = false;
    juce::Rectangle<int> markArea;
};

/** One amp's path, drawn as a thin strip: "Amp A" or "Amp B" (its On), the
    capture, Gain / Level / Pan, then its cab with its own On. Two of them, one above the other, show
    the two amps side by side. */
class AmpStrip final : public juce::Component
{
public:
    AmpStrip (RigProcessor&, rig::AmpStage);

    /** Call regularly: the files, problems and TONE3000 sources. */
    void refresh();

    FileRow amp { "Load a .nam capture" }, cab { "Load an IR (WAV or AIFF)" };

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Where the value popups appear while dragging a knob. */
    void setPopupParent (juce::Component*);

private:
    RigProcessor& rigProcessor;
    const rig::AmpStage stage;
    juce::ToggleButton on, cabOn { "Cab" };
    juce::AudioProcessorValueTreeState::ButtonAttachment onAttachment, cabOnAttachment;
    rs::ui::ConsoleKnob gain, level, pan;
    int dividerX = 0;
};
