#pragma once

#include "Presets.h"
#include "RigProcessor.h"
#include "SlotView.h"
#include "Tone3000Browser.h"
#include "rs_ui/Pedal.h"

/** A box for something loaded from a file (amp capture, cabinet IR):
    title, Load/Clear buttons, the file's name, any problem, and an on switch.
    Drawn like every other box in the row (see drawBox in RigEditor.cpp). */
class FilePanel final : public juce::Component
{
public:
    FilePanel (const juce::String& title, const juce::String& emptyText,
               juce::AudioProcessorValueTreeState&, const juce::ParameterID& onParam);

    /** source: where the file came from on TONE3000 (invalid for a local file). */
    void show (const juce::File&, const juce::String& problem, const rig::Tone3000Source& source = {});

    /** Whether to offer TONE3000 (not in a build without its key). */
    void setTone3000Available (bool);

    std::function<void()> onLoad, onClear;
    std::function<void()> onTone3000;       // the TONE3000 button
    std::function<void()> onShowTone3000;   // clicking a TONE3000 file's name: its tone

    /** Room on the right for extra controls (the amp's knobs). */
    juce::Rectangle<int> getExtraArea() const noexcept { return extraArea; }
    void setExtraWidth (int w) { extraWidth = w; resized(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String title, emptyText;
    juce::TextButton load { "Load..." }, clear { juce::String::fromUTF8 ("\xc3\x97") }, tone3000 { "TONE3000" };
    bool fromTone3000 = false;
    juce::Label name, problem;
    juce::ToggleButton on { "On" };
    juce::AudioProcessorValueTreeState::ButtonAttachment onAttachment;
    juce::Rectangle<int> extraArea;
    int extraWidth = 0;
};

//==============================================================================
/** Preset browsing in the header: previous / name (a menu) / next / save. */
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (PresetManager&);

    /** Call now and then: picks up preset changes and the modified mark. */
    void refresh();

    void resized() override;

private:
    void showMenu();
    void saveAs();
    void report (const juce::String& error);

    PresetManager& presets;
    juce::TextButton previous { "<" }, next { ">" }, name, save { "Save" };
    std::unique_ptr<juce::AlertWindow> dialog;
};

//==============================================================================
/** The tuner, as in RoughTrack: a strobe whose bands drift right when the
    string is sharp, left when it's flat, and stand still in tune; the note
    and how many cents off beside it. Shown over the amp row while tuning. */
class TunerDisplay final : public juce::Component
{
public:
    /** Call regularly with the processor's latest reading and the time since
        the last call (the strobe moves with it). */
    void update (const RigProcessor::TunerReading&, double secondsSinceLast);

    void paint (juce::Graphics&) override;

private:
    void paintBand (juce::Graphics&, juce::Rectangle<float>, float barWidth, float offset, juce::Colour) const;

    std::vector<double> recentCents;   // a plucked string wobbles: the median of a few
    int shownNote = -1;                // MIDI note, -1 for none
    double shownCents = 0.0;
    double secondsSinceNote = 0.0;
    double strobePhase = 0.0;
};

//==============================================================================
class RigEditor final : public juce::AudioProcessorEditor,
                        public juce::DragAndDropContainer,   // pedals are dragged between slots
                        private juce::Timer
{
public:
    explicit RigEditor (RigProcessor&);
    ~RigEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** The design-size surface everything sits on; scaled to the window. */
    struct Board final : public juce::Component
    {
        std::function<void (juce::Graphics&)> onPaint;
        std::function<void()> onResize;
        void paint (juce::Graphics& g) override { if (onPaint) onPaint (g); }
        void resized() override { if (onResize) onResize(); }
    };

    static constexpr double minScale = 0.7, maxScale = 1.5;
    static int designWidth();
    static int designHeight();
    void paintBoard (juce::Graphics&);
    void layoutBoard();

    void timerCallback() override;
    void chooseAmp (rig::AmpStage);
    void chooseCabinet();
    void refreshPanels();

    RigProcessor& rigProcessor;
    rs::ui::FamilyLookAndFeel lookAndFeel;   // the app around the pedals, as RoughTrack
    Board board;   // before the controls: they're its children

    std::array<std::unique_ptr<SlotView>, rig::slotsPerSection> preSlots, loopSlots;

    rs::ui::ConsoleKnob input, gateThreshold, loopSend, loopReturn, loopMix, output, ampGain, ampLevel;
    juce::ToggleButton gateOnButton { "Gate" };
    juce::AudioProcessorValueTreeState::ButtonAttachment gateOnAttachment;
    juce::TextButton tunerButton { "Tuner" };
    juce::AudioProcessorValueTreeState::ButtonAttachment tunerAttachment;
    TunerDisplay tuner;
    PresetBar presetBar;
    int ticks = 0;
    double lastTick = 0.0;   // ms, for the tuner's strobe
    juce::ToggleButton loopOnButton { "On" };
    juce::AudioProcessorValueTreeState::ButtonAttachment loopOnAttachment;

    FilePanel ampPanel, powerPanel, cabPanel;
    std::unique_ptr<Tone3000Browser> tone3000Browser;   // made the first time it's opened

public:
    /** Opens TONE3000's browser for a slot; atItsTone opens on the tone the
        slot's file came from (if it did). */
    void openTone3000 (rig::FileSlot, bool atItsTone);
    Tone3000Browser* getTone3000Browser() noexcept { return tone3000Browser.get(); }

private:
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastFolder;

    juce::Rectangle<int> preArea, ampArea, loopArea;
    juce::Rectangle<int> inputBox, outputBox, loopBox;   // drawn behind their controls

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigEditor)
};
