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

/** Call-outs in the family look, filled solid (JUCE's are see-through). */
struct CallOutLook final : rs::ui::FamilyLookAndFeel
{
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
};

/** An amp's tone and its cab's cuts: Bass / Middle / Treble | Low cut / High
    cut, and Reset. Shown in a call-out from the strip's Tone button. */
class TonePanel final : public juce::Component
{
public:
    TonePanel (RigProcessor&, rig::AmpStage, juce::Component* popupParent);

    static bool isFlat (RigProcessor&, rig::AmpStage);

    /** For the call-out holding the panel: it lives as long as the panel. */
    juce::SharedResourcePointer<CallOutLook> callOutLook;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    RigProcessor& rigProcessor;
    const rig::AmpStage stage;
    const ParamIDs::AmpTone& ids;
    rs::ui::ConsoleKnob bass, middle, treble, lowCut, highCut;
    juce::TextButton reset { "Reset" };
    int dividerX = 0;
};

/** One amp's path, drawn as a thin strip: "Amp A" or "Amp B" (its On), the
    capture, Gain / Level / Pan and Tone, then its cab with its own On. Two of
    them, one above the other, show the two amps side by side. */
class AmpStrip final : public juce::Component
{
public:
    AmpStrip (RigProcessor&, rig::AmpStage);

    /** Call regularly: the files, problems and TONE3000 sources. */
    void refresh();

    FileRow amp { "Load a .nam capture" }, cab { "Load an IR (WAV or AIFF)" };

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Where the value popups (and the tone panel) appear. */
    void setPopupParent (juce::Component*);

    /** Opens the tone panel, as the Tone button does. */
    void showTone();

private:
    RigProcessor& rigProcessor;
    const rig::AmpStage stage;
    juce::ToggleButton on, cabOn { "Cab" };
    juce::AudioProcessorValueTreeState::ButtonAttachment onAttachment, cabOnAttachment;
    rs::ui::ConsoleKnob gain, level, pan;
    juce::TextButton tone { "Tone" };   // lit while the tone or cuts aren't flat
    juce::Component* popupParent = nullptr;
    int dividerX = 0;
};
